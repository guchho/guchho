#include "guchho/linker.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <optional>
#include <thread>
#include <unordered_set>


#include "guchho/css/css_lexer.hpp"
#include "guchho/javascript/js_runtime.hpp"


namespace guchho::linker {
    ////////////////////////////////////////////////////////////////////////////////
    // Tree shaking & code splitting
    //
    // The first pass over the module graph decides which files and which parts of
    // each file survive. A file is "live" when an entry point reaches it through a
    // side-effectful or used import; a part is "live" when a live part references
    // it. Reachability is tracked per entry point so a later pass can split shared
    // code into chunks.
    ////////////////////////////////////////////////////////////////////////////////

    // Runs the liveness and reachability passes over the module graph.
    //
    // Every entry point is marked live first (tree shaking), then the graph is
    // walked once per entry point to record which files are reachable from which
    // entry points and how far away they are (code splitting). The entry-point bit
    // and distance recorded here drive the chunk computation that follows.
    //
    // Input : a graph whose entry points have been resolved.
    // Output: graph.files marked is_live, entry_bits and distance_from_entry_point.
    void LinkerContext::TreeShakingAndCodeSplitting() {
        timer->Begin("Tree shaking");
        for (auto& ep : graph.EntryPoints()) {
            MarkFileLiveForTreeShaking(ep.source_index);
        }
        timer->End("Tree shaking");
        timer->Begin("Code splitting");
        for (size_t i = 0; i < graph.EntryPoints().size(); i++) {
            MarkFileReachableForCodeSplitting(graph.EntryPoints()[i].source_index, static_cast<uint32_t>(i), 0);
        }
        timer->End("Code splitting");
    }

    // Marks a file (and, transitively, everything it needs) as live.
    //
    // For a JavaScript file the walk follows side-effectful static imports, always
    // keeps parts that cannot be removed, and also keeps the associated CSS file.
    // For a CSS file it follows every resolved import record. Recursion is guarded
    // by the file's is_live flag so cycles terminate.
    void LinkerContext::MarkFileLiveForTreeShaking(uint32_t source_index) {
        auto& file = graph.files[source_index];
        if (file.is_live) return;
        file.is_live = true;

        auto* repr_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr);
        if (repr_ptr && *repr_ptr) {
            auto& repr = **repr_ptr;
            if (repr.css_source_index.IsValid()) {
                MarkFileLiveForTreeShaking(repr.css_source_index.GetIndex());
            }
            for (size_t part_index = 0; part_index < repr.ast.parts.size(); part_index++) {
                bool can_be_removed = repr.ast.parts[part_index].can_be_removed_if_unused;
                for (auto import_record_index : repr.ast.parts[part_index].import_record_indices) {
                    auto& record = repr.ast.import_records[import_record_index];
                    if (record.kind != compiler::ImportKind::kStmt) continue;
                    if (record.source_index.IsValid()) {
                        uint32_t other = record.source_index.GetIndex();
                        if (graph.files[other].input_file.side_effects.kind != graph::SideEffectsKind::kHasSideEffects &&
                            !options->IgnoreDCEAnnotations) {
                            continue;
                        }
                        MarkFileLiveForTreeShaking(other);
                    } else if (compiler::Has(record.flags, compiler::ImportRecordFlags::kIsExternalWithoutSideEffects)) {
                        continue;
                    }
                    can_be_removed = false;
                }
                if (!can_be_removed || (!repr.ast.parts[part_index].force_tree_shaking && !options->TreeShaking && file.IsEntryPoint())) {
                    MarkPartLiveForTreeShaking(source_index, static_cast<uint32_t>(part_index));
                }
            }
        } else {
            auto* css_ptr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&file.input_file.repr);
            if (css_ptr && *css_ptr) {
                for (auto& record : (*css_ptr)->ast.import_records) {
                    if (record.source_index.IsValid()) {
                        MarkFileLiveForTreeShaking(record.source_index.GetIndex());
                    }
                }
            }
        }
    }

    // Marks one part of a JavaScript file as live and recurses into its
    // dependencies. The owning file is marked live as well, and the part's is_live
    // flag stops cycles from recursing forever.
    void LinkerContext::MarkPartLiveForTreeShaking(uint32_t source_index, uint32_t part_index) {
        auto& file = graph.files[source_index];
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(file.input_file.repr);
        auto& part = repr.ast.parts[part_index];
        if (part.is_live) return;
        part.is_live = true;
        MarkFileLiveForTreeShaking(source_index);
        for (auto& dep : part.dependencies) {
            MarkPartLiveForTreeShaking(dep.source_index, dep.part_index);
        }
    }

    // Reports whether an import record is a dynamic import of a different entry
    // point while code splitting is enabled. Such imports are the split points
    // that are allowed to stay external: they become a separate chunk instead of
    // being inlined.
    bool LinkerContext::IsExternalDynamicImport(const compiler::ImportRecord& record, uint32_t source_index) {
        return options->CodeSplitting &&
               record.kind == compiler::ImportKind::kDynamic &&
               record.source_index.IsValid() &&
               graph.files[record.source_index.GetIndex()].IsEntryPoint() &&
               record.source_index.GetIndex() != source_index;
    }

    // Records which entry points can reach a file, and the shortest distance to
    // it, then recurses into everything that file imports.
    //
    // A file is revisited whenever a shorter distance is discovered, so the
    // entry-point bit for `entry_point_bit` is set and the walk continues. The
    // distance is used later to decide which chunk should own shared code.
    //
    // Input : entry point bit b at distance 0 from the entry file.
    // Output: every reachable file gains bit b and a distance_from_entry_point.
    void LinkerContext::MarkFileReachableForCodeSplitting(uint32_t source_index, uint32_t entry_point_bit, uint32_t distance) {
        auto& file = graph.files[source_index];
        if (!file.is_live) return;
        bool traverse_again = false;
        if (distance < file.distance_from_entry_point) {
            file.distance_from_entry_point = distance;
            traverse_again = true;
        }
        distance++;
        if (file.entry_bits.HasBit(entry_point_bit) && !traverse_again) return;
        file.entry_bits.SetBit(entry_point_bit, true);

        auto* repr_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr);
        if (repr_ptr && *repr_ptr) {
            auto& repr = **repr_ptr;
            if (repr.css_source_index.IsValid()) {
                MarkFileReachableForCodeSplitting(repr.css_source_index.GetIndex(), entry_point_bit, distance);
            }
            for (auto& record : repr.ast.import_records) {
                if (record.source_index.IsValid() && !IsExternalDynamicImport(record, source_index)) {
                    MarkFileReachableForCodeSplitting(record.source_index.GetIndex(), entry_point_bit, distance);
                }
            }
            for (auto& part : repr.ast.parts) {
                for (auto& dep : part.dependencies) {
                    if (dep.source_index != source_index) {
                        MarkFileReachableForCodeSplitting(dep.source_index, entry_point_bit, distance);
                    }
                }
            }
        } else {
            auto* css_ptr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&file.input_file.repr);
            if (css_ptr && *css_ptr) {
                for (auto& record : (*css_ptr)->ast.import_records) {
                    if (record.source_index.IsValid()) {
                        MarkFileReachableForCodeSplitting(record.source_index.GetIndex(), entry_point_bit, distance);
                    }
                }
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Import/Export resolution
    //
    // These routines turn the raw import and export records of the module graph
    // into concrete bindings: which module must be wrapped, which symbol an import
    // refers to, and how re-export chains resolve. The top-level orchestration
    // lives in ScanImportsAndExports() at the bottom of this file.
    ////////////////////////////////////////////////////////////////////////////////

    // Marks a file and, transitively, every file it statically imports as needing
    // a CommonJS or ESM wrapper.
    //
    // A file with no explicit wrap inherits one from its export kind (CommonJS
    // gets kCJS, everything else kESM), and the walk then visits each resolved
    // import. did_wrap_dependencies prevents revisiting a file, and the runtime
    // file itself is never wrapped.
    void LinkerContext::RecursivelyWrapDependencies(uint32_t source_index) {
        auto& file = graph.files[source_index];
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(file.input_file.repr);
        if (repr.meta.did_wrap_dependencies) return;
        repr.meta.did_wrap_dependencies = true;

        if (source_index == javascript::kSourceIndex) return;

        if (repr.meta.wrap == graph::WrapKind::kNone) {
            if (repr.ast.exports_kind == javascript::ExportsKind::kCommonJS) {
                repr.meta.wrap = graph::WrapKind::kCJS;
            } else {
                repr.meta.wrap = graph::WrapKind::kESM;
            }
        }

        for (auto& record : repr.ast.import_records) {
            if (record.source_index.IsValid()) {
                RecursivelyWrapDependencies(record.source_index.GetIndex());
            }
        }
    }

    // Detects whether a file's exports must be treated as dynamic because of an
    // "export * from" chain. A CommonJS module, or one already marked as having a
    // dynamic fallback, counts immediately. Otherwise the export-star records are
    // followed recursively; reaching an unresolved (external) star while the format
    // cannot keep ESM syntax, or reaching a dynamic module, promotes the file to
    // kESMWithDynamicFallback.
    //
    // `visited` guards against cycles in the star graph.
    // Input : a file that has export-star records.
    // Output: true (and the file marked kESMWithDynamicFallback) or false.
    bool LinkerContext::HasDynamicExportsDueToExportStar(uint32_t source_index, std::unordered_set<uint32_t>& visited) {
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[source_index].input_file.repr);
        if (repr.ast.exports_kind == javascript::ExportsKind::kCommonJS ||
            repr.ast.exports_kind == javascript::ExportsKind::kESMWithDynamicFallback) {
            return true;
        }
        if (visited.count(source_index)) return false;
        visited.insert(source_index);

        for (auto import_record_index : repr.ast.export_star_import_records) {
            auto& record = repr.ast.import_records[import_record_index];
            if ((!record.source_index.IsValid() &&
                 (!graph.files[source_index].IsEntryPoint() || !config::FormatKeepESMImportExportSyntax(options->OutputFormat))) ||
                (record.source_index.IsValid() && record.source_index.GetIndex() != source_index &&
                 HasDynamicExportsDueToExportStar(record.source_index.GetIndex(), visited))) {
                repr.ast.exports_kind = javascript::ExportsKind::kESMWithDynamicFallback;
                return true;
            }
        }
        return false;
    }

    // Adds the exports contributed by "export * from" records to
    // `resolved_exports`.
    //
    // For each star record whose target is not CommonJS, every non-default named
    // export of the target is copied over. A name already exported by an enclosing
    // file in the chain is shadowed and skipped. If the same alias is reached from
    // two different modules, the extra definitions are recorded as potentially
    // ambiguous rather than silently overwriting the first. The walk recurses into
    // the target's own star records, using `source_index_stack` to detect cycles.
    void LinkerContext::AddExportsForExportStar(
        std::unordered_map<std::string, graph::ExportData>& resolved_exports,
        uint32_t source_index,
        std::vector<uint32_t>& source_index_stack) {

        for (auto prev : source_index_stack) {
            if (prev == source_index) return;
        }
        source_index_stack.push_back(source_index);
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[source_index].input_file.repr);

        for (auto import_record_index : repr.ast.export_star_import_records) {
            auto& record = repr.ast.import_records[import_record_index];
            if (!record.source_index.IsValid()) continue;
            uint32_t other_source_index = record.source_index.GetIndex();
            auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[other_source_index].input_file.repr);
            if (other_repr.ast.exports_kind == javascript::ExportsKind::kCommonJS) continue;

            for (auto& [alias, name] : other_repr.ast.named_exports) {
                if (alias == "default") continue;

                bool shadowed = false;
                for (auto prev_si : source_index_stack) {
                    auto& prev_repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[prev_si].input_file.repr);
                    if (prev_repr.ast.named_exports.count(alias)) { shadowed = true; break; }
                }
                if (shadowed) continue;

                auto it = resolved_exports.find(alias);
                if (it == resolved_exports.end()) {
                    resolved_exports[alias] = graph::ExportData{
                        .ref = name.ref,
                        .name_loc = name.alias_loc,
                        .source_index = other_source_index,
                    };
                    repr.meta.imports_to_bind[name.ref] = graph::ImportData{
                        .ref = name.ref,
                        .source_index = other_source_index,
                    };
                } else if (it->second.source_index != other_source_index) {
                    it->second.potentially_ambiguous_export_star_refs.push_back(graph::ImportData{
                        .name_loc = name.alias_loc,
                        .ref = name.ref,
                        .source_index = other_source_index,
                    });
                }
            }

            AddExportsForExportStar(resolved_exports, other_source_index, source_index_stack);
        }
        source_index_stack.pop_back();
    }

    // Checks the CSS "composes: ... from" properties of every local symbol for
    // conflicts.
    //
    // For each local class it walks the composed chain (both imported names and
    // local names) and collects every `property: value` declaration. If the same
    // property is declared with a value coming from two different files, that is
    // an undefined composition, and a warning is reported pointing at both
    // declarations. A property already reported is cleared so it is not reported
    // twice.
    void LinkerContext::ValidateComposesFromProperties(graph::LinkerFile& root_file, graph::CSSRepr& root_repr) {
        for (auto& local : root_repr.ast.local_symbols) {
            struct PropertyInFile {
                graph::LinkerFile* file{};
                logger::Loc loc{};
            };

            std::unordered_set<compiler::Ref, javascript::RefHash> visited;
            std::unordered_map<std::string, PropertyInFile> properties;

            std::function<void(graph::LinkerFile*, graph::CSSRepr*, compiler::Ref)> visit;
            visit = [&](graph::LinkerFile* file, graph::CSSRepr* repr, compiler::Ref ref) {
                if (visited.count(ref)) return;
                visited.insert(ref);

                auto composes_it = repr->ast.composes.find(ref);
                if (composes_it == repr->ast.composes.end()) return;
                auto& composes = *composes_it->second;

                for (auto& name : composes.imported_names) {
                    auto& record = repr->ast.import_records[name.import_record_index];
                    if (record.source_index.IsValid()) {
                        auto& other_file = graph.files[record.source_index.GetIndex()];
                        if (auto* other_css_ptr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&other_file.input_file.repr)) {
                            auto other_it = (*other_css_ptr)->ast.local_scope.find(name.alias);
                            if (other_it != (*other_css_ptr)->ast.local_scope.end()) {
                                visit(&other_file, other_css_ptr->get(), other_it->second.ref);
                            }
                        }
                    }
                }

                for (auto& name : composes.names) {
                    visit(file, repr, name.ref);
                }

                for (auto& [key_text, key_loc] : composes.properties) {
                    auto prop_it = properties.find(key_text);
                    if (prop_it == properties.end()) {
                        properties[key_text] = {file, key_loc};
                        continue;
                    }
                    auto& property = prop_it->second;
                    if (property.file == file || property.file == nullptr) continue;

                    std::string local_original_name = graph.symbols.Get(local.ref)->original_name;
                    log.AddMsgID(logger::MsgID::kCSS_UndefinedComposesFrom, logger::Msg{
                        .notes = {
                            property.file->LineColumnTracker().MakeMsgData(
                                css::RangeOfIdentifier(property.file->input_file.source, property.loc),
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_ComposesValueFirstNote, key_text)),
                            file->LineColumnTracker().MakeMsgData(
                                css::RangeOfIdentifier(file->input_file.source, key_loc),
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_ComposesValueSecondNote, key_text)),
                            {.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_ComposesValueUndefinedNote, key_text, local_original_name, key_text, local_original_name)},
                        },
                        .data = root_file.LineColumnTracker().MakeMsgData(
                            css::RangeOfIdentifier(root_file.input_file.source, local.loc),
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_ComposesValueUndefined, key_text, local_original_name)),
                        .kind = logger::MsgKind::kWarning,
                    });

                    property.file = nullptr;
                    prop_it->second = property;
                }
            };

            visit(&root_file, &root_repr, local.ref);
        }
    }

    // Materialises the body of a file whose export is created lazily.
    //
    // A lazy export starts life as a single placeholder statement. This routine
    // replaces it with real code: for a JavaScript stub that shadows a CSS file it
    // builds an object literal exposing the CSS local names (expanding their
    // composes chains into a template string), and it then emits either a
    // "module.exports = ..." assignment for CommonJS files or individual ES export
    // statements otherwise. Object literals that are not JSON are also unwrapped
    // into separate named exports so tree shaking can drop the unused ones.
    //
    // Input : a file whose last part holds one SLazyExport statement.
    // Output: that statement replaced by the concrete export code.
    void LinkerContext::GenerateCodeForLazyExport(uint32_t source_index) {
        auto& file = graph.files[source_index];
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(file.input_file.repr);

        assert(repr.ast.parts.size() >= 1 && "Internal error: no parts for lazy export");
        auto& part = repr.ast.parts[repr.ast.parts.size() - 1];
        assert(part.stmts.size() == 1 && "Internal error: expected exactly 1 stmt for lazy export");
        auto& lazy_export = std::get<std::shared_ptr<javascript::SLazyExport>>(part.stmts[0].data);
        auto lazy_value = lazy_export->value;

        if (repr.css_source_index.IsValid()) {
            uint32_t css_source_index = repr.css_source_index.GetIndex();
            if (auto* css_ptr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&graph.files[css_source_index].input_file.repr)) {
                auto& css = **css_ptr;
                javascript::EObject exports;

                for (auto& local : css.ast.local_symbols) {
                    javascript::Expr value;
                    value.loc = local.loc;
                    value.data = std::make_shared<javascript::ENameOfSymbol>(javascript::ENameOfSymbol{.ref = local.ref});

                    std::unordered_set<compiler::Ref, javascript::RefHash> visited;
                    visited.insert(local.ref);
                    std::vector<javascript::TemplatePart> parts;
                    std::string tail_cooked_str = " ";

                    std::function<void(graph::CSSRepr*, compiler::Ref)> visit_name;
                    std::function<void(graph::CSSRepr*, compiler::Ref)> visit_composes;

                    visit_name = [&](graph::CSSRepr* r, compiler::Ref ref) {
                        if (visited.count(ref)) return;
                        visited.insert(ref);
                        visit_composes(r, ref);
                        parts.push_back(javascript::TemplatePart{
                            .value = javascript::Expr(std::make_shared<javascript::ENameOfSymbol>(javascript::ENameOfSymbol{.ref = ref}), {}),
                            .tail_cooked = std::u16string(tail_cooked_str.begin(), tail_cooked_str.end()),
                        });
                    };

                    visit_composes = [&](graph::CSSRepr* r, compiler::Ref ref) {
                        auto composes_it = r->ast.composes.find(ref);
                        if (composes_it == r->ast.composes.end()) return;
                        auto& composes = *composes_it->second;

                        for (auto& name : composes.imported_names) {
                            auto& record = r->ast.import_records[name.import_record_index];
                            if (record.source_index.IsValid()) {
                                auto& other_file = graph.files[record.source_index.GetIndex()];
                                if (auto* other_css_ptr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&other_file.input_file.repr)) {
                                    auto other_it = (*other_css_ptr)->ast.local_scope.find(name.alias);
                                    if (other_it != (*other_css_ptr)->ast.local_scope.end()) {
                                        visit_name(other_css_ptr->get(), other_it->second.ref);
                                    }
                                }
                            }
                        }

                        for (auto& name : composes.names) {
                            visit_name(r, name.ref);
                        }
                    };

                    visit_composes(&css, local.ref);

                    if (!parts.empty()) {
                        parts.push_back(javascript::TemplatePart{.value = value});
                        value = javascript::Expr(std::make_shared<javascript::ETemplate>(javascript::ETemplate{
                            .head_cooked = {},
                            .parts = std::move(parts),
                        }), {});
                    }

                    exports.properties.push_back(javascript::Property{
                        .key = javascript::Expr(std::make_shared<javascript::EString>(
                            helpers::StringToUTF16(graph.symbols.Get(local.ref)->original_name)), {}),
                        .value_or_nil = value,
                    });
                }

                lazy_value = javascript::Expr(std::make_shared<javascript::EObject>(std::move(exports)), {});
            }
        }

        if (repr.ast.exports_kind == javascript::ExportsKind::kCommonJS) {
            auto target = javascript::Expr(std::make_shared<javascript::EDot>(javascript::EDot{
                .target = javascript::Expr(std::make_shared<javascript::EIdentifier>(javascript::EIdentifier{.ref = repr.ast.module_ref}), {}),
                .name = "exports",
            }), {});
            part.stmts = {javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{
                .value = javascript::Expr(std::make_shared<javascript::EBinary>(javascript::EBinary{
                    .left = std::move(target),
                    .right = std::move(lazy_value),
                    .op = javascript::OpCode::kBinOpAssign,
                }), {}),
            })}};
            graph.GenerateSymbolImportAndUse(source_index, 0, repr.ast.module_ref, 1, source_index);
            return;
        }

        part.stmts.clear();

        auto generate_export = [&](logger::Loc loc, const std::string& name, const std::string& alias) -> std::pair<compiler::Ref, uint32_t> {
            auto ref = graph.GenerateNewSymbol(source_index, compiler::SymbolKind::kOther, name);
            uint32_t part_index = graph.AddPartToFile(source_index, javascript::Part{
                .declared_symbols = {{.ref = ref, .is_top_level = true}},
                .can_be_removed_if_unused = true,
            });
            graph.GenerateSymbolImportAndUse(source_index, part_index, repr.ast.module_ref, 1, source_index);
            repr.meta.top_level_symbol_to_parts_overlay[ref] = {part_index};
            repr.meta.resolved_exports[alias] = graph::ExportData{
                .ref = ref,
                .name_loc = loc,
                .source_index = source_index,
            };
            return {ref, part_index};
        };

        if (auto* object_ptr = std::get_if<std::shared_ptr<javascript::EObject>>(&lazy_value.data)) {
            if (file.input_file.loader != config::Loader::kWithTypeJSON) {
                for (auto& property : (*object_ptr)->properties) {
                    if (auto* str = std::get_if<std::shared_ptr<javascript::EString>>(&property.key.data)) {
                        if (!file.IsEntryPoint() || javascript::IsIdentifierUTF16(std::span<const uint16_t>(
                            reinterpret_cast<const uint16_t*>((*str)->value.data()), (*str)->value.size())) ||
                            !compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArbitraryModuleNamespaceNames)) {
                            auto name = helpers::UTF16ToString((*str)->value);
                            if (name != "default") {
                                auto [ref, part_index] = generate_export(property.key.loc, name, name);
                                repr.ast.parts[part_index].stmts = {javascript::Stmt{
                                    .data = std::make_shared<javascript::SLocal>(javascript::SLocal{
                                        .decls = {{.binding = javascript::Binding{
                                            .data = std::make_shared<javascript::BIdentifier>(javascript::BIdentifier{.ref = ref}),
                                            .loc = property.key.loc,
                                        }, .value_or_nil = property.value_or_nil}},
                                        .is_export = true,
                                    }),
                                    .loc = property.key.loc,
                                }};
                            }
                        }
                    }
                }
            }
        }

        auto default_name = file.input_file.source.identifier_name + "_default";
        auto [ref, part_index] = generate_export(lazy_value.loc, default_name, "default");
        repr.ast.parts[part_index].stmts = {javascript::Stmt{
            .data = std::make_shared<javascript::SExportDefault>(javascript::SExportDefault{
                .value = javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{.value = lazy_value}), .loc = lazy_value.loc},
                .default_name = {.loc = lazy_value.loc, .ref = ref},
            }),
            .loc = lazy_value.loc,
        }};
    }

    // Builds the namespace-exports part for a file: the runtime calls that expose
    // its exports object.
    //
    // Every resolved export alias becomes a getter property passed to the runtime
    // "__export" helper, and the corresponding dependencies are recorded so the
    // target parts are included. A getter is emitted as an arrow function where
    // supported, or a plain function otherwise, and "__proto__" is made a computed
    // key where object extensions are unavailable. For CommonJS entry points that
    // must expose an exports object, a final "module.exports = __toCommonJS(...)"
    // statement is appended.
    //
    // The whole part is stored at the reserved namespace-export part index and is
    // marked as removable and force-tree-shaken.
    void LinkerContext::CreateExportsForFile(uint32_t source_index) {
        auto& file = graph.files[source_index];
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(file.input_file.repr);

        std::vector<javascript::Property> properties;
        std::vector<javascript::Dependency> ns_export_dependencies;
        std::unordered_map<compiler::Ref, javascript::SymbolUse, javascript::RefHash> ns_export_symbol_uses;

        for (auto& alias : repr.meta.sorted_and_filtered_export_aliases) {
            auto export_it = repr.meta.resolved_exports.find(alias);
            if (export_it == repr.meta.resolved_exports.end()) continue;
            auto export_data = export_it->second;

            auto& export_file_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                graph.files[export_data.source_index].input_file.repr);
            auto import_it = export_file_repr.meta.imports_to_bind.find(export_data.ref);
            if (import_it != export_file_repr.meta.imports_to_bind.end()) {
                export_data.ref = import_it->second.ref;
                export_data.source_index = import_it->second.source_index;
                ns_export_dependencies.insert(ns_export_dependencies.end(),
                    import_it->second.re_exports.begin(), import_it->second.re_exports.end());
            }

            javascript::Expr value;
            auto* sym = graph.symbols.Get(export_data.ref);
            if (sym->namespace_alias) {
                value = javascript::Expr(std::make_shared<javascript::EImportIdentifier>(export_data.ref), {});
            } else {
                value = javascript::Expr(std::make_shared<javascript::EIdentifier>(export_data.ref), {});
            }

            javascript::FnBody body;
            body.block.stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SReturn>(javascript::SReturn{.value_or_nil = std::move(value)})});

            javascript::Expr getter;
            if (compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArrow)) {
                getter = javascript::Expr(std::make_shared<javascript::EFunction>(javascript::EFunction{.fn = javascript::Fn{.body = std::move(body)}}), {});
            } else {
                getter = javascript::Expr(std::make_shared<javascript::EArrow>(javascript::EArrow{.body = std::move(body), .prefer_expr = true}), {});
            }

            auto property_flags = javascript::PropertyFlags::kNone;
            if (alias == "__proto__" && !compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kObjectExtensions)) {
                property_flags = property_flags | javascript::PropertyFlags::kIsComputed;
            }

            properties.push_back(javascript::Property{
                .key = javascript::Expr(std::make_shared<javascript::EString>(helpers::StringToUTF16(alias)), {}),
                .value_or_nil = std::move(getter),
                .flags = property_flags,
            });
            ns_export_symbol_uses[export_data.ref] = javascript::SymbolUse{.count_estimate = 1};

            auto& target_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                graph.files[export_data.source_index].input_file.repr);
            for (auto part_index : target_repr.TopLevelSymbolToParts(export_data.ref)) {
                ns_export_dependencies.push_back(javascript::Dependency{
                    .source_index = export_data.source_index,
                    .part_index = part_index,
                });
            }
        }

        std::vector<javascript::DeclaredSymbol> declared_symbols;
        std::vector<javascript::Stmt> ns_export_stmts;

        if (repr.meta.needs_exports_variable) {
            ns_export_stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SLocal>(javascript::SLocal{
                .decls = {{.binding = javascript::Binding{.data = std::make_shared<javascript::BIdentifier>(repr.ast.exports_ref)},
                           .value_or_nil = javascript::Expr(std::make_shared<javascript::EObject>(), {})}},
            })});
            declared_symbols.push_back(javascript::DeclaredSymbol{.ref = repr.ast.exports_ref, .is_top_level = true});
        }

        compiler::Ref export_ref = compiler::kInvalidRef;
        if (!properties.empty()) {
            auto& runtime_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                graph.files[javascript::kSourceIndex].input_file.repr);
            export_ref = runtime_repr.ast.module_scope->members["__export"].ref;

            std::vector<javascript::Expr> call_args;
            call_args.push_back(javascript::Expr(std::make_shared<javascript::EIdentifier>(repr.ast.exports_ref), {}));
            call_args.push_back(javascript::Expr(std::make_shared<javascript::EObject>(javascript::EObject{.properties = std::move(properties)}), {}));

            ns_export_stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{
                .value = javascript::Expr(std::make_shared<javascript::ECall>(javascript::ECall{
                    .target = javascript::Expr(std::make_shared<javascript::EIdentifier>(export_ref), {}),
                    .args = std::move(call_args),
                }), {})
            })});

            for (auto part_index : runtime_repr.TopLevelSymbolToParts(export_ref)) {
                ns_export_dependencies.push_back(javascript::Dependency{
                    .source_index = javascript::kSourceIndex,
                    .part_index = part_index,
                });
            }
            repr.ast.uses_exports_ref = true;
        }

        if (repr.meta.force_include_exports_for_entry_point &&
            options->OutputFormat == config::Format::kCommonJS) {
            auto& runtime_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                graph.files[javascript::kSourceIndex].input_file.repr);
            auto to_commonjs_ref = runtime_repr.ast.named_exports["__toCommonJS"].ref;

            auto target = javascript::Expr(std::make_shared<javascript::EDot>(javascript::EDot{
                .target = javascript::Expr(std::make_shared<javascript::EIdentifier>(unbound_module_ref), {}),
                .name = "exports",
            }), {});
            auto value = javascript::Expr(std::make_shared<javascript::ECall>(javascript::ECall{
                .target = javascript::Expr(std::make_shared<javascript::EIdentifier>(to_commonjs_ref), {}),
                .args = {{javascript::Expr(std::make_shared<javascript::EIdentifier>(repr.ast.exports_ref), {})}},
            }), {});
            ns_export_stmts.push_back(javascript::Stmt{.data = std::make_shared<javascript::SExpr>(javascript::SExpr{
                .value = javascript::Expr(std::make_shared<javascript::EBinary>(javascript::EBinary{
                    .left = std::move(target),
                    .right = std::move(value),
                    .op = javascript::OpCode::kBinOpAssign,
                }), {})
            })});
        }

        if (!ns_export_stmts.empty()) {
            repr.ast.parts[javascript::kNSExportPartIndex] = javascript::Part{
                .stmts = std::move(ns_export_stmts),
                .declared_symbols = std::move(declared_symbols),
                .symbol_uses = std::move(ns_export_symbol_uses),
                .dependencies = std::move(ns_export_dependencies),
                .can_be_removed_if_unused = true,
                .force_tree_shaking = true,
            };
            if (export_ref != compiler::kInvalidRef) {
                repr.meta.needs_export_symbol_from_runtime = true;
            }
        }
    }

    // Adds the wrapper part that runs a file inside its CommonJS or ESM shim.
    //
    // For a kCJS file the wrapper declares the file's `exports`, `module` and
    // wrapper symbols and depends on the runtime's CommonJS helper. For a kESM
    // file it declares just the wrapper symbol and depends on the runtime's ESM
    // helper. The created part index is remembered in wrapper_part_index, and the
    // relevant runtime symbol is recorded as an import so it stays live. Files
    // that need no wrapper are left untouched.
    void LinkerContext::CreateWrapperForFile(uint32_t source_index) {
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[source_index].input_file.repr);

        switch (repr.meta.wrap) {
        case graph::WrapKind::kCJS: {
            auto& runtime_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                graph.files[javascript::kSourceIndex].input_file.repr);
            auto commonjs_parts = runtime_repr.TopLevelSymbolToParts(cjs_runtime_ref);

            std::vector<javascript::Dependency> dependencies;
            for (auto part_index : commonjs_parts) {
                dependencies.push_back({.source_index = javascript::kSourceIndex, .part_index = part_index});
            }

            std::unordered_map<compiler::Ref, javascript::SymbolUse, javascript::RefHash> symbol_uses;
            symbol_uses[repr.ast.wrapper_ref] = {.count_estimate = 1};

            std::vector<javascript::DeclaredSymbol> declared;
            declared.push_back({.ref = repr.ast.exports_ref, .is_top_level = true});
            declared.push_back({.ref = repr.ast.module_ref, .is_top_level = true});
            declared.push_back({.ref = repr.ast.wrapper_ref, .is_top_level = true});

            uint32_t part_index = graph.AddPartToFile(source_index, javascript::Part{
                .declared_symbols = std::move(declared),
                .symbol_uses = std::move(symbol_uses),
                .dependencies = std::move(dependencies),
            });
            repr.meta.wrapper_part_index = compiler::Index32::Make(part_index);
            graph.GenerateSymbolImportAndUse(source_index, part_index, cjs_runtime_ref, 1, javascript::kSourceIndex);
            break;
        }
        case graph::WrapKind::kESM: {
            auto& runtime_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                graph.files[javascript::kSourceIndex].input_file.repr);
            auto esm_parts = runtime_repr.TopLevelSymbolToParts(esm_runtime_ref);

            std::vector<javascript::Dependency> dependencies;
            for (auto part_index : esm_parts) {
                dependencies.push_back({.source_index = javascript::kSourceIndex, .part_index = part_index});
            }

            std::unordered_map<compiler::Ref, javascript::SymbolUse, javascript::RefHash> symbol_uses;
            symbol_uses[repr.ast.wrapper_ref] = {.count_estimate = 1};

            std::vector<javascript::DeclaredSymbol> declared;
            declared.push_back({.ref = repr.ast.wrapper_ref, .is_top_level = true});

            uint32_t part_index = graph.AddPartToFile(source_index, javascript::Part{
                .declared_symbols = std::move(declared),
                .symbol_uses = std::move(symbol_uses),
                .dependencies = std::move(dependencies),
            });
            repr.meta.wrapper_part_index = compiler::Index32::Make(part_index);
            graph.GenerateSymbolImportAndUse(source_index, part_index, esm_runtime_ref, 1, javascript::kSourceIndex);
            break;
        }
        default:
            break;
        }
    }

    // Steps an import one hop along its resolution chain and classifies the
    // result.
    //
    // Starting from a named import, this looks up the record it came from and
    // returns the next tracker to follow. It reports kNoMatch when the import
    // cannot be found, kExternal when the target is not a resolved file,
    // kCommonJSWithoutExports when the target has no exports at all,
    // kCommonJS/kDynamicFallback for the corresponding module kinds, kFound when a
    // concrete export was located (filling in potentially-ambiguous alternatives),
    // and kProbablyTypeScriptType for an exported type-only import.
    //
    // Input : tracker {source_index, import_ref} for one named import.
    // Output: the next tracker plus an ImportStatus describing the hop.
    ImportTracker LinkerContext::AdvanceImportTracker(
        const ImportTracker& tracker,
        ImportStatus& status,
        std::vector<graph::ImportData>& potentially_ambiguous_export_star_refs) {

        auto& file = graph.files[tracker.source_index];
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(file.input_file.repr);
        auto named_import_it = repr.ast.named_imports.find(tracker.import_ref);
        if (named_import_it == repr.ast.named_imports.end()) {
            status = ImportStatus::kNoMatch;
            return {};
        }
        auto& named_import = named_import_it->second;

        auto& record = repr.ast.import_records[named_import.import_record_index];
        if (!record.source_index.IsValid()) {
            status = ImportStatus::kExternal;
            return {};
        }

        uint32_t other_source_index = record.source_index.GetIndex();
        auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[other_source_index].input_file.repr);

        if (!named_import.alias_is_star && !other_repr.ast.has_lazy_export &&
            other_repr.ast.export_keyword.len == 0 && named_import.alias != "default" &&
            !other_repr.ast.uses_exports_ref && !other_repr.ast.uses_module_ref) {
            status = ImportStatus::kCommonJSWithoutExports;
            return {.source_index = other_source_index, .import_ref = compiler::kInvalidRef};
        }

        if (other_repr.ast.exports_kind == javascript::ExportsKind::kCommonJS) {
            status = ImportStatus::kCommonJS;
            return {.source_index = other_source_index, .import_ref = compiler::kInvalidRef};
        }

        if (named_import.alias_is_star) {
            if (other_repr.meta.resolved_export_star.has_value()) {
                auto& matching_export = *other_repr.meta.resolved_export_star;
                potentially_ambiguous_export_star_refs = matching_export.potentially_ambiguous_export_star_refs;
                status = ImportStatus::kFound;
                return {
                    .source_index = matching_export.source_index,
                    .name_loc = matching_export.name_loc,
                    .import_ref = matching_export.ref,
                };
            }
        }

        auto export_it = other_repr.meta.resolved_exports.find(named_import.alias);
        if (export_it != other_repr.meta.resolved_exports.end()) {
            auto& matching_export = export_it->second;
            potentially_ambiguous_export_star_refs = matching_export.potentially_ambiguous_export_star_refs;
            status = ImportStatus::kFound;
            return {
                .source_index = matching_export.source_index,
                .name_loc = matching_export.name_loc,
                .import_ref = matching_export.ref,
            };
        }

        if (other_repr.ast.exports_kind == javascript::ExportsKind::kESMWithDynamicFallback) {
            status = ImportStatus::kDynamicFallback;
            return {.source_index = other_source_index, .import_ref = other_repr.ast.exports_ref};
        }

        if (config::IsTypeScript(file.input_file.loader) && named_import.is_exported) {
            status = ImportStatus::kProbablyTypeScriptType;
            return {};
        }

        status = ImportStatus::kNoMatch;
        return {.source_index = other_source_index};
    }

    // Reports an error when an exported or imported name is not a valid
    // identifier, but only when the target environment cannot represent arbitrary
    // string names in module namespace objects. `kind` names the construct
    // ("import" or "export") for the message.
    void LinkerContext::MaybeForbidArbitraryModuleNamespaceIdentifier(
        std::string_view kind, uint32_t source_index, logger::Loc loc, const std::string& alias) {
        if (!javascript::IsIdentifier(alias)) {
            auto& file = graph.files[source_index];
            auto where = config::PrettyPrintTargetEnvironment(
                options->OriginalTargetEnv, options->UnsupportedJSFeatureOverridesMask);
            log.AddError(&file.LineColumnTracker(), file.input_file.source.RangeOfString(loc),
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_StringAsNameNotSupported, alias, std::string(kind), where));
        }
    }



    // Adds a "did you mean ..." suggestion to a missing-import message.
    //
    // The file's export aliases are collected (once, then cached on the repr) into
    // a typo detector, and if the misspelled alias is close to a real export the
    // suggestion is attached to the message and a note pointing at the real export
    // is added.
    void LinkerContext::MaybeCorrectObviousTypo(
        graph::JSRepr& repr, const std::string& alias, logger::Msg& msg) {
        if (!repr.meta.resolved_export_typos.has_value()) {
            std::vector<std::string> valid;
            valid.reserve(repr.meta.resolved_exports.size());
            for (auto& [a, _] : repr.meta.resolved_exports) {
                valid.push_back(a);
            }
            std::sort(valid.begin(), valid.end());
            repr.meta.resolved_export_typos = helpers::TypoDetector(valid);
        }

        auto corrected = repr.meta.resolved_export_typos->MaybeCorrectTypo(alias);
        if (corrected.has_value()) {
            if (msg.data.location) msg.data.location->suggestion = *corrected;
            auto export_it = repr.meta.resolved_exports.find(*corrected);
            if (export_it != repr.meta.resolved_exports.end()) {
                auto& imported_file = graph.files[export_it->second.source_index];
                std::string text = "Did you mean to import \"" + *corrected + "\" instead?";
                logger::MsgData note;
                note.text = text;
                if (export_it->second.name_loc.start != 0) {
                    logger::Range r;
                    if (config::IsCSS(imported_file.input_file.loader)) {
                        r = css::RangeOfIdentifier(imported_file.input_file.source, export_it->second.name_loc);
                    } else {
                        r = javascript::RangeOfIdentifier(imported_file.input_file.source, export_it->second.name_loc);
                    }
                    note = imported_file.LineColumnTracker().MakeMsgData(r, text);
                }
                msg.notes.push_back(std::move(note));
            }
        }
    }

    // Resolves a single import all the way through the re-export chain it may pass
    // through, producing the final binding.
    //
    // It repeatedly advances the tracker (AdvanceImportTracker). A found export is
    // followed further while the target is itself a named import, accumulating the
    // parts that must be re-exported; a namespace member access adds a namespace
    // binding; and terminal states such as CommonJS, external, no-match or a cycle
    // stop the walk. When several ambiguous export-star paths disagree the result
    // becomes kAmbiguous. Missing imports produce the appropriate undefined-import
    // warning or no-matching-export error, deduplicated across passes.
    //
    // Input : tracker for one named import, plus an output vector for re-exports.
    // Output: a MatchImportResult describing the final binding (or a failure kind).
    MatchImportResult LinkerContext::MatchImportWithExport(
        ImportTracker tracker,
        std::vector<javascript::Dependency>& re_exports) {

        std::vector<MatchImportResult> ambiguous_results;
        MatchImportResult result;

        while (true) {
            bool cycle_found = false;
            for (auto& prev : cycle_detector) {
                if (tracker.source_index == prev.source_index && tracker.import_ref == prev.import_ref &&
                    tracker.name_loc.start == prev.name_loc.start) {
                    cycle_found = true;
                    break;
                }
            }
            if (cycle_found) {
                return {.kind = MatchImportKind::kCycle};
            }
            cycle_detector.push_back(tracker);

            ImportStatus status;
            std::vector<graph::ImportData> potentially_ambiguous;
            auto next = AdvanceImportTracker(tracker, status, potentially_ambiguous);

            switch (status) {
            case ImportStatus::kCommonJS:
            case ImportStatus::kCommonJSWithoutExports:
            case ImportStatus::kExternal:
            case ImportStatus::kDisabled: {
                if (status == ImportStatus::kExternal && config::FormatKeepESMImportExportSyntax(options->OutputFormat)) {
                    break;
                }

                auto& tracker_file = graph.files[tracker.source_index];
                auto& tracker_repr = *std::get<std::shared_ptr<graph::JSRepr>>(tracker_file.input_file.repr);
                auto ni = tracker_repr.ast.named_imports.find(tracker.import_ref);
                if (ni != tracker_repr.ast.named_imports.end()) {
                    auto& named_import = ni->second;
                    if (named_import.namespace_ref != compiler::kInvalidRef) {
                        if (status == ImportStatus::kCommonJSWithoutExports) {
                            auto* symbol = graph.symbols.Get(tracker.import_ref);
                            if (symbol->import_item_status == compiler::ImportItemStatus::kNone ||
                                symbol->import_item_status == compiler::ImportItemStatus::kGenerated) {
                                symbol->import_item_status = compiler::ImportItemStatus::kMissing;
                                if (symbol->use_count_estimate > 0 &&
                                    !helpers::IsInsideNodeModules(tracker_file.input_file.source.key_path.text)) {
                                    std::string reason = "because the file \"" +
                                        graph.files[next.source_index].input_file.source.pretty_paths.Select(options->LogPathStyle) +
                                        "\" has no exports";
                                    auto msg = logger::Msg{
                                        .data = tracker_file.LineColumnTracker().MakeMsgData(
                                            javascript::RangeOfIdentifier(tracker_file.input_file.source, named_import.alias_loc),
                                            "Import \"" + named_import.alias + "\" will always be undefined " + reason),
                                        .kind = logger::MsgKind::kWarning,
                                    };
                                    bool already_warned = false;
                                    for (const auto& existing : log.peek()) {
                                        if (existing.id == logger::MsgID::kBundler_ImportIsUndefined &&
                                            existing.data.text == msg.data.text &&
                                            existing.data.location && msg.data.location &&
                                            existing.data.location->file.abs == msg.data.location->file.abs) {
                                            already_warned = true;
                                            break;
                                        }
                                    }
                                    if (!already_warned) {
                                        log.AddMsgID(logger::MsgID::kBundler_ImportIsUndefined, std::move(msg));
                                    }
                                }
                            }
                        }

                        if (result.kind == MatchImportKind::kNormal) {
                            result.kind = MatchImportKind::kNormalAndNamespace;
                            result.namespace_ref = named_import.namespace_ref;
                            result.alias = named_import.alias;
                        } else {
                            result = MatchImportResult{
                                .alias = named_import.alias,
                                .kind = MatchImportKind::kNamespace,
                                .namespace_ref = named_import.namespace_ref,
                            };
                        }
                    }
                }
                break;
            }
            case ImportStatus::kDynamicFallback: {
                auto& tracker_file = graph.files[tracker.source_index];
                auto& tracker_repr = *std::get<std::shared_ptr<graph::JSRepr>>(tracker_file.input_file.repr);
                auto ni = tracker_repr.ast.named_imports.find(tracker.import_ref);
                if (ni != tracker_repr.ast.named_imports.end() && result.kind != MatchImportKind::kNormal) {
                    auto& named_import = ni->second;
                    if (named_import.namespace_ref != compiler::kInvalidRef) {
                        result = MatchImportResult{
                            .alias = named_import.alias,
                            .kind = MatchImportKind::kNamespace,
                            .namespace_ref = next.import_ref,
                        };
                    }
                } else if (ni != tracker_repr.ast.named_imports.end()) {
                    auto& named_import = ni->second;
                    if (named_import.namespace_ref != compiler::kInvalidRef) {
                        result.kind = MatchImportKind::kNormalAndNamespace;
                        result.namespace_ref = next.import_ref;
                        result.alias = named_import.alias;
                    }
                }
                break;
            }
case ImportStatus::kNoMatch: {
    auto& tracker_file = graph.files[tracker.source_index];
    auto& tracker_repr =
        *std::get<std::shared_ptr<graph::JSRepr>>(tracker_file.input_file.repr);

    auto ni = tracker_repr.ast.named_imports.find(tracker.import_ref);

    if (ni != tracker_repr.ast.named_imports.end()) {
        auto& named_import = ni->second;
        auto* symbol = graph.symbols.Get(tracker.import_ref);

        auto& next_file = graph.files[next.source_index].input_file;
        auto& next_repr =
            *std::get<std::shared_ptr<graph::JSRepr>>(next_file.repr);

        if (symbol->import_item_status == compiler::ImportItemStatus::kGenerated) {
            symbol->import_item_status = compiler::ImportItemStatus::kMissing;

            if (symbol->use_count_estimate > 0 &&
                !helpers::IsInsideNodeModules(
                    tracker_file.input_file.source.key_path.text)) {
                auto msg = logger::Msg{
                    .data = tracker_file.LineColumnTracker().MakeMsgData(
                        javascript::RangeOfIdentifier(
                            tracker_file.input_file.source,
                            named_import.alias_loc),
                        "Import \"" + named_import.alias +
                        "\" will always be undefined because there is no matching export in \"" +
                        next_file.source.pretty_paths.Select(options->LogPathStyle) +
                        "\""),
                    .kind = logger::MsgKind::kWarning,
                };

                bool already_warned = false;
                for (const auto& existing : log.peek()) {
                    if (existing.id ==
                            logger::MsgID::kBundler_ImportIsUndefined &&
                        existing.data.text == msg.data.text &&
                        existing.data.location && msg.data.location &&
                        existing.data.location->file.abs == msg.data.location->file.abs) {
                        already_warned = true;
                        break;
                    }
                }

                if (!already_warned) {
                    MaybeCorrectObviousTypo(
                        next_repr, named_import.alias, msg);
                    log.AddMsgID(
                        logger::MsgID::kBundler_ImportIsUndefined,
                        std::move(msg));
                }
            }
        } else {
            auto msg = logger::Msg{
                .data = tracker_file.LineColumnTracker().MakeMsgData(
                    javascript::RangeOfIdentifier(
                        tracker_file.input_file.source,
                        named_import.alias_loc),
                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_NoMatchingExport,
                        next_file.source.pretty_paths.Select(options->LogPathStyle),
                        named_import.alias)),
                .kind = logger::MsgKind::kError,
            };

            MaybeCorrectObviousTypo(
                next_repr, named_import.alias, msg);
            log.add_msg(std::move(msg));
        }
    }

    return {.kind = MatchImportKind::kIgnore};
}
            case ImportStatus::kProbablyTypeScriptType:
                return {.kind = MatchImportKind::kProbablyTypeScriptType};

            case ImportStatus::kFound: {
                for (auto& ambiguous_tracker : potentially_ambiguous) {
                    auto& amb_file = graph.files[ambiguous_tracker.source_index];
                    auto& amb_repr = *std::get<std::shared_ptr<graph::JSRepr>>(amb_file.input_file.repr);
                    if (amb_repr.ast.named_imports.count(ambiguous_tracker.ref)) {
                        auto old_cycle = cycle_detector;
                        auto amb_result = MatchImportWithExport(
                            {.source_index = ambiguous_tracker.source_index, .import_ref = ambiguous_tracker.ref},
                            re_exports);
                        cycle_detector = old_cycle;
                        ambiguous_results.push_back(amb_result);
                    } else {
                        ambiguous_results.push_back({
                            .kind = MatchImportKind::kNormal,
                            .source_index = ambiguous_tracker.source_index,
                            .name_loc = ambiguous_tracker.name_loc,
                            .ref = ambiguous_tracker.ref,
                        });
                    }
                }

                result = MatchImportResult{
                    .kind = MatchImportKind::kNormal,
                    .source_index = next.source_index,
                    .name_loc = next.name_loc,
                    .ref = next.import_ref,
                };

                auto& tracker_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                    graph.files[tracker.source_index].input_file.repr);
                for (auto part_index : tracker_repr.TopLevelSymbolToParts(tracker.import_ref)) {
                    re_exports.push_back({.source_index = tracker.source_index, .part_index = part_index});
                }

                auto& next_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                    graph.files[next.source_index].input_file.repr);
                if (next_repr.ast.named_imports.count(next.import_ref)) {
                    tracker = next;
                    continue;
                }
                break;
            }
            }

            break;
        }

        for (auto& ambiguous_result : ambiguous_results) {
            bool same = (ambiguous_result.kind == result.kind) &&
                        (ambiguous_result.source_index == result.source_index) &&
                        (ambiguous_result.ref == result.ref) &&
                        (ambiguous_result.name_loc.start == result.name_loc.start);
            if (!same) {
                if (result.kind == MatchImportKind::kNormal && ambiguous_result.kind == MatchImportKind::kNormal &&
                    result.name_loc.start != 0 && ambiguous_result.name_loc.start != 0) {
                    return MatchImportResult{
                        .kind = MatchImportKind::kAmbiguous,
                        .source_index = result.source_index,
                        .name_loc = result.name_loc,
                        .other_source_index = ambiguous_result.source_index,
                        .other_name_loc = ambiguous_result.name_loc,
                    };
                }
                return {.kind = MatchImportKind::kAmbiguous};
            }
        }

        return result;
    }

    // Resolves every named import of one file, in a deterministic order.
    //
    // Imports are visited by ascending symbol inner index so diagnostics are
    // stable. Each result is applied to the file's metadata: a normal match is
    // recorded in imports_to_bind, a namespace match sets the symbol's namespace
    // alias, and the failure kinds log a cycle error, mark a type-only import, or
    // report an ambiguous or undefined import with the locations of the competing
    // exports. The cycle detector is reset per import.
    void LinkerContext::MatchImportsWithExportsForFile(uint32_t source_index) {
        auto& file = graph.files[source_index];
        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(file.input_file.repr);

        std::vector<uint32_t> sorted_inner_indices;
        for (auto& [ref, _] : repr.ast.named_imports) {
            sorted_inner_indices.push_back(ref.inner_index);
        }
        std::sort(sorted_inner_indices.begin(), sorted_inner_indices.end());

        for (auto inner_index : sorted_inner_indices) {
            cycle_detector.clear();
            compiler::Ref import_ref{.source_index = source_index, .inner_index = inner_index};

            std::vector<javascript::Dependency> re_exports;
            auto result = MatchImportWithExport(
                {.source_index = source_index, .import_ref = import_ref}, re_exports);

            switch (result.kind) {
            case MatchImportKind::kIgnore:
                break;

            case MatchImportKind::kNormal:
                repr.meta.imports_to_bind[import_ref] = graph::ImportData{
                    .re_exports = std::move(re_exports),
                    .ref = result.ref,
                    .source_index = result.source_index,
                };
                break;

            case MatchImportKind::kNamespace:
                graph.symbols.Get(import_ref)->namespace_alias = new compiler::NamespaceAlias(
                    compiler::NamespaceAlias{.alias = result.alias, .namespace_ref = result.namespace_ref});
                break;

            case MatchImportKind::kNormalAndNamespace:
                repr.meta.imports_to_bind[import_ref] = graph::ImportData{
                    .re_exports = std::move(re_exports),
                    .ref = result.ref,
                    .source_index = result.source_index,
                };
                graph.symbols.Get(import_ref)->namespace_alias = new compiler::NamespaceAlias(
                    compiler::NamespaceAlias{.alias = result.alias, .namespace_ref = result.namespace_ref});
                break;

            case MatchImportKind::kCycle: {
                auto ni = repr.ast.named_imports.find(import_ref);
                if (ni != repr.ast.named_imports.end()) {
                    log.AddError(&file.LineColumnTracker(),
                        javascript::RangeOfIdentifier(file.input_file.source, ni->second.alias_loc),
                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_CycleWhileResolving, ni->second.alias));
                }
                break;
            }

            case MatchImportKind::kProbablyTypeScriptType:
                repr.meta.is_probably_typescript_type[import_ref] = true;
                break;

            case MatchImportKind::kAmbiguous: {
                auto ni = repr.ast.named_imports.find(import_ref);
                if (ni != repr.ast.named_imports.end()) {
                    auto r = javascript::RangeOfIdentifier(file.input_file.source, ni->second.alias_loc);
                    auto* symbol = graph.symbols.Get(import_ref);

                    std::vector<logger::MsgData> notes;
                    if (result.name_loc.start != 0 && result.other_name_loc.start != 0) {
                        auto& file_a = graph.files[result.source_index];
                        auto& file_b = graph.files[result.other_source_index];
                        logger::Range ra = javascript::RangeOfIdentifier(
                            file_a.input_file.source, result.name_loc);
                        logger::Range rb = javascript::RangeOfIdentifier(
                            file_b.input_file.source, result.other_name_loc);
                        notes = {
                            file_a.LineColumnTracker().MakeMsgData(
                                ra, guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_OneMatchingExportNote)),
                            file_b.LineColumnTracker().MakeMsgData(
                                rb, guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_AnotherMatchingExportNote)),
                        };
                    }

                    if (symbol->import_item_status == compiler::ImportItemStatus::kGenerated) {
                        symbol->import_item_status = compiler::ImportItemStatus::kMissing;
                        log.AddIDWithNotes(logger::MsgID::kBundler_ImportIsUndefined, logger::MsgKind::kWarning,
                            &file.LineColumnTracker(), r,
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_ImportAlwaysUndefinedMultipleMatches, ni->second.alias),
                            notes);
                    } else {
                        log.AddErrorWithNotes(&file.LineColumnTracker(), r,
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_AmbiguousImportMultipleMatches, ni->second.alias),
                            notes);
                    }
                }
                break;
            }
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Main scanImportsAndExports pipeline
    //
    // Scanning runs in six ordered steps over the reachable files. Each step
    // depends on the results of the previous one, so they must stay in order.
    ////////////////////////////////////////////////////////////////////////////////

    // Runs the six-step import/export scan over every reachable file.
    //
    //   Step 1 decides which modules must be wrapped and which export kind they
    //          have, resolves CSS-to-JavaScript import records, validates CSS
    //          composes imports, and collects additional (copied) files.
    //   Step 2 propagates dynamic-export status, wrapping dependencies of dynamic
    //          and CommonJS files.
    //   Step 3 generates code for lazy exports and resolves "export * from".
    //   Step 4 matches imports with exports and sets up wrappers, deciding which
    //          entry points need an exports object.
    //   Step 5 (parallel) builds namespace exports per file and wires the
    //          cross-part dependencies, rethrowing any worker failure on the main
    //          thread.
    //   Step 6 binds imports to exports, adds entry-point parts, and encodes the
    //          per-part import constraints (to-ESM / to-CommonJS / require uses).
    void LinkerContext::ScanImportsAndExports() {
        timer->Begin("Scan imports and exports");

        timer->Begin("Step 1");

        for (auto source_index : graph.reachable_files) {
            auto& file = graph.files[source_index];
            auto& additional_files = file.input_file.additional_files;

            if (auto* css_ptr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&file.input_file.repr)) {
                auto& repr = **css_ptr;
                for (size_t i = 0; i < repr.ast.import_records.size(); i++) {
                    auto& record = repr.ast.import_records[i];
                    if (record.source_index.IsValid()) {
                        auto& other_file = graph.files[record.source_index.GetIndex()];
                        if (auto* other_js_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&other_file.input_file.repr)) {
                            record.path.text = (*other_js_ptr)->ast.url_for_css;
                            record.path.namespace_ = "";
                            record.source_index = compiler::Index32{};
                            if (other_file.input_file.loader == config::Loader::kEmpty) {
                                record.flags = record.flags | compiler::ImportRecordFlags::kWasLoadedWithEmptyLoader;
                            } else {
                                record.flags = record.flags | compiler::ImportRecordFlags::kShouldNotBeExternalInMetafile;
                            }
                            if ((*other_js_ptr)->ast.url_for_css.find(unique_key_prefix) != std::string::npos) {
                                record.flags = record.flags | compiler::ImportRecordFlags::kContainsUniqueKey;
                            }
                            additional_files.insert(additional_files.end(),
                                other_file.input_file.additional_files.begin(),
                                other_file.input_file.additional_files.end());
                        } else if (auto* other_copy_ptr = std::get_if<std::shared_ptr<graph::CopyRepr>>(&other_file.input_file.repr)) {
                            record.path.text = (*other_copy_ptr)->url_for_code;
                            record.path.namespace_ = "";
                            record.source_index = compiler::Index32{};
                            record.flags = record.flags |
                                compiler::ImportRecordFlags::kShouldNotBeExternalInMetafile |
                                compiler::ImportRecordFlags::kContainsUniqueKey;
                            additional_files.insert(additional_files.end(),
                                other_file.input_file.additional_files.begin(),
                                other_file.input_file.additional_files.end());
                        } else if (std::get_if<std::shared_ptr<graph::CSSRepr>>(&other_file.input_file.repr) != nullptr) {
                            additional_files.insert(additional_files.end(),
                                other_file.input_file.additional_files.begin(),
                                other_file.input_file.additional_files.end());
                        }
                    } else if (record.copy_source_index.IsValid()) {
                        auto& other_file = graph.files[record.copy_source_index.GetIndex()];
                        if (auto* copy_ptr = std::get_if<std::shared_ptr<graph::CopyRepr>>(&other_file.input_file.repr)) {
                            record.path.text = (*copy_ptr)->url_for_code;
                            record.path.namespace_ = "";
                            record.copy_source_index = compiler::Index32{};
                            record.flags = record.flags |
                                compiler::ImportRecordFlags::kShouldNotBeExternalInMetafile |
                                compiler::ImportRecordFlags::kContainsUniqueKey;
                            additional_files.insert(additional_files.end(),
                                other_file.input_file.additional_files.begin(),
                                other_file.input_file.additional_files.end());
                        }
                    }
                }
                for (auto& [ref, composes_ptr] : repr.ast.composes) {
                    if (!composes_ptr) continue;
                    for (auto& name : composes_ptr->imported_names) {
                        auto& record = repr.ast.import_records[name.import_record_index];
                        if (!record.source_index.IsValid()) continue;
                        auto& other_file = graph.files[record.source_index.GetIndex()];
                        auto* other_css_ptr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&other_file.input_file.repr);
                        if (!other_css_ptr) continue;
                        auto& other_repr = **other_css_ptr;

                        auto local_it = other_repr.ast.local_scope.find(name.alias);
                        if (local_it != other_repr.ast.local_scope.end()) continue;

                        auto global_it = other_repr.ast.global_scope.find(name.alias);
                        if (global_it != other_repr.ast.global_scope.end()) {
std::string hint;
                        if (other_file.input_file.loader == config::Loader::kCSS) {
                            hint = guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_UseLocalCSSLoaderHint,
                                other_file.input_file.source.pretty_paths.Select(options->LogPathStyle));
                        } else {
                            hint = guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_UseLocalSelectorHint, name.alias);
                        }
                        log.AddErrorWithNotes(&file.LineColumnTracker(),
                                css::RangeOfIdentifier(file.input_file.source, name.alias_loc),
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_CannotUseGlobalNameWithComposes, name.alias),
                                {
                                    other_file.LineColumnTracker().MakeMsgData(
                                        css::RangeOfIdentifier(other_file.input_file.source, global_it->second.loc),
                                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_GlobalNameDefinedHereNote, name.alias)),
                                    {.text = hint},
                                });
                        } else {
                            log.AddError(&file.LineColumnTracker(),
                                css::RangeOfIdentifier(file.input_file.source, name.alias_loc),
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_NameNeverAppearsIn, name.alias,
                                    other_file.input_file.source.pretty_paths.Select(options->LogPathStyle)));
                        }
                    }
                }

                ValidateComposesFromProperties(file, repr);
            } else if (auto* js_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr)) {
                auto& repr = **js_ptr;
                for (auto& record : repr.ast.import_records) {
                    if (!record.source_index.IsValid()) {
                        if (record.copy_source_index.IsValid()) {
                            auto& other_file = graph.files[record.copy_source_index.GetIndex()];
                            if (auto* copy_ptr = std::get_if<std::shared_ptr<graph::CopyRepr>>(&other_file.input_file.repr)) {
                                record.path.text = (*copy_ptr)->url_for_code;
                                record.path.namespace_ = "";
                                record.copy_source_index = compiler::Index32{};
                                record.flags = record.flags |
                                    compiler::ImportRecordFlags::kShouldNotBeExternalInMetafile |
                                    compiler::ImportRecordFlags::kContainsUniqueKey;
                                additional_files.insert(additional_files.end(),
                                    other_file.input_file.additional_files.begin(),
                                    other_file.input_file.additional_files.end());
                            }
                        }
                        continue;
                    }

                    auto& other_file = graph.files[record.source_index.GetIndex()];
                    auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(other_file.input_file.repr);

                    switch (record.kind) {
                    case compiler::ImportKind::kStmt:
                        if ((compiler::Has(record.flags, compiler::ImportRecordFlags::kContainsImportStar) ||
                             compiler::Has(record.flags, compiler::ImportRecordFlags::kContainsDefaultAlias)) &&
                            other_repr.ast.exports_kind == javascript::ExportsKind::kNone && !other_repr.ast.has_lazy_export) {
                            other_repr.meta.wrap = graph::WrapKind::kCJS;
                            other_repr.ast.exports_kind = javascript::ExportsKind::kCommonJS;
                        }
                        break;

                    case compiler::ImportKind::kRequire:
                        if (other_repr.ast.exports_kind == javascript::ExportsKind::kESM) {
                            other_repr.meta.wrap = graph::WrapKind::kESM;
                        } else {
                            other_repr.meta.wrap = graph::WrapKind::kCJS;
                            other_repr.ast.exports_kind = javascript::ExportsKind::kCommonJS;
                        }
                        break;

                    case compiler::ImportKind::kDynamic:
                        if (!options->CodeSplitting) {
                            if (other_repr.ast.exports_kind == javascript::ExportsKind::kESM) {
                                other_repr.meta.wrap = graph::WrapKind::kESM;
                            } else {
                                other_repr.meta.wrap = graph::WrapKind::kCJS;
                                other_repr.ast.exports_kind = javascript::ExportsKind::kCommonJS;
                            }
                        }
                        break;

                    default:
                        break;
                    }
                }

                if (repr.ast.exports_kind == javascript::ExportsKind::kCommonJS &&
                    (!file.IsEntryPoint() ||
                     options->OutputFormat == config::Format::kIIFE ||
                     options->OutputFormat == config::Format::kESModule)) {
                    repr.meta.wrap = graph::WrapKind::kCJS;
                }
            }
        }
        timer->End("Step 1");

        timer->Begin("Step 2");
        for (auto source_index : graph.reachable_files) {
            if (auto* js_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&graph.files[source_index].input_file.repr)) {
                auto& repr = **js_ptr;
                if (repr.meta.wrap != graph::WrapKind::kNone) {
                    RecursivelyWrapDependencies(source_index);
                }
                if (!repr.ast.export_star_import_records.empty()) {
                    std::unordered_set<uint32_t> visited;
                    HasDynamicExportsDueToExportStar(source_index, visited);
                }
                for (auto& record : repr.ast.import_records) {
                    if (record.source_index.IsValid()) {
                        auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                            graph.files[record.source_index.GetIndex()].input_file.repr);
                        if (other_repr.ast.exports_kind == javascript::ExportsKind::kCommonJS) {
                            RecursivelyWrapDependencies(record.source_index.GetIndex());
                        }
                    }
                }
            }
        }
        timer->End("Step 2");

        timer->Begin("Step 3");
        std::vector<uint32_t> export_star_stack;
        export_star_stack.reserve(32);
        for (auto source_index : graph.reachable_files) {
            if (auto* js_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&graph.files[source_index].input_file.repr)) {
                auto& repr = **js_ptr;
                if (repr.ast.has_lazy_export) {
                    GenerateCodeForLazyExport(source_index);
                }
                if (!repr.ast.export_star_import_records.empty()) {
                    AddExportsForExportStar(repr.meta.resolved_exports, source_index, export_star_stack);
                }
                repr.meta.resolved_export_star = graph::ExportData{
                    .ref = repr.ast.exports_ref,
                    .source_index = source_index,
                };
            }
        }
        timer->End("Step 3");

        timer->Begin("Step 4");
        for (auto source_index : graph.reachable_files) {
            auto& file = graph.files[source_index];
            if (auto* js_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr)) {
                auto& repr = **js_ptr;
                if (!repr.ast.named_imports.empty()) {
                    MatchImportsWithExportsForFile(source_index);
                }

                if (file.IsEntryPoint() && repr.ast.exports_kind == javascript::ExportsKind::kCommonJS &&
                    repr.meta.wrap == graph::WrapKind::kNone &&
                    (options->OutputFormat == config::Format::kPreserve || options->OutputFormat == config::Format::kCommonJS)) {
                    auto exports_ref = compiler::FollowSymbols(graph.symbols, repr.ast.exports_ref);
                    auto module_ref = compiler::FollowSymbols(graph.symbols, repr.ast.module_ref);
                    graph.symbols.Get(exports_ref)->kind = compiler::SymbolKind::kUnbound;
                    graph.symbols.Get(module_ref)->kind = compiler::SymbolKind::kUnbound;
                } else if (repr.meta.force_include_exports_for_entry_point || repr.ast.exports_kind != javascript::ExportsKind::kCommonJS) {
                    repr.meta.needs_exports_variable = true;
                }

                CreateWrapperForFile(source_index);
            }
        }
        timer->End("Step 4");

        timer->Begin("Step 5");
        {
            std::vector<std::thread> step5_threads;
            std::vector<std::exception_ptr> step5_exceptions(graph.reachable_files.size());
            step5_threads.reserve(graph.reachable_files.size());
            size_t step5_index = 0;
            for (auto source_index : graph.reachable_files) {
                size_t thread_index = step5_index++;
                if (std::get_if<std::shared_ptr<graph::JSRepr>>(&graph.files[source_index].input_file.repr) != nullptr) {
                    step5_threads.emplace_back([this, source_index, thread_index, &step5_exceptions]() {
                        try {
                        auto& repr = *std::get<std::shared_ptr<graph::JSRepr>>(graph.files[source_index].input_file.repr);

                        std::vector<std::string> aliases;
                        for (auto& [alias, export_data] : repr.meta.resolved_exports) {
                            auto& other_file = graph.files[export_data.source_index].input_file;
                            auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(other_file.repr);

                            if (!export_data.potentially_ambiguous_export_star_refs.empty()) {
                                auto main_ref = export_data.ref;
                                auto main_loc = export_data.name_loc;
                                if (auto it = other_repr.meta.imports_to_bind.find(export_data.ref); it != other_repr.meta.imports_to_bind.end()) {
                                    main_ref = it->second.ref;
                                    main_loc = it->second.name_loc;
                                }
                                bool skip = false;
                                for (auto& amb : export_data.potentially_ambiguous_export_star_refs) {
                                    auto& amb_file = graph.files[amb.source_index].input_file;
                                    auto& amb_repr = *std::get<std::shared_ptr<graph::JSRepr>>(amb_file.repr);
                                    auto amb_ref = amb.ref;
                                    auto amb_loc = amb.name_loc;
                                    if (auto it = amb_repr.meta.imports_to_bind.find(amb.ref); it != amb_repr.meta.imports_to_bind.end()) {
                                        amb_ref = it->second.ref;
                                        amb_loc = it->second.name_loc;
                                    }
                                    if (main_ref != amb_ref) {
                                        log.AddIDWithNotes(
                                            logger::MsgID::kBundler_AmbiguousReexport, logger::MsgKind::kDebug, nullptr, logger::Range{},
                                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_AmbiguousReexport, alias,
                                                graph.files[source_index].input_file.source.pretty_paths.Select(options->LogPathStyle)),
                                            {
                                                graph.files[export_data.source_index].LineColumnTracker().MakeMsgData(
                                                    javascript::RangeOfIdentifier(other_file.source, main_loc),
                                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_OneDefinitionFromNote, alias,
                                                        other_file.source.pretty_paths.Select(options->LogPathStyle))),
                                                graph.files[amb.source_index].LineColumnTracker().MakeMsgData(
                                                    javascript::RangeOfIdentifier(amb_file.source, amb_loc),
                                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kLinker_AnotherDefinitionFromNote, alias,
                                                        amb_file.source.pretty_paths.Select(options->LogPathStyle))),
                                            });
                                        skip = true;
                                        break;
                                    }
                                }
                                if (skip) continue;
                            }

                            if (other_repr.meta.is_probably_typescript_type.count(export_data.ref)) continue;

                            if (options->OutputFormat == config::Format::kESModule &&
                                compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kArbitraryModuleNamespaceNames) &&
                                graph.files[source_index].IsEntryPoint()) {
                                MaybeForbidArbitraryModuleNamespaceIdentifier(
                                    "export", export_data.source_index, export_data.name_loc, alias);
                            }

                            aliases.push_back(alias);
                        }
                        std::sort(aliases.begin(), aliases.end());
                        repr.meta.sorted_and_filtered_export_aliases = std::move(aliases);

                        CreateExportsForFile(source_index);

                        std::unordered_map<uint32_t, uint32_t> local_dependencies;
                        for (size_t part_index = 0; part_index < repr.ast.parts.size(); part_index++) {
                            auto& part = repr.ast.parts[part_index];

                            for (auto& [ref, property_uses] : part.import_symbol_property_uses) {
                                auto use_it = part.symbol_uses.find(ref);
                                javascript::SymbolUse use = (use_it != part.symbol_uses.end()) ? use_it->second : javascript::SymbolUse{};

                                if (auto it = repr.meta.imports_to_bind.find(ref); it != repr.meta.imports_to_bind.end()) {
                                    auto* symbol = graph.symbols.Get(it->second.ref);
                                    if (symbol->kind == compiler::SymbolKind::kTSEnum) {
                                        if (auto enum_it = graph.ts_enums.find(it->second.ref); enum_it != graph.ts_enums.end()) {
                                            bool found_non_inlined = false;
                                            for (auto& [name, prop_use] : property_uses) {
                                                if (!enum_it->second.count(name)) {
                                                    found_non_inlined = true;
                                                    use.count_estimate += prop_use.count_estimate;
                                                }
                                            }
                                            if (found_non_inlined) {
                                                part.symbol_uses[ref] = use;
                                            }
                                        }
                                        continue;
                                    }
                                }
                                for (auto& [_, prop_use] : property_uses) {
                                    use.count_estimate += prop_use.count_estimate;
                                }
                                part.symbol_uses[ref] = use;
                            }

                            for (auto& [ref, call_use] : part.symbol_call_uses) {
                                auto use_it = part.symbol_uses.find(ref);
                                javascript::SymbolUse use = (use_it != part.symbol_uses.end()) ? use_it->second : javascript::SymbolUse{};

                                auto* symbol = graph.symbols.Get(ref);
                                if (symbol->kind == compiler::SymbolKind::kImport) {
                                    if (auto it = repr.meta.imports_to_bind.find(ref); it != repr.meta.imports_to_bind.end()) {
                                        symbol = graph.symbols.Get(it->second.ref);
                                    }
                                }
                                auto flags = symbol->flags;

                                if ((flags & (compiler::SymbolFlags::kIsEmptyFunction | compiler::SymbolFlags::kCouldPotentiallyBeMutated)) == compiler::SymbolFlags::kIsEmptyFunction) {
                                    continue;
                                } else if ((flags & (compiler::SymbolFlags::kIsIdentityFunction | compiler::SymbolFlags::kCouldPotentiallyBeMutated)) == compiler::SymbolFlags::kIsIdentityFunction) {
                                    auto adjusted = call_use.call_count_estimate - call_use.single_arg_non_spread_call_count_estimate;
                                    if (adjusted == 0) continue;
                                    call_use.call_count_estimate = adjusted;
                                }

                                use.count_estimate += call_use.call_count_estimate;
                                part.symbol_uses[ref] = use;
                            }

                            for (auto& [ref, _] : part.symbol_uses) {
                                if (!graph.const_values.empty()) {
                                    if (auto it = repr.meta.imports_to_bind.find(ref); it != repr.meta.imports_to_bind.end()) {
                                        if (graph.const_values.count(it->second.ref)) {
                                            continue;
                                        }
                                    }
                                }
                                for (auto other_part_index : repr.TopLevelSymbolToParts(ref)) {
                                    auto dep_it = local_dependencies.find(other_part_index);
                                    if (dep_it == local_dependencies.end() || dep_it->second != static_cast<uint32_t>(part_index)) {
                                        local_dependencies[other_part_index] = static_cast<uint32_t>(part_index);
                                        part.dependencies.push_back(javascript::Dependency{
                                            .source_index = source_index,
                                            .part_index = other_part_index,
                                        });
                                    }
                                }

                                if (auto named_import_it = repr.ast.named_imports.find(ref);
                                    named_import_it != repr.ast.named_imports.end()) {
                                    named_import_it->second.local_parts_with_uses.push_back(static_cast<uint32_t>(part_index));
                                }
                            }
                        }
                        } catch (...) {
                            step5_exceptions[thread_index] = std::current_exception();
                        }
                    });
                }
            }
            for (auto& t : step5_threads) t.join();
            for (auto& e : step5_exceptions) {
                if (e) std::rethrow_exception(e);
            }
        }
        timer->End("Step 5");

        timer->Begin("Step 6");
        for (auto source_index : graph.reachable_files) {
            auto& file = graph.files[source_index];
            if (auto* js_ptr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.input_file.repr)) {
                auto& repr = **js_ptr;

                if (file.IsEntryPoint() && (options->OutputFormat == config::Format::kESModule ||
                    options->OutputFormat == config::Format::kSystem)) {
                    std::vector<compiler::Ref> copies(repr.meta.sorted_and_filtered_export_aliases.size());
                    for (size_t i = 0; i < repr.meta.sorted_and_filtered_export_aliases.size(); i++) {
                        copies[i] = graph.GenerateNewSymbol(source_index, compiler::SymbolKind::kOther,
                            "export_" + repr.meta.sorted_and_filtered_export_aliases[i]);
                    }
                    repr.meta.cjs_export_copies = std::move(copies);
                }

                if (repr.meta.wrap == graph::WrapKind::kESM) {
                    graph.symbols.Get(repr.ast.wrapper_ref)->original_name =
                        "init_" + file.input_file.source.identifier_name;
                }

                if (repr.meta.wrap != graph::WrapKind::kCJS && repr.ast.exports_kind != javascript::ExportsKind::kCommonJS) {
                    auto name = file.input_file.source.identifier_name;
                    graph.symbols.Get(repr.ast.exports_ref)->original_name = name + "_exports";
                    graph.symbols.Get(repr.ast.module_ref)->original_name = name + "_module";
                }

                if (repr.meta.needs_export_symbol_from_runtime) {
                    auto& runtime_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                        graph.files[javascript::kSourceIndex].input_file.repr);
                    auto export_ref = runtime_repr.ast.module_scope->members["__export"].ref;
                    graph.GenerateSymbolImportAndUse(source_index, javascript::kNSExportPartIndex, export_ref, 1, javascript::kSourceIndex);
                }

                for (auto& [import_ref, import_data] : repr.meta.imports_to_bind) {
                    auto& resolved_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                        graph.files[import_data.source_index].input_file.repr);
                    auto parts = resolved_repr.TopLevelSymbolToParts(import_data.ref);

                    auto named_import_it = repr.ast.named_imports.find(import_ref);
                    if (named_import_it != repr.ast.named_imports.end()) {
                        for (auto part_index : named_import_it->second.local_parts_with_uses) {
                            auto& part = repr.ast.parts[part_index];
                            for (auto resolved_part : parts) {
                                part.dependencies.push_back(javascript::Dependency{
                                    .source_index = import_data.source_index,
                                    .part_index = resolved_part,
                                });
                            }
                            part.dependencies.insert(part.dependencies.end(),
                                import_data.re_exports.begin(), import_data.re_exports.end());
                        }
                    }

                    compiler::MergeSymbols(graph.symbols, import_ref, import_data.ref);
                }

                if (file.IsEntryPoint()) {
                    std::vector<javascript::Dependency> dependencies;
                    for (auto& alias : repr.meta.sorted_and_filtered_export_aliases) {
                        auto export_it = repr.meta.resolved_exports.find(alias);
                        if (export_it == repr.meta.resolved_exports.end()) continue;
                        auto target_source_index = export_it->second.source_index;
                        auto target_ref = export_it->second.ref;

                        auto& target_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                            graph.files[target_source_index].input_file.repr);
                        if (auto it = target_repr.meta.imports_to_bind.find(target_ref); it != target_repr.meta.imports_to_bind.end()) {
                            target_source_index = it->second.source_index;
                            target_ref = it->second.ref;
                            dependencies.insert(dependencies.end(),
                                it->second.re_exports.begin(), it->second.re_exports.end());
                        }

                        auto& final_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                            graph.files[target_source_index].input_file.repr);
                        for (auto part_index : final_repr.TopLevelSymbolToParts(target_ref)) {
                            dependencies.push_back({.source_index = target_source_index, .part_index = part_index});
                        }
                    }

                    if (repr.meta.force_include_exports_for_entry_point) {
                        dependencies.push_back({.source_index = source_index, .part_index = javascript::kNSExportPartIndex});
                    }
                    if (repr.meta.wrap != graph::WrapKind::kNone) {
                        dependencies.push_back({.source_index = source_index, .part_index = repr.meta.wrapper_part_index.GetIndex()});
                    }

                    uint32_t entry_part_index = graph.AddPartToFile(source_index, javascript::Part{
                        .dependencies = std::move(dependencies),
                        .can_be_removed_if_unused = false,
                    });
                    repr.meta.entry_point_part_index = compiler::Index32::Make(entry_part_index);

                    if (repr.meta.force_include_exports_for_entry_point) {
                        graph.GenerateRuntimeSymbolImportAndUse(source_index, entry_part_index, "__toCommonJS", 1);
                    }
                }

                for (uint32_t part_index = 0; part_index < repr.ast.parts.size(); part_index++) {
                    auto& part = repr.ast.parts[part_index];
                    uint32_t to_esm_uses = 0;
                    uint32_t to_commonjs_uses = 0;
                    uint32_t runtime_require_uses = 0;

                    for (auto import_record_index : part.import_record_indices) {
                        auto& record = repr.ast.import_records[import_record_index];

                        if (!record.source_index.IsValid() || IsExternalDynamicImport(record, source_index)) {
                            if (record.kind == compiler::ImportKind::kRequire ||
                                !config::FormatKeepESMImportExportSyntax(options->OutputFormat) ||
                                (record.kind == compiler::ImportKind::kDynamic &&
                                 compat::Has(options->UnsupportedJSFeatures, compat::JSFeature::kDynamicImport))) {
                                if (config::ShouldCallRuntimeRequire(options->BuildMode, options->OutputFormat)) {
                                    record.flags = record.flags | compiler::ImportRecordFlags::kCallRuntimeRequire;
                                    runtime_require_uses++;
                                }
                                if (record.kind != compiler::ImportKind::kRequire &&
                                    (record.kind != compiler::ImportKind::kStmt ||
                                     compiler::Has(record.flags, compiler::ImportRecordFlags::kContainsImportStar) ||
                                     compiler::Has(record.flags, compiler::ImportRecordFlags::kContainsDefaultAlias) ||
                                     compiler::Has(record.flags, compiler::ImportRecordFlags::kContainsESModuleAlias))) {
                                    record.flags = record.flags | compiler::ImportRecordFlags::kWrapWithToESM;
                                    to_esm_uses++;
                                }
                            }
                            continue;
                        }

                        uint32_t other_source_index = record.source_index.GetIndex();
                        auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                            graph.files[other_source_index].input_file.repr);

                        if (other_repr.meta.wrap != graph::WrapKind::kNone) {
                            graph.GenerateSymbolImportAndUse(source_index, part_index, other_repr.ast.wrapper_ref, 1, other_source_index);
                            if (record.kind != compiler::ImportKind::kRequire && other_repr.ast.exports_kind == javascript::ExportsKind::kCommonJS) {
                                record.flags = record.flags | compiler::ImportRecordFlags::kWrapWithToESM;
                                to_esm_uses++;
                            }
                            if (other_repr.meta.wrap == graph::WrapKind::kESM && record.kind != compiler::ImportKind::kStmt) {
                                graph.GenerateSymbolImportAndUse(source_index, part_index, other_repr.ast.exports_ref, 1, other_source_index);
                                if (record.kind == compiler::ImportKind::kRequire) {
                                    record.flags = record.flags | compiler::ImportRecordFlags::kWrapWithToCJS;
                                    to_commonjs_uses++;
                                }
                            }
                        } else if (record.kind == compiler::ImportKind::kStmt &&
                                   other_repr.ast.exports_kind == javascript::ExportsKind::kESMWithDynamicFallback) {
                            graph.GenerateSymbolImportAndUse(source_index, part_index, other_repr.ast.exports_ref, 1, other_source_index);
                        }
                    }

                    graph.GenerateRuntimeSymbolImportAndUse(source_index, part_index, "__toESM", to_esm_uses);
                    graph.GenerateRuntimeSymbolImportAndUse(source_index, part_index, "__toCommonJS", to_commonjs_uses);
                    graph.GenerateRuntimeSymbolImportAndUse(source_index, part_index, "__require", runtime_require_uses);

                    uint32_t re_export_uses = 0;
                    for (auto import_record_index : repr.ast.export_star_import_records) {
                        auto& record = repr.ast.import_records[import_record_index];
                        bool happens_at_runtime = !record.source_index.IsValid() &&
                            (!file.IsEntryPoint() || !config::FormatKeepESMImportExportSyntax(options->OutputFormat));
                        if (record.source_index.IsValid()) {
                            uint32_t other_si = record.source_index.GetIndex();
                            auto& other_repr = *std::get<std::shared_ptr<graph::JSRepr>>(
                                graph.files[other_si].input_file.repr);
                            if (other_si != source_index && javascript::IsDynamic(other_repr.ast.exports_kind)) {
                                happens_at_runtime = true;
                            }
                            if (other_repr.ast.exports_kind == javascript::ExportsKind::kESMWithDynamicFallback) {
                                graph.GenerateSymbolImportAndUse(source_index, part_index, other_repr.ast.exports_ref, 1, other_si);
                            }
                        }
                        if (happens_at_runtime) {
                            graph.GenerateSymbolImportAndUse(source_index, part_index, repr.ast.exports_ref, 1, source_index);
                            record.flags = record.flags | compiler::ImportRecordFlags::kCallsRunTimeReExportFn;
                            repr.ast.uses_exports_ref = true;
                            re_export_uses++;
                        }
                    }
                    graph.GenerateRuntimeSymbolImportAndUse(source_index, part_index, "__reExport", re_export_uses);
                }
            }
        }
        timer->End("Step 6");

        timer->End("Scan imports and exports");
    }

}
