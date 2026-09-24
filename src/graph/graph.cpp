#include <algorithm>
#include <utility>
#include <cassert>

#include "guchho/graph.hpp"
#include "guchho/javascript/js_runtime.hpp"

namespace guchho::graph {

    // Implements LinkerFile::LineColumnTracker: creates the file's position
    // tracker on first use and reuses it afterwards. Building it eagerly would
    // spend memory on every file even though most never log a warning, so the
    // allocation is deferred. The routine is intentionally unsynchronized;
    // the caller must not invoke it from a parallel section of the build.
    //
    // Input:  first call for a file whose first diagnostic points at line 3
    // Output: the tracker for that file, ready to turn byte offsets into
    //         line/column positions for every future lookup
    logger::LineColumnTracker& LinkerFile::LineColumnTracker()
    {
        if (!lazy_line_column_tracker) {
            lazy_line_column_tracker = std::make_unique<logger::LineColumnTracker>(&input_file.source);
        }
        return *lazy_line_column_tracker;
    }

    // Creates the private working graph for a single linking operation. The
    // scanned input files remain shared and read-only, so each reachable
    // file's content payload is cloned here: representations are deep-copied,
    // per-file symbol ownership moves into the graph, and dynamic imports
    // grow into entry points of their own when code splitting is enabled.
    // Everything runs serially and dynamic entry points are sorted at the end
    // so the result is deterministic without locking or worker threads.
    //
    // Input:  three scanned files, reachable = {0, 2}, one user-specified
    //         entry point, code splitting on, file 0 dynamically imports
    //         file 2
    // Output: a LinkerGraph spanning files 0 and 2 with cloned payloads and
    //         two entry points ordered by stable index
    LinkerGraph CloneLinkerGraph(
        const std::vector<InputFile>& input_files,
        const std::vector<uint32_t>& reachable_files,
        const std::vector<EntryPoint>& original_entry_points,
        bool code_splitting)
    {
        LinkerGraph graph;
        graph.files.resize(input_files.size());
        graph.stable_source_indices.resize(input_files.size());
        graph.symbols = compiler::NewSymbolMap(input_files.size());
        graph.entry_points_ = original_entry_points;

        std::vector<EntryPoint>& entry_points = graph.entry_points_;
        compiler::SymbolMap& symbols = graph.symbols;

        // Pre-tag every user-specified entry point so the same file is never
        // registered twice: once by the user and again by a dynamic import
        // that happens to target it.
        for (const EntryPoint& entry_point : entry_points) {
            graph.files[entry_point.source_index].entry_point_kind = EntryPointKind::kUserSpecified;
        }

        // Scan the reachable slice and clone each representation. All work
        // could in principle be spread across threads with one worker per
        // file and a mutex guarding the shared dynamic-import vector, but a
        // serial loop plus the stable-index sort below already yields exactly
        // the same deterministic ordering, so Guchho keeps it simple.
        std::vector<uint32_t> dynamic_import_entry_points;
        for (size_t stable_index = 0; stable_index < reachable_files.size(); stable_index++) {
            uint32_t source_index = reachable_files[stable_index];

            // Remember the position of this source index in the stable order,
            // giving later passes a reproducible way to sort by source index.
            graph.stable_source_indices[source_index] = static_cast<uint32_t>(stable_index);

            LinkerFile& file = graph.files[source_index];
            file.input_file = input_files[source_index];

            if (auto* js_repr = std::get_if<std::shared_ptr<JSRepr>>(&file.input_file.repr); js_repr && *js_repr) {
                // Deep-copy the JavaScript payload. Parts, named exports, and
                // import records are reproduced by the copy constructor, so
                // the clone can be rewritten freely without touching the
                // scanned original.
                auto clone = std::make_shared<JSRepr>(**js_repr);

                // Move the clone's own symbol vector into the graph-wide
                // table. The copy constructor gave the clone an independent
                // collection of symbols, so ownership can safely transfer;
                // each linking operation winds up with private symbol state.
                symbols.symbols_for_source[source_index] = std::move(clone->ast.symbols);

                // With code splitting active, each dynamic import() makes
                // its target a candidate entry point.
                if (code_splitting) {
                    for (compiler::ImportRecord& record : clone->ast.import_records) {
                        if (record.source_index.IsValid() && record.kind == compiler::ImportKind::kDynamic) {
                            dynamic_import_entry_points.push_back(record.source_index.GetIndex());

                            // Drop the import attributes declared on dynamic
                            // imports that became standalone entry points.
                            // Attributes such as
                            // "import('./data.json', { assert: { type: 'json' } })"
                            // describe the original resource; once the import
                            // points at generated code the attribute would
                            // block the load, so it must be removed.
                            record.assert_or_with = nullptr;
                        }
                    }
                }

                // Rebuild the export map from the clone's named exports.
                // Every entry starts out with no ambiguous star-export paths
                // and is tagged with this file's source index.
                std::unordered_map<std::string, ExportData> resolved_exports;
                resolved_exports.reserve(clone->ast.named_exports.size());
                for (const auto& [alias, named_export] : clone->ast.named_exports) {
                    resolved_exports.emplace(alias, ExportData{
                        .potentially_ambiguous_export_star_refs = {},
                        .ref = named_export.ref,
                        .name_loc = named_export.alias_loc,
                        .source_index = source_index,
                    });
                }

                // Clone the top-level scope so the linker can mint new
                // variables inside it. Scopes are heap-allocated and never
                // freed (the parser follows the same convention), so a raw
                // new is the appropriate allocation strategy.
                javascript::Scope* new_scope = nullptr;
                if (clone->ast.module_scope) {
                    new_scope = new javascript::Scope(*clone->ast.module_scope);

                    // Re-materialize the generated-symbol list so the clone
                    // owns a distinct allocation and shares no backing array
                    // with the original scope.
                    new_scope->generated = std::vector<compiler::Ref>(clone->ast.module_scope->generated);
                } else {
                    new_scope = new javascript::Scope();
                }
                clone->ast.module_scope = new_scope;

                // Attach the fresh export map and reset the two maps that are
                // exclusively owned by the linker, so the clone carries no
                // scan-phase leftovers that could disrupt later inserts.
                clone->meta.resolved_exports = std::move(resolved_exports);
                clone->meta.is_probably_typescript_type.clear();
                clone->meta.imports_to_bind.clear();

                file.input_file.repr = clone;
            } else if (auto* css_repr = std::get_if<std::shared_ptr<CSSRepr>>(&file.input_file.repr); css_repr && *css_repr) {
                // Deep-copy the stylesheet payload; its import records come
                // along with the copy constructor.
                auto clone = std::make_shared<CSSRepr>(**css_repr);

                // Move the clone's symbol vector into the graph-wide table.
                symbols.symbols_for_source[source_index] = std::move(clone->ast.symbols);

                file.input_file.repr = clone;
            } else if (auto* html_repr = std::get_if<std::shared_ptr<HTMLRepr>>(&file.input_file.repr); html_repr && *html_repr) {
                // Deep-copy the HTML payload. The tree is duplicated and the
                // per-record element pointers are re-pointed at the clone by
                // the copy constructor; HTML files own no linker symbols, so
                // the graph-wide symbol map entry stays untouched.
                auto clone = std::make_shared<HTMLRepr>(**html_repr);
                file.input_file.repr = clone;
            }

            // Every file begins as far from any entry point as possible; the
            // real distances are computed by later passes.
            file.distance_from_entry_point = UINT32_MAX;
        }

        // Back on a single thread, turn the collected dynamic imports into
        // entry points, skipping anything that already is one.
        std::vector<uint32_t> stable_entry_points;
        stable_entry_points.reserve(dynamic_import_entry_points.size());
        for (uint32_t source_index : dynamic_import_entry_points) {
            LinkerFile& other_file = graph.files[source_index];
            if (other_file.entry_point_kind == EntryPointKind::kNone) {
                stable_entry_points.push_back(graph.stable_source_indices[source_index]);
                other_file.entry_point_kind = EntryPointKind::kDynamicImport;
            }
        }

        // Sorting by stable index appends the dynamic entry points in one
        // reproducible order no matter how they were discovered.
        std::sort(stable_entry_points.begin(), stable_entry_points.end());
        for (uint32_t stable_index : stable_entry_points) {
            EntryPoint entry_point;
            entry_point.source_index = reachable_files[stable_index];
            entry_points.push_back(std::move(entry_point));
        }

        graph.reachable_files = reachable_files;

        // Final quick pass over the reachable files to finish setup that only
        // makes sense now that the whole graph is assembled.
        uint32_t bit_count = static_cast<uint32_t>(entry_points.size());
        for (uint32_t source_index : reachable_files) {
            LinkerFile& file = graph.files[source_index];

            // Size each file's entry-bit set from the final entry point count.
            file.entry_bits = helpers::BitSet(bit_count);

            if (const auto* js_repr = std::get_if<std::shared_ptr<JSRepr>>(&file.input_file.repr); js_repr && *js_repr) {
                const javascript::AST& ast = (*js_repr)->ast;

                // Fold every file's TypeScript enum tables into one
                // graph-wide map. Enums are rare relative to overall code, so
                // serial merging is cheap.
                for (const auto& [ref, enum_values] : ast.ts_enums) {
                    graph.ts_enums.insert_or_assign(ref, enum_values);
                }

                // Fold the safe-to-inline constants into one graph-wide map
                // the same way.
                for (const auto& [ref, value] : ast.const_values) {
                    graph.const_values.insert_or_assign(ref, value);
                }
            }
        }

        return graph;
    }

    // Appends "part" to the file at "source_index" and reports the index it
    // took. The part becomes visible to dependency wiring for every top-level
    // symbol it declares.
    //
    // Input:  source_index = 4, an empty part with a fresh symbol-uses map
    //         and an empty declared-symbol list (file 4 has parts 0..6)
    // Output: 7, the index the part now occupies; the AST's part vector grew
    //         by one
    uint32_t LinkerGraph::AddPartToFile(uint32_t source_index, javascript::Part part)
    {
        // Preserve the invariant that the symbol-uses map always exists: a
        // brand-new empty map is installed when the incoming part has none, so
        // no later write can ever meet a missing container.
        if (part.symbol_uses.empty() && part.symbol_uses.bucket_count() == 0) {
            part.symbol_uses = std::unordered_map<compiler::Ref, javascript::SymbolUse, javascript::RefHash>{};
        }

        JSRepr* repr = std::get<std::shared_ptr<JSRepr>>(files[source_index].input_file.repr).get();

        // The index of the new part is its future position; dependencies will
        // reference the part by that same number.
        uint32_t part_index = static_cast<uint32_t>(repr->ast.parts.size());

        // Register the part for each of its top-level declared symbols so the
        // symbol-to-parts mapping knows where the declaration lives.
        for (const javascript::DeclaredSymbol& declared_symbol : part.declared_symbols) {
            if (!declared_symbol.is_top_level) {
                continue;
            }

            // Prefer existing overlay entries; fall back to the original
            // assignments computed by the parser for first-time registration.
            std::vector<uint32_t> part_indices;
            auto overlay_it = repr->meta.top_level_symbol_to_parts_overlay.find(declared_symbol.ref);

            if (overlay_it != repr->meta.top_level_symbol_to_parts_overlay.end()) {
                part_indices = overlay_it->second;
            } else {
                // If the overlay has no entry yet, seed it from the parser map.
                auto parser_it = repr->ast.top_level_symbol_to_parts_from_parser.find(declared_symbol.ref);
                if (parser_it != repr->ast.top_level_symbol_to_parts_from_parser.end()) {
                    part_indices = parser_it->second;
                }
            }

            // Append the new part's index and write the extended list back
            // into the overlay.
            part_indices.push_back(part_index);
            repr->meta.top_level_symbol_to_parts_overlay.insert_or_assign(declared_symbol.ref, std::move(part_indices));
        }

        repr->ast.parts.push_back(std::move(part));
        return part_index;
    }

    // Mints a new symbol owned by "source_index": appends it to that file's
    // symbol vector, records it in the module scope's generated list, and
    // leaves its link slot invalid for the linker to fill in later. Returns a
    // reference other parts can import and use.
    //
    // Input:  source_index = 2, kind = kImport, original_name = "helper"
    //         (file 2 already carries five symbols)
    // Output: ref {2, 5} bound to the name "helper", now importable anywhere
    compiler::Ref LinkerGraph::GenerateNewSymbol(uint32_t source_index, compiler::SymbolKind kind, std::string original_name)
    {
        std::vector<compiler::Symbol>& source_symbols = symbols.symbols_for_source[source_index];

        compiler::Ref ref{source_index, static_cast<uint32_t>(source_symbols.size())};

        compiler::Symbol symbol;
        symbol.kind = kind;
        symbol.original_name = std::move(original_name);
        symbol.link = compiler::kInvalidRef;
        source_symbols.push_back(std::move(symbol));

        JSRepr* repr = std::get<std::shared_ptr<JSRepr>>(files[source_index].input_file.repr).get();
        repr->ast.module_scope->generated.push_back(ref);
        return ref;
    }

    // Registers that the part at ("source_index", "part_index") consumes the
    // symbol "ref" "use_count" times. If the symbol lives in another file, an
    // import is staged for the binding pass and every part that declares the
    // symbol is attached as a dependency. A zero use count makes the routine
    // a no-op.
    //
    // Input:  file 5, part 0, a ref declared in file 2, use_count = 3
    // Output: part 0's symbol-use estimate for "ref" rises by 3; an
    //         import-to-bind entry is created for "ref"; part 0 gains one
    //         dependency per file-2 part declaring "ref"
    void LinkerGraph::GenerateSymbolImportAndUse(
        uint32_t source_index,
        uint32_t part_index,
        compiler::Ref ref,
        uint32_t use_count,
        uint32_t source_index_to_import_from)
    {
        if (use_count == 0) {
            return;
        }

        JSRepr* repr = std::get<std::shared_ptr<JSRepr>>(files[source_index].input_file.repr).get();
        javascript::Part& part = repr->ast.parts[part_index];

        // Accumulate the use count on the part; tree shaking weighs these
        // numbers when deciding what can be dropped.
        part.symbol_uses[ref].count_estimate += use_count;

        // Flip the well-known flags when "exports" or "module" is used, so
        // CommonJS-aware rewriting knows the bindings are required.
        if (ref == repr->ast.exports_ref) {
            repr->ast.uses_exports_ref = true;
        }
        if (ref == repr->ast.module_ref) {
            repr->ast.uses_module_ref = true;
        }

        // Stage the cross-file import for the later binding pass; the source
        // index recorded on the ref is checked against the declared target to
        // catch wiring mistakes early.
        if (source_index_to_import_from != source_index) {
            assert(ref.source_index == source_index_to_import_from);
            ImportData import_data;
            import_data.ref = ref;
            import_data.source_index = source_index_to_import_from;
            repr->meta.imports_to_bind.insert_or_assign(ref, std::move(import_data));
        }

        // Make every declaring part a hard dependency so dropping any of them
        // would break this part's ability to use the symbol.
        JSRepr* target_repr = std::get<std::shared_ptr<JSRepr>>(files[source_index_to_import_from].input_file.repr).get();
        for (uint32_t target_part_index : target_repr->TopLevelSymbolToParts(ref)) {
            javascript::Dependency dependency;
            dependency.source_index = source_index_to_import_from;
            dependency.part_index = target_part_index;
            part.dependencies.push_back(dependency);
        }
    }

    // Shortcut for importing a helper from Guchho's built-in runtime library:
    // the runtime's export under "name" is resolved to a ref and handed to
    // GenerateSymbolImportAndUse exactly as if an ordinary module had imported
    // it.
    //
    // Input:  file 6, part 1, name = "__toESM", use_count = 2
    // Output: part 1 records a weighted use of the runtime's "__toESM" symbol
    //         with a dependency on the runtime file
    void LinkerGraph::GenerateRuntimeSymbolImportAndUse(
        uint32_t source_index,
        uint32_t part_index,
        std::string_view name,
        uint32_t use_count)
    {
        if (use_count == 0) {
            return;
        }

        JSRepr* runtime_repr = std::get<std::shared_ptr<JSRepr>>(files[javascript::kSourceIndex].input_file.repr).get();
        compiler::Ref ref = runtime_repr->ast.named_exports.at(std::string(name)).ref;
        GenerateSymbolImportAndUse(source_index, part_index, ref, use_count, javascript::kSourceIndex);
    }

}