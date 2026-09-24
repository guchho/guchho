#include "guchho/linker.hpp"

#include <cassert>
#include <cstring>
#include <unordered_set>

#include "guchho/javascript/js_renamer.hpp"
#include "guchho/javascript/js_runtime.hpp"


namespace guchho::linker {

    ////////////////////////////////////////////////////////////////////////////////
    // Prevent exports from being renamed (pass-through mode)
    //
    // Pass-through files (re-export-only modules and entry points) must keep their
    // public export names exactly as written, so this pass pins those symbols.
    ////////////////////////////////////////////////////////////////////////////////

    // Marks every exported name of a file as "must not be renamed".
    //
    // This is used for pass-through files where the public export names must stay
    // exactly as written so consumers see the original identifiers. Exported
    // declarations and functions have their symbol flagged kMustNotBeRenamed
    // (functions/classes are also made kUnbound); any import or export statement at
    // all sets the flag for the whole module scope, covering re-exports and export
    // clauses. Files with no imports or exports fall back to marking every
    // module-scope member.
    void LinkerContext::PreventExportsFromBeingRenamed(uint32_t source_index) {
        auto* repr_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&graph.files[source_index].input_file.repr);
        if (!repr_ptr) return;
        auto& repr = **repr_ptr;
        bool has_import_or_export = false;

        for (auto& part : repr.ast.parts) {
            for (auto& stmt : part.stmts) {
                if (auto* import = std::get_if<std::shared_ptr<javascript::SImport>>(&stmt.data)) {
                    if (repr.ast.import_records[(*import)->import_record_index].source_index.IsValid()) {
                        continue;
                    }
                    has_import_or_export = true;
                } else if (auto* local = std::get_if<std::shared_ptr<javascript::SLocal>>(&stmt.data)) {
                    if ((*local)->is_export) {
                        javascript::ForEachIdentifierBindingInDecls((*local)->decls,
                            [&](logger::Loc, javascript::BIdentifier& b) {
                                graph.symbols.Get(b.ref)->flags = static_cast<compiler::SymbolFlags>(
                                    static_cast<uint16_t>(graph.symbols.Get(b.ref)->flags) | static_cast<uint16_t>(compiler::SymbolFlags::kMustNotBeRenamed));
                            });
                        has_import_or_export = true;
                    }
                } else if (auto* func = std::get_if<std::shared_ptr<javascript::SFunction>>(&stmt.data)) {
                    if ((*func)->is_export && (*func)->fn.name) {
                        graph.symbols.Get((*func)->fn.name->ref)->kind = compiler::SymbolKind::kUnbound;
                        has_import_or_export = true;
                    }
                } else if (auto* cls = std::get_if<std::shared_ptr<javascript::SClass>>(&stmt.data)) {
                    if ((*cls)->is_export && (*cls)->class_.name) {
                        graph.symbols.Get((*cls)->class_.name->ref)->kind = compiler::SymbolKind::kUnbound;
                        has_import_or_export = true;
                    }
                } else if (std::holds_alternative<std::shared_ptr<javascript::SExportClause>>(stmt.data) ||
                           std::holds_alternative<std::shared_ptr<javascript::SExportDefault>>(stmt.data) ||
                           std::holds_alternative<std::shared_ptr<javascript::SExportStar>>(stmt.data) ||
                           std::holds_alternative<std::shared_ptr<javascript::SExportFrom>>(stmt.data)) {
                    has_import_or_export = true;
                }
            }
        }

        if (!has_import_or_export && repr.ast.module_scope) {
            for (auto& [name, member] : repr.ast.module_scope->members) {
                graph.symbols.Get(member.ref)->flags = static_cast<compiler::SymbolFlags>(
                    static_cast<uint16_t>(graph.symbols.Get(member.ref)->flags) | static_cast<uint16_t>(compiler::SymbolFlags::kMustNotBeRenamed));
            }
        }
    }


    ////////////////////////////////////////////////////////////////////////////////
    // Property mangling
    //
    // Assigns short minified names to object property names across all reachable
    // files, merging same-named properties and keeping existing cache mappings.
    ////////////////////////////////////////////////////////////////////////////////

    // Assigns short minified names to object property names across all files.
    //
    // A global pool of property symbols is built by merging the same property name
    // from every reachable file, then sorted by estimated use count so the most
    // frequently used names get the shortest minified names. Names already present
    // in the persistent cache keep their existing mapping, and JavaScript keywords
    // plus properties the cache marks as reserved are never used. `mangle_cache` is
    // both read (existing mappings) and updated (new mappings), so later builds
    // stay stable.
    //
    // Input : mangle_cache mapping original property names to reserved flags.
    // Output: mangled_props mapping each property symbol to its minified name.
    void LinkerContext::MangleProps(std::unordered_map<std::string, bool>& mangle_cache) {
        if (timer) timer->Begin("Mangle props");
        auto finally = [&]() { if (timer) timer->End("Mangle props"); };
        struct RAIIEnd { std::function<void()> fn; ~RAIIEnd() { fn(); } } raii_end{finally};

        mangled_props.clear();

        std::unordered_map<std::string, bool> reserved_props;
        static const std::vector<std::string_view> js_keywords = {
            "await", "break", "case", "catch", "class", "const", "continue",
            "debugger", "default", "delete", "do", "else", "enum", "export",
            "extends", "false", "finally", "for", "function", "if", "import",
            "in", "instanceof", "let", "new", "null", "return", "super", "switch",
            "this", "throw", "true", "try", "typeof", "var", "void", "while",
            "with", "yield", "async", "from", "get", "of", "set", "static",
            "constructor",
        };
        for (auto kw : js_keywords) {
            reserved_props[std::string(kw)] = true;
        }

        for (auto& [original, reserved] : mangle_cache) {
            if (reserved) {
                reserved_props[original] = true;
            }
        }

        compiler::CharFreq freq;
        std::unordered_map<std::string, compiler::Ref> merged_props;
        for (auto source_index : graph.reachable_files) {
            if (source_index == javascript::kSourceIndex) continue;

            auto* repr_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&graph.files[source_index].input_file.repr);
            if (!repr_ptr) continue;
            auto& repr = **repr_ptr;

            for (auto& [prop, _] : repr.ast.reserved_props) {
                reserved_props[prop] = true;
            }

            for (auto& [name, ref] : repr.ast.mangled_props) {
                if (merged_props.count(name)) {
                    compiler::MergeSymbols(graph.symbols, ref, merged_props[name]);
                } else {
                    merged_props[name] = ref;
                }
            }

            if (repr.ast.char_freq) {
                freq.Include(*repr.ast.char_freq);
            }
        }

        javascript::StableSymbolCountArray sorted;
        sorted.reserve(merged_props.size());
        for (auto& [_, ref] : merged_props) {
            sorted.push_back({
                .stable_source_index = graph.stable_source_indices[ref.source_index],
                .ref = ref,
                .count = graph.symbols.Get(ref)->use_count_estimate,
            });
        }
        javascript::SortStableSymbolCounts(sorted);

        auto minifier = compiler::kDefaultNameMinifierJS.ShuffleByCharFreq(freq);
        int next_name = 0;
        for (auto& symbol_count : sorted) {
            auto* symbol = graph.symbols.Get(symbol_count.ref);

            if (mangle_cache.count(symbol->original_name)) {
                continue;
            }

            auto name = minifier.NumberToMinifiedName(next_name);
            next_name++;

            while (reserved_props[name]) {
                name = minifier.NumberToMinifiedName(next_name);
                next_name++;
            }

            mangle_cache[symbol->original_name] = true;
            mangled_props[symbol_count.ref] = name;
        }
    }


    ////////////////////////////////////////////////////////////////////////////////
    // CSS local identifier mangling
    //
    // Assigns short names to local CSS class identifiers, avoiding global CSS names
    // and names already claimed by another file.
    ////////////////////////////////////////////////////////////////////////////////

    // Assigns short names to local CSS class identifiers across all files.
    //
    // Every non-global CSS symbol is collected, sorted by use count, and renamed.
    // With MinifyIdentifiers the names are minified while avoiding any global CSS
    // name or name already claimed by another file; otherwise a readable
    // "<source>_<original>" name is produced with a numeric suffix on collision.
    // `used_local_names` is shared across files so two files never pick the same
    // local name.
    //
    // Input : used_local_names set of names already taken in this build.
    // Output: mangled_props mapping each CSS symbol to its assigned name.
    void LinkerContext::MangleLocalCSS(std::unordered_map<std::string, bool>& used_local_names) {
        if (timer) timer->Begin("Mangle local CSS");
        auto finally = [&]() { if (timer) timer->End("Mangle local CSS"); };
        struct RAIIEnd { std::function<void()> fn; ~RAIIEnd() { fn(); } } raii_end{finally};

        std::unordered_map<std::string, bool> global_names;
        std::unordered_set<compiler::Ref, javascript::RefHash> local_names_set;
        compiler::CharFreq freq;

        for (auto source_index : graph.reachable_files) {
            auto* repr_ptr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&graph.files[source_index].input_file.repr);
            if (!repr_ptr) continue;

            auto& symbols_for_source = graph.symbols.symbols_for_source;
            if (source_index >= symbols_for_source.size()) continue;

            for (size_t inner_index = 0; inner_index < symbols_for_source[source_index].size(); inner_index++) {
                auto& symbol = symbols_for_source[source_index][inner_index];
                if (symbol.kind == compiler::SymbolKind::kGlobalCSS) {
                    global_names[symbol.original_name] = true;
                } else {
                    compiler::Ref ref = {.source_index = source_index, .inner_index = static_cast<uint32_t>(inner_index)};
                    ref = compiler::FollowSymbols(graph.symbols, ref);
                    local_names_set.insert(ref);
                }
            }

            auto& repr = **repr_ptr;
            if (repr.ast.char_freq) {
                freq.Include(*repr.ast.char_freq);
            }
        }

        javascript::StableSymbolCountArray sorted;
        sorted.reserve(local_names_set.size());
        for (auto& ref : local_names_set) {
            sorted.push_back({
                .stable_source_index = graph.stable_source_indices[ref.source_index],
                .ref = ref,
                .count = graph.symbols.Get(ref)->use_count_estimate,
            });
        }
        javascript::SortStableSymbolCounts(sorted);

        if (options->MinifyIdentifiers) {
            auto minifier = compiler::kDefaultNameMinifierCSS.ShuffleByCharFreq(freq);
            int next_name = 0;
            for (auto& symbol_count : sorted) {
                auto name = minifier.NumberToMinifiedName(next_name);
                while (global_names[name] || used_local_names[name]) {
                    next_name++;
                    name = minifier.NumberToMinifiedName(next_name);
                }
                mangled_props[symbol_count.ref] = name;
                used_local_names[name] = true;
            }
        } else {
            std::unordered_map<std::string, uint32_t> name_counts;
            for (auto& symbol_count : sorted) {
                auto* symbol = graph.symbols.Get(symbol_count.ref);
                auto name = graph.files[symbol_count.ref.source_index].input_file.source.identifier_name +
                    "_" + symbol->original_name;

                if (global_names[name] || used_local_names[name]) {
                    uint32_t tries = 1;
                    if (name_counts.count(name)) {
                        tries = name_counts[name];
                    }
                    auto prefix = name;
                    while (true) {
                        tries++;
                        name = prefix + std::to_string(tries);
                        if (!global_names[name] && !used_local_names[name]) {
                            name_counts[prefix] = tries;
                            break;
                        }
                    }
                }
                mangled_props[symbol_count.ref] = name;
                used_local_names[name] = true;
            }
        }
    }

}
