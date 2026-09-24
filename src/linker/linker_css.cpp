#include "guchho/linker.hpp"


#include <cassert>
#include <cstring>
#include <unordered_set>

#include "guchho/bundler.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/css/css_parser.hpp"


namespace guchho::linker {


    // Renders one CSS chunk to its final text and source map.
    //
    // It walks the chunk's imports in order (deepest first) building an AST per
    // import: layer wrappers, external @import rules (with nested conditions
    // encoded as data-URL imports), and source files with @charset/@import and
    // leading @layer rules filtered out and duplicate rules removed. Each AST is
    // printed, the pieces are joined with the chunk banner/footer and legal
    // comments, the source map is generated when requested, and the metafile JSON
    // is produced lazily via a callback. The isolated hash is then computed.
    //
    // Input : index into chunks of a CSS chunk.
    // Output: chunk.intermediate_output, output_source_map and json metadata set.
    void LinkerContext::GenerateChunkCSS(int chunk_index) {
        auto& chunk = chunks[static_cast<size_t>(chunk_index)];

        auto& chunk_repr = std::get<ChunkReprCSS>(chunk.chunk_repr);
        auto compile_results = std::vector<CompileResultCSS>(chunk_repr.imports_in_chunk_in_order.size());
        auto dfs = data_for_source_maps();

        auto chunk_abs_dir = fs->Dir(fs->Join({options->AbsOutputDir, config::TemplateToString(chunk.final_template)}));

        auto asts = std::vector<css::AST>(chunk_repr.imports_in_chunk_in_order.size());
        css::DeadRuleRemover remover(graph.symbols);
        bool use_remover = options->MinifySyntax;

        for (int i = static_cast<int>(chunk_repr.imports_in_chunk_in_order.size()) - 1; i >= 0; i--) {
            auto& entry = chunk_repr.imports_in_chunk_in_order[static_cast<size_t>(i)];

            switch (entry.kind) {
            case CssImportKind::kLayers: {
                auto rules = std::vector<css::Rule>();
                if (!entry.layers.empty()) {
                    auto layer_rule = std::make_shared<css::RAtLayer>();
                    layer_rule->names = entry.layers;
                    rules.push_back({.data = layer_rule});
                }
                auto [wrapped_rules, import_records] = WrapRulesWithConditions(rules, {}, entry.conditions, entry.condition_import_records);
                asts[static_cast<size_t>(i)].rules = std::move(wrapped_rules);
                asts[static_cast<size_t>(i)].import_records = std::move(import_records);
                break;
            }

            case CssImportKind::kExternalPath: {
                css::ImportConditions* conditions_ptr = nullptr;
                css::ImportConditions first_condition;
                if (!entry.conditions.empty()) {
                    first_condition = entry.conditions[0];
                    conditions_ptr = &first_condition;
                }

                auto external_path = entry.external_path;
                for (int j = static_cast<int>(entry.conditions.size()) - 1; j > 0; j--) {
                    css::AST import_ast;
                    auto import_rule = std::make_shared<css::RAtImport>();
                    import_rule->import_record_index = static_cast<uint32_t>(entry.condition_import_records.size());
                    import_rule->import_conditions = std::make_shared<css::ImportConditions>(entry.conditions[static_cast<size_t>(j)]);
                    import_ast.rules.push_back({.data = import_rule});
                    import_ast.import_records = entry.condition_import_records;
                    import_ast.import_records.push_back({
                        .path = external_path,
                        .kind = compiler::ImportKind::kAt,
                    });

                    css::PrinterOptions print_opts;
                    print_opts.minify_whitespace = options->MinifyWhitespace;
                    print_opts.ascii_only = options->ASCIIOnly;
                    auto ast_result = css::Print(import_ast, graph.symbols, print_opts);

                    std::string trimmed = ast_result.css;
                    {
                        size_t start = 0;
                        size_t end = trimmed.size();
                        while (start < end && (trimmed[start] == ' ' || trimmed[start] == '\t' || trimmed[start] == '\r' || trimmed[start] == '\n')) {
                            start++;
                        }
                        while (end > start && (trimmed[end - 1] == ' ' || trimmed[end - 1] == '\t' || trimmed[end - 1] == '\r' || trimmed[end - 1] == '\n')) {
                            end--;
                        }
                        trimmed = trimmed.substr(start, end - start);
                    }
                    external_path.text = helpers::EncodeStringAsShortestDataURL("text/css", trimmed);
                }

                auto ast_rules = std::vector<css::Rule>();
                auto ast_import_records = entry.condition_import_records;
                ast_import_records.push_back({
                    .path = external_path,
                    .kind = compiler::ImportKind::kAt,
                });
                auto import_rule = std::make_shared<css::RAtImport>();
                import_rule->import_record_index = static_cast<uint32_t>(entry.condition_import_records.size());
                if (conditions_ptr) {
                    import_rule->import_conditions = std::make_shared<css::ImportConditions>(first_condition);
                }
                ast_rules.push_back({.data = import_rule});

                asts[static_cast<size_t>(i)].rules = std::move(ast_rules);
                asts[static_cast<size_t>(i)].import_records = std::move(ast_import_records);
                break;
            }

            case CssImportKind::kSourceIndex: {
                auto file_index = entry.source_index;
                auto* css_repr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&graph.files[file_index].input_file.repr);
                if (!css_repr) { break; }
                auto ast = (*css_repr)->ast;

                auto rules = std::vector<css::Rule>();
                bool did_find_at_import = false;
                bool did_find_at_layer = false;
                for (auto& rule : ast.rules) {
                    if (dynamic_cast<css::RAtCharset*>(rule.data.get()) != nullptr) {
                        compile_results[static_cast<size_t>(i)].has_charset = true;
                        continue;
                    }
                    if (dynamic_cast<css::RAtLayer*>(rule.data.get()) != nullptr) {
                        did_find_at_layer = true;
                    }
                    if (dynamic_cast<css::RAtImport*>(rule.data.get()) != nullptr) {
                        if (!did_find_at_import) {
                            did_find_at_import = true;
                            if (did_find_at_layer) {
                                size_t end = 0;
                                for (auto& r : rules) {
                                    if (!dynamic_cast<css::RAtLayer*>(r.data.get())) {
                                        rules[end] = r;
                                        end++;
                                    }
                                }
                                rules.resize(end);
                            }
                        }
                        continue;
                    }
                    rules.push_back(rule);
                }

                auto [wrapped_rules, import_records] = WrapRulesWithConditions(rules, ast.import_records, entry.conditions, entry.condition_import_records);

                if (use_remover) {
                    wrapped_rules = remover.RemoveDeadRulesInPlace(entry.source_index, wrapped_rules, import_records);
                }

                ast.rules = std::move(wrapped_rules);
                ast.import_records = std::move(import_records);
                asts[static_cast<size_t>(i)] = std::move(ast);
                break;
            }

            case CssImportKind::kNone:
                break;
            }
        }

        for (size_t i = 0; i < chunk_repr.imports_in_chunk_in_order.size(); i++) {
            auto& entry = chunk_repr.imports_in_chunk_in_order[i];

            css::PrinterOptions css_options;
            css_options.minify_whitespace = options->MinifyWhitespace;
            css_options.line_limit = options->LineLimit;
            css_options.ascii_only = options->ASCIIOnly;
            css_options.legal_comments = options->LegalCommentsData;
            css_options.source_map = options->SourceMapData;
            css_options.unsupported_features = options->UnsupportedCSSFeatures;
            css_options.needs_metafile = options->NeedsMetafile;
            css_options.metafile_format = options->MetafileFormatData;
            css_options.local_names = {mangled_props.begin(), mangled_props.end()};

            if (entry.kind == CssImportKind::kSourceIndex) {
                auto file_index = entry.source_index;
                auto& file = graph.files[file_index];
                auto* css_repr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&file.input_file.repr);

                if (css_repr && config::CanHaveSourceMap(file.input_file.loader) && options->SourceMapData != config::SourceMap::kNone) {
                    css_options.add_source_mappings = true;
                    css_options.input_source_map = file.input_file.input_source_map.get();
                    css_options.line_offset_tables = dfs[file_index].line_offset_tables;
                }

                css_options.input_source_index = entry.source_index;
                compile_results[i].source_index = compiler::Index32::Make(entry.source_index);
            }

            auto result = css::Print(asts[i], graph.symbols, css_options);
            compile_results[i].print_result = std::move(result);
        }

        helpers::Joiner j;
        auto prev_offset = sourcemap::LineColumnOffset{};
        bool newline_before_comment = false;

        if (!options->CSSBanner.empty()) {
            prev_offset.AdvanceString(options->CSSBanner);
            j.AddString(options->CSSBanner);
            prev_offset.AdvanceString("\n");
            j.AddString("\n");
        }

        auto json_metadata_imports = std::vector<std::string>();
        {
            css::AST tree;
            for (auto& compile_result : compile_results) {
                if (compile_result.has_charset) {
                    auto charset = std::make_shared<css::RAtCharset>();
                    charset->encoding = "UTF-8";
                    tree.rules.push_back({.data = charset});
                    break;
                }
            }

            if (!tree.rules.empty()) {
                css::PrinterOptions prefix_opts;
                prefix_opts.minify_whitespace = options->MinifyWhitespace;
                prefix_opts.line_limit = options->LineLimit;
                prefix_opts.ascii_only = options->ASCIIOnly;
                prefix_opts.needs_metafile = options->NeedsMetafile;
                prefix_opts.metafile_format = options->MetafileFormatData;
                auto result = css::Print(tree, graph.symbols, prefix_opts);
                json_metadata_imports = std::move(result.json_metadata_imports);
                if (!result.css.empty()) {
                    prev_offset.AdvanceString(result.css);
                    j.AddString(result.css);
                    newline_before_comment = true;
                }
            }
        }

        helpers::Joiner j_meta;
        if (options->NeedsMetafile) {
            bool is_first_meta = true;
            j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "{\n      \"imports\": ["));
            for (auto& json : json_metadata_imports) {
                if (is_first_meta) is_first_meta = false;
                else j_meta.AddString(",");
                j_meta.AddString(json);
            }
            for (auto& compile_result : compile_results) {
                for (auto& json : compile_result.print_result.json_metadata_imports) {
                    if (is_first_meta) is_first_meta = false;
                    else j_meta.AddString(",");
                    j_meta.AddString(json);
                }
            }
            if (!is_first_meta) {
                j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "\n      "));
            }
            if (chunk.is_entry_point) {
                auto& file = graph.files[chunk.source_index];
                auto* css_repr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&file.input_file.repr);
                if (css_repr) {
                    j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData,
                        "],\n      \"entryPoint\": " + helpers::QuoteForJSON(
                            file.input_file.source.pretty_paths.Select(options->MetafilePathStyle), options->ASCIIOnly) + ",\n      \"inputs\": {"));
                } else {
                    j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "],\n      \"inputs\": {"));
                }
            } else {
                j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "],\n      \"inputs\": {"));
            }
        }

        auto compile_results_for_source_map = std::vector<CompileResultForSourceMap>();
        auto legal_comment_list = std::vector<LegalCommentEntry>();
        for (size_t cri = 0; cri < compile_results.size(); cri++) {
            auto& compile_result = compile_results[cri];
            if (!compile_result.print_result.extracted_legal_comments.empty() && compile_result.source_index.IsValid()) {
                legal_comment_list.push_back(LegalCommentEntry{
                    .source_index = compile_result.source_index.GetIndex(),
                    .extracted_comments = compile_result.print_result.extracted_legal_comments,
                });
            }

            if (options->BuildMode == config::Mode::kBundle && !options->MinifyWhitespace && compile_result.source_index.IsValid()) {
                std::string newline;
                if (newline_before_comment) {
                    newline = "\n";
                }
                auto comment = newline + "/* " +
                    graph.files[compile_result.source_index.GetIndex()].input_file.source.pretty_paths.Select(options->CodePathStyle) +
                    " */\n";
                prev_offset.AdvanceString(comment);
                j.AddString(comment);
            }
            if (!compile_result.print_result.css.empty()) {
                newline_before_comment = true;
            }

            compile_result.generated_offset = prev_offset;
            j.AddString(compile_result.print_result.css);

            if (compile_result.print_result.source_map_chunk.should_ignore) {
                prev_offset.AdvanceString(compile_result.print_result.css);

                if (!compile_result.print_result.css.empty() && options->SourceMapData != config::SourceMap::kNone && compile_result.source_index.IsValid()) {
                    if (!compile_results_for_source_map.empty() && !compile_results_for_source_map.back().is_null_entry) {
                        compile_results_for_source_map.push_back(CompileResultForSourceMap{
                            .source_index = compile_result.source_index.GetIndex(),
                            .is_null_entry = true,
                        });
                    }
                }
            } else {
                prev_offset = sourcemap::LineColumnOffset{};

                if (options->SourceMapData != config::SourceMap::kNone && compile_result.source_index.IsValid()) {
                    compile_results_for_source_map.push_back(CompileResultForSourceMap{
                        .source_map_chunk = compile_result.print_result.source_map_chunk,
                        .generated_offset = compile_result.generated_offset,
                        .source_index = compile_result.source_index.GetIndex(),
                    });
                }
            }
        }

        j.EnsureNewlineAtEnd();
        std::string slash_tag = "/style";
        if (compat::Has(options->UnsupportedCSSFeatures, compat::CSSFeature::kInlineStyle)) {
            slash_tag = "";
        }
        MaybeAppendLegalComments(options->LegalCommentsData, legal_comment_list, chunk, j, slash_tag);

        if (!options->CSSFooter.empty()) {
            j.AddString(options->CSSFooter);
            j.AddString("\n");
        }

        chunk.intermediate_output = BreakJoinerIntoPieces(std::move(j));

        if (options->SourceMapData != config::SourceMap::kNone) {
            bool can_have_shifts = !chunk.intermediate_output.pieces.empty();
            chunk.output_source_map = GenerateSourceMapForChunk(
                compile_results_for_source_map, chunk_abs_dir, dfs, can_have_shifts);
        }

        if (options->NeedsMetafile) {
            auto compile_results_copy = compile_results;
            std::vector<IntermediateOutput> css_pieces;
            css_pieces.reserve(compile_results_copy.size());
            for (auto& cr : compile_results_copy) {
                css_pieces.push_back(BreakOutputIntoPieces(std::string_view(cr.print_result.css.data(), cr.print_result.css.size())));
            }
            chunk.json_metadata_chunk_callback = [this, &chunk, compile_results = std::move(compile_results_copy), css_pieces = std::move(css_pieces), j_meta = std::move(j_meta)](int final_output_size) mutable {
                auto final_rel_dir = fs->Dir(chunk.final_rel_path);
                bool is_first = true;
                for (size_t i = 0; i < compile_results.size(); ++i) {
                    auto& compile_result = compile_results[i];
                    if (!compile_result.source_index.IsValid()) continue;
                    if (is_first) is_first = false;
                    else j_meta.AddString(",");
                    int count = AccurateFinalByteCount(css_pieces[i], final_rel_dir);
                    j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData,
                        "\n        " + helpers::QuoteForJSON(
                            graph.files[compile_result.source_index.GetIndex()].input_file.source.pretty_paths.Select(options->MetafilePathStyle),
                            options->ASCIIOnly) + ": {\n          \"bytesInOutput\": " + std::to_string(count) + "\n        }"));
                }
                if (!compile_results.empty()) {
                    j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "\n      "));
                }
                j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData,
                    "},\n      \"bytes\": " + std::to_string(final_output_size) + "\n    }"));
                return j_meta;
            };
        }

        GenerateIsolatedHashInParallel(chunk);
    }



    // Wraps a list of CSS rules in the @layer, @supports and @media conditions
    // that apply to them.
    //
    // Conditions are applied innermost-first so the outermost condition ends up on
    // the outside. An empty @layer rule still has a side effect (it fixes layer
    // order) and is kept; empty @supports and @media wrappers are skipped. Tokens
    // in each prelude are cloned with their import records so nested @import
    // references keep working.
    //
    // Input : rules, their import records, the conditions, and the condition import
    //         records.
    // Output: the wrapped rules plus the updated import record list.
    std::pair<std::vector<css::Rule>, std::vector<compiler::ImportRecord>> LinkerContext::WrapRulesWithConditions(
        std::vector<css::Rule> rules,
        const std::vector<compiler::ImportRecord>& import_records,
        const std::vector<css::ImportConditions>& conditions,
        const std::vector<compiler::ImportRecord>& condition_import_records) {

        auto ir_out = import_records;

        for (int i = static_cast<int>(conditions.size()) - 1; i >= 0; i--) {
            auto& item = conditions[static_cast<size_t>(i)];

            for (auto& t : item.layers) {
                if (rules.empty()) {
                    if (!t.children) {
                        continue;
                    } else {
                        rules.clear();
                    }
                }
                std::vector<css::Token> prelude;
                if (t.children) {
                    prelude = *t.children;
                }
                prelude = css::CloneTokensWithImportRecords(prelude, condition_import_records, ir_out);
                auto known_at = std::make_shared<css::RKnownAt>();
                known_at->at_token = "layer";
                known_at->prelude = std::move(prelude);
                known_at->rules = std::move(rules);
                rules = {{.data = known_at}};
            }

            if (!rules.empty()) {
                for (auto& t : item.supports) {
                    auto support_token = t;
                    support_token.kind = css::TokenType::kOpenParen;
                    support_token.text = "(";
                    std::vector<css::Token> prelude;
                    prelude.push_back(support_token);
                    prelude = css::CloneTokensWithImportRecords(prelude, condition_import_records, ir_out);
                    auto known_at = std::make_shared<css::RKnownAt>();
                    known_at->at_token = "supports";
                    known_at->prelude = std::move(prelude);
                    known_at->rules = std::move(rules);
                    rules = {{.data = known_at}};
                }
            }

            if (!rules.empty() && !item.queries.empty()) {
                auto queries = css::CloneMediaQueriesWithImportRecords(item.queries, condition_import_records, ir_out);
                auto media = std::make_shared<css::RAtMedia>();
                media->queries = std::move(queries);
                media->rules = std::move(rules);
                rules = {{.data = media}};
            }
        }

        return {std::move(rules), std::move(ir_out)};
    }



    // Collects the CSS files reachable from a JS entry point, in the order a JS
    // traversal would visit them.
    //
    // The JS module graph is walked depth-first; each file's parts and their
    // imports are followed, and a file's associated CSS file is appended after its
    // dependencies (postorder) so CSS is emitted in dependency order.
    //
    // Input : a JS entry point source index.
    // Output: CSS source indices in traversal order.
    std::vector<uint32_t> LinkerContext::FindImportedCSSFilesInJSOrder(uint32_t entry_point) {
        std::vector<uint32_t> order;
        std::unordered_set<uint32_t> visited;

        std::function<void(uint32_t)> visit;
        visit = [&](uint32_t source_index) {
            if (visited.count(source_index)) return;
            visited.insert(source_index);
            auto& file = graph.files[source_index];
            auto* repr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr);
            if (!repr) return;

            for (auto& part : (*repr)->ast.parts) {
                for (auto& import_record_index : part.import_record_indices) {
                    auto& record = (*repr)->ast.import_records[import_record_index];
                    if (record.source_index.IsValid()) {
                        visit(record.source_index.GetIndex());
                    }
                }
            }

            if ((*repr)->css_source_index.IsValid()) {
                order.push_back((*repr)->css_source_index.GetIndex());
            }
        };

        visit(entry_point);
        return order;
    }



    // Builds the ordered list of CSS imports for a set of entry points.
    //
    // The CSS graph is traversed depth-first in postorder, tracking the @layer,
    // @supports and @media conditions in effect and recording external @import
    // paths separately. Cycles are ignored. When any external import exists the
    // leading @layer and external @import rules are hoisted to the front. The list
    // is then optimised: duplicate source files or external paths that are covered
    // by an earlier conditional import are replaced by just their layer names,
    // redundant @layer entries are dropped, and adjacent @layer entries with
    // identical conditions are merged.
    //
    // Input : entry point source indices.
    // Output: the ordered, optimised CssImportOrder list for the chunk.
    std::vector<CssImportOrder> LinkerContext::FindImportedFilesInCSSOrder(const std::vector<uint32_t>& entry_points) {
        std::vector<CssImportOrder> order;
        bool has_external_import = false;

        std::function<void(uint32_t, std::vector<uint32_t>, std::vector<css::ImportConditions>, std::vector<compiler::ImportRecord>)> visit;
        visit = [&](uint32_t source_index,
                    std::vector<uint32_t> visited,
                    std::vector<css::ImportConditions> wrapping_conditions,
                    std::vector<compiler::ImportRecord> wrapping_import_records) {
            for (auto& v : visited) {
                if (v == source_index) return;
            }
            visited.push_back(source_index);

            auto& file = graph.files[source_index];
            auto* css_repr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&file.input_file.repr);
            if (!css_repr) return;
            auto& ast = (*css_repr)->ast;
            auto& top_level_rules = ast.rules;

            if (!ast.layers_pre_import.empty()) {
                order.push_back(CssImportOrder{
                    .conditions = wrapping_conditions,
                    .condition_import_records = wrapping_import_records,
                    .layers = ast.layers_pre_import,
                    .kind = CssImportKind::kLayers,
                });
            }

            for (auto& rule : top_level_rules) {
                auto* at_import = dynamic_cast<css::RAtImport*>(rule.data.get());
                if (!at_import) continue;
                auto& record = ast.import_records[at_import->import_record_index];
                if (record.source_index.IsValid()) {
                    auto nested_conditions = wrapping_conditions;
                    auto nested_import_records = wrapping_import_records;

                    if (at_import->import_conditions) {
                        nested_conditions = std::vector<css::ImportConditions>(wrapping_conditions);
                        nested_import_records = std::vector<compiler::ImportRecord>(wrapping_import_records);
                        auto conditions = at_import->import_conditions->CloneWithImportRecords(ast.import_records, nested_import_records);
                        nested_conditions.push_back(std::move(conditions));
                    }

                    visit(record.source_index.GetIndex(), visited, nested_conditions, nested_import_records);
                    continue;
                }

                if (!compiler::Has(record.flags, compiler::ImportRecordFlags::kWasLoadedWithEmptyLoader)) {
                    auto all_conditions = wrapping_conditions;
                    auto all_import_records = wrapping_import_records;

                    if (at_import->import_conditions) {
                        all_conditions = std::vector<css::ImportConditions>(wrapping_conditions);
                        all_import_records = std::vector<compiler::ImportRecord>(wrapping_import_records);
                        auto conditions = at_import->import_conditions->CloneWithImportRecords(ast.import_records, all_import_records);
                        all_conditions.push_back(std::move(conditions));
                    }

                    order.push_back(CssImportOrder{
                        .conditions = all_conditions,
                        .condition_import_records = all_import_records,
                        .external_path = record.path,
                        .kind = CssImportKind::kExternalPath,
                    });
                    has_external_import = true;
                }
            }

            for (auto& record : ast.import_records) {
                if (record.kind == compiler::ImportKind::kComposesFrom && record.source_index.IsValid()) {
                    visit(record.source_index.GetIndex(), visited, wrapping_conditions, wrapping_import_records);
                }
            }

            order.push_back(CssImportOrder{
                .conditions = wrapping_conditions,
                .condition_import_records = wrapping_import_records,
                .source_index = source_index,
                .kind = CssImportKind::kSourceIndex,
            });
        };

        for (auto& source_index : entry_points) {
            std::vector<uint32_t> visited;
            visit(source_index, visited, {}, {});
        }

        if (has_external_import) {
            std::vector<CssImportOrder> wip_order;

            bool is_at_layer_prefix = true;
            for (auto& entry : order) {
                if ((entry.kind == CssImportKind::kLayers && is_at_layer_prefix) || entry.kind == CssImportKind::kExternalPath) {
                    wip_order.push_back(entry);
                }
                if (entry.kind != CssImportKind::kLayers) {
                    is_at_layer_prefix = false;
                }
            }

            is_at_layer_prefix = true;
            for (auto& entry : order) {
                if ((entry.kind != CssImportKind::kLayers || !is_at_layer_prefix) && entry.kind != CssImportKind::kExternalPath) {
                    wip_order.push_back(entry);
                }
                if (entry.kind != CssImportKind::kLayers) {
                    is_at_layer_prefix = false;
                }
            }

            order = std::move(wip_order);
        }

        {
            std::unordered_map<uint32_t, std::vector<size_t>> source_index_duplicates;
            std::unordered_map<std::string, std::vector<size_t>> external_path_duplicates;

            for (int i = static_cast<int>(order.size()) - 1; i >= 0; i--) {
                auto& entry = order[static_cast<size_t>(i)];
                switch (entry.kind) {
                case CssImportKind::kSourceIndex: {
                    auto& duplicates = source_index_duplicates[entry.source_index];
                    bool replaced = false;
                    for (auto j : duplicates) {
                        if (IsConditionalImportRedundant(entry.conditions, order[j].conditions)) {
                            auto* css_repr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&graph.files[entry.source_index].input_file.repr);
                            entry.kind = CssImportKind::kLayers;
                            entry.layers = css_repr ? (*css_repr)->ast.layers_post_import : std::vector<std::vector<std::string>>();
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) {
                        duplicates.push_back(static_cast<size_t>(i));
                    }
                    break;
                }
                case CssImportKind::kExternalPath: {
                    auto& duplicates = external_path_duplicates[entry.external_path.text];
                    bool replaced = false;
                    for (auto j : duplicates) {
                        if (IsConditionalImportRedundant(entry.conditions, order[j].conditions)) {
                            entry.kind = CssImportKind::kLayers;
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) {
                        duplicates.push_back(static_cast<size_t>(i));
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }

        {
            struct DuplicateEntry {
                std::vector<std::vector<std::string>> layers;
                std::vector<size_t> indices;
            };
            std::vector<DuplicateEntry> layer_duplicates;
            std::vector<CssImportOrder> wip_order;

            for (size_t i = 0; i < order.size(); i++) {
                auto entry = order[i];

                if (entry.kind == CssImportKind::kLayers) {
                    for (size_t ci = 0; ci < entry.conditions.size(); ci++) {
                        auto& conditions = entry.conditions[ci];
                        if (!conditions.layers.empty() && conditions.layers.size() == 1 && !conditions.layers[0].children) {
                            entry.conditions.resize(ci);
                            entry.layers.clear();
                            break;
                        }
                    }

                    if (entry.layers.empty()) {
                        for (int ci = static_cast<int>(entry.conditions.size()) - 1; ci >= 0; ci--) {
                            if (!entry.conditions[static_cast<size_t>(ci)].layers.empty()) break;
                            entry.conditions.resize(static_cast<size_t>(ci));
                        }
                    }

                    if (entry.conditions.empty() && entry.layers.empty()) {
                        continue;
                    }
                }

                auto layers_key = entry.layers;
                if (entry.kind == CssImportKind::kSourceIndex) {
                    auto* css_repr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&graph.files[entry.source_index].input_file.repr);
                    if (css_repr) layers_key = (*css_repr)->ast.layers_post_import;
                }

                size_t index = 0;
                for (index = 0; index < layer_duplicates.size(); index++) {
                    if (helpers::StringArrayArraysEqual(layers_key, layer_duplicates[index].layers)) break;
                }
                if (index == layer_duplicates.size()) {
                    layer_duplicates.push_back(DuplicateEntry{layers_key, {}});
                }

                auto& duplicates = layer_duplicates[index].indices;
                bool is_redundant = false;
                for (int j = static_cast<int>(duplicates.size()) - 1; j >= 0; j--) {
                    auto dup_idx = duplicates[static_cast<size_t>(j)];
                    if (IsConditionalImportRedundant(entry.conditions, wip_order[dup_idx].conditions)) {
                        if (entry.kind != CssImportKind::kLayers) {
                            if (static_cast<size_t>(j) == duplicates.size() - 1 && dup_idx == wip_order.size() - 1) {
                                auto& other = wip_order[dup_idx];
                                if (other.kind == CssImportKind::kLayers && ImportConditionsAreEqual(entry.conditions, other.conditions)) {
                                    duplicates.resize(static_cast<size_t>(j));
                                    wip_order.pop_back();
                                    break;
                                }
                            }
                            wip_order.push_back(entry);
                        }
                        is_redundant = true;
                        break;
                    }
                }
                if (!is_redundant) {
                    duplicates.push_back(wip_order.size());
                    wip_order.push_back(entry);
                }
            }
            order = std::move(wip_order);
        }

        {
            std::vector<CssImportOrder> wip_order;
            int did_clone = -1;
            for (auto& entry : order) {
                if (entry.kind == CssImportKind::kLayers && !wip_order.empty()) {
                    auto prev_index = static_cast<int>(wip_order.size()) - 1;
                    auto& prev = wip_order[static_cast<size_t>(prev_index)];
                    if (prev.kind == CssImportKind::kLayers && ImportConditionsAreEqual(prev.conditions, entry.conditions)) {
                        if (did_clone != prev_index) {
                            did_clone = prev_index;
                            auto cloned = prev.layers;
                            prev.layers = std::move(cloned);
                        }
                        for (auto& l : entry.layers) {
                            wip_order[static_cast<size_t>(prev_index)].layers.push_back(l);
                        }
                        continue;
                    }
                }
                wip_order.push_back(std::move(entry));
            }
            order = std::move(wip_order);
        }

        return order;
    }



    // Returns true when two condition lists match exactly, comparing layer,
    // supports and media tokens while ignoring whitespace.
    bool LinkerContext::ImportConditionsAreEqual(
        const std::vector<css::ImportConditions>& a,
        const std::vector<css::ImportConditions>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) {
            if (!css::TokensEqualIgnoringWhitespace(a[i].layers, b[i].layers) ||
                !css::TokensEqualIgnoringWhitespace(a[i].supports, b[i].supports) ||
                !css::MediaQueriesEqualIgnoringWhitespace(a[i].queries, b[i].queries)) {
                return false;
            }
        }
        return true;
    }



    // Returns true when the "later" condition list is fully covered by the
    // "earlier" one, i.e. the later import can be skipped because an earlier import
    // already pulled in everything it would.
    //
    // A later list is redundant when it is no longer than the earlier list and
    // every prefix entry matches: same layers, and either the same supports/media,
    // or the later entry adds no constraint for that dimension.
    //
    // Input : earlier and later condition lists.
    // Output: true if the later list is redundant.
    bool LinkerContext::IsConditionalImportRedundant(
        const std::vector<css::ImportConditions>& earlier,
        const std::vector<css::ImportConditions>& later) {
        if (later.size() > earlier.size()) return false;

        for (size_t i = 0; i < later.size(); i++) {
            auto& a = earlier[i];
            auto& b = later[i];

            if (css::TokensEqualIgnoringWhitespace(a.layers, b.layers)) {
                bool same_supports = css::TokensEqualIgnoringWhitespace(a.supports, b.supports);
                bool same_media = css::MediaQueriesEqualIgnoringWhitespace(a.queries, b.queries);

                if (same_supports && same_media) continue;
                if (same_media && b.supports.empty()) continue;
                if (same_supports && b.queries.empty()) continue;
            }
            return false;
        }
        return true;
    }

}
