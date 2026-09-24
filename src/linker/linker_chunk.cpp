#include "guchho/linker.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <unordered_set>
#include "guchho/bundler.hpp"
#include "guchho/javascript/js_renamer.hpp"
#include "guchho/javascript/js_runtime.hpp"

namespace guchho::linker {

    ////////////////////////////////////////////////////////////////////////////////
    // Statement conversion
    //
    // Small helpers that compact the per-chunk statement and part lists before the
    // import/export statements are rewritten into their output form.
    ////////////////////////////////////////////////////////////////////////////////

    // Appends a one-part range, or extends the previous range when the new part
    // continues the same file's range. This keeps the per-chunk part list compact
    // when consecutive parts come from the same file.
    //
    // Input : ranges plus (source_index, part_index) for one part.
    // Output: the range that now covers that part (existing extended, or new).
    static PartRange AppendOrExtendPartRange(std::vector<PartRange>& ranges, uint32_t source_index, uint32_t part_index) {
        if (!ranges.empty()) {
            auto& last = ranges.back();
            if (last.source_index == source_index && last.part_index_end == part_index) {
                last.part_index_end = part_index + 1;
                return last;
            }
        }
        ranges.push_back(PartRange{
            .source_index = source_index,
            .part_index_begin = part_index,
            .part_index_end = part_index + 1,
        });
        return ranges.back();
    }

    // Fuses runs of adjacent local variable statements of the same kind and export
    // status into a single statement, so "var a = 1; var b = 2;" becomes
    // "var a = 1, b = 2;". The first statement of a run is cloned before merging so
    // the original AST is not mutated; non-mergeable statements are moved down and
    // the vector is shrunk to the number of surviving statements.
    //
    // Input : a mutable statement list.
    // Output: the same list with adjacent local statements merged.
    std::vector<javascript::Stmt> MergeAdjacentLocalStmts(std::vector<javascript::Stmt>& stmts) {
        if (stmts.empty()) return stmts;

        bool did_merge_with_previous_local = false;
        size_t end = 1;

        for (size_t i = 1; i < stmts.size(); i++) {
            auto& stmt = stmts[i];

            if (auto* after = std::get_if<std::shared_ptr<javascript::SLocal>>(&stmt.data)) {
                if (auto* before = std::get_if<std::shared_ptr<javascript::SLocal>>(&stmts[end - 1].data)) {
                    if ((*before)->kind == (*after)->kind && (*before)->is_export == (*after)->is_export) {
                        if (did_merge_with_previous_local) {
                            (*before)->decls.insert((*before)->decls.end(), (*after)->decls.begin(), (*after)->decls.end());
                        } else {
                            did_merge_with_previous_local = true;
                            auto clone = std::make_shared<javascript::SLocal>(**before);
                            clone->decls.reserve((*before)->decls.size() + (*after)->decls.size());
                            clone->decls.insert(clone->decls.end(), (*before)->decls.begin(), (*before)->decls.end());
                            clone->decls.insert(clone->decls.end(), (*after)->decls.begin(), (*after)->decls.end());
                            stmts[end - 1].data = std::move(clone);
                        }
                        continue;
                    }
                }
            }

            did_merge_with_previous_local = false;
            stmts[end] = std::move(stmt);
            end++;
        }

        stmts.resize(end);
        return std::move(stmts);
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Chunk computation
    //
    // Decides how the module graph is split into output files: one JS (and often
    // one companion CSS) chunk per entry point, with shared code factored into
    // chunks keyed by their entry-point bit set, then computes the cross-chunk
    // symbol imports/exports needed when code splitting is enabled.
    ////////////////////////////////////////////////////////////////////////////////

    // Builds the output chunks from the module graph.
    //
    // Entry points each get a JS chunk and, when they import CSS, a companion CSS
    // chunk; non-entry JS files are grouped by their entry-point bit set so shared
    // code lands in its own chunk. CSS files that are entry points themselves are
    // excluded from JS chunks to avoid duplicating their rules. Chunks are sorted
    // by key for determinism, entry-point files are mapped to their chunk, the
    // order of files and parts inside each JS chunk is precomputed, and each chunk
    // gets a padded unique key and its output path template applied.
    //
    // Input : graph with liveness, entry bits and distances computed.
    // Output: chunks fully populated and each entry-point file mapped to its chunk.
    void LinkerContext::ComputeChunks() {
        timer->Begin("Compute chunks");

        std::unordered_map<std::string, ChunkInfo> js_chunks;
        std::unordered_map<std::string, ChunkInfo> css_chunks;

        std::unordered_set<uint32_t> css_entry_sources;
        for (const graph::EntryPoint& ep : graph.EntryPoints()) {
            if (std::holds_alternative<std::shared_ptr<graph::CSSRepr>>(
                    graph.files[ep.source_index].input_file.repr)) {
                css_entry_sources.insert(ep.source_index);
            }
        }
        for (uint32_t source_index : separate_css_entry_sources) {
            css_entry_sources.insert(source_index);
        }

        for (uint32_t i = 0; i < graph.EntryPoints().size(); i++) {
            auto& entry_point = graph.EntryPoints()[i];
            auto& file = graph.files[entry_point.source_index];

            auto entry_bits = helpers::BitSet(static_cast<uint32_t>(graph.EntryPoints().size()));
            entry_bits.SetBit(i, true);
            auto key = entry_bits.ToString();

            ChunkInfo chunk;
            chunk.entry_bits = entry_bits;
            chunk.is_entry_point = true;
            chunk.source_index = entry_point.source_index;
            chunk.entry_point_bit = i;

            if (std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr)) {
                ChunkReprJS chunk_repr;
                chunk.chunk_repr = std::move(chunk_repr);
                js_chunks[key] = std::move(chunk);

                auto css_source_indices = FindImportedCSSFilesInJSOrder(entry_point.source_index);
                css_source_indices.erase(
                    std::remove_if(css_source_indices.begin(), css_source_indices.end(),
                                   [&](uint32_t source_index) {
                                       return css_entry_sources.count(source_index) != 0;
                                   }),
                    css_source_indices.end());
                if (!css_source_indices.empty()) {
                    auto order = FindImportedFilesInCSSOrder(css_source_indices);
                    std::unordered_map<uint32_t, bool> css_files_with_parts_in_chunk;
                    for (auto& entry : order) {
                        if (entry.kind == CssImportKind::kSourceIndex) {
                            css_files_with_parts_in_chunk[entry.source_index] = true;
                        }
                    }
                    ChunkInfo css_chunk;
                    css_chunk.entry_bits = entry_bits;
                    css_chunk.is_entry_point = true;
                    css_chunk.source_index = entry_point.source_index;
                    css_chunk.entry_point_bit = i;
                    css_chunk.files_with_parts_in_chunk = std::move(css_files_with_parts_in_chunk);
                    ChunkReprCSS css_chunk_repr;
                    css_chunk_repr.imports_in_chunk_in_order = std::move(order);
                    css_chunk.chunk_repr = std::move(css_chunk_repr);
                    css_chunks[key] = std::move(css_chunk);

                    auto& js_chunk = js_chunks[key];
                    auto& js_repr = std::get<ChunkReprJS>(js_chunk.chunk_repr);
                    js_repr.has_css_chunk = true;
                }
            } else if (std::get_if<std::shared_ptr<graph::CSSRepr>>(&file.input_file.repr)) {
                auto order = FindImportedFilesInCSSOrder({entry_point.source_index});
                for (auto& entry : order) {
                    if (entry.kind == CssImportKind::kSourceIndex) {
                        chunk.files_with_parts_in_chunk[entry.source_index] = true;
                    }
                }
                ChunkReprCSS chunk_repr;
                chunk_repr.imports_in_chunk_in_order = std::move(order);
                chunk.chunk_repr = std::move(chunk_repr);
                css_chunks[key] = std::move(chunk);
            }
        }

        for (auto source_index : graph.reachable_files) {
            auto& file = graph.files[source_index];
            if (file.is_live && std::holds_alternative<std::shared_ptr<graph::JSRepr>>(file.input_file.repr)) {
                auto key = file.entry_bits.ToString();
                auto it = js_chunks.find(key);
                if (it == js_chunks.end()) {
                    ChunkInfo chunk;
                    chunk.entry_bits = file.entry_bits;
                    chunk.chunk_repr = ChunkReprJS{};
                    js_chunks[key] = std::move(chunk);
                    it = js_chunks.find(key);
                }
                it->second.files_with_parts_in_chunk[source_index] = true;
            }
        }

        std::vector<ChunkInfo> sorted_chunks;
        std::vector<std::string> sorted_keys;
        sorted_keys.reserve(js_chunks.size() + css_chunks.size());
        for (auto& [key, _] : js_chunks) {
            sorted_keys.push_back(key);
        }
        std::sort(sorted_keys.begin(), sorted_keys.end());

        std::unordered_map<std::string, uint32_t> js_chunk_indices_for_css;
        for (auto& key : sorted_keys) {
            auto chunk_it = js_chunks.find(key);
            if (auto* js_repr = std::get_if<ChunkReprJS>(&chunk_it->second.chunk_repr)) {
                if (js_repr->has_css_chunk) {
                    js_chunk_indices_for_css[key] = static_cast<uint32_t>(sorted_chunks.size());
                }
            }
            sorted_chunks.push_back(std::move(chunk_it->second));
        }

        sorted_keys.clear();
        for (auto& [key, _] : css_chunks) {
            sorted_keys.push_back(key);
        }
        std::sort(sorted_keys.begin(), sorted_keys.end());
        for (auto& key : sorted_keys) {
            auto chunk_it = css_chunks.find(key);
            auto css_it = js_chunk_indices_for_css.find(key);
            if (css_it != js_chunk_indices_for_css.end()) {
                auto& js_repr = std::get<ChunkReprJS>(sorted_chunks[css_it->second].chunk_repr);
                js_repr.css_chunk_index = static_cast<uint32_t>(sorted_chunks.size());
            }
            sorted_chunks.push_back(std::move(chunk_it->second));
        }

        for (uint32_t chunk_index = 0; chunk_index < sorted_chunks.size(); chunk_index++) {
            auto& chunk = sorted_chunks[chunk_index];
            if (chunk.is_entry_point) {
                auto& file = graph.files[chunk.source_index];
                if (std::holds_alternative<ChunkReprCSS>(chunk.chunk_repr) &&
                    std::holds_alternative<std::shared_ptr<graph::JSRepr>>(file.input_file.repr)) {
                    continue;
                }
                file.entry_point_chunk_index = chunk_index;
            }
        }

        for (auto& chunk : sorted_chunks) {
            if (auto* chunk_repr = std::get_if<ChunkReprJS>(&chunk.chunk_repr)) {
                FindImportedPartsInJSOrder(chunk, chunk_repr->files_in_chunk_in_order, chunk_repr->parts_in_chunk_in_order);
            }
        }

        for (uint32_t chunk_index = 0; chunk_index < sorted_chunks.size(); chunk_index++) {
            auto& chunk = sorted_chunks[chunk_index];

            chunk.unique_key = unique_key_prefix + "C" + std::to_string(chunk_index);
            while (chunk.unique_key.size() < unique_key_prefix.size() + 9) {
                chunk.unique_key.insert(chunk.unique_key.begin() + static_cast<std::string::difference_type>(unique_key_prefix.size() + 1), '0');
            }

            std::string std_ext;
            if (std::holds_alternative<ChunkReprJS>(chunk.chunk_repr)) {
                std_ext = options->OutputExtensionJS;
            } else {
                std_ext = options->OutputExtensionCSS;
            }

            std::string dir, base, ext;
            std::vector<config::PathTemplate> tmpl;
            if (chunk.is_entry_point) {
                auto& file = graph.files[chunk.source_index];
                if (file.IsUserSpecifiedEntryPoint()) {
                    tmpl = options->EntryPathTemplate;
                } else {
                    tmpl = options->ChunkPathTemplate;
                }

                if (!options->AbsOutputFile.empty()) {
                    dir = "/";
                    base = fs->Base(options->AbsOutputFile);
                    auto original_ext = fs->Ext(base);
                    base = base.substr(0, base.size() - original_ext.size());

                    if (std::holds_alternative<std::shared_ptr<graph::CSSRepr>>(file.input_file.repr) ||
                        std_ext != options->OutputExtensionCSS) {
                        ext = original_ext;
                    } else {
                        ext = std_ext;
                    }
                } else {
                    auto [rel_dir, rel_base] = bundler::PathRelativeToOutbase(
                        file.input_file, *options, *fs,
                        !file.IsUserSpecifiedEntryPoint(),
                        graph.EntryPoints()[chunk.entry_point_bit].output_path);
                    dir = rel_dir;
                    base = rel_base;
                    ext = std_ext;
                }
            } else {
                dir = "/";
                base = "chunk";
                ext = std_ext;
                tmpl = options->ChunkPathTemplate;
            }

            auto template_ext = ext;
            if (!template_ext.empty() && template_ext[0] == '.') {
                template_ext = template_ext.substr(1);
            }
            tmpl.push_back({.Data = ext});
            chunk.final_template = config::SubstituteTemplate(tmpl, config::PathPlaceholders{
                .Dir = &dir,
                .Name = &base,
                .Ext = &template_ext,
            });
        }

        chunks = std::move(sorted_chunks);
        timer->End("Compute chunks");
    }


    // Computes, for every JS chunk, the symbols it imports from and exports to
    // other chunks when code splitting is on.
    //
    // It collects the symbols used by each chunk (following imports to their bound
    // targets and substituting namespace symbols), rewrites external dynamic
    // imports to point at the target entry point's chunk, records which chunk
    // declares each top-level symbol, marks the needed symbols as exports of their
    // declaring chunk, and then emits the cross-chunk export clauses and import
    // statements (sorted for determinism). Entry-point chunks also import every
    // chunk belonging to their entry point.
    //
    // Input : chunks computed by ComputeChunks.
    // Output: cross_chunk_imports, imports_from_other_chunks and
    //         exports_to_other_chunks populated for each JS chunk.
    void LinkerContext::ComputeCrossChunkDependencies() {
        timer->Begin("Compute cross-chunk dependencies");

        if (!options->CodeSplitting) {
            timer->End("Compute cross-chunk dependencies");
            return;
        }

        struct ChunkMeta {
            std::unordered_set<compiler::Ref, javascript::RefHash> imports;
            std::unordered_set<compiler::Ref, javascript::RefHash> exports;
            std::unordered_set<uint32_t> dynamic_imports;
        };

        std::vector<ChunkMeta> chunk_metas(chunks.size());

        for (uint32_t chunk_index = 0; chunk_index < chunks.size(); chunk_index++) {
            auto& chunk = chunks[chunk_index];
            auto* chunk_repr = std::get_if<ChunkReprJS>(&chunk.chunk_repr);
            if (!chunk_repr) continue;

            auto& chunk_meta = chunk_metas[chunk_index];

            for (auto [source_index, _] : chunk.files_with_parts_in_chunk) {
                auto& file = graph.files[source_index];
                auto* js_repr_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr);
                if (!js_repr_ptr) continue;
                auto& js_repr = *js_repr_ptr;

                for (uint32_t part_index = 0; part_index < js_repr->ast.parts.size(); part_index++) {
                    auto& part_meta = js_repr->ast.parts[part_index];
                    if (!part_meta.is_live) continue;
                    auto& part = js_repr->ast.parts[part_index];

                    for (auto import_record_index : part.import_record_indices) {
                        auto& record = js_repr->ast.import_records[import_record_index];
                        if (record.source_index.IsValid() && IsExternalDynamicImport(record, source_index)) {
                            uint32_t other_chunk_index = graph.files[record.source_index.GetIndex()].entry_point_chunk_index;
                            record.path.text = chunks[other_chunk_index].unique_key;
                            record.source_index = compiler::Index32{};
                            record.flags = record.flags | compiler::ImportRecordFlags::kShouldNotBeExternalInMetafile |
                                            compiler::ImportRecordFlags::kContainsUniqueKey;

                            if (other_chunk_index != chunk_index) {
                                chunk_meta.dynamic_imports.insert(other_chunk_index);
                            }
                        }
                    }

                    for (auto& declared : part.declared_symbols) {
                        if (declared.is_top_level) {
                            graph.symbols.Get(declared.ref)->chunk_index = compiler::Index32::Make(chunk_index);
                        }
                    }

                    for (auto& [use_ref, _] : part.symbol_uses) {
                        auto ref = use_ref;
                        auto* symbol = graph.symbols.Get(ref);

                        if (symbol->kind == compiler::SymbolKind::kUnbound) continue;

                        if (symbol->import_item_status == compiler::ImportItemStatus::kMissing) continue;

                        auto imports_it = js_repr->meta.imports_to_bind.find(ref);
                        if (imports_it != js_repr->meta.imports_to_bind.end()) {
                            ref = imports_it->second.ref;
                            symbol = graph.symbols.Get(ref);
                        } else if (js_repr->meta.wrap == graph::WrapKind::kCJS && ref != js_repr->ast.wrapper_ref) {
                            continue;
                        }

                        if (symbol->namespace_alias) {
                            ref = symbol->namespace_alias->namespace_ref;
                        }

                        chunk_meta.imports.insert(ref);
                    }
                }
            }

            if (chunk.is_entry_point) {
                auto& file = graph.files[chunk.source_index];
                if (auto* js_repr_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr)) {
                    auto& js_repr = *js_repr_ptr;
                    if (js_repr->meta.wrap != graph::WrapKind::kCJS) {
                        for (auto& alias : js_repr->meta.sorted_and_filtered_export_aliases) {
                            auto export_it = js_repr->meta.resolved_exports.find(alias);
                            if (export_it == js_repr->meta.resolved_exports.end()) continue;
                            auto& export_data = export_it->second;
                            auto target_ref = export_data.ref;

                            auto& other_file = graph.files[export_data.source_index];
                            if (auto* other_js_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&other_file.input_file.repr)) {
                                auto other_import_it = (*other_js_ptr)->meta.imports_to_bind.find(target_ref);
                                if (other_import_it != (*other_js_ptr)->meta.imports_to_bind.end()) {
                                    target_ref = other_import_it->second.ref;
                                }
                            }

                            auto* target_symbol = graph.symbols.Get(target_ref);
                            if (target_symbol->namespace_alias) {
                                target_ref = target_symbol->namespace_alias->namespace_ref;
                            }

                            chunk_meta.imports.insert(target_ref);
                        }
                    }

                    if (js_repr->meta.force_include_exports_for_entry_point) {
                        chunk_meta.imports.insert(js_repr->ast.exports_ref);
                    }

                    if (js_repr->meta.wrap != graph::WrapKind::kNone) {
                        chunk_meta.imports.insert(js_repr->ast.wrapper_ref);
                    }
                }
            }
        }

        for (uint32_t chunk_index = 0; chunk_index < chunks.size(); chunk_index++) {
            auto& chunk = chunks[chunk_index];
            auto* chunk_repr = std::get_if<ChunkReprJS>(&chunk.chunk_repr);
            if (!chunk_repr) continue;
            auto& chunk_meta = chunk_metas[chunk_index];

            for (auto& import_ref : chunk_meta.imports) {
                auto* symbol = graph.symbols.Get(import_ref);
                if (symbol->chunk_index.IsValid()) {
                    uint32_t other_chunk_index = symbol->chunk_index.GetIndex();
                    if (other_chunk_index != chunk_index) {
                        chunk_repr->imports_from_other_chunks[other_chunk_index].push_back(
                            CrossChunkImportItem{.ref = import_ref});
                        chunk_metas[other_chunk_index].exports.insert(import_ref);
                    }
                }
            }

            if (chunk.is_entry_point) {
                for (uint32_t other_chunk_index = 0; other_chunk_index < chunks.size(); other_chunk_index++) {
                    auto& other_chunk = chunks[other_chunk_index];
                    if (std::holds_alternative<ChunkReprJS>(other_chunk.chunk_repr) &&
                        other_chunk_index != chunk_index &&
                        other_chunk.entry_bits.HasBit(chunk.entry_point_bit)) {
                        auto& items = chunk_repr->imports_from_other_chunks[other_chunk_index];
                        (void)items;
                    }
                }
            }

            for (auto dynamic_chunk_index : chunk_meta.dynamic_imports) {
                chunk.cross_chunk_imports.push_back(ChunkImport{
                    .chunk_index = dynamic_chunk_index,
                    .import_kind = compiler::ImportKind::kDynamic,
                });
            }
        }

        for (uint32_t chunk_index = 0; chunk_index < chunks.size(); chunk_index++) {
            auto& chunk = chunks[chunk_index];
            auto* chunk_repr = std::get_if<ChunkReprJS>(&chunk.chunk_repr);
            if (!chunk_repr) continue;

            std::vector<StableRef> sorted_exports;
            for (auto& ref : chunk_metas[chunk_index].exports) {
                sorted_exports.push_back(StableRef{
                    .stable_source_index = graph.stable_source_indices[ref.source_index],
                    .ref = ref,
                });
            }
            std::sort(sorted_exports.begin(), sorted_exports.end(),
                [](const StableRef& a, const StableRef& b) {
                    if (a.stable_source_index != b.stable_source_index)
                        return a.stable_source_index < b.stable_source_index;
                    return a.ref.inner_index < b.ref.inner_index;
                });

            if (options->OutputFormat == config::Format::kESModule) {
                javascript::ExportRenamer r;
                std::vector<javascript::ClauseItem> items;
                for (auto& export_ref : sorted_exports) {
                    std::string alias;
                    if (options->MinifyIdentifiers) {
                        alias = r.NextMinifiedName();
                    } else {
                        alias = r.NextRenamedName(graph.symbols.Get(export_ref.ref)->original_name);
                    }
                    items.push_back(javascript::ClauseItem{
                        .alias = alias,
                        .name = {.ref = export_ref.ref},
                    });
                    chunk_repr->exports_to_other_chunks[export_ref.ref] = alias;
                }
                if (!items.empty()) {
                    auto sexport = std::make_shared<javascript::SExportClause>();
                    sexport->items = std::move(items);
                    chunk_repr->cross_chunk_suffix_stmts.push_back(javascript::Stmt{
                        .data = std::move(sexport),
                        .loc = {},
                    });
                }
            }
        }

        for (uint32_t chunk_index = 0; chunk_index < chunks.size(); chunk_index++) {
            auto& chunk = chunks[chunk_index];
            auto* chunk_repr = std::get_if<ChunkReprJS>(&chunk.chunk_repr);
            if (!chunk_repr) continue;

            std::vector<std::pair<uint32_t, CrossChunkImportItemArray*>> sorted_import_groups;
            for (auto& [other_chunk_index, items] : chunk_repr->imports_from_other_chunks) {
                sorted_import_groups.push_back({other_chunk_index, &items});
            }
            std::sort(sorted_import_groups.begin(), sorted_import_groups.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });

            std::vector<javascript::Stmt> cross_chunk_prefix_stmts;

            for (auto& [other_chunk_index, items_ptr] : sorted_import_groups) {
                auto& items = *items_ptr;

                auto& other_chunk = chunks[other_chunk_index];
                auto* other_repr = std::get_if<ChunkReprJS>(&other_chunk.chunk_repr);
                for (auto& item : items) {
                    auto it = other_repr->exports_to_other_chunks.find(item.ref);
                    if (it != other_repr->exports_to_other_chunks.end()) {
                        item.export_alias = it->second;
                    }
                }

                std::sort(items.begin(), items.end(),
                    [](const CrossChunkImportItem& a, const CrossChunkImportItem& b) {
                        return a.export_alias < b.export_alias;
                    });

                uint32_t import_record_index = static_cast<uint32_t>(chunk.cross_chunk_imports.size());
                chunk.cross_chunk_imports.push_back(ChunkImport{
                    .chunk_index = other_chunk_index,
                    .import_kind = compiler::ImportKind::kStmt,
                });

                if (options->OutputFormat == config::Format::kESModule) {
                    if (!items.empty()) {
                        auto items_vec = std::make_shared<std::vector<javascript::ClauseItem>>();
                        for (auto& item : items) {
                            items_vec->push_back(javascript::ClauseItem{
                                .alias = item.export_alias,
                                .name = {.ref = item.ref},
                            });
                        }
                        auto simport = std::make_shared<javascript::SImport>();
                        simport->items = std::move(items_vec);
                        simport->import_record_index = import_record_index;
                        cross_chunk_prefix_stmts.push_back(javascript::Stmt{
                            .data = std::move(simport),
                            .loc = {},
                        });
                    } else {
                        auto simport = std::make_shared<javascript::SImport>();
                        simport->import_record_index = import_record_index;
                        cross_chunk_prefix_stmts.push_back(javascript::Stmt{
                            .data = std::move(simport),
                            .loc = {},
                        });
                    }
                }
            }

            chunk_repr->cross_chunk_prefix_stmts = std::move(cross_chunk_prefix_stmts);
        }

        timer->End("Compute cross-chunk dependencies");
    }



    // Returns false for a part that is only a single import statement of an
    // internal, unwrapped file. Such a part would be dropped later anyway, so
    // skipping it early avoids doing work for it.
    bool LinkerContext::ShouldIncludePart(graph::JSRepr& repr, const javascript::Part& part) {
        if (part.stmts.size() == 1) {
            if (auto* simport = std::get_if<std::shared_ptr<javascript::SImport>>(&part.stmts[0].data)) {
                auto& record = repr.ast.import_records[(*simport)->import_record_index];
                if (record.source_index.IsValid() &&
                    std::holds_alternative<std::shared_ptr<graph::JSRepr>>(graph.files[record.source_index.GetIndex()].input_file.repr)) {
                    auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[record.source_index.GetIndex()].input_file.repr);
                    if (other_repr.meta.wrap == graph::WrapKind::kNone) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    // Linearises the live files and parts of a JS chunk into output order.
    //
    // Files are visited closest-to-entry-point first, with a stable tie-breaker,
    // and the graph is walked depth-first so a file's dependencies are emitted
    // before the file itself. The runtime file always comes first. Namespace-export
    // parts are emitted before a file's other parts, and CommonJS files (which
    // cannot be split) contribute one contiguous range covering all their parts.
    // Prefix parts (the runtime) are prepended at the end.
    //
    // Input : chunk with files_with_parts_in_chunk and entry_bits.
    // Output: js_files in order and js_parts as ordered PartRanges.
    void LinkerContext::FindImportedPartsInJSOrder(ChunkInfo& chunk, std::vector<uint32_t>& js_files, std::vector<PartRange>& js_parts) {
        ChunkOrderArray sorted;

        for (auto [source_index, _] : chunk.files_with_parts_in_chunk) {
            auto& file = graph.files[source_index];
            sorted.push_back(ChunkOrder{
                .source_index = source_index,
                .distance = file.distance_from_entry_point,
                .tie_breaker = graph.stable_source_indices[source_index],
            });
        }

        std::sort(sorted.begin(), sorted.end(), [](const ChunkOrder& a, const ChunkOrder& b) {
            return a.distance < b.distance || (a.distance == b.distance && a.tie_breaker < b.tie_breaker);
        });

        std::unordered_set<uint32_t> visited;
        std::vector<PartRange> js_parts_prefix;

        std::function<void(uint32_t)> visit;
        visit = [&](uint32_t source_index) {
            if (visited.count(source_index)) return;
            visited.insert(source_index);
            auto& file = graph.files[source_index];

            auto* js_repr_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr);
            if (!js_repr_ptr) return;
            auto& repr = **js_repr_ptr;

            bool is_file_in_this_chunk = chunk.entry_bits == file.entry_bits;
            bool can_file_be_split = repr.meta.wrap == graph::WrapKind::kNone;

            if (can_file_be_split && is_file_in_this_chunk && repr.ast.parts[javascript::kNSExportPartIndex].is_live) {
                AppendOrExtendPartRange(js_parts, source_index, javascript::kNSExportPartIndex);
            }

            for (uint32_t part_index = 0; part_index < repr.ast.parts.size(); part_index++) {
                auto& part = repr.ast.parts[part_index];
                bool is_part_in_this_chunk = is_file_in_this_chunk && part.is_live;

                for (auto import_record_index : part.import_record_indices) {
                    auto& record = repr.ast.import_records[import_record_index];
                    if (record.source_index.IsValid() &&
                        (record.kind == compiler::ImportKind::kStmt || is_part_in_this_chunk)) {
                        if (IsExternalDynamicImport(record, source_index)) continue;
                        visit(record.source_index.GetIndex());
                    }
                }

                if (is_part_in_this_chunk) {
                    is_file_in_this_chunk = true;
                    if (can_file_be_split && part_index != javascript::kNSExportPartIndex && ShouldIncludePart(repr, part)) {
                        if (source_index == javascript::kSourceIndex) {
                            AppendOrExtendPartRange(js_parts_prefix, source_index, part_index);
                        } else {
                            AppendOrExtendPartRange(js_parts, source_index, part_index);
                        }
                    }
                }
            }

            if (is_file_in_this_chunk) {
                js_files.push_back(source_index);

                if (!can_file_be_split) {
                    js_parts_prefix.push_back(PartRange{
                        .source_index = source_index,
                        .part_index_begin = 0,
                        .part_index_end = static_cast<uint32_t>(repr.ast.parts.size()),
                    });
                }
            }
        };

        visit(javascript::kSourceIndex);
        for (auto& data : sorted) {
            visit(data.source_index);
        }

        auto prefix_copy = std::move(js_parts_prefix);
        js_parts.reserve(prefix_copy.size() + js_parts.size());
        js_parts.insert(js_parts.begin(), prefix_copy.begin(), prefix_copy.end());
    }

    // Decides what to do with one import or export statement during conversion and
    // appends the replacement code to stmt_list.
    //
    // External imports are kept when the format preserves ESM syntax, otherwise
    // replaced by a require() call (or, for SystemJS, captured as a setter so the
    // wrapper can bind the name). Internal imports become a require() for a
    // CommonJS target, an init() call for an ESM target, and are removed entirely
    // for an unwrapped target. Self-imports of a CommonJS module's own exports are
    // dropped. Returns true when the caller should drop the original statement.
    //
    // Input : source_index, output stmt_list, statement location, namespace ref,
    //         import record index, optional SystemJS setter capture vector.
    // Output: true if the original statement should be removed.
    bool LinkerContext::ShouldRemoveImportExportStmt(
        uint32_t source_index,
        StmtList& stmt_list,
        logger::Loc loc,
        compiler::Ref namespace_ref,
        uint32_t import_record_index,
        std::vector<SystemJSSetterCapture>* system_setter_captures
    ) {
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[source_index].input_file.repr);
        auto& record = repr.ast.import_records[import_record_index];

        if (!record.source_index.IsValid()) {
            if (config::FormatKeepESMImportExportSyntax(options->OutputFormat)) {
                return false;
            }

            if (options->OutputFormat == config::Format::kSystem && namespace_ref != compiler::kInvalidRef) {
                if (system_setter_captures) {
                    SystemJSSetterCapture capture;
                    capture.import_path = record.path.text;
                    capture.namespace_ref = namespace_ref;
                    system_setter_captures->push_back(std::move(capture));
                }
                return true;
            }

            auto slocal = std::make_shared<javascript::SLocal>();
            slocal->kind = javascript::LocalKind::kVar;
            auto decl = javascript::Decl{
                .binding = javascript::Binding{
                    .data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = namespace_ref}),
                    .loc = loc,
                },
                .value_or_nil = javascript::Expr(std::make_shared<javascript::ERequireString>(javascript::ERequireString{.import_record_index = import_record_index}), record.range.loc),
            };
            slocal->decls.push_back(std::move(decl));
            stmt_list.inside_wrapper_prefix.push_back(javascript::Stmt{
                .data = std::move(slocal),
                .loc = loc,
            });
            return true;
        }

        if (repr.ast.exports_kind == javascript::ExportsKind::kCommonJS &&
            compiler::FollowSymbols(graph.symbols, namespace_ref) == repr.ast.exports_ref) {
            return true;
        }

        auto& other_file = graph.files[record.source_index.GetIndex()];
        auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(other_file.input_file.repr);

        switch (other_repr.meta.wrap) {
        case graph::WrapKind::kNone:
            break;

        case graph::WrapKind::kCJS: {
            auto slocal = std::make_shared<javascript::SLocal>();
            slocal->kind = javascript::LocalKind::kVar;
            auto decl = javascript::Decl{
                .binding = javascript::Binding{
                    .data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = namespace_ref}),
                    .loc = loc,
                },
                .value_or_nil = javascript::Expr(std::make_shared<javascript::ERequireString>(javascript::ERequireString{.import_record_index = import_record_index}), record.range.loc),
            };
            slocal->decls.push_back(std::move(decl));
            stmt_list.inside_wrapper_prefix.push_back(javascript::Stmt{
                .data = std::move(slocal),
                .loc = loc,
            });
            break;
        }

        case graph::WrapKind::kESM: {
            if (!other_file.is_live) break;

            auto ecall = std::make_shared<javascript::ECall>();
            ecall->target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = other_repr.ast.wrapper_ref}), loc);
            javascript::Expr value(std::move(ecall), loc);

            if (other_repr.meta.is_async_or_has_async_dependency) {
                auto eawait = std::make_shared<javascript::EAwait>();
                eawait->value = std::move(value);
                value = javascript::Expr(std::move(eawait), loc);
            }

            auto sexpr = std::make_shared<javascript::SExpr>();
            sexpr->value = std::move(value);
            stmt_list.inside_wrapper_prefix.push_back(javascript::Stmt{
                .data = std::move(sexpr),
                .loc = loc,
            });
            break;
        }
        }

        return true;
    }



    // Rewrites a file's statements for inclusion in a chunk.
    //
    // Each statement is classified and converted: imports/exports are handled by
    // ShouldRemoveImportExportStmt and may be turned into import-star or
    // export-star handling, export-from and export-clause statements are rewritten,
    // and plain declarations simply lose their export flag when exports are being
    // stripped. Export-star records evaluated at run time become a
    // "__reExport(...)" call, and "export default" is lowered to a named
    // declaration. ESM statements that must stay outside the wrapper closure are
    // routed to outside_wrapper_prefix. For a CommonJS entry point, re-exports are
    // double-written onto module.exports.
    void LinkerContext::ConvertStmtsForChunk(uint32_t source_index, StmtList& stmt_list, std::vector<javascript::Stmt>& part_stmts, std::vector<SystemJSSetterCapture>* system_setter_captures) {
        auto& file = graph.files[source_index];
        bool should_strip_exports = options->BuildMode != config::Mode::kPassThrough || !file.IsEntryPoint();
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(file.input_file.repr);
        bool should_extract_esm_stmts_for_wrap = repr.meta.wrap != graph::WrapKind::kNone;

        javascript::Expr module_exports_for_re_export;
        bool has_module_exports_for_re_export = false;
        if (options->OutputFormat == config::Format::kCommonJS && file.IsEntryPoint()) {
            has_module_exports_for_re_export = true;
            module_exports_for_re_export = javascript::Expr(std::make_shared<javascript::EDot>(javascript::EDot{
                    .target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = unbound_module_ref}), {}),
                    .name = "exports",
                }), {});
        }

        for (auto& stmt : part_stmts) {
            javascript::Stmt new_stmt = stmt;

            if (auto* simport = std::get_if<std::shared_ptr<javascript::SImport>>(&stmt.data)) {
                if (ShouldRemoveImportExportStmt(source_index, stmt_list, stmt.loc, (*simport)->namespace_ref, (*simport)->import_record_index, system_setter_captures)) {
                    continue;
                }

                if (compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArbitraryModuleNamespaceNames)) {
                    if ((*simport)->items) {
                        for (auto& item : *(*simport)->items) {
                            MaybeForbidArbitraryModuleNamespaceIdentifier(
                                "import", source_index, item.alias_loc, item.alias);
                        }
                    }
                }

                if (should_extract_esm_stmts_for_wrap) {
                    stmt_list.outside_wrapper_prefix.push_back(stmt);
                    continue;
                }

            } else if (auto* sexport_star = std::get_if<std::shared_ptr<javascript::SExportStar>>(&stmt.data)) {
                if ((*sexport_star)->alias) {
                    if (ShouldRemoveImportExportStmt(source_index, stmt_list, stmt.loc, (*sexport_star)->namespace_ref, (*sexport_star)->import_record_index, system_setter_captures)) {
                        continue;
                    }

                    if (compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArbitraryModuleNamespaceNames)) {
                        MaybeForbidArbitraryModuleNamespaceIdentifier(
                            "export", source_index, (*sexport_star)->alias->loc, (*sexport_star)->alias->original_name);
                    }

                    if (should_strip_exports) {
                        auto new_simport = std::make_shared<javascript::SImport>();
                        new_simport->namespace_ref = (*sexport_star)->namespace_ref;
                        new_simport->star_name_loc = std::make_shared<logger::Loc>((*sexport_star)->alias->loc);
                        new_simport->import_record_index = (*sexport_star)->import_record_index;
                        new_stmt.data = std::move(new_simport);
                    }

                    if (should_extract_esm_stmts_for_wrap) {
                        stmt_list.outside_wrapper_prefix.push_back(new_stmt);
                        continue;
                    }
                } else {
                    if (!should_strip_exports) {
                        stmt_list.inside_wrapper_suffix.push_back(new_stmt);
                        continue;
                    }

                    auto& record = repr.ast.import_records[(*sexport_star)->import_record_index];

                    if (!record.source_index.IsValid() && config::FormatKeepESMImportExportSyntax(options->OutputFormat)) {
                        if (compiler::Has(record.flags, compiler::ImportRecordFlags::kCallsRunTimeReExportFn)) {
                            auto new_simport = std::make_shared<javascript::SImport>();
                            new_simport->namespace_ref = (*sexport_star)->namespace_ref;
                            new_simport->star_name_loc = std::make_shared<logger::Loc>(stmt.loc.start);
                            new_simport->import_record_index = (*sexport_star)->import_record_index;
                            new_stmt.data = std::move(new_simport);

                            auto& runtime_repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[javascript::kSourceIndex].input_file.repr);
                            auto export_star_ref = runtime_repr.ast.module_scope->members["__reExport"].ref;

                            std::vector<javascript::Expr> args;
                            args.push_back(javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = repr.ast.exports_ref}), stmt.loc));
                            args.push_back(javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = (*sexport_star)->namespace_ref}), stmt.loc));
                            if (has_module_exports_for_re_export) {
                                args.push_back(module_exports_for_re_export);
                            }

                            auto ecall = std::make_shared<javascript::ECall>();
                            ecall->target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = export_star_ref}), stmt.loc);
                            ecall->args = std::move(args);
                            stmt_list.inside_wrapper_prefix.push_back(javascript::Stmt{
                                .data = std::make_shared<javascript::SExpr>(javascript::SExpr{
                                    .value = javascript::Expr(std::move(ecall), stmt.loc),
                                }),
                                .loc = stmt.loc,
                            });

                            if (should_extract_esm_stmts_for_wrap) {
                                stmt_list.outside_wrapper_prefix.push_back(new_stmt);
                                continue;
                            }
                        }
                    } else {
                        if (record.source_index.IsValid()) {
                            auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[record.source_index.GetIndex()].input_file.repr);
                            if (other_repr.meta.wrap == graph::WrapKind::kESM) {
                                auto ecall = std::make_shared<javascript::ECall>();
                                ecall->target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = other_repr.ast.wrapper_ref}), stmt.loc);
                                stmt_list.inside_wrapper_prefix.push_back(javascript::Stmt{
                                    .data = std::make_shared<javascript::SExpr>(javascript::SExpr{
                                        .value = javascript::Expr(std::move(ecall), stmt.loc),
                                    }),
                                    .loc = stmt.loc,
                                });
                            }
                        }

                        if (compiler::Has(record.flags, compiler::ImportRecordFlags::kCallsRunTimeReExportFn)) {
                            javascript::Expr target;
                            bool has_target = false;
                            if (record.source_index.IsValid()) {
                                auto& other_repr_ptr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[record.source_index.GetIndex()].input_file.repr);
                                if (other_repr_ptr.ast.exports_kind == javascript::ExportsKind::kESMWithDynamicFallback) {
                                    target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = other_repr_ptr.ast.exports_ref}), stmt.loc);
                                    has_target = true;
                                }
                            }
                            if (!has_target) {
                                target = javascript::Expr(std::make_shared<javascript::ERequireString>(javascript::ERequireString{.import_record_index = (*sexport_star)->import_record_index}), record.range.loc);
                            }

                            auto& runtime_repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[javascript::kSourceIndex].input_file.repr);
                            auto export_star_ref = runtime_repr.ast.module_scope->members["__reExport"].ref;

                            std::vector<javascript::Expr> args;
                            args.push_back(javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = repr.ast.exports_ref}), stmt.loc));
                            args.push_back(std::move(target));
                            if (has_module_exports_for_re_export) {
                                args.push_back(module_exports_for_re_export);
                            }

                            auto ecall = std::make_shared<javascript::ECall>();
                            ecall->target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = export_star_ref}), stmt.loc);
                            ecall->args = std::move(args);
                            stmt_list.inside_wrapper_prefix.push_back(javascript::Stmt{
                                .data = std::make_shared<javascript::SExpr>(javascript::SExpr{
                                    .value = javascript::Expr(std::move(ecall), stmt.loc),
                                }),
                                .loc = stmt.loc,
                            });
                        }

                        continue;
                    }
                }

            } else if (auto* sexport_from = std::get_if<std::shared_ptr<javascript::SExportFrom>>(&stmt.data)) {
                if (ShouldRemoveImportExportStmt(source_index, stmt_list, stmt.loc, (*sexport_from)->namespace_ref, (*sexport_from)->import_record_index, system_setter_captures)) {
                    continue;
                }

                if (compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArbitraryModuleNamespaceNames)) {
                    for (auto& item : (*sexport_from)->items) {
                        MaybeForbidArbitraryModuleNamespaceIdentifier(
                            "import", source_index, item.name.loc, item.original_name);
                    }
                }

                if (should_strip_exports) {
                    auto items_copy = (*sexport_from)->items;
                    for (auto& item : items_copy) {
                        item.alias = item.original_name;
                    }
                    auto new_simport = std::make_shared<javascript::SImport>();
                    new_simport->namespace_ref = (*sexport_from)->namespace_ref;
                    new_simport->items = std::make_shared<std::vector<javascript::ClauseItem>>(std::move(items_copy));
                    new_simport->import_record_index = (*sexport_from)->import_record_index;
                    new_simport->is_single_line = (*sexport_from)->is_single_line;
                    new_stmt.data = std::move(new_simport);
                } else if (compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArbitraryModuleNamespaceNames)) {
                    for (auto& item : (*sexport_from)->items) {
                        if (item.alias_loc != item.name.loc) {
                            MaybeForbidArbitraryModuleNamespaceIdentifier(
                                "export", source_index, item.alias_loc, item.alias);
                        }
                    }
                }

                if (should_extract_esm_stmts_for_wrap) {
                    stmt_list.outside_wrapper_prefix.push_back(new_stmt);
                    continue;
                }

            } else if (auto* sexport_clause = std::get_if<std::shared_ptr<javascript::SExportClause>>(&stmt.data)) {
                if (should_strip_exports) {
                    continue;
                }

                if (compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArbitraryModuleNamespaceNames)) {
                    for (auto& item : (*sexport_clause)->items) {
                        MaybeForbidArbitraryModuleNamespaceIdentifier(
                            "export", source_index, item.alias_loc, item.alias);
                    }
                }

                if (should_extract_esm_stmts_for_wrap) {
                    stmt_list.outside_wrapper_prefix.push_back(new_stmt);
                    continue;
                }

            } else if (auto* sfunction = std::get_if<std::shared_ptr<javascript::SFunction>>(&stmt.data)) {
                if (should_strip_exports && (*sfunction)->is_export) {
                    auto clone = std::make_shared<javascript::SFunction>(**sfunction);
                    clone->is_export = false;
                    new_stmt.data = std::move(clone);
                }

            } else if (auto* sclass = std::get_if<std::shared_ptr<javascript::SClass>>(&stmt.data)) {
                if (should_strip_exports && (*sclass)->is_export) {
                    auto clone = std::make_shared<javascript::SClass>(**sclass);
                    clone->is_export = false;
                    new_stmt.data = std::move(clone);
                }

            } else if (auto* slocal = std::get_if<std::shared_ptr<javascript::SLocal>>(&stmt.data)) {
                if (should_strip_exports && (*slocal)->is_export) {
                    auto clone = std::make_shared<javascript::SLocal>(**slocal);
                    clone->is_export = false;
                    new_stmt.data = std::move(clone);
                }

            } else if (auto* sexport_default = std::get_if<std::shared_ptr<javascript::SExportDefault>>(&stmt.data)) {
                if (should_strip_exports) {
                    if (auto* sexpr = std::get_if<std::shared_ptr<javascript::SExpr>>(&(*sexport_default)->value.data)) {
                        auto new_slocal = std::make_shared<javascript::SLocal>();
                        new_slocal->kind = javascript::LocalKind::kVar;
                        new_slocal->decls.push_back(javascript::Decl{
                            .binding = javascript::Binding{
                                .data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = (*sexport_default)->default_name.ref}),
                                .loc = (*sexport_default)->default_name.loc,
                            },
                            .value_or_nil = (*sexpr)->value,
                        });
                        new_stmt = javascript::Stmt{.data = std::move(new_slocal), .loc = stmt.loc};

                    } else if (auto* sf = std::get_if<std::shared_ptr<javascript::SFunction>>(&(*sexport_default)->value.data)) {
                        auto clone = std::make_shared<javascript::SFunction>(**sf);
                        clone->fn.name = std::make_shared<compiler::LocRef>((*sexport_default)->default_name);
                        new_stmt = javascript::Stmt{.data = std::move(clone), .loc = (*sexport_default)->value.loc};

                    } else if (auto* sc = std::get_if<std::shared_ptr<javascript::SClass>>(&(*sexport_default)->value.data)) {
                        auto clone = std::make_shared<javascript::SClass>(**sc);
                        clone->class_.name = std::make_shared<compiler::LocRef>((*sexport_default)->default_name);
                        new_stmt = javascript::Stmt{.data = std::move(clone), .loc = (*sexport_default)->value.loc};
                    }
                }
            }

            stmt_list.inside_wrapper_suffix.push_back(new_stmt);
        }
    }

}
