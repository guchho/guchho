#include "guchho/linker.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <unordered_set>

#include "guchho/bundler.hpp"
#include "guchho/javascript/js_renamer.hpp"
#include "guchho/javascript/js_runtime.hpp"


namespace guchho::linker {

    ////////////////////////////////////////////////////////////////////////////////
    // JS/CSS chunk generation
    //
    // Renders each chunk to its final JavaScript. This file owns per-file code
    // generation, symbol renaming, cross-chunk wiring, the output-format wrappers
    // (IIFE/AMD/UMD/SystemJS), the entry-point tail, source maps and the metafile.
    // The CSS side of chunk generation lives in linker_css.cpp.
    ////////////////////////////////////////////////////////////////////////////////

    // Returns the runtime metadata a "require" or "import" of a file needs: its
    // wrapper ref, whether that wrapper is async, and (only for ESM-wrapped files)
    // its exports ref.
    //
    // Input : source index of a JS file.
    // Output: RequireOrImportMeta with wrapper_ref, is_wrapper_async and exports_ref.
    javascript::RequireOrImportMeta LinkerContext::RequireOrImportMetaForSource(uint32_t source_index) {
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[source_index].input_file.repr);
        javascript::RequireOrImportMeta meta;
        meta.wrapper_ref = repr.ast.wrapper_ref;
        meta.is_wrapper_async = repr.meta.is_async_or_has_async_dependency;
        if (repr.meta.wrap == graph::WrapKind::kESM) {
            meta.exports_ref = repr.ast.exports_ref;
        } else {
            meta.exports_ref = compiler::Ref{};
        }
        return meta;
    }



    // Creates the renamer that gives every symbol in a chunk a collision-free name.
    //
    // The set of reserved names is computed from the module scopes plus format
    // specific bindings ("exports"/"module"/"require"/"Promise"). With identifier
    // minification the renamer allocates top-level slots by use count and frequency;
    // otherwise it simply numbers top-level symbols and names nested scopes.
    // Imported cross-chunk symbols are added first so they can be renamed too.
    //
    // Input : chunk and its files in output order.
    // Output: a renamer to pass to the printer.
    std::unique_ptr<javascript::Renamer> LinkerContext::RenameSymbolsInChunk(ChunkInfo& chunk, std::vector<uint32_t>& files_in_order) {
        auto* chunk_repr = std::get_if<ChunkReprJS>(&chunk.chunk_repr);
        if (!chunk_repr) {
            return javascript::NewNoOpRenamer(graph.symbols);
        }

        std::vector<javascript::Scope*> module_scopes;
        module_scopes.reserve(files_in_order.size());
        for (auto source_index : files_in_order) {
            auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[source_index].input_file.repr);
            module_scopes.push_back(repr.ast.module_scope);
        }
        auto reserved_names = javascript::ComputeReservedNames(module_scopes, graph.symbols);

        if (options->OutputFormat == config::Format::kCommonJS && options->OutputPlatform == config::Platform::kNode) {
            reserved_names["exports"] = 1;
            reserved_names["module"] = 1;
        }
        if (options->OutputFormat == config::Format::kSystem) {
            reserved_names["exports"] = 1;
        }
        if (options->BuildMode != config::Mode::kPassThrough) {
            reserved_names["require"] = 1;
            reserved_names["Promise"] = 1;
        }

        std::vector<StableRef> sorted_imports;
        for (auto& [_, items] : chunk_repr->imports_from_other_chunks) {
            for (auto& item : items) {
                sorted_imports.push_back(StableRef{
                    .stable_source_index = graph.stable_source_indices[item.ref.source_index],
                    .ref = item.ref,
                });
            }
        }
        std::sort(sorted_imports.begin(), sorted_imports.end(), [](const StableRef& a, const StableRef& b) {
            if (a.stable_source_index != b.stable_source_index) return a.stable_source_index < b.stable_source_index;
            return a.ref.inner_index < b.ref.inner_index;
        });

        if (options->MinifyIdentifiers) {
            compiler::SlotCounts first_top_level_slots;
            for (auto source_index : files_in_order) {
                auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[source_index].input_file.repr);
                first_top_level_slots.UnionMax(repr.ast.nested_scope_slot_counts);
            }
            auto r = javascript::NewMinifyRenamer(graph.symbols, first_top_level_slots, reserved_names);

            std::vector<javascript::StableSymbolCountArray> all_top_level_symbols(files_in_order.size());
            compiler::CharFreq freq;

            for (size_t i = 0; i < files_in_order.size(); i++) {
                auto source_index = files_in_order[i];
                auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[source_index].input_file.repr);

                if (repr.ast.char_freq) {
                    freq.Include(*repr.ast.char_freq);
                }

                auto& topLevelSymbols = all_top_level_symbols[i];
                if (repr.ast.uses_exports_ref) {
                    r->AccumulateSymbolCount(topLevelSymbols, repr.ast.exports_ref, 1, graph.stable_source_indices);
                }
                if (repr.ast.uses_module_ref) {
                    r->AccumulateSymbolCount(topLevelSymbols, repr.ast.module_ref, 1, graph.stable_source_indices);
                }
                for (uint32_t part_index = 0; part_index < repr.ast.parts.size(); part_index++) {
                    auto& part = repr.ast.parts[part_index];
                    if (!part.is_live) continue;
                    r->AccumulateSymbolUseCounts(topLevelSymbols, part.symbol_uses, graph.stable_source_indices);
                    for (auto& declared : part.declared_symbols) {
                        r->AccumulateSymbolCount(topLevelSymbols, declared.ref, 1, graph.stable_source_indices);
                    }
                }
                javascript::SortStableSymbolCounts(all_top_level_symbols[i]);
            }

            javascript::StableSymbolCountArray topLevelSymbols;
            topLevelSymbols.reserve(sorted_imports.size());
            for (auto& stable : sorted_imports) {
                r->AccumulateSymbolCount(topLevelSymbols, stable.ref, 1, graph.stable_source_indices);
            }
            for (auto& array : all_top_level_symbols) {
                topLevelSymbols.insert(topLevelSymbols.end(), array.begin(), array.end());
            }
            r->AllocateTopLevelSymbolSlots(topLevelSymbols);

            auto minifier = compiler::kDefaultNameMinifierJS.ShuffleByCharFreq(freq);
            r->AssignNamesByFrequency(minifier);
            return r;
        }

        auto r = javascript::NewNumberRenamer(graph.symbols, reserved_names);
        std::unordered_map<uint32_t, std::vector<javascript::Scope*>> nested_scopes;

        for (auto& stable : sorted_imports) {
            r->AddTopLevelSymbol(stable.ref);
        }
        for (auto source_index : files_in_order) {
            auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[source_index].input_file.repr);
            std::vector<javascript::Scope*> scopes;

            if (repr.meta.wrap == graph::WrapKind::kCJS) {
                r->AddTopLevelSymbol(repr.ast.wrapper_ref);

                if (config::FormatKeepESMImportExportSyntax(options->OutputFormat)) {
                    for (auto& part : repr.ast.parts) {
                        for (auto& stmt : part.stmts) {
                            if (auto* simport = std::get_if<std::shared_ptr<javascript::SImport>>(&stmt.data)) {
                                if (!repr.ast.import_records[(*simport)->import_record_index].source_index.IsValid()) {
                                    r->AddTopLevelSymbol((*simport)->namespace_ref);
                                    if ((*simport)->default_name) {
                                        r->AddTopLevelSymbol((*simport)->default_name->ref);
                                    }
                                    if ((*simport)->items) {
                                        for (auto& item : *(*simport)->items) {
                                            r->AddTopLevelSymbol(item.name.ref);
                                        }
                                    }
                                }
                            } else if (auto* sexport_star = std::get_if<std::shared_ptr<javascript::SExportStar>>(&stmt.data)) {
                                if (!repr.ast.import_records[(*sexport_star)->import_record_index].source_index.IsValid()) {
                                    r->AddTopLevelSymbol((*sexport_star)->namespace_ref);
                                }
                            } else if (auto* sexport_from = std::get_if<std::shared_ptr<javascript::SExportFrom>>(&stmt.data)) {
                                if (!repr.ast.import_records[(*sexport_from)->import_record_index].source_index.IsValid()) {
                                    r->AddTopLevelSymbol((*sexport_from)->namespace_ref);
                                    for (auto& item : (*sexport_from)->items) {
                                        r->AddTopLevelSymbol(item.name.ref);
                                    }
                                }
                            }
                        }
                    }
                }

                nested_scopes[source_index] = {repr.ast.module_scope};
                continue;
            }

            if (repr.meta.wrap == graph::WrapKind::kESM) {
                r->AddTopLevelSymbol(repr.ast.wrapper_ref);
            }

            for (uint32_t part_index = 0; part_index < repr.ast.parts.size(); part_index++) {
                auto& part = repr.ast.parts[part_index];
                if (part.is_live) {
                    for (auto& declared : part.declared_symbols) {
                        if (declared.is_top_level) {
                            r->AddTopLevelSymbol(declared.ref);
                        }
                    }
                    scopes.insert(scopes.end(), part.scopes.begin(), part.scopes.end());
                }
            }
            nested_scopes[source_index] = std::move(scopes);
        }
        r->AssignNamesByScope(nested_scopes);
        return r;
    }



    // Builds the leading "var global.name = ..." text that seeds a global for IIFE
    // output, creating missing intermediate objects with "||=" (or the older
    // "x = x || {}" form when logical assignment is unsupported).
    //
    // Input : options->GlobalName (e.g. ["A", "B"]).
    // Output: the assignment prefix text, or "" when no global name is set.
    std::string LinkerContext::GenerateGlobalNamePrefix() {
        auto& global_name = options->GlobalName;
        std::string text;
        std::string space = options->MinifyWhitespace ? "" : " ";
        std::string join = options->MinifyWhitespace ? ";" : ";\n";

        if (global_name.empty()) return "";

        std::string prefix = global_name[0];
        std::vector<std::string> remaining(global_name.begin() + 1, global_name.end());

        bool is_existing_object = (prefix == "this");
        if (prefix == "import" && !remaining.empty() && remaining[0] == "meta") {
            prefix = "import.meta";
            remaining.erase(remaining.begin());
            is_existing_object = true;
        }

        if (!remaining.empty() && !compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kLogicalAssignment)) {
            if (is_existing_object) {
            } else if (javascript::CanEscapeIdentifier(prefix, options->UnsupportedJSFeatures, options->ASCIIOnly)) {
                text = "var " + prefix + join;
            } else {
                prefix = "this[" + helpers::QuoteForJSON(prefix, options->ASCIIOnly) + "]";
            }
            for (auto& name : remaining) {
                std::string dot_or_index;
                if (javascript::CanEscapeIdentifier(name, options->UnsupportedJSFeatures, options->ASCIIOnly)) {
                    dot_or_index = "." + name;
                } else {
                    dot_or_index = "[" + helpers::QuoteForJSON(name, options->ASCIIOnly) + "]";
                }
                if (is_existing_object) {
                    prefix = prefix + dot_or_index;
                    is_existing_object = false;
                } else {
                    prefix = "(" + prefix + space + "||=" + space + "{})" + dot_or_index;
                }
            }
            return text + prefix + space + "=" + space;
        }

        if (is_existing_object) {
            text = prefix + space + "=" + space;
        } else if (javascript::CanEscapeIdentifier(prefix, options->UnsupportedJSFeatures, options->ASCIIOnly)) {
            text = "var " + prefix + space + "=" + space;
        } else {
            prefix = "this[" + helpers::QuoteForJSON(prefix, options->ASCIIOnly) + "]";
            text = prefix + space + "=" + space;
        }

        for (auto& name : remaining) {
            std::string old_prefix = prefix;
            if (javascript::CanEscapeIdentifier(name, options->UnsupportedJSFeatures, options->ASCIIOnly)) {
                prefix = prefix + "." + name;
            } else {
                prefix = prefix + "[" + helpers::QuoteForJSON(name, options->ASCIIOnly) + "]";
            }
            text += old_prefix + space + "||" + space + "{}" + join + prefix + space + "=" + space;
        }

        return text;
    }
    

    // Turns an arbitrary module id into a usable global identifier prefix by
    // collapsing runs of non-identifier characters into single underscores and
    // prefixing an underscore when the result would start with a digit.
    //
    // Input : an arbitrary id such as "dep/sub-name".
    // Output: a safe identifier such as "dep_sub_name" (or "" if empty).
    std::string LinkerContext::SanitizeGlobalName(const std::string& id) {
        std::string result;
        result.reserve(id.size());
        bool prev_delim = false;
        for (char c : id) {
            bool is_ident = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
            if (is_ident) {
                result.push_back(c);
                prev_delim = false;
            } else if (!prev_delim) {
                result.push_back('_');
                prev_delim = true;
            }
        }
        if (result.empty()) return "";
        if (std::isdigit(static_cast<unsigned char>(result[0]))) {
            result.insert(result.begin(), '_');
        }
        return result;
    }



    // Prints the JavaScript for one part range of one file inside a chunk.
    //
    // It assembles the statement list: the top-level directives, the namespace
    // export part, all live parts (converting imports/exports via
    // ConvertStmtsForChunk and substituting lazily-imported default properties),
    // and then wraps the result in the CommonJS or ESM runtime closure when needed.
    // Optionally adjacent locals are merged, source mappings are attached, and the
    // statements are printed with the renamer and printer options.
    //
    // Input : renamer, the part range, the runtime refs, and source-map data.
    // Output: a CompileResultJS with the printed code and SystemJS setter captures.
    CompileResultJS LinkerContext::GenerateCodeForFileInChunkJS(
        javascript::Renamer& renamer,
        PartRange part_range,
        compiler::Ref to_commonjs_ref,
        compiler::Ref to_esm_ref,
        compiler::Ref runtime_require_ref,
        const std::vector<bundler::DataForSourceMap>& data_for_source_maps_list
    ) {
        CompileResultJS result;
        result.source_index = part_range.source_index;

        std::vector<SystemJSSetterCapture> captures;

        auto& file = graph.files[part_range.source_index];
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(file.input_file.repr);
        bool needs_wrapper = false;
        StmtList stmt_list;

        if (repr.meta.wrap != graph::WrapKind::kNone && !file.IsEntryPoint()) {
            for (auto& directive : repr.ast.directives) {
                stmt_list.inside_wrapper_prefix.push_back(javascript::Stmt{
                    .data = std::make_shared<javascript::SDirective>(javascript::SDirective{.value = helpers::StringToUTF16(directive)}),
                });
            }
        }

        if (part_range.part_index_begin <= javascript::kNSExportPartIndex &&
            part_range.part_index_end > javascript::kNSExportPartIndex &&
            repr.ast.parts[javascript::kNSExportPartIndex].is_live) {
            ConvertStmtsForChunk(part_range.source_index, stmt_list, repr.ast.parts[javascript::kNSExportPartIndex].stmts, &captures);

            if (repr.meta.wrap == graph::WrapKind::kESM) {
                stmt_list.outside_wrapper_prefix.insert(stmt_list.outside_wrapper_prefix.end(),
                    std::make_move_iterator(stmt_list.inside_wrapper_suffix.begin()),
                    std::make_move_iterator(stmt_list.inside_wrapper_suffix.end()));
            } else {
                stmt_list.inside_wrapper_prefix.insert(stmt_list.inside_wrapper_prefix.end(),
                    std::make_move_iterator(stmt_list.inside_wrapper_suffix.begin()),
                    std::make_move_iterator(stmt_list.inside_wrapper_suffix.end()));
            }
            stmt_list.inside_wrapper_suffix.clear();
        }

        compiler::Index32 part_index_for_lazy_default_export;
        if (repr.ast.has_lazy_export) {
            auto default_it = repr.meta.resolved_exports.find("default");
            if (default_it != repr.meta.resolved_exports.end()) {
                auto& parts_declaring_default = repr.TopLevelSymbolToParts(default_it->second.ref);
                if (!parts_declaring_default.empty()) {
                    part_index_for_lazy_default_export =
                        compiler::Index32::Make(parts_declaring_default[0]);
                }
            }
        }

        for (uint32_t part_index = part_range.part_index_begin; part_index < part_range.part_index_end; part_index++) {
            auto& part = repr.ast.parts[part_index];
            if (!part.is_live) continue;
            if (part_index == javascript::kNSExportPartIndex) continue;
            if (part_index == repr.meta.wrapper_part_index.GetIndex()) {
                needs_wrapper = true;
                continue;
            }

            std::vector<javascript::Stmt> stmts = part.stmts;

            if (part_index_for_lazy_default_export.IsValid() && part_index == part_index_for_lazy_default_export.GetIndex()) {
                auto& stmt = stmts[0];
                auto& default_export = std::get<std::shared_ptr<javascript::SExportDefault>>(stmt.data);
                auto& default_expr = std::get<std::shared_ptr<javascript::SExpr>>(default_export->value.data);
                if (auto object_ptr = std::get_if<std::shared_ptr<javascript::EObject>>(&default_expr->value.data)) {
                    auto& object = **object_ptr;
                    auto object_clone = *object_ptr;
                    auto& object_clone_ref = *object_clone;

                    for (size_t i = 0; i < object.properties.size(); i++) {
                        auto& property = object.properties[i];
                        if (auto str = std::get_if<std::shared_ptr<javascript::EString>>(&property.key.data)) {
                            std::string name = helpers::UTF16ToString((*str)->value);
                            if (name != "default") {
                                auto export_it = repr.meta.resolved_exports.find(name);
                                if (export_it != repr.meta.resolved_exports.end()) {
                                    auto& parts_declaring_export = repr.TopLevelSymbolToParts(export_it->second.ref);
                                    if (!parts_declaring_export.empty()) {
                                        auto& export_part = repr.ast.parts[parts_declaring_export[0]];
                                        if (export_part.is_live) {
                                            auto& local = std::get<std::shared_ptr<javascript::SLocal>>(export_part.stmts[0].data);
                                            auto& bident = std::get<std::shared_ptr<javascript::BIdentifier>>(local->decls[0].binding.data);
                                            object_clone_ref.properties[i].value_or_nil = javascript::Expr(std::make_shared<javascript::EIdentifier>(
                                                    javascript::EIdentifier{.ref = bident->ref}), property.key.loc);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    auto default_expr_clone = std::make_shared<javascript::SExpr>(*default_expr);
                    default_expr_clone->value.data = object_clone;
                    auto default_export_clone = std::make_shared<javascript::SExportDefault>(*default_export);
                    default_export_clone->value.data = default_expr_clone;
                    stmts = {javascript::Stmt{.data = default_export_clone, .loc = stmt.loc}};
                }
            }

            ConvertStmtsForChunk(part_range.source_index, stmt_list, stmts, &captures);
        }

        std::vector<javascript::Stmt> stmts = std::move(stmt_list.inside_wrapper_suffix);
        if (!stmt_list.inside_wrapper_prefix.empty()) {
            stmts.insert(stmts.begin(),
                std::make_move_iterator(stmt_list.inside_wrapper_prefix.begin()),
                std::make_move_iterator(stmt_list.inside_wrapper_prefix.end()));
        }
        if (options->MinifySyntax) {
            stmts = MergeAdjacentLocalStmts(stmts);
        }

        if (needs_wrapper) {
            if (repr.meta.wrap == graph::WrapKind::kCJS) {
                std::vector<javascript::Arg> args;
                if (repr.ast.uses_exports_ref || repr.ast.uses_module_ref) {
                    args.push_back(javascript::Arg{.binding = javascript::Binding{.data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = repr.ast.exports_ref})}});
                    if (repr.ast.uses_module_ref) {
                        args.push_back(javascript::Arg{.binding = javascript::Binding{.data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = repr.ast.module_ref})}});
                    }
                }

                javascript::Expr cjs_body;
                if (options->ProfilerNames) {
                    auto kind = javascript::PropertyKind::kField;
                    if (!compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kObjectExtensions)) {
                        kind = javascript::PropertyKind::kMethod;
                    }
                    auto eobj = std::make_shared<javascript::EObject>();
                    javascript::Property prop;
                    prop.kind = kind;
                    prop.key = javascript::Expr(std::make_shared<javascript::EString>(javascript::EString{.value = helpers::StringToUTF16(file.input_file.source.pretty_paths.Select(options->CodePathStyle))}), {});
                    auto efn = std::make_shared<javascript::EFunction>();
                    efn->fn.args = std::move(args);
                    efn->fn.body.block.stmts = std::move(stmts);
                    prop.value_or_nil = javascript::Expr(std::move(efn), {});
                    eobj->properties.push_back(std::move(prop));
                    cjs_body = javascript::Expr(std::move(eobj), {});
                } else if (compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArrow)) {
                    auto efn = std::make_shared<javascript::EFunction>();
                    efn->fn.args = std::move(args);
                    efn->fn.body.block.stmts = std::move(stmts);
                    cjs_body = javascript::Expr(std::move(efn), {});
                } else {
                    auto earrow = std::make_shared<javascript::EArrow>();
                    earrow->args = std::move(args);
                    earrow->body.block.stmts = std::move(stmts);
                    cjs_body = javascript::Expr(std::move(earrow), {});
                }

                auto ecall = std::make_shared<javascript::ECall>();
                ecall->target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = cjs_runtime_ref}), {});
                ecall->args.push_back(std::move(cjs_body));

                stmts = std::move(stmt_list.outside_wrapper_prefix);
                auto slocal = std::make_shared<javascript::SLocal>();
                slocal->decls.push_back(javascript::Decl{
                    .binding = javascript::Binding{.data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = repr.ast.wrapper_ref})},
                    .value_or_nil = javascript::Expr(std::move(ecall), {}),
                });
                stmts.push_back(javascript::Stmt{.data = std::move(slocal)});

            } else if (repr.meta.wrap == graph::WrapKind::kESM) {
                bool is_async = repr.meta.is_async_or_has_async_dependency;

                std::vector<javascript::Decl> decls;
                size_t end = 0;
                for (size_t i = 0; i < stmts.size(); i++) {
                    auto& stmt = stmts[i];

                    if (auto* slocal = std::get_if<std::shared_ptr<javascript::SLocal>>(&stmt.data)) {
                        auto wrap_identifier = [&](logger::Loc loc, compiler::Ref ref) -> javascript::Expr {
                            decls.push_back(javascript::Decl{.binding = javascript::Binding{.data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = ref}), .loc = loc}});
                            return javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = ref}), loc);
                        };
                        javascript::Expr value;
                        for (auto& decl : (*slocal)->decls) {
                            auto binding = javascript::ConvertBindingToExpr(decl.binding, wrap_identifier);
                            if (decl.value_or_nil.data.index() != 0) {
                                value = javascript::JoinWithComma(value, javascript::Assign(binding, decl.value_or_nil));
                            }
                        }
                        if (value.data.index() == 0) continue;
                        stmt = javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = std::move(value)}), .loc = stmt.loc};
                    } else if (std::get_if<std::shared_ptr<javascript::SFunction>>(&stmt.data) != nullptr) {
                        stmt_list.outside_wrapper_prefix.push_back(stmt);
                        continue;
                    }

                    stmts[end++] = std::move(stmt);
                }
                stmts.resize(end);

                javascript::Expr esm_body;
                if (options->ProfilerNames) {
                    auto kind = javascript::PropertyKind::kField;
                    if (!compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kObjectExtensions)) {
                        kind = javascript::PropertyKind::kMethod;
                    }
                    auto eobj = std::make_shared<javascript::EObject>();
                    javascript::Property prop;
                    prop.kind = kind;
                    prop.key = javascript::Expr(std::make_shared<javascript::EString>(javascript::EString{.value = helpers::StringToUTF16(file.input_file.source.pretty_paths.Select(options->CodePathStyle))}), {});
                    auto efn = std::make_shared<javascript::EFunction>();
                    efn->fn.body.block.stmts = std::move(stmts);
                    efn->fn.is_async = is_async;
                    prop.value_or_nil = javascript::Expr(std::move(efn), {});
                    eobj->properties.push_back(std::move(prop));
                    esm_body = javascript::Expr(std::move(eobj), {});
                } else if (compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArrow)) {
                    auto efn = std::make_shared<javascript::EFunction>();
                    efn->fn.body.block.stmts = std::move(stmts);
                    efn->fn.is_async = is_async;
                    esm_body = javascript::Expr(std::move(efn), {});
                } else {
                    auto earrow = std::make_shared<javascript::EArrow>();
                    earrow->body.block.stmts = std::move(stmts);
                    earrow->is_async = is_async;
                    esm_body = javascript::Expr(std::move(earrow), {});
                }

                auto ecall = std::make_shared<javascript::ECall>();
                ecall->target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = esm_runtime_ref}), {});
                ecall->args.push_back(std::move(esm_body));

                if (!options->MinifySyntax && !decls.empty()) {
                    stmt_list.outside_wrapper_prefix.push_back(javascript::Stmt{
                        .data = std::make_shared<javascript::SLocal>(javascript::SLocal{.decls = std::move(decls)}),
                    });
                }

                stmts = std::move(stmt_list.outside_wrapper_prefix);
                auto slocal = std::make_shared<javascript::SLocal>();
                slocal->decls = std::move(decls);
                slocal->decls.push_back(javascript::Decl{
                    .binding = javascript::Binding{.data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = repr.ast.wrapper_ref})},
                    .value_or_nil = javascript::Expr(std::move(ecall), {}),
                });
                stmts.push_back(javascript::Stmt{.data = std::move(slocal)});
            }
        }

        bool add_source_mappings = false;
        sourcemap::SourceMapData* input_source_map = nullptr;
        std::vector<sourcemap::LineOffsetTable> line_offset_tables;
        if (config::CanHaveSourceMap(file.input_file.loader) && options->SourceMapData != config::SourceMap::kNone) {
            add_source_mappings = true;
            input_source_map = file.input_file.input_source_map.get();
            if (part_range.source_index < data_for_source_maps_list.size()) {
                line_offset_tables = data_for_source_maps_list[part_range.source_index].line_offset_tables;
            }
        }

        int indent = (options->OutputFormat == config::Format::kIIFE ||
                      options->OutputFormat == config::Format::kUMD ||
                      options->OutputFormat == config::Format::kAMD) ? 1 :
                     (options->OutputFormat == config::Format::kSystem) ? 3 : 0;

        javascript::PrinterOptions print_options;
        print_options.indent = indent;
        print_options.output_format = options->OutputFormat;
        print_options.minify_identifiers = options->MinifyIdentifiers;
        print_options.minify_whitespace = options->MinifyWhitespace;
        print_options.minify_syntax = options->MinifySyntax;
        print_options.line_limit = options->LineLimit;
        print_options.ascii_only = options->ASCIIOnly;
        print_options.input_source_map = input_source_map;
        print_options.line_offset_tables = line_offset_tables;
        print_options.to_commonjs_ref = to_commonjs_ref;
        print_options.to_esm_ref = to_esm_ref;
        print_options.runtime_require_ref = runtime_require_ref;
        print_options.ts_enums = graph.ts_enums;
        print_options.const_values = graph.const_values;
        print_options.legal_comments = options->LegalCommentsData;
        print_options.unsupported_features = options->UnsupportedJSFeatures;
        print_options.source_map = options->SourceMapData;
        print_options.add_source_mappings = add_source_mappings;
        print_options.require_or_import_meta_for_source = [this](uint32_t si) {
            return RequireOrImportMetaForSource(si);
        };
        print_options.mangled_props = mangled_props;
        print_options.needs_metafile = options->NeedsMetafile;
        print_options.metafile_format = options->MetafileFormatData;

        auto tree = repr.ast;
        tree.directives.clear();
        tree.parts = {javascript::Part{.stmts = std::move(stmts)}};

        result.print_result = javascript::Print(tree, graph.symbols, renamer, print_options);
        result.system_setter_captures = std::move(captures);
        return result;
    }



    // Prints the trailing code that runs an entry point for the chosen output format.
    //
    // Depending on the format it calls the wrapper, assigns or returns the exports
    // (CommonJS/IIFE/ESM/AMD/UMD/SystemJS), emits the node "cjs-module-lexer"
    // annotation for CommonJS, and re-exports the entry point's names. Returns an
    // empty result when there is nothing to emit.
    //
    // Input : renamer, runtime refs and the entry point source index.
    // Output: a CompileResultJS with the tail statements.
    CompileResultJS LinkerContext::GenerateEntryPointTailJS(
        javascript::Renamer& renamer,
        compiler::Ref to_commonjs_ref,
        compiler::Ref to_esm_ref,
        uint32_t source_index
    ) {
        CompileResultJS result;
        result.source_index = source_index;

        auto& file = graph.files[source_index];
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(file.input_file.repr);
        std::vector<javascript::Stmt> stmts;

        auto make_wrapper_call = [&]() {
            return javascript::Expr(std::make_shared<javascript::ECall>(javascript::ECall{
                    .target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = repr.ast.wrapper_ref}), {}),
                }), {});
        };

        switch (options->OutputFormat) {
        case config::Format::kPreserve:
            if (repr.meta.wrap != graph::WrapKind::kNone) {
                stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = make_wrapper_call()})});
            }
            break;

        case config::Format::kIIFE:
            if (repr.meta.wrap == graph::WrapKind::kCJS) {
                if (!options->GlobalName.empty()) {
                    stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SReturn>(javascript::SReturn{.value_or_nil = make_wrapper_call()})});
                } else {
                    stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = make_wrapper_call()})});
                }
            } else {
                if (repr.meta.wrap == graph::WrapKind::kESM) {
                    stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = make_wrapper_call()})});
                }
                if (repr.meta.force_include_exports_for_entry_point) {
                    auto ecall = std::make_shared<javascript::ECall>();
                    ecall->target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = to_commonjs_ref}), {});
                    ecall->args.push_back(javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = repr.ast.exports_ref}), {}));
                    stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SReturn>(javascript::SReturn{.value_or_nil = javascript::Expr(std::move(ecall), {})})});
                }
            }
            break;

        case config::Format::kCommonJS:
            if (repr.meta.wrap == graph::WrapKind::kCJS) {
                auto assign = std::make_shared<javascript::SExpr>();
                assign->value = javascript::Assign(
                    javascript::Expr(std::make_shared<javascript::EDot>(javascript::EDot{
                        .target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = unbound_module_ref}), {}),
                        .name = "exports",
                    }), {}),
                    make_wrapper_call()
                );
                stmts.push_back(javascript::Stmt{.data = std::move(assign)});
            } else {
                if (repr.meta.wrap == graph::WrapKind::kESM) {
                    stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = make_wrapper_call()})});
                }
            }

            if (options->OutputPlatform == config::Platform::kNode) {
                std::vector<javascript::Property> module_exports;
                for (auto& export_name : repr.meta.sorted_and_filtered_export_aliases) {
                    if (export_name == "default") {
                        continue;
                    }

                    javascript::Expr value_or_nil;
                    if (javascript::kKeywords.count(export_name) > 0 || !javascript::IsIdentifier(export_name)) {
                        value_or_nil = javascript::Expr(javascript::kENullShared, {});
                    }

                    module_exports.push_back(javascript::Property{
                        .key = javascript::Expr(std::make_shared<javascript::EString>(helpers::StringToUTF16(export_name)), {}),
                        .value_or_nil = std::move(value_or_nil),
                    });
                }

                for (auto import_record_index : repr.ast.export_star_import_records) {
                    auto& record = repr.ast.import_records[import_record_index];
                    if (!record.source_index.IsValid()) {
                        module_exports.push_back(javascript::Property{
                            .value_or_nil = javascript::Expr(std::make_shared<javascript::ERequireString>(javascript::ERequireString{.import_record_index = import_record_index}), {}),
                            .kind = javascript::PropertyKind::kSpread,
                        });
                    }
                }

                if (!module_exports.empty()) {
                    auto assign_expr = javascript::Assign(
                        javascript::Expr(std::make_shared<javascript::EDot>(javascript::EDot{
                            .target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = unbound_module_ref}), {}),
                            .name = "exports",
                        }), {}),
                        javascript::Expr(std::make_shared<javascript::EObject>(javascript::EObject{.properties = std::move(module_exports)}), {})
                    );
                    auto annotation = std::make_shared<javascript::EBinary>();
                    annotation->op = javascript::OpCode::kBinOpLogicalAnd;
                    annotation->left = javascript::Expr(std::make_shared<javascript::ENumber>(javascript::ENumber{.value = 0}), {});
                    annotation->right = std::move(assign_expr);

                    if (!options->MinifyWhitespace) {
                        stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SComment>(javascript::SComment{.text = "// Annotate the CommonJS export names for ESM import in node:"})});
                    }
                    stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = javascript::Expr(std::move(annotation), {})})});
                }
            }
            break;

        case config::Format::kESModule:
            if (repr.meta.wrap == graph::WrapKind::kCJS) {
                stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExportDefault>(javascript::SExportDefault{
                    .value = javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = make_wrapper_call()})},
                })});
            } else {
                if (repr.meta.wrap == graph::WrapKind::kESM) {
                    if (repr.meta.is_async_or_has_async_dependency) {
                        auto eawait = std::make_shared<javascript::EAwait>();
                        eawait->value = make_wrapper_call();
                        stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = javascript::Expr(std::move(eawait), {})})});
                    } else {
                        stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = make_wrapper_call()})});
                    }
                }
            }

            if (!repr.meta.sorted_and_filtered_export_aliases.empty()) {
                std::vector<javascript::ClauseItem> items;
                for (auto& alias : repr.meta.sorted_and_filtered_export_aliases) {
                    auto export_it = repr.meta.resolved_exports.find(alias);
                    if (export_it == repr.meta.resolved_exports.end()) continue;
                    auto& export_data = export_it->second;
                    auto export_ref = export_data.ref;

                    auto& other_file = graph.files[export_data.source_index];
                    auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(other_file.input_file.repr);
                    auto import_it = other_repr.meta.imports_to_bind.find(export_ref);
                    if (import_it != other_repr.meta.imports_to_bind.end()) {
                        export_ref = import_it->second.ref;
                    }

                    if (auto* symbol = graph.symbols.Get(export_ref); symbol->namespace_alias) {
                        auto temp_ref = repr.meta.cjs_export_copies[items.size()];
                        auto slocal = std::make_shared<javascript::SLocal>();
                        slocal->decls.push_back(javascript::Decl{
                            .binding = javascript::Binding{.data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = temp_ref})},
                            .value_or_nil = javascript::Expr(std::make_shared<javascript::EImportIdentifier>(javascript::EImportIdentifier{.ref = export_ref}), {}),
                        });
                        stmts.push_back(javascript::Stmt{.data = std::move(slocal)});
                        items.push_back(javascript::ClauseItem{.alias = alias, .name = {.ref = temp_ref}});
                    } else {
                        items.push_back(javascript::ClauseItem{.alias = alias, .name = {.ref = export_ref}});
                    }
                }

                if (!items.empty()) {
                    auto sexport = std::make_shared<javascript::SExportClause>();
                    sexport->items = std::move(items);
                    stmts.push_back(javascript::Stmt{.data = std::move(sexport)});
                }
            }
            break;

        case config::Format::kUMD:
        case config::Format::kAMD:
            if (repr.meta.wrap == graph::WrapKind::kCJS) {
                stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SReturn>(javascript::SReturn{.value_or_nil = make_wrapper_call()})});
            } else {
                if (repr.meta.wrap == graph::WrapKind::kESM) {
                    stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = make_wrapper_call()})});
                }
            }
            if (repr.meta.force_include_exports_for_entry_point) {
                auto ecall = std::make_shared<javascript::ECall>();
                ecall->target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = to_commonjs_ref}), {});
                ecall->args.push_back(javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = repr.ast.exports_ref}), {}));
                stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SReturn>(javascript::SReturn{.value_or_nil = javascript::Expr(std::move(ecall), {})})});
            }
            break;

        case config::Format::kSystem:
            if (repr.meta.wrap == graph::WrapKind::kCJS) {
                stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = make_wrapper_call()})});
            } else {
                if (repr.meta.wrap == graph::WrapKind::kESM) {
                    stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = make_wrapper_call()})});
                }
            }

            if (unbound_exports_ref != compiler::kInvalidRef) {
                size_t copy_index = 0;
                for (auto& alias : repr.meta.sorted_and_filtered_export_aliases) {
                    auto export_it = repr.meta.resolved_exports.find(alias);
                    if (export_it == repr.meta.resolved_exports.end()) continue;
                    auto& export_data = export_it->second;
                    auto export_ref = export_data.ref;

                    auto& other_file = graph.files[export_data.source_index];
                    auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(other_file.input_file.repr);
                    auto import_it = other_repr.meta.imports_to_bind.find(export_ref);
                    if (import_it != other_repr.meta.imports_to_bind.end()) {
                        export_ref = import_it->second.ref;
                    }

                    if (auto* symbol = graph.symbols.Get(export_ref); symbol->namespace_alias) {
                        auto temp_ref = repr.meta.cjs_export_copies[copy_index++];
                        auto slocal = std::make_shared<javascript::SLocal>();
                        slocal->decls.push_back(javascript::Decl{
                            .binding = javascript::Binding{.data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = temp_ref})},
                            .value_or_nil = javascript::Expr(std::make_shared<javascript::EImportIdentifier>(javascript::EImportIdentifier{.ref = export_ref}), {}),
                        });
                        stmts.push_back(javascript::Stmt{.data = std::move(slocal)});
                        export_ref = temp_ref;
                    }

                    auto ecall = std::make_shared<javascript::ECall>();
                    ecall->target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = unbound_exports_ref}), {});
                    ecall->args.push_back(javascript::Expr(std::make_shared<javascript::EString>(helpers::StringToUTF16(alias)), {}));
                    ecall->args.push_back(javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = export_ref}), {}));
                    stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = javascript::Expr(std::move(ecall), {})})});
                }
            }
            break;
        }

        if (stmts.empty()) return result;

        int indent = (options->OutputFormat == config::Format::kIIFE ||
                      options->OutputFormat == config::Format::kUMD ||
                      options->OutputFormat == config::Format::kAMD) ? 1 :
                     (options->OutputFormat == config::Format::kSystem) ? 3 : 0;

        javascript::PrinterOptions print_options;
        print_options.indent = indent;
        print_options.output_format = options->OutputFormat;
        print_options.minify_identifiers = options->MinifyIdentifiers;
        print_options.minify_whitespace = options->MinifyWhitespace;
        print_options.minify_syntax = options->MinifySyntax;
        print_options.line_limit = options->LineLimit;
        print_options.ascii_only = options->ASCIIOnly;
        print_options.to_commonjs_ref = to_commonjs_ref;
        print_options.to_esm_ref = to_esm_ref;
        print_options.legal_comments = options->LegalCommentsData;
        print_options.unsupported_features = options->UnsupportedJSFeatures;
        print_options.require_or_import_meta_for_source = [this](uint32_t si) {
            return RequireOrImportMetaForSource(si);
        };
        print_options.mangled_props = mangled_props;

        auto tree = repr.ast;
        tree.directives.clear();
        tree.parts = {javascript::Part{.stmts = std::move(stmts)}};

        result.print_result = javascript::Print(tree, graph.symbols, renamer, print_options);
        return result;
    }



    // Renders one JavaScript chunk to its final text, source map and metafile.
    //
    // It renames the chunk's symbols, generates code for each part range, prints the
    // cross-chunk import/export binding code, generates the entry-point tail, and
    // joins everything with the hashbang, banner, directives and the format wrapper
    // (IIFE/AMD/UMD/SystemJS). Source maps and the lazy metafile callback are built
    // while concatenating, and the isolated hash and executable flag are recorded.
    //
    // Input : index into chunks of a JS chunk.
    // Output: chunk.intermediate_output, output_source_map, json metadata and
    //         is_executable set.
    void LinkerContext::GenerateChunkJS(int chunk_index) {
        auto& chunk = chunks[static_cast<size_t>(chunk_index)];
        auto* chunk_repr = std::get_if<ChunkReprJS>(&chunk.chunk_repr);
        if (!chunk_repr) return;

        auto dfs_js = data_for_source_maps();

        auto r = RenameSymbolsInChunk(chunk, chunk_repr->files_in_chunk_in_order);

        auto& runtime_repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[javascript::kSourceIndex].input_file.repr);
        auto to_commonjs_ref = compiler::FollowSymbols(graph.symbols, runtime_repr.ast.named_exports["__toCommonJS"].ref);
        auto to_esm_ref = compiler::FollowSymbols(graph.symbols, runtime_repr.ast.named_exports["__toESM"].ref);
        auto runtime_require_ref = compiler::FollowSymbols(graph.symbols, runtime_repr.ast.named_exports["__require"].ref);

        std::vector<CompileResultJS> compile_results;
        for (auto& part_range : chunk_repr->parts_in_chunk_in_order) {
            if (part_range.source_index == javascript::kSourceIndex && options->OmitRuntimeForTests) {
                continue;
            }
            compile_results.push_back(GenerateCodeForFileInChunkJS(
                *r, part_range, to_commonjs_ref, to_esm_ref, runtime_require_ref, dfs_js));
        }

        helpers::Joiner cross_chunk_prefix_j, cross_chunk_suffix_j;
        std::vector<std::string> cross_chunk_json_metadata_imports;
        {
config::Format format = options->OutputFormat;
            int indent = (format == config::Format::kIIFE ||
                          format == config::Format::kUMD ||
                          format == config::Format::kAMD) ? 1 :
                         (format == config::Format::kSystem) ? 3 : 0;
            javascript::PrinterOptions print_options;
            print_options.indent = indent;
            print_options.output_format = options->OutputFormat;
            print_options.minify_identifiers = options->MinifyIdentifiers;
            print_options.minify_whitespace = options->MinifyWhitespace;
            print_options.minify_syntax = options->MinifySyntax;
            print_options.line_limit = options->LineLimit;
            print_options.needs_metafile = options->NeedsMetafile;
            print_options.metafile_format = options->MetafileFormatData;
            print_options.ascii_only = options->ASCIIOnly;

            std::vector<compiler::ImportRecord> cross_chunk_import_records;
            cross_chunk_import_records.reserve(chunk.cross_chunk_imports.size());
            for (auto& chunk_import : chunk.cross_chunk_imports) {
                compiler::ImportRecord record;
                record.kind = chunk_import.import_kind;
                record.path.text = chunks[chunk_import.chunk_index].unique_key;
                record.flags = compiler::ImportRecordFlags::kShouldNotBeExternalInMetafile |
                               compiler::ImportRecordFlags::kContainsUniqueKey;
                cross_chunk_import_records.push_back(std::move(record));
            }

            {
                javascript::AST tree;
                tree.import_records = std::move(cross_chunk_import_records);
                tree.parts = {javascript::Part{.stmts = chunk_repr->cross_chunk_prefix_stmts}};
                auto result = javascript::Print(tree, graph.symbols, *r, print_options);
                cross_chunk_prefix_j.AddString(result.js);
                cross_chunk_json_metadata_imports = std::move(result.json_metadata_imports);
            }

            {
                javascript::AST tree;
                tree.parts = {javascript::Part{.stmts = chunk_repr->cross_chunk_suffix_stmts}};
                auto result = javascript::Print(tree, graph.symbols, *r, print_options);
                cross_chunk_suffix_j.AddString(result.js);
            }
        }

        CompileResultJS entry_point_tail;
        if (chunk.is_entry_point) {
            entry_point_tail = GenerateEntryPointTailJS(*r, to_commonjs_ref, to_esm_ref, chunk.source_index);
        }

        helpers::Joiner j;
        sourcemap::LineColumnOffset prev_offset;
        bool newline_before_comment = false;
        bool is_executable = false;
        std::string space = options->MinifyWhitespace ? "" : " ";
        std::string newline = options->MinifyWhitespace ? "" : "\n";
        std::string indent;

        if (chunk.is_entry_point) {
            if (auto* repr_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&graph.files[chunk.source_index].input_file.repr)) {
                auto& repr = **repr_ptr;
                if (!repr.ast.hashbang.empty()) {
                    auto hashbang = repr.ast.hashbang + "\n";
                    prev_offset.AdvanceString(hashbang);
                    j.AddString(hashbang);
                    newline_before_comment = true;
                    is_executable = true;
                }
            }
        }

        if (!options->JSBanner.empty()) {
            prev_offset.AdvanceString(options->JSBanner);
            prev_offset.AdvanceString("\n");
            j.AddString(options->JSBanner);
            j.AddString("\n");
            newline_before_comment = true;
        }

        if (chunk.is_entry_point) {
            if (auto* repr_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&graph.files[chunk.source_index].input_file.repr)) {
                auto& repr = **repr_ptr;
                for (auto& directive : repr.ast.directives) {
                    if (directive != "use strict" || options->OutputFormat != config::Format::kESModule) {
                        auto quoted = helpers::QuoteForJSON(directive, options->ASCIIOnly) + ";" + newline;
                        prev_offset.AdvanceString(quoted);
                        j.AddString(quoted);
                        newline_before_comment = true;
                    }
                }
            }
        }

        std::vector<std::string> wrapper_external_deps;
        if (options->OutputFormat == config::Format::kAMD ||
            options->OutputFormat == config::Format::kUMD ||
            options->OutputFormat == config::Format::kSystem) {
            std::unordered_set<std::string> seen;
            for (auto [source_index, _] : chunk.files_with_parts_in_chunk) {
                auto& file = graph.files[source_index];
                auto* js_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr);
                if (!js_ptr) continue;
                for (auto& record : (*js_ptr)->ast.import_records) {
                    if (!record.source_index.IsValid() && !record.path.text.empty()) {
                        if (seen.insert(record.path.text).second) {
                            wrapper_external_deps.push_back(record.path.text);
                        }
                    }
                }
            }
            std::sort(wrapper_external_deps.begin(), wrapper_external_deps.end());
        }

        std::unordered_map<std::string, size_t> setter_index_for_dep;
        std::vector<std::vector<SystemJSSetterCapture>> setter_captures;
        if (options->OutputFormat == config::Format::kSystem) {
            for (size_t i = 0; i < wrapper_external_deps.size(); i++) {
                setter_index_for_dep[wrapper_external_deps[i]] = i;
            }
            setter_captures.resize(wrapper_external_deps.size());
            for (auto& compile_result : compile_results) {
                for (auto& capture : compile_result.system_setter_captures) {
                    auto it = setter_index_for_dep.find(capture.import_path);
                    if (it != setter_index_for_dep.end()) {
                        setter_captures[it->second].push_back(std::move(capture));
                    }
                }
            }
        }

        auto quote_json = [this](const std::string& s) {
            return helpers::QuoteForJSON(s, options->ASCIIOnly);
        };

        if (options->OutputFormat == config::Format::kIIFE) {
            std::string text;
            indent = "  ";
            if (!options->GlobalName.empty()) {
                text = GenerateGlobalNamePrefix();
            }
            if (compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArrow)) {
                text += "(function()" + space + "{" + newline;
            } else {
                text += "(()" + space + "=>" + space + "{" + newline;
            }
            prev_offset.AdvanceString(text);
            j.AddString(text);
            newline_before_comment = false;
        } else if (options->OutputFormat == config::Format::kAMD) {
            indent = "  ";
            std::string text = options->Amd.define + "([" + quote_json("require");
            for (auto& dep : wrapper_external_deps) {
                text += "," + space + quote_json(dep);
            }
            text += "]," + space + "function(require";
            for (size_t i = 0; i < wrapper_external_deps.size(); i++) {
                text += "," + space + "dep" + std::to_string(i);
            }
            text += ")" + space + "{" + newline;
            prev_offset.AdvanceString(text);
            j.AddString(text);
            newline_before_comment = false;
        } else if (options->OutputFormat == config::Format::kUMD) {
            indent = "  ";

            auto global_parts = options->GlobalName;

            std::string text;
            text += "(function(global, factory) {" + newline;
            text += indent + "typeof exports === " + quote_json("object") +
                    " && typeof module !== " + quote_json("undefined") + " ? module.exports = factory(require";
            for (auto& dep : wrapper_external_deps) {
                text += "," + space + "require(" + quote_json(dep) + ")";
            }
            text += ") :" + newline;
            text += indent + "typeof define === " + quote_json("function") +
                    " && define.amd ? define([" + quote_json("require");
            for (auto& dep : wrapper_external_deps) {
                text += "," + space + quote_json(dep);
            }
            text += "], factory) :" + newline;
            std::string global_close;
            text += indent + "(global = typeof globalThis !== " + quote_json("undefined") +
                    " ? globalThis : global || self, ";
            if (!global_parts.empty()) {
                text += "(";
                std::string ns = "global";
                for (size_t i = 0; i < global_parts.size(); i++) {
                    text += ns + "." + global_parts[i] + " = " + ns + "." + global_parts[i] + " || {}, ";
                    ns += "." + global_parts[i];
                }
                text += ns + " = factory(void 0";
                global_close = "))";
            } else {
                text += "factory(void 0";
                global_close = ")";
            }
            for (auto& dep : wrapper_external_deps) {
                std::string global_dep = dep;
                {
                    auto it = options->Globals.find(dep);
                    if (it != options->Globals.end()) {
                        global_dep = it->second;
                    } else {
                        global_dep = SanitizeGlobalName(dep);
                        if (global_dep.empty()) global_dep = "dep";
                    }
                }
                text += "," + space + "global." + global_dep;
            }
            text += global_close + ")" + ";" + newline;
            text += "})(this" + space + "," + space + "function(require";
            for (size_t i = 0; i < wrapper_external_deps.size(); i++) {
                text += "," + space + "dep" + std::to_string(i);
            }
            text += ")" + space + "{" + newline;
            prev_offset.AdvanceString(text);
            j.AddString(text);
            newline_before_comment = false;
        } else if (options->OutputFormat == config::Format::kSystem) {
            indent = "  ";
            std::string text = "System.register([";
            for (size_t i = 0; i < wrapper_external_deps.size(); i++) {
                if (i > 0) text += "," + space;
                text += quote_json(wrapper_external_deps[i]);
            }
            text += "]," + space + "function(exports)" + space + "{" + newline;
            for (auto& dep_captures : setter_captures) {
                for (auto& capture : dep_captures) {
                    text += "  " + space + "var " + r->NameForSymbol(capture.namespace_ref) + ";" + newline;
                }
            }
            text += "  " + space + "return {" + newline;
            text += "    " + space + "setters: [";
            for (size_t i = 0; i < wrapper_external_deps.size(); i++) {
                if (i > 0) text += "," + space;
                text += "function(dep" + std::to_string(i) + ")" + space + "{";
                for (auto& capture : setter_captures[i]) {
                    text += space;
                    text += r->NameForSymbol(capture.namespace_ref) + space + "=" + space + "dep" + std::to_string(i) + ";";
                }
                text += space + "}";
            }
            text += "]," + newline;
            text += "    " + space + "execute:" + space + "function()" + space + "{" + newline;
            prev_offset.AdvanceString(text);
            j.AddString(text);
            newline_before_comment = false;
        }

        auto cross_chunk_prefix_str = cross_chunk_prefix_j.Done();
        if (!cross_chunk_prefix_str.empty()) {
            newline_before_comment = true;
            prev_offset.AdvanceString(cross_chunk_prefix_str);
            j.AddString(cross_chunk_prefix_str);
        }

        helpers::Joiner j_meta;
        std::vector<std::string> json_metadata_imports;
        if (options->NeedsMetafile) {
            bool is_first_meta = true;
            j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "{\n      \"imports\": ["));
            for (auto& json : cross_chunk_json_metadata_imports) {
                if (is_first_meta) {
                    is_first_meta = false;
                } else {
                    j_meta.AddString(",");
                }
                j_meta.AddString(json);
            }
            for (auto& compile_result : compile_results) {
                for (auto& json : compile_result.print_result.json_metadata_imports) {
                    if (is_first_meta) {
                        is_first_meta = false;
                    } else {
                        j_meta.AddString(",");
                    }
                    j_meta.AddString(json);
                }
            }
            if (!is_first_meta) {
                j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "\n      "));
            }

            j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "],\n      \"exports\": ["));
            std::vector<std::string> aliases;
            if (config::FormatKeepESMImportExportSyntax(options->OutputFormat)) {
                if (chunk.is_entry_point) {
                    if (auto* repr_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&graph.files[chunk.source_index].input_file.repr)) {
                        auto& file_repr = **repr_ptr;
                        if (file_repr.meta.wrap == graph::WrapKind::kCJS) {
                            aliases.push_back("default");
                        } else {
                            for (auto& [alias, _] : file_repr.meta.resolved_exports) {
                                aliases.push_back(alias);
                            }
                        }
                    }
                } else {
                    for (auto& [_, alias] : chunk_repr->exports_to_other_chunks) {
                        aliases.push_back(alias);
                    }
                }
            }
            is_first_meta = true;
            std::sort(aliases.begin(), aliases.end());
            for (auto& alias : aliases) {
                if (is_first_meta) {
                    is_first_meta = false;
                } else {
                    j_meta.AddString(",");
                }
                j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData,
                    "\n        " + helpers::QuoteForJSON(alias, options->ASCIIOnly)));
            }
            if (!is_first_meta) {
                j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "\n      "));
            }
            j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "],\n"));
            if (chunk.is_entry_point) {
                auto entry_point = graph.files[chunk.source_index].input_file.source.pretty_paths.Select(options->MetafilePathStyle);
                j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData,
                    "      \"entryPoint\": " + helpers::QuoteForJSON(entry_point, options->ASCIIOnly) + ",\n"));
            }
            if (chunk_repr->has_css_chunk) {
                j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData,
                    "      \"cssBundle\": " + helpers::QuoteForJSON(chunks[chunk_repr->css_chunk_index].unique_key, options->ASCIIOnly) + ",\n"));
            }
            j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "      \"inputs\": {"));
        }

        std::vector<CompileResultForSourceMap> compile_results_for_source_map;
        std::vector<LegalCommentEntry> legal_comment_list;
        std::vector<uint32_t> meta_order;
        std::unordered_map<uint32_t, std::vector<std::string>> meta_bytes;
        uint32_t prev_file_name_comment = 0;

        if (options->NeedsMetafile) {
            meta_order.reserve(compile_results.size());
        }

        for (auto& compile_result : compile_results) {
            if (!is_executable && !compile_result.print_result.js.empty()) {
                is_executable = true;
            }

            if (!compile_result.print_result.extracted_legal_comments.empty()) {
                legal_comment_list.push_back(LegalCommentEntry{
                    .source_index = compile_result.source_index,
                    .extracted_comments = compile_result.print_result.extracted_legal_comments,
                });
            }

            if (options->BuildMode == config::Mode::kBundle && !options->MinifyWhitespace &&
                prev_file_name_comment != compile_result.source_index && !compile_result.print_result.js.empty()) {
                if (newline_before_comment) {
                    prev_offset.AdvanceString("\n");
                    j.AddString("\n");
                }

                auto path = graph.files[compile_result.source_index].input_file.source.pretty_paths.Select(options->CodePathStyle);
                auto replace_all = [](std::string& str, std::string_view from, std::string_view to) {
                    size_t pos = 0;
                    while ((pos = str.find(from, pos)) != std::string::npos) {
                        str.replace(pos, from.size(), to);
                        pos += to.size();
                    }
                };
                replace_all(path, "\r", "\\r");
                replace_all(path, "\n", "\\n");
                replace_all(path, "\u2028", "\\u2028");
                replace_all(path, "\u2029", "\\u2029");

                auto text = indent + "// " + path + "\n";
                prev_offset.AdvanceString(text);
                j.AddString(text);
                prev_file_name_comment = compile_result.source_index;
            }

            if (graph.files[compile_result.source_index].input_file.omit_from_source_maps_and_metafile) {
                prev_offset.AdvanceString(compile_result.print_result.js);
                j.AddString(compile_result.print_result.js);
            } else {
                compile_result.generated_offset = prev_offset;
                j.AddString(compile_result.print_result.js);

                if (compile_result.print_result.source_map_chunk.should_ignore) {
                    prev_offset.AdvanceString(compile_result.print_result.js);

                    if (!compile_result.print_result.js.empty() && options->SourceMapData != config::SourceMap::kNone) {
                        auto n = compile_results_for_source_map.size();
                        if (n > 0 && !compile_results_for_source_map[n - 1].is_null_entry) {
                            compile_results_for_source_map.push_back(CompileResultForSourceMap{
                                .source_index = compile_result.source_index,
                                .is_null_entry = true,
                            });
                        }
                    }
                } else {
                    prev_offset = sourcemap::LineColumnOffset{};

                    if (options->SourceMapData != config::SourceMap::kNone) {
                        compile_results_for_source_map.push_back(CompileResultForSourceMap{
                            .source_map_chunk = compile_result.print_result.source_map_chunk,
                            .generated_offset = compile_result.generated_offset,
                            .source_index = compile_result.source_index,
                        });
                    }
                }

                if (options->NeedsMetafile) {
                    auto it = meta_bytes.find(compile_result.source_index);
                    if (it == meta_bytes.end()) {
                        meta_order.push_back(compile_result.source_index);
                    }
                    meta_bytes[compile_result.source_index].push_back(compile_result.print_result.js);
                }
            }

            if (!compile_result.print_result.js.empty()) {
                newline_before_comment = true;
            }
        }

        j.AddString(entry_point_tail.print_result.js);

        auto cross_chunk_suffix_str = cross_chunk_suffix_j.Done();
        if (!cross_chunk_suffix_str.empty()) {
            if (newline_before_comment) {
                j.AddString(newline);
            }
            j.AddString(cross_chunk_suffix_str);
        }

        if (options->OutputFormat == config::Format::kIIFE) {
            j.AddString("})();" + newline);
        } else if (options->OutputFormat == config::Format::kAMD) {
            j.AddString("});" + newline);
        } else if (options->OutputFormat == config::Format::kUMD) {
            j.AddString("});" + newline);
        } else if (options->OutputFormat == config::Format::kSystem) {
            j.AddString("      " + space + "}" + newline);
            j.AddString("    " + space + "};" + newline);
            j.AddString("  });" + newline);
        }

        j.EnsureNewlineAtEnd();
        std::string slash_tag = "/script";
        if (compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kInlineScript)) {
            slash_tag = "";
        }
        MaybeAppendLegalComments(options->LegalCommentsData, legal_comment_list, chunk, j, slash_tag);

        if (!options->JSFooter.empty()) {
            j.AddString(options->JSFooter);
            j.AddString("\n");
        }

        chunk.intermediate_output = BreakJoinerIntoPieces(std::move(j));

        if (options->SourceMapData != config::SourceMap::kNone) {
            auto chunk_abs_dir = fs->Dir(fs->Join({options->AbsOutputDir, config::TemplateToString(chunk.final_template)}));
            bool can_have_shifts = !chunk.intermediate_output.pieces.empty();
            chunk.output_source_map = GenerateSourceMapForChunk(
                compile_results_for_source_map, chunk_abs_dir, dfs_js, can_have_shifts);
        }

        if (options->NeedsMetafile) {
            std::vector<uint32_t> css_source_indices;
            if (chunk_repr->has_css_chunk &&
                std::holds_alternative<ChunkReprCSS>(chunks[chunk_repr->css_chunk_index].chunk_repr)) {
                auto& css_repr = std::get<ChunkReprCSS>(chunks[chunk_repr->css_chunk_index].chunk_repr);
                for (auto& entry : css_repr.imports_in_chunk_in_order) {
                    if (entry.kind == CssImportKind::kSourceIndex) {
                        css_source_indices.push_back(entry.source_index);
                    }
                }
            }
            meta_order.insert(meta_order.begin(), css_source_indices.begin(), css_source_indices.end());

            std::vector<std::vector<IntermediateOutput>> js_pieces;
            js_pieces.reserve(meta_order.size());
            for (auto source_index : meta_order) {
                auto it = meta_bytes.find(source_index);
                if (it == meta_bytes.end()) {
                    js_pieces.emplace_back();
                    continue;
                }
                std::vector<IntermediateOutput> outputs;
                outputs.reserve(it->second.size());
                for (auto& slice : it->second) {
                    outputs.push_back(BreakOutputIntoPieces(std::string_view(slice.data(), slice.size())));
                }
                js_pieces.push_back(std::move(outputs));
            }
            chunk.json_metadata_chunk_callback = [this, &chunk, meta_order = std::move(meta_order), js_pieces = std::move(js_pieces), j_meta = std::move(j_meta)](int final_output_size) mutable {
                auto final_rel_dir = fs->Dir(chunk.final_rel_path);
                for (size_t i = 0; i < meta_order.size(); ++i) {
                    if (i > 0) j_meta.AddString(",");
                    int count = 0;
                    for (auto& output : js_pieces[i]) {
                        count += AccurateFinalByteCount(output, final_rel_dir);
                    }
                    auto source_index = meta_order[i];
                    j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData,
                        std::string("\n        ") + helpers::QuoteForJSON(graph.files[source_index].input_file.source.pretty_paths.Select(options->MetafilePathStyle), options->ASCIIOnly) +
                        ": {\n          \"bytesInOutput\": " + std::to_string(count) + "\n        }"));
                }
                if (!meta_order.empty()) {
                    j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData, "\n      "));
                }
                j_meta.AddString(config::MaybeRemoveWhitespace(options->MetafileFormatData,
                    "},\n      \"bytes\": " + std::to_string(final_output_size) + "\n    }"));
                return j_meta;
            };
        }

        GenerateIsolatedHashInParallel(chunk);
        chunk.is_executable = is_executable;
    }

}

