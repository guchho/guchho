#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>


#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/config.hpp"
#include "guchho/compiler.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_lexer.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/javascript/js_helpers.hpp"


namespace guchho::javascript {

    // convertSymbolUseToCall
    // ----------------------
    // Reclassifies a previously recorded symbol use as a function call instead
    // of a plain reference.  This matters for tree-shaking: a bare reference
    // to a function may prevent it from being removed, but when the reference
    // is known to be a single non-spread call, the bundler can safely unwrap
    // the call and eliminate the function entirely if the result is unused.
    //
    // The function decrements the normal use count and, if it reaches zero,
    // erases the entry from symbolUses.  It then increments the call count
    // (and, when applicable, the single-argument non-spread call count) in
    // symbolCallUses.
    //
    // Input:  ref = some function reference, isSingleNonSpreadArgCall = true
    // Output: symbolUses[ref].count_estimate decremented by one;
    //         symbolCallUses[ref].call_count_estimate incremented by one;
    //         symbolCallUses[ref].single_arg_non_spread_call_count_estimate++
    void Parser::convertSymbolUseToCall(compiler::Ref ref, bool isSingleNonSpreadArgCall) {
        SymbolUse use = this->symbolUses[ref];
        use.count_estimate--;
        if (use.count_estimate == 0) {
            this->symbolUses.erase(ref);
        } else {
            this->symbolUses[ref] = use;
        }

        SymbolCallUse callUse = this->symbolCallUses[ref];
        callUse.call_count_estimate++;
        if (isSingleNonSpreadArgCall) {
            callUse.single_arg_non_spread_call_count_estimate++;
        }
        this->symbolCallUses[ref] = callUse;
    }

    // globPatternFromExpr
    // -------------------
    // Attempts to interpret an AST expression as a glob pattern and returns a
    // sequence of glob segments together with the approximate source range that
    // covers the entire expression.  This is used to support dynamic import()
    // calls whose argument is a string template or concatenation expression
    // such as:
    //
    //   import(`./dir/${name}.js`)
    //   import('./dir/' + name + '.js')
    //
    // Three expression forms are recognised:
    //
    //   1. Plain string literal — becomes a single text segment.
    //
    //   2. Untagged template literal — head text and each substitution become
    //      alternating text/wildcard segments.  If a substitution is itself a
    //      string or template it is recursively expanded; otherwise it becomes
    //      a wildcard.
    //
    //   3. Binary "+" concatenation — left and right operands are recursively
    //      converted and their segments are concatenated.  If the right side
    //      cannot be converted, a wildcard is appended instead.
    //
    // When the right side of a "+" expression is an identifier or call, the
    // source range is extended to cover it so that later diagnostics can
    // pinpoint the whole expression.
    //
    // Returns an empty vector when the expression does not match any of the
    // above forms (e.g. a variable reference or a function call that is not a
    // template tag).
    //
    // Input:  expr = EString{u"./dir/file.js"}
    // Output: {{text="./dir/file.js"}}, range covering the literal
    //
    // Input:  expr = ETemplate with head "./dir/" and part `name`
    // Output: {{text="./dir/"}, {wildcard}, {text=""}}, range covering template
    std::pair<std::vector<globPart>, logger::Range> Parser::globPatternFromExpr(Expr expr) {
        if (const EString *e2 = Get<EString>(expr.data); e2 != nullptr) {
            return {{globPart{/*text=*/helpers::UTF16ToString(e2->value), /*isWildcard=*/false}}, this->source.RangeOfString(expr.loc)};
        }

        if (const ETemplate *e2 = Get<ETemplate>(expr.data); e2 != nullptr) {
            if (IsNil(e2->tag_or_nil.data)) {
                std::vector<globPart> pattern;
                pattern.reserve(1 + 2 * e2->parts.size());
                pattern.push_back(globPart{/*text=*/helpers::UTF16ToString(e2->head_cooked), /*isWildcard=*/false});

                for (const TemplatePart &part : e2->parts) {
                    std::vector<globPart> partPattern;
                    std::tie(partPattern, std::ignore) = this->globPatternFromExpr(part.value);
                    if (!partPattern.empty()) {
                        pattern.insert(pattern.end(), partPattern.begin(), partPattern.end());
                    } else {
                        pattern.push_back(globPart{/*text=*/"", /*isWildcard=*/true});
                    }
                    pattern.push_back(globPart{/*text=*/helpers::UTF16ToString(part.tail_cooked), /*isWildcard=*/false});
                }

                if (e2->parts.empty()) {
                    return {pattern, this->source.RangeOfString(expr.loc)};
                }

                std::string text = this->source.contents;
                logger::Range templateRange = logger::Range{e2->head_loc};

                for (int32_t i = e2->parts[e2->parts.size() - 1].tail_loc.start; i < static_cast<int32_t>(text.size()); i++) {
                    uint8_t c = static_cast<uint8_t>(text[static_cast<size_t>(i)]);
                    if (c == '`') {
                        templateRange.len = i + 1 - templateRange.loc.start;
                        break;
                    } else if (c == '\\') {
                        i += 1;
                    }
                }

                return {pattern, templateRange};
            }
        }

        if (const EBinary *e2 = Get<EBinary>(expr.data); e2 != nullptr) {
            if (e2->op == OpCode::kBinOpAdd) {
                std::vector<globPart> pattern;
                logger::Range leftRange;
                std::tie(pattern, leftRange) = this->globPatternFromExpr(e2->left);
                if (!pattern.empty()) {
                    std::vector<globPart> rightPattern;
                    logger::Range rightRange;
                    std::tie(rightPattern, rightRange) = this->globPatternFromExpr(e2->right);
                    if (!rightPattern.empty()) {
                        pattern.insert(pattern.end(), rightPattern.begin(), rightPattern.end());
                        leftRange.len = rightRange.End() - leftRange.loc.start;
                        return {pattern, leftRange};
                    }

                    pattern.push_back(globPart{/*text=*/"", /*isWildcard=*/true});

                    if (const EIdentifier *rightId = Get<EIdentifier>(e2->right.data); rightId != nullptr) {
                        leftRange.len = RangeOfIdentifier(this->source, e2->right.loc).End() - leftRange.loc.start;
                    } else if (const ECall *rightCall = Get<ECall>(e2->right.data); rightCall != nullptr) {
                        if (rightCall->close_paren_loc.start > 0) {
                            leftRange.len = rightCall->close_paren_loc.start + 1 - leftRange.loc.start;
                        }
                    }

                    return {pattern, leftRange};
                }
            }
        }

        return {std::vector<globPart>{}, logger::Range{}};
    }

    // handleGlobPattern
    // -----------------
    // Takes a glob pattern extracted from an expression and either reuses an
    // existing glob import or creates a new one.  The generated import is
    // returned as an ECall node that calls the glob helper function with the
    // original expression as its argument.
    //
    // The glob segments are normalised into a sequence of prefix/wildcard
    // pairs.  A wildcard that follows a "/" becomes a directory-wildcard
    // (matching anything including "/"), while other wildcards only match
    // within a single path segment.  Adjacent text segments are merged into
    // a single prefix.
    //
    // Duplicate detection: before creating a new glob import the function
    // checks whether an identical pattern (same parts, kind, phase, and
    // import assertions) already exists.  If so, the existing symbol is
    // reused.  This prevents redundant runtime helpers.
    //
    // Only relative paths (starting with "./" or "../") are handled.
    // Absolute or bare-specifier patterns are silently ignored and the
    // function returns an empty expression.
    //
    // Input:  expr = ETemplate for `./dir/${name}.js`, prefix = "glob$"
    // Output: ECall{target=EIdentifier{ref=<new glob symbol>}, args=[expr]}
    //
    // Input:  expr = EString{u"./constant.js"}
    // Output: Expr{}  (no wildcard, treated as a plain string)
    Expr Parser::handleGlobPattern(Expr expr, compiler::ImportKind kind, compiler::ImportPhase phase, const std::string &prefix, const std::shared_ptr<compiler::ImportAssertOrWith> &assertOrWith) {
        std::vector<globPart> pattern;
        logger::Range approximateRange;
        std::tie(pattern, approximateRange) = this->globPatternFromExpr(expr);
        if (pattern.empty()) {
            return Expr{};
        }

        helpers::GlobPart last;
        std::vector<helpers::GlobPart> parts;

        for (const globPart &part : pattern) {
            if (part.isWildcard) {
                if (last.wildcard == helpers::GlobWildcard::kNone) {
                    if (!(last.prefix.size() > 0 && last.prefix[last.prefix.size() - 1] == '/')) {
                        last.wildcard = helpers::GlobWildcard::kAllExceptSlash;
                    } else {
                        last.wildcard = helpers::GlobWildcard::kAllIncludingSlash;
                        parts.push_back(last);
                        helpers::GlobPart newLast;
                        newLast.prefix = "/";
                        newLast.wildcard = helpers::GlobWildcard::kAllExceptSlash;
                        last = newLast;
                    }
                }
            } else if (part.text != "") {
                if (last.wildcard != helpers::GlobWildcard::kNone) {
                    parts.push_back(last);
                    last = helpers::GlobPart{};
                }
                last.prefix += part.text;
            }
        }

        parts.push_back(last);

        if (parts.size() == 1 && parts[0].wildcard == helpers::GlobWildcard::kNone) {
            return Expr{};
        }

        {
            std::string firstPrefix = parts[0].prefix;
            if (!(firstPrefix.rfind("./", 0) == 0) && !(firstPrefix.rfind("../", 0) == 0)) {
                return Expr{};
            }
        }

        compiler::Ref ref = compiler::kInvalidRef;

        for (const globPatternImport &globPattern : this->globPatternImports) {
            if (globPattern.kind != kind || globPattern.phase != phase) {
                continue;
            }

            if (globPattern.parts.size() != parts.size()) {
                continue;
            }
            bool foundMismatch = false;
            for (size_t i = 0; i < parts.size(); i++) {
                if (globPattern.parts[i].prefix != parts[i].prefix || globPattern.parts[i].wildcard != parts[i].wildcard) {
                    foundMismatch = true;
                    break;
                }
            }
            if (foundMismatch) {
                continue;
            }

            if (assertOrWith == nullptr) {
                if (globPattern.assertOrWith != nullptr) {
                    continue;
                }
            } else {
                if (globPattern.assertOrWith == nullptr) {
                    continue;
                }
                if (assertOrWith->keyword != globPattern.assertOrWith->keyword) {
                    continue;
                }
                const std::vector<compiler::AssertOrWithEntry> &a = assertOrWith->entries;
                const std::vector<compiler::AssertOrWithEntry> &b = globPattern.assertOrWith->entries;
                if (a.size() != b.size()) {
                    continue;
                }
                foundMismatch = false;
                for (size_t i = 0; i < a.size(); i++) {
                    const compiler::AssertOrWithEntry &ai = a[i];
                    const compiler::AssertOrWithEntry &bi = b[i];
                    if (!helpers::UTF16EqualsUTF16(ai.key, bi.key) || !helpers::UTF16EqualsUTF16(ai.value, bi.value)) {
                        foundMismatch = true;
                        break;
                    }
                }
                if (foundMismatch) {
                    continue;
                }
            }

            ref = globPattern.ref;
            break;
        }

        if (ref == compiler::kInvalidRef && prefix != "") {
            std::string name = prefix;

            for (const helpers::GlobPart &part : parts) {
                bool gap = true;
                for (size_t i = 0; i < part.prefix.size(); i++) {
                    uint8_t c = static_cast<uint8_t>(part.prefix[i]);
                    if (!IsIdentifierContinue(c)) {
                        gap = true;
                    } else {
                        if (gap) {
                            name.push_back('_');
                            gap = false;
                        }
                        name.push_back(static_cast<char>(c));
                    }
                }
            }

            ref = newSymbol(compiler::SymbolKind::kOther, name);
            this->moduleScope->generated.push_back(ref);

            this->globPatternImports.push_back(globPatternImport{/*assertOrWith=*/assertOrWith,
                /*parts=*/parts,
                /*name=*/name,
                /*approximateRange=*/approximateRange,
                /*ref=*/ref,
                /*kind=*/kind,
                /*phase=*/phase,
            });
        }

        recordUsage(ref);
        ECall *call = new ECall();
        EIdentifier *identifier = new EIdentifier();
        identifier->ref = ref;
        call->target = Expr{std::shared_ptr<EIdentifier>(identifier), expr.loc};
        call->args = std::vector<Expr>{expr};
        return Expr{std::shared_ptr<ECall>(call), expr.loc};
    }

    // recordExport
    // ------------
    // Registers a named export in the module's export table.  Each export is
    // identified by its alias (the name visible to consumers of the module).
    // If a duplicate alias is detected, a diagnostic error is emitted that
    // points at both the new and the original declaration sites.
    //
    // Input:  loc = source location of the export name
    //         alias = "default"
    //         ref = the binding that is being exported
    // Output: namedExports["default"] = {ref, loc}
    //
    // Input:  loc = second export of "foo"
    // Output: error diagnostic with notes pointing at both declarations
    void Parser::recordExport(logger::Loc loc, const std::string &alias, compiler::Ref ref) {
        if (auto it = this->namedExports.find(alias); it != this->namedExports.end()) {
            this->log.AddErrorWithNotes(&this->tracker, RangeOfIdentifier(this->source, loc),
                logger::FormatMsg(logger::MsgCat::kJS_MultipleExportsSameName, alias),
                { this->tracker.MakeMsgData(RangeOfIdentifier(this->source, it->second.alias_loc),
                    logger::FormatMsg(logger::MsgCat::kJS_MultipleExportsOriginalNote, alias)) });
        } else {
            this->namedExports[alias] = NamedExport{ref, loc};
        }
    }

    // checkForUnusedTSImportEquals
    // ----------------------------
    // Examines a single `import X = require("...")` or `import X = Y.Z` style
    // TypeScript import-equals statement.  If the imported binding has zero
    // uses in the module, the statement is marked for removal and the symbol
    // references are cleaned up so that downstream passes do not see stale
    // data.
    //
    // The function walks through any chain of property accesses (e.g.
    // `X.Y.Z`) to find the root identifier.  Only bare identifier references
    // and ES-module import identifiers qualify; call expressions such as
    // `require("foo")` are intentionally excluded because they may have side
    // effects.
    //
    // When a removable import-equals is found, `result->removedImportEquals`
    // is set to true.  Because import-equals statements can appear in any
    // order, removal of one may expose another as unused.  The caller is
    // expected to iterate until no more removals occur (fixed-point iteration).
    //
    // Input:  s = SLocal for `import X = Y.Z`, X has zero uses
    // Output: true, result->removedImportEquals = true
    //
    // Input:  s = SLocal for `import X = Y.Z`, X is used somewhere
    // Output: false, result->keptImportEquals = true
    bool Parser::checkForUnusedTSImportEquals(SLocal *s, importsExportsScanResult *result) {
        if (s->was_ts_import_equals && !s->is_export) {
            Expr value = s->decls[0].value_or_nil;
            for (;;) {
                if (EDot *dot = Get<EDot>(value.data); dot != nullptr) {
                    value = dot->target;
                } else {
                    break;
                }
            }

            compiler::Ref value_ref = compiler::kInvalidRef;
            if (EIdentifier *v = Get<EIdentifier>(value.data); v != nullptr) {
                value_ref = v->ref;
            } else if (EImportIdentifier *impId = Get<EImportIdentifier>(value.data); impId != nullptr) {
                value_ref = impId->ref;
            }
            if (value_ref != compiler::kInvalidRef) {
                BIdentifier *b = Get<BIdentifier>(s->decls[0].binding.data);
                if (this->symbols[b->ref.inner_index].use_count_estimate == 0) {
                    this->ignoreUsage(value_ref);

                    result->removedImportEquals = true;
                    return true;
                } else {
                    result->keptImportEquals = true;
                }
            }
        }

        return false;
    }

    // notesForAssertTypeJSON
    // ----------------------
    // Builds a pair of diagnostic note messages that explain the two valid
    // ways to import a JSON module that carries a `type: "json"` assertion:
    //
    //   1. Keep the assertion and import only the "default" export.
    //   2. Remove the assertion and use a named import.
    //
    // These notes are attached to diagnostics for non-default JSON imports
    // (e.g. `import { version } from './pkg.json' assert { type: "json" }`)
    // which are disallowed when bundling.
    std::vector<logger::MsgData> Parser::notesForAssertTypeJSON(compiler::ImportRecord *record, const std::string &alias) {
        return {
            this->tracker.MakeMsgData(
                RangeOfImportAssertOrWith(this->source, *compiler::FindAssertOrWithEntry(record->assert_or_with->entries, "type"), KeyOrValue::kKeyAndValueRange),
                "The JSON import assertion is here:"),
            logger::MsgData{nullptr, nullptr,
                "You can either keep the import assertion and only use the \"default\" import, or you can remove the import assertion and use the \"" + alias + "\" import."},
        };
    }

    // ignoreUsage
    // -----------
    // Undoes the effect of a previous recordUsage() call for the given
    // symbol reference.  This is used when an import is determined to be
    // unused (e.g. a TypeScript import-equals) and the reference count
    // must be rolled back so that the symbol does not appear live.
    //
    // The normal use count and the symbolUses map entry are decremented
    // (and erased when they reach zero).  The tsUseCounts entry is NOT
    // rolled back because the TypeScript compiler counts type-only
    // references even when the value is ultimately ignored.
    //
    // This function is a no-op when the current code region is control-flow
    // dead, because recordUsage() already skips dead regions.
    //
    // Input:  ref = some symbol, previously recorded once
    // Output: symbols[ref].use_count_estimate decremented by one
    void Parser::ignoreUsage(compiler::Ref ref) {
        if (!this->isControlFlowDead) {
            this->symbols[ref.inner_index].use_count_estimate--;
            auto it = this->symbolUses.find(ref);
            if (it != this->symbolUses.end()) {
                it->second.count_estimate--;
                if (it->second.count_estimate == 0) {
                    this->symbolUses.erase(it);
                }
            }
        }
    }

    // scanForUnusedTSImportEquals
    // ---------------------------
    // Performs a first pass over the statement list to remove unused TypeScript
    // import-equals statements.  Because import-equals removal can expose
    // further removals (import A = B may reference import B = C which is also
    // unused), the function calls checkForUnusedTSImportEquals repeatedly on
    // each statement until a full pass produces no removals.
    //
    // The result contains the filtered statement list and flags indicating
    // whether any import-equals were removed or kept.
    //
    // Input:  stmts containing `import X = Y` where X is unused
    // Output: result.stmts with the unused import-equals removed
    importsExportsScanResult Parser::scanForUnusedTSImportEquals(std::vector<Stmt> stmts) {
        importsExportsScanResult result;
        size_t stmtsEnd = 0;

        for (Stmt &stmt : stmts) {
            if (SLocal *s = Get<SLocal>(stmt.data); s != nullptr && this->checkForUnusedTSImportEquals(s, &result)) {
                continue;
            }

            stmts[stmtsEnd] = stmt;
            stmtsEnd++;
        }

        stmts.resize(stmtsEnd);
        result.stmts = stmts;
        return result;
    }

    // scanForImportsAndExports
    // ------------------------
    // The main import/export processing pass.  Iterates over every top-level
    // statement and handles each of the following:
    //
    //   - SImport: import statements.  Unused named imports are pruned when
    //     minifySyntax or TypeScript mode is active.  Namespace imports
    //     (`import * as ns`) and default imports are also tracked.  When the
    //     namespace is accessed via property access (e.g. `ns.foo`), the
    //     access is converted into a named import for better diagnostics and
    //     bundling.  JSON import assertions that import non-default names
    //     produce an error in bundle mode.  TypeScript mode may mark the
    //     entire import record as unused when all named imports are unused.
    //
    //   - SFunction / SClass / SLocal with is_export: these are registered
    //     via recordExport().
    //
    //   - SExportDefault: records the "default" export.
    //
    //   - SExportClause: `export { a, b }` — each item is registered as an
    //     export and, if it has a re-export source, as a named import.
    //
    //   - SExportStar: `export * from 'path'` or `export * as ns from 'path'`.
    //     The former is tracked for star-export merging; the latter creates
    //     both a named import and a named export.
    //
    //   - SExportFrom: `export { a as b } from 'path'` — each item is
    //     registered as both an import (under the original name) and an export
    //     (under the alias).  Non-default JSON imports produce an error.
    //     Empty re-export lists in TypeScript mode cause the statement to be
    //     dropped.
    //
    // The function returns the filtered statement list (with any removed
    // statements excluded) and an importsExportsScanResult carrying the
    // import records that were consumed.
    //
    // Input:  stmts containing `import { unused } from './mod'`
    // Output: result.stmts with the unused import removed (in TS/minify mode)
    //
    // Input:  stmts containing `export * from './other'`
    // Output: exportStarImportRecords updated with the import record index
    importsExportsScanResult Parser::scanForImportsAndExports(std::vector<Stmt> stmts) {
        importsExportsScanResult result;
        config::TSUnusedImportFlags unusedImportFlags = config::TSConfigUnusedImportFlags(this->options.optionsThatSupportStructuralEquality.ts.Config);
        size_t stmtsEnd = 0;

        for (Stmt &stmt : stmts) {
            if (SImport *s = Get<SImport>(stmt.data); s != nullptr) {
                compiler::ImportRecord *record = &this->importRecords[s->import_record_index];

                bool keepUnusedImports = this->options.optionsThatSupportStructuralEquality.ts.Parse && config::Has(unusedImportFlags, config::TSUnusedImportFlags::kKeepValues) &&
                    this->options.optionsThatSupportStructuralEquality.mode != config::Mode::kBundle && !this->options.optionsThatSupportStructuralEquality.minifyIdentifiers;

                if (compiler::Has(record->flags, compiler::ImportRecordFlags::kAssertTypeJSON) && this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle && s->items != nullptr) {
                    for (ClauseItem &item : *s->items) {
                        if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->tsUseCounts[item.name.ref.inner_index] == 0 && !config::Has(unusedImportFlags, config::TSUnusedImportFlags::kKeepValues)) {
                            continue;
                        }
                        if (item.alias != "default") {
                            this->log.AddErrorWithNotes(&this->tracker, RangeOfIdentifier(this->source, item.alias_loc),
                                logger::FormatMsg(logger::MsgCat::kJS_NonDefaultJSONImportWithAssertion, item.alias),
                                this->notesForAssertTypeJSON(record, item.alias));
                        }
                    }
                }

                if ((this->options.optionsThatSupportStructuralEquality.minifySyntax || this->options.optionsThatSupportStructuralEquality.ts.Parse) && !keepUnusedImports) {
                    bool foundImports = false;
                    bool isUnusedInTypeScript = true;

                    if (s->default_name != nullptr) {
                        foundImports = true;
                        compiler::Symbol symbol = this->symbols[s->default_name->ref.inner_index];

                        if (this->options.optionsThatSupportStructuralEquality.ts.Parse && (this->tsUseCounts[s->default_name->ref.inner_index] != 0 || config::Has(unusedImportFlags, config::TSUnusedImportFlags::kKeepValues))) {
                            isUnusedInTypeScript = false;
                        }

                        if (symbol.use_count_estimate == 0 && (this->options.optionsThatSupportStructuralEquality.ts.Parse || !this->moduleScope->contains_direct_eval)) {
                            s->default_name = nullptr;
                        }
                    }

                    if (s->star_name_loc != nullptr) {
                        foundImports = true;
                        compiler::Symbol symbol = this->symbols[s->namespace_ref.inner_index];

                        if (this->options.optionsThatSupportStructuralEquality.ts.Parse && (this->tsUseCounts[s->namespace_ref.inner_index] != 0 || config::Has(unusedImportFlags, config::TSUnusedImportFlags::kKeepValues))) {
                            isUnusedInTypeScript = false;
                        }

                        if (symbol.use_count_estimate == 0 && (this->options.optionsThatSupportStructuralEquality.ts.Parse || !this->moduleScope->contains_direct_eval)) {
                            if (auto it = this->importItemsForNamespace.find(s->namespace_ref); it != this->importItemsForNamespace.end() && it->second.entries.empty()) {
                                s->star_name_loc = nullptr;
                            }
                        }
                    }

                    if (s->items != nullptr) {
                        foundImports = true;
                        size_t itemsEnd = 0;

                        for (ClauseItem &item : *s->items) {
                            compiler::Symbol symbol = this->symbols[item.name.ref.inner_index];

                            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && (this->tsUseCounts[item.name.ref.inner_index] != 0 || config::Has(unusedImportFlags, config::TSUnusedImportFlags::kKeepValues))) {
                                isUnusedInTypeScript = false;
                            }

                            if (symbol.use_count_estimate != 0 || (!this->options.optionsThatSupportStructuralEquality.ts.Parse && this->moduleScope->contains_direct_eval)) {
                                (*s->items)[itemsEnd] = item;
                                itemsEnd++;
                            }
                        }

                        if (itemsEnd == 0) {
                            s->items = nullptr;
                        } else {
                            s->items->resize(itemsEnd);
                        }
                    }

                    if (this->options.optionsThatSupportStructuralEquality.ts.Parse && foundImports && isUnusedInTypeScript && !config::Has(unusedImportFlags, config::TSUnusedImportFlags::kKeepStmt)) {
                        if (!record->source_index.IsValid() && !record->copy_source_index.IsValid()) {
                            record->flags = record->flags | compiler::ImportRecordFlags::kIsUnused;
                            continue;
                        }
                    }
                }

                if (this->options.optionsThatSupportStructuralEquality.mode != config::Mode::kPassThrough) {
                    if (s->star_name_loc != nullptr) {
                        if (auto it = this->importItemsForNamespace.find(s->namespace_ref); it != this->importItemsForNamespace.end() && !it->second.entries.empty()) {
                            std::vector<std::string> sorted;
                            sorted.reserve(it->second.entries.size());
                            for (auto &entry : it->second.entries) {
                                sorted.push_back(entry.first);
                            }
                            std::sort(sorted.begin(), sorted.end());

                            for (const std::string &alias : sorted) {
                                compiler::LocRef name = it->second.entries[alias];
                                NamedImport ni;
                                ni.alias = alias;
                                ni.alias_loc = name.loc;
                                ni.namespace_ref = s->namespace_ref;
                                ni.import_record_index = s->import_record_index;
                                this->namedImports[name.ref] = ni;

                                this->symbols[name.ref.inner_index].namespace_alias = new compiler::NamespaceAlias{alias, s->namespace_ref};

                                this->declaredSymbols.push_back(DeclaredSymbol{name.ref, true});
                            }
                        }
                    }

                    if (s->default_name != nullptr) {
                        NamedImport ni;
                        ni.alias = "default";
                        ni.alias_loc = s->default_name->loc;
                        ni.namespace_ref = s->namespace_ref;
                        ni.import_record_index = s->import_record_index;
                        this->namedImports[s->default_name->ref] = ni;
                    }

                    if (s->star_name_loc != nullptr) {
                        NamedImport ni;
                        ni.alias_is_star = true;
                        ni.alias_loc = *s->star_name_loc;
                        ni.namespace_ref = compiler::kInvalidRef;
                        ni.import_record_index = s->import_record_index;
                        this->namedImports[s->namespace_ref] = ni;
                    }

                    if (s->items != nullptr) {
                        for (ClauseItem &item : *s->items) {
                            NamedImport ni;
                            ni.alias = item.alias;
                            ni.alias_loc = item.alias_loc;
                            ni.namespace_ref = s->namespace_ref;
                            ni.import_record_index = s->import_record_index;
                            this->namedImports[item.name.ref] = ni;
                        }
                    }
                }

                this->importRecordsForCurrentPart.push_back(s->import_record_index);

                if (s->star_name_loc != nullptr) {
                    record->flags = record->flags | compiler::ImportRecordFlags::kContainsImportStar;
                }

                if (s->default_name != nullptr) {
                    record->flags = record->flags | compiler::ImportRecordFlags::kContainsDefaultAlias;
                } else if (s->items != nullptr) {
                    for (ClauseItem &item : *s->items) {
                        if (item.alias == "default") {
                            record->flags = record->flags | compiler::ImportRecordFlags::kContainsDefaultAlias;
                        } else if (item.alias == "__esModule") {
                            record->flags = record->flags | compiler::ImportRecordFlags::kContainsESModuleAlias;
                        }
                    }
                }
            } else if (SFunction *sFn = Get<SFunction>(stmt.data); sFn != nullptr) {
                if (sFn->is_export) {
                    this->recordExport(sFn->fn.name->loc, this->symbols[sFn->fn.name->ref.inner_index].original_name, sFn->fn.name->ref);
                }
            } else if (SClass *sCls = Get<SClass>(stmt.data); sCls != nullptr) {
                if (sCls->is_export) {
                    this->recordExport(sCls->class_.name->loc, this->symbols[sCls->class_.name->ref.inner_index].original_name, sCls->class_.name->ref);
                }
            } else if (SLocal *sLoc = Get<SLocal>(stmt.data); sLoc != nullptr) {
                if (sLoc->is_export) {
                    ForEachIdentifierBindingInDecls(sLoc->decls, [&](logger::Loc loc, BIdentifier &b) {
                        this->recordExport(loc, this->symbols[b.ref.inner_index].original_name, b.ref);
                    });
                }

                if (this->checkForUnusedTSImportEquals(sLoc, &result)) {
                    continue;
                }
            } else if (SExportDefault *sDef = Get<SExportDefault>(stmt.data); sDef != nullptr) {
                this->recordExport(sDef->default_name.loc, "default", sDef->default_name.ref);
            } else if (SExportClause *sClause = Get<SExportClause>(stmt.data); sClause != nullptr) {
                for (ClauseItem &item : sClause->items) {
                    this->recordExport(item.alias_loc, item.alias, item.name.ref);
                }
            } else if (SExportStar *sStar = Get<SExportStar>(stmt.data); sStar != nullptr) {
                compiler::ImportRecord *record = &this->importRecords[sStar->import_record_index];
                this->importRecordsForCurrentPart.push_back(sStar->import_record_index);

                if (sStar->alias != nullptr) {
                    NamedImport ni;
                    ni.alias_is_star = true;
                    ni.alias_loc = sStar->alias->loc;
                    ni.namespace_ref = compiler::kInvalidRef;
                    ni.import_record_index = sStar->import_record_index;
                    ni.is_exported = true;
                    this->namedImports[sStar->namespace_ref] = ni;
                    this->recordExport(sStar->alias->loc, sStar->alias->original_name, sStar->namespace_ref);

                    record->flags = record->flags | compiler::ImportRecordFlags::kContainsImportStar;
                } else {
                    this->exportStarImportRecords.push_back(sStar->import_record_index);
                }
            } else if (SExportFrom *sFrom = Get<SExportFrom>(stmt.data); sFrom != nullptr) {
                compiler::ImportRecord *record = &this->importRecords[sFrom->import_record_index];
                this->importRecordsForCurrentPart.push_back(sFrom->import_record_index);

                for (ClauseItem &item : sFrom->items) {
                    NamedImport ni;
                    ni.alias = item.original_name;
                    ni.alias_loc = item.name.loc;
                    ni.namespace_ref = sFrom->namespace_ref;
                    ni.import_record_index = sFrom->import_record_index;
                    ni.is_exported = true;
                    this->namedImports[item.name.ref] = ni;
                    this->recordExport(item.name.loc, item.alias, item.name.ref);

                    if (item.original_name == "default") {
                        record->flags = record->flags | compiler::ImportRecordFlags::kContainsDefaultAlias;
                    } else if (item.original_name == "__esModule") {
                        record->flags = record->flags | compiler::ImportRecordFlags::kContainsESModuleAlias;
                    }
                }

                if (compiler::Has(record->flags, compiler::ImportRecordFlags::kAssertTypeJSON) && this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle) {
                    for (ClauseItem &item : sFrom->items) {
                        if (item.original_name != "default") {
                            this->log.AddErrorWithNotes(&this->tracker, RangeOfIdentifier(this->source, item.name.loc),
                                logger::FormatMsg(logger::MsgCat::kJS_NonDefaultJSONImportWithAssertion, item.original_name),
                                this->notesForAssertTypeJSON(record, item.original_name));
                        }
                    }
                }

                if (this->options.optionsThatSupportStructuralEquality.ts.Parse && sFrom->items.empty() && !config::Has(unusedImportFlags, config::TSUnusedImportFlags::kKeepStmt)) {
                    continue;
                }
            }

            stmts[stmtsEnd] = stmt;
            stmtsEnd++;
        }

        stmts.resize(stmtsEnd);
        result.stmts = stmts;
        return result;
    }


    // whyESModule
    // -----------
    // Determines the reason the current file is treated as an ECMAScript
    // module.  The reason is reported both as a enum tag (for programmatic
    // use) and as a human-readable diagnostic note (for error messages).
    //
    // The priority order is:
    //   1. Presence of the "export" keyword in source.
    //   2. Use of "import.meta" in source.
    //   3. Top-level "await" keyword in source.
    //   4. File extension ".mjs".
    //   5. File extension ".mts".
    //   6. Nearest enclosing "package.json" has `"type": "module"`.
    //   7. Presence of the "import" statement keyword (checked last because
    //      some code paths care about the specific keyword and others do not).
    //
    // When none of the above triggers, the function returns whyESMUnknown
    // with an empty note vector.
    std::pair<whyESM, std::vector<logger::MsgData>> Parser::whyESModule() {
        std::string because = "This file is considered to be an ECMAScript module because";
        if (this->esmExportKeyword.len > 0) {
            return {whyESMExportKeyword, std::vector<logger::MsgData>{this->tracker.MakeMsgData(this->esmExportKeyword,
                because + " of the \"export\" keyword here:")}};
        } else if (this->esmImportMeta.len > 0) {
            return {whyESMImportMeta, std::vector<logger::MsgData>{this->tracker.MakeMsgData(this->esmImportMeta,
                because + " of the use of \"import.meta\" here:")}};
        } else if (this->topLevelAwaitKeyword.len > 0) {
            return {whyESMTopLevelAwait, std::vector<logger::MsgData>{this->tracker.MakeMsgData(this->topLevelAwaitKeyword,
                because + " of the top-level \"await\" keyword here:")}};
        } else if (this->options.optionsThatSupportStructuralEquality.moduleTypeData.type == ModuleType::kESM_MJS) {
            logger::MsgData note;
            note.text = because + " the file name ends in \".mjs\".";
            return {whyESMFileMJS, std::vector<logger::MsgData>{note}};
        } else if (this->options.optionsThatSupportStructuralEquality.moduleTypeData.type == ModuleType::kESM_MTS) {
            logger::MsgData note;
            note.text = because + " the file name ends in \".mts\".";
            return {whyESMFileMTS, std::vector<logger::MsgData>{note}};
        } else if (this->options.optionsThatSupportStructuralEquality.moduleTypeData.type == ModuleType::kESM_PackageJSON) {
            logger::LineColumnTracker pkgTracker(this->options.optionsThatSupportStructuralEquality.moduleTypeData.source);
            return {whyESMTypeModulePackageJSON, std::vector<logger::MsgData>{pkgTracker.MakeMsgData(this->options.optionsThatSupportStructuralEquality.moduleTypeData.range,
                because + " the enclosing \"package.json\" file sets the type of this file to \"module\":")}};
        } else if (this->esmImportStatementKeyword.len > 0) {
            return {whyESMImportStatement, std::vector<logger::MsgData>{this->tracker.MakeMsgData(this->esmImportStatementKeyword,
                because + " of the \"import\" keyword here:")}};
        }
        return {whyESMUnknown, {}};
    }

    // warnAboutImportNamespaceCall
    // ----------------------------
    // Emits a warning when the result of `import * as ns from '...'` is
    // called directly (e.g. `ns(args)`) or used as a constructor
    // (`new ns(args)`) or as a JSX tag (`<ns />`).  The namespace object
    // is not callable and doing so will always throw at runtime.
    //
    // The warning is suppressed when output format is "preserve" because the
    // code may be valid in the target runtime (e.g. SystemJS).  It is also
    // deduplicated per (ref, kind) pair so that repeated references to the
    // same namespace do not produce multiple warnings.
    //
    // When the namespace was imported with `import * as ns`, the note
    // suggests changing to a default import instead.  When TypeScript mode
    // is active, an additional note reminds the user to enable the
    // "esModuleInterop" tsconfig setting.
    //
    // Input:  target = EIdentifier for `ns`, kind = exprKindCall
    // Output: warning diagnostic: "Calling the namespace \"ns\" directly ..."
    void Parser::warnAboutImportNamespaceCall(Expr target, importNamespaceCallKind kind) {
        if (this->options.optionsThatSupportStructuralEquality.outputFormat != config::Format::kPreserve) {
            if (EIdentifier *id = Get<EIdentifier>(target.data); id != nullptr) {
                if (this->importItemsForNamespace.count(id->ref) > 0) {
                    importNamespaceCall key{id->ref, kind};
                    if (this->importNamespaceCCMap.empty() || !this->importNamespaceCCMap.count(key)) {
                        this->importNamespaceCCMap[key] = true;
                        logger::Range r = RangeOfIdentifier(this->source, target.loc);

                        std::vector<logger::MsgData> notes;
                        std::string name = this->symbols[id->ref.inner_index].original_name;
                        auto memberIt = this->moduleScope->members.find(name);
                        if (memberIt != this->moduleScope->members.end() && memberIt->second.ref == id->ref) {
                            logger::Range star = this->source.RangeOfOperatorBefore(memberIt->second.loc, "*");
                            if (star.len > 0) {
                                logger::Range as = this->source.RangeOfOperatorBefore(memberIt->second.loc, "as");
                                if (as.len > 0 && as.loc.start > star.loc.start) {
                                    logger::MsgData note = this->tracker.MakeMsgData(
                                        logger::Range{star.loc, RangeOfIdentifier(this->source, memberIt->second.loc).End() - star.loc.start},
                                        "Consider changing \"" + name + "\" to a default import instead:");
                                    note.location->suggestion = name;
                                    notes.push_back(note);
                                }
                            }
                        }

                        if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                            logger::MsgData tsNote;
                            tsNote.text = std::string("Make sure to enable TypeScript's \"esModuleInterop\" setting so that TypeScript's type checker generates an error when you try to do this. ") +
                                "You can read more about this setting here: https://www.typescriptlang.org/tsconfig#esModuleInterop";
                            notes.push_back(tsNote);
                        }

                        std::string verb, where, noun;
                        switch (kind) {
                        case importNamespaceCallKind::exprKindCall:
                            verb = "Calling";
                            noun = "function";
                            break;
                        case importNamespaceCallKind::exprKindNew:
                            verb = "Constructing";
                            noun = "constructor";
                            break;
                        case importNamespaceCallKind::exprKindJSXTag:
                            verb = "Using";
                            where = " in a JSX expression";
                            noun = "component";
                            break;
                        }

                        this->log.AddIDWithNotes(logger::MsgID::kJS_CallImportNamespace, logger::MsgKind::kWarning, &this->tracker, r,
                            logger::FormatMsg(logger::MsgCat::kJS_ImportNamespaceCrash, verb, this->symbols[id->ref.inner_index].original_name, where, noun),
                            notes);
                    }
                }
            }
        }
    }

}
