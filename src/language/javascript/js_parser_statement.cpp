#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
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

    // Declares all identifier bindings found in a binding pattern (simple names,
    // array destructuring, object destructuring, defaults, rest elements).
    // For each leaf identifier, calls declareSymbol() unless we are inside a
    // TypeScript "declare" block at non-module scope (where declarations are
    // type-only and should not create runtime symbols).
    void Parser::declareBinding(compiler::SymbolKind kind, Binding binding, parseStmtOpts opts) {
        ForEachIdentifierBinding(binding, [this, &opts, kind](logger::Loc loc, BIdentifier &b) {
            if (!opts.isTypeScriptDeclare || (opts.isNamespaceScope && opts.isExport)) {
                b.ref = this->declareSymbol(kind, loc, this->loadNameFromRef(b.ref));
            }
        });
    }


    // Returns a human-readable name for a property key expression, suitable for
    // inclusion in error messages. String keys are quoted (e.g. "\"foo\""),
    // private identifiers show their full name (e.g. "\"#bar\""), and anything
    // else (computed keys, numeric keys) falls back to the generic "property".
    std::string Parser::keyNameForError(Expr key) {
        if (auto k = Get<EString>(key.data)) {
            return "\"" + helpers::UTF16ToString(k->value) + "\"";
        } else if (auto kp = Get<EPrivateIdentifier>(key.data)) {
            return "\"" + this->loadNameFromRef(kp->ref) + "\"";
        }
        return "property";
    }



    // Converts a bigint literal written as text (e.g. "0b100101", "0x1f", or
    // "102030405060708090807060504030201") to its decimal string representation,
    // Note: underscores have already been removed by the lexer.
    static std::string BigIntLiteralToDecimal(const std::string& text) {
        int base = 10;
        size_t start = 0;
        if (text.size() >= 2 && text[0] == '0') {
            switch (text[1]) {
            case 'b': case 'B': base = 2; start = 2; break;
            case 'o': case 'O': base = 8; start = 2; break;
            case 'x': case 'X': base = 16; start = 2; break;
            }
        }

        std::string result = "0";
        for (size_t i = start; i < text.size(); i++) {
            char c = text[i];
            int digit;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = c - 'a' + 10;
            } else {
                digit = c - 'A' + 10;
            }

            // "result = result * base + digit" done digit-by-digit. Each
            // multiply-and-add is bounded by 9 * base + carry, so it always fits
            // in a plain int.
            int carry = digit;
            for (size_t j = result.size(); j > 0; j--) {
                int cur = (result[j - 1] - '0') * base + carry;
                result[j - 1] = static_cast<char>('0' + (cur % 10));
                carry = cur / 10;
            }
            while (carry > 0) {
                result.insert(result.begin(), static_cast<char>('0' + (carry % 10)));
                carry /= 10;
            }
        }
        return result;
    }



    // Parses a BigInt literal. If the target environment does not support
    // BigInt (as configured via unsupportedJSFeatures), the literal is
    // converted to its decimal string representation instead, so that
    // property keys and class field names remain valid JavaScript.
    Expr Parser::parseBigIntOrStringIfUnsupported() {
        if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kBigint)) {
            // Bigint literals are unavailable, so object keys and class field
            // names must be re-emitted as their decimal string value instead.
            EString value{};
            value.value = helpers::StringToUTF16(BigIntLiteralToDecimal(this->lexer.identifier.str));
            return Expr{std::make_shared<EString>(std::move(value)), this->lexer.Loc()};
        }
        EBigInt value{};
        value.value = this->lexer.identifier.str;
        return Expr{std::make_shared<EBigInt>(std::move(value)), this->lexer.Loc()};
    }




    // These properties have special semantics in JavaScript. They must not be
    // mangled or we could potentially fail to parse valid JavaScript syntax or
    // generate invalid JavaScript syntax as output.
    //
    // This list is only intended to contain properties specific to the JavaScript
    // language itself to avoid syntax errors in the generated output. It's not
    // intended to contain properties for JavaScript APIs. Those must be provided
    // by the user.
    static const std::unordered_map<std::string, bool> &permanentReservedProps() {
        static std::unordered_map<std::string, bool> map{
            {"__proto__", true},
            {"constructor", true},
            {"prototype", true},
        };
        return map;
    }




    // Returns true if the given property name should be mangled (renamed) during
    // output. A property is eligible if it matches the user-supplied mangleProps
    // regex, does not match the reserveProps regex, and is not one of the
    // permanently reserved properties (__proto__, constructor, prototype).
    // Results are cached in reservedProps for names that are explicitly excluded.
    bool Parser::isMangledProp(const std::string &name) {
        if (this->options.mangleProps == nullptr) {
            return false;
        }
        if (std::regex_search(name, *this->options.mangleProps) && !permanentReservedProps().count(name) &&
            (this->options.reserveProps == nullptr || !std::regex_search(name, *this->options.reserveProps))) {
            return true;
        }

        this->reservedProps[name] = true;
        return false;
    }

    // Builds a property access expression. If the property name is mangled,
    // produces `target[symbolFor(name)]` (an index expression). Otherwise
    // produces `target.name` (a dot expression). Used during the visit pass
    // to transform dot-chain accesses that may need renaming.
    E *Parser::dotOrMangledPropVisit(Expr target, const std::string &name, logger::Loc nameLoc) {
        if (this->isMangledProp(name)) {
            EIndex index{};
            index.target = std::move(target);
            ENameOfSymbol e{};
            e.ref = this->symbolForMangledProp(name);
            index.index = Expr{std::make_shared<ENameOfSymbol>(std::move(e)), nameLoc};
            return new E(std::make_shared<EIndex>(std::move(index)));
        }

        EDot dot{};
        dot.target = std::move(target);
        dot.name = name;
        dot.name_loc = nameLoc;
        return new E(std::make_shared<EDot>(std::move(dot)));
    }

    // Parses the body of an arrow function after the `=>` token.
    // If the body is a brace block `{ ... }`, delegates to parseFnBody() for a
    // full function body. Otherwise parses a concise expression and wraps it in
    // an implicit `return` statement. Throws on a newline before `=>`.
    // Input:  args already parsed, lexer positioned at `=>`
    // Output: EArrow with body (block or concise)
    EArrow *Parser::parseArrowBody(std::vector<Arg> args, struct fnOrArrowDataParse data) {
        logger::Loc arrowLoc = this->lexer.Loc();

        // Newlines are not allowed before "=>"
        if (this->lexer.has_newline_before) {
            this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_NewlineBeforeArrow));
            throw LexerPanic();
        }

        this->lexer.Expect(T::kEqualsGreaterThan);

        for (const Arg &arg : args) {
            this->declareBinding(compiler::SymbolKind::kHoisted, arg.binding, parseStmtOpts());
        }

        // The ability to use "this" and "super" is inherited by arrow functions
        data.isThisDisallowed = this->fnOrArrowDataParse.isThisDisallowed;
        data.allowSuperCall = this->fnOrArrowDataParse.allowSuperCall;
        data.allowSuperProperty = this->fnOrArrowDataParse.allowSuperProperty;

        if (this->lexer.token == T::kOpenBrace) {
            FnBody body = this->parseFnBody(data);
            this->afterArrowBodyLoc = this->lexer.Loc();
            EArrow arrow{};
            arrow.args = args;
            arrow.body = body;
            return new EArrow(std::move(arrow));
        }

        this->pushScopeForParsePass(ScopeKind::kFunctionBody, arrowLoc);
        struct ScopePopper {
            Parser *p;
            ~ScopePopper() { p->popScope(); }
        } popper{this};

        struct fnOrArrowDataParse oldFnOrArrowData = this->fnOrArrowDataParse;
        this->fnOrArrowDataParse = data;
        Expr expr = this->parseExpr(L::kComma);
        this->fnOrArrowDataParse = oldFnOrArrowData;

        EArrow arrow{};
        arrow.args = args;
        arrow.prefer_expr = true;
        arrow.body.loc = arrowLoc;
        SReturn returnStmt;
        returnStmt.value_or_nil = expr;
        arrow.body.block.stmts.push_back(Stmt{std::make_shared<SReturn>(returnStmt), expr.loc});
        return new EArrow(std::move(arrow));
    }


    // Main statement parsing dispatcher. Reads the current token and routes to
    // the appropriate sub-parser based on the token type. This is the central
    // entry point for all statement-level parsing (blocks, declarations, control
    // flow, import/export, labels, expression statements, etc.).
    //
    // Input:  opts controls what declarations are allowed (e.g., isModuleScope,
    //         isExport, lexicalDecl level, isTypeScriptDeclare), whether we are
    //         in an export context, and carries deferred decorator state.
    // Output: A fully parsed Stmt AST node.
    //
    // Edge cases handled:
    //   - Bare semicolon → empty statement
    //   - export keyword → dispatches to export sub-handlers (default, star, clause,
    //     equals, and declaration re-parsing with isExport=true)
    //   - @decorator → parsed before class declarations
    //   - Labeled statements when `identifier:` is detected
    //   - TypeScript keywords (type, namespace, interface, abstract, declare)
    //   - Automatic semicolon insertion via ExpectOrInsertSemicolon
    //
    // Throws LexerPanic on unrecoverable syntax errors.
    Stmt Parser::parseStmt(parseStmtOpts opts) {
        logger::Loc loc = this->lexer.Loc();

        if (Has(this->lexer.has_comment_before, CommentBefore::kNoSideEffects)) {
            opts.hasNoSideEffectsComment = true;
        }

        // Do not attach any leading comments to the next expression
        this->lexer.comments_before_token.clear();

        switch (this->lexer.token) {

        // ---- Empty statement ----
        // A bare semicolon produces an empty statement node.
        case T::kSemicolon:
            this->lexer.Next();
            return Stmt{kSEmptyShared, loc};

        // ---- Export statement ----
        // Handles all ESM export forms: `export default`, `export *`, `export {}`,
        // `export =` (TypeScript), `export function/class/var/let/const`, and
        // TypeScript-specific forms (export type, export namespace, etc.).
        // For declarative exports, sets opts.isExport=true and re-dispatches
        // through parseStmt() so the inner declaration is parsed normally.
        case T::kExport: {
            logger::Range previousExportKeyword = this->esmExportKeyword;
            if (opts.isModuleScope) {
                this->esmExportKeyword = this->lexer.Range();
            } else if (!opts.isNamespaceScope) {
                this->lexer.Unexpected();
            }
            this->lexer.Next();

            switch (this->lexer.token) {
            case T::kClass:
            case T::kConst:
            case T::kFunction:
            case T::kVar:
            case T::kAt:
                opts.isExport = true;
                return this->parseStmt(opts);

            case T::kImport:
                // "export import foo = bar"
                if (this->options.optionsThatSupportStructuralEquality.ts.Parse && (opts.isModuleScope || opts.isNamespaceScope)) {
                    opts.isExport = true;
                    return this->parseStmt(opts);
                }

                this->lexer.Unexpected();
                return Stmt();

            case T::kEnum:
                if (!this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                    this->lexer.Unexpected();
                }
                opts.isExport = true;
                return this->parseStmt(opts);

            case T::kIdentifier: {
                if (this->lexer.IsContextualKeyword("let")) {
                    opts.isExport = true;
                    return this->parseStmt(opts);
                }

                if (this->lexer.IsContextualKeyword("as")) {
                    // "export as namespace ns;"
                    this->lexer.Next();
                    this->lexer.ExpectContextualKeyword("namespace");
                    this->lexer.Expect(T::kIdentifier);
                    this->lexer.ExpectOrInsertSemicolon();
                    return Stmt{kSTypeScriptShared, loc};
                }

                if (this->lexer.IsContextualKeyword("async")) {
                    // "export async function foo() {}"
                    logger::Range asyncRange = this->lexer.Range();
                    this->lexer.Next();
                    if (this->lexer.has_newline_before) {
                        this->log.AddError(&this->tracker, logger::Range{logger::Loc{asyncRange.End()}}, logger::FormatMsg(logger::MsgCat::kJS_NewlineAfterAsync));
                        throw LexerPanic();
                    }
                    this->lexer.Expect(T::kFunction);
                    opts.isExport = true;
                    return this->parseFnStmt(loc, opts, true /* isAsync */, asyncRange);
                }

                if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                    const std::string &identifier = this->lexer.identifier.str;
                    if (identifier == "type") {
                        // "export type foo = ..."
                        logger::Range typeRange = this->lexer.Range();
                        this->lexer.Next();
                        if (this->lexer.has_newline_before && this->lexer.token != T::kOpenBrace && this->lexer.token != T::kAsterisk) {
                            this->log.AddError(&this->tracker, logger::Range{logger::Loc{typeRange.End()}}, logger::FormatMsg(logger::MsgCat::kJS_NewlineAfterType));
                            throw LexerPanic();
                        }
                        parseStmtOpts tssOpts;
                        tssOpts.isModuleScope = opts.isModuleScope;
                        tssOpts.isExport = true;
                        this->skipTypeScriptTypeStmt(tssOpts);
                        return Stmt{kSTypeScriptShared, loc};
                    } else if (identifier == "namespace" || identifier == "abstract" || identifier == "module" || identifier == "interface") {
                        // "export namespace Foo {}"
                        // "export abstract class Foo {}"
                        // "export module Foo {}"
                        // "export interface Foo {}"
                        opts.isExport = true;
                        return this->parseStmt(opts);
                    } else if (identifier == "declare") {
                        // "export declare class Foo {}"
                        opts.isExport = true;
                        opts.lexicalDecl = lexicalDeclAllowAll;
                        opts.isTypeScriptDeclare = true;
                        return this->parseStmt(opts);
                    }
                }

                this->lexer.Unexpected();
                return Stmt();
            }

            case T::kDefault: {
                if (!opts.isModuleScope && (!opts.isNamespaceScope || !opts.isTypeScriptDeclare)) {
                    this->lexer.Unexpected();
                }

                logger::Loc defaultLoc = this->lexer.Loc();
                this->lexer.Next();

                // Also pick up comments after the "default" keyword
                if (Has(this->lexer.has_comment_before, CommentBefore::kNoSideEffects)) {
                    opts.hasNoSideEffectsComment = true;
                }

                // The default name is lazily generated only if no other name is present
                auto createDefaultName = [&]() -> compiler::LocRef {
                    // This must be named "default" for when "--keep-names" is active
                    compiler::LocRef defaultName{defaultLoc, this->newSymbol(compiler::SymbolKind::kOther, "default")};
                    this->currentScope->generated.push_back(defaultName.ref);
                    return defaultName;
                };

                // "export default async function() {}"
                // "export default async function foo() {}"
                if (this->lexer.IsContextualKeyword("async")) {
                    logger::Range asyncRange = this->lexer.Range();
                    this->lexer.Next();

                    if (this->lexer.token == T::kFunction && !this->lexer.has_newline_before) {
                        this->lexer.Next();
                        parseStmtOpts stmtOpts;
                        stmtOpts.isNameOptional = true;
                        stmtOpts.lexicalDecl = lexicalDeclAllowAll;
                        stmtOpts.hasNoSideEffectsComment = opts.hasNoSideEffectsComment;
                        Stmt stmt = this->parseFnStmt(loc, stmtOpts, true /* isAsync */, asyncRange);
                        if (Get<STypeScript>(stmt.data) != nullptr) {
                            return stmt; // This was just a type annotation
                        }

                        // Use the statement name if present, since it's a better name
                        compiler::LocRef defaultName;
                        if (auto s = Get<SFunction>(stmt.data); s != nullptr && s->fn.name != nullptr) {
                            defaultName = compiler::LocRef{defaultLoc, s->fn.name->ref};
                        } else {
                            defaultName = createDefaultName();
                        }

                        std::shared_ptr<SExportDefault> e = std::make_shared<SExportDefault>();
                        e->default_name = defaultName;
                        e->value = stmt;
                        return Stmt{e, loc};
                    }

                    compiler::LocRef defaultName = createDefaultName();
                    Expr expr = this->parseSuffix(this->parseAsyncPrefixExpr(asyncRange, L::kComma, static_cast<exprFlag>(0)), L::kComma, nullptr, static_cast<exprFlag>(0));
                    this->lexer.ExpectOrInsertSemicolon();
                    std::shared_ptr<SExportDefault> e = std::make_shared<SExportDefault>();
                    e->default_name = defaultName;
                    std::shared_ptr<SExpr> sv = std::make_shared<SExpr>();
                    sv->value = expr;
                    e->value = Stmt{sv, loc};
                    return Stmt{e, loc};
                }

                // "export default class {}"
                // "export default class Foo {}"
                // "export default @x class {}"
                // "export default @x class Foo {}"
                // "export default function() {}"
                // "export default function foo() {}"
                // "export default interface Foo {}"
                // "export default interface + 1"
                if (this->lexer.token == T::kFunction || this->lexer.token == T::kClass || this->lexer.token == T::kAt ||
                    (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.IsContextualKeyword("interface"))) {
                    parseStmtOpts stmtOpts;
                    stmtOpts.deferredDecorators = opts.deferredDecorators;
                    stmtOpts.isNameOptional = true;
                    stmtOpts.isExportDefault = true;
                    stmtOpts.lexicalDecl = lexicalDeclAllowAll;
                    stmtOpts.hasNoSideEffectsComment = opts.hasNoSideEffectsComment;
                    Stmt stmt = this->parseStmt(stmtOpts);

                    // Use the statement name if present, since it's a better name
                    compiler::LocRef defaultName;
                    if (Get<STypeScript>(stmt.data) != nullptr || Get<SExpr>(stmt.data) != nullptr) {
                        return stmt; // Handle the "interface" case above
                    }
                    if (auto s = Get<SFunction>(stmt.data); s != nullptr) {
                        if (s->fn.name != nullptr) {
                            defaultName = compiler::LocRef{defaultLoc, s->fn.name->ref};
                        } else {
                            defaultName = createDefaultName();
                        }
                    } else if (auto sc = Get<SClass>(stmt.data); sc != nullptr) {
                        if (sc->class_.name != nullptr) {
                            defaultName = compiler::LocRef{defaultLoc, sc->class_.name->ref};
                        } else {
                            defaultName = createDefaultName();
                        }
                    } else {
                        throw std::runtime_error("Internal error");
                    }
                    std::shared_ptr<SExportDefault> e = std::make_shared<SExportDefault>();
                    e->default_name = defaultName;
                    e->value = stmt;
                    return Stmt{e, loc};
                }

                bool isIdentifier = this->lexer.token == T::kIdentifier;
                std::string name = this->lexer.identifier.str;
                Expr expr = this->parseExpr(L::kComma);

                // "export default abstract class {}"
                // "export default abstract class Foo {}"
                if (this->options.optionsThatSupportStructuralEquality.ts.Parse && isIdentifier && name == "abstract" && !this->lexer.has_newline_before) {
                    if (Get<EIdentifier>(expr.data) != nullptr && this->lexer.token == T::kClass) {
                        parseStmtOpts classStmtOpts;
                        classStmtOpts.deferredDecorators = opts.deferredDecorators;
                        classStmtOpts.isNameOptional = true;
                        Stmt stmt = this->parseClassStmt(loc, classStmtOpts);

                        // Use the statement name if present, since it's a better name
                        compiler::LocRef defaultName;
                        if (auto s = Get<SClass>(stmt.data); s != nullptr && s->class_.name != nullptr) {
                            defaultName = compiler::LocRef{defaultLoc, s->class_.name->ref};
                        } else {
                            defaultName = createDefaultName();
                        }

                        std::shared_ptr<SExportDefault> e = std::make_shared<SExportDefault>();
                        e->default_name = defaultName;
                        e->value = stmt;
                        return Stmt{e, loc};
                    }
                }

                this->lexer.ExpectOrInsertSemicolon();
                compiler::LocRef defaultName = createDefaultName();
                std::shared_ptr<SExportDefault> e = std::make_shared<SExportDefault>();
                e->default_name = defaultName;
                std::shared_ptr<SExpr> sv = std::make_shared<SExpr>();
                sv->value = expr;
                e->value = Stmt{sv, loc};
                return Stmt{e, loc};
            }

            case T::kAsterisk: {
                if (!opts.isModuleScope && (!opts.isNamespaceScope || !opts.isTypeScriptDeclare)) {
                    this->lexer.Unexpected();
                }

                this->lexer.Next();
                compiler::Ref namespaceRef;
                std::shared_ptr<ExportStarAlias> alias;
                logger::Range pathRange;
                std::string pathText;
                compiler::ImportAssertOrWith *assertOrWith = nullptr;
                compiler::ImportRecordFlags flags;

                if (this->lexer.IsContextualKeyword("as")) {
                    // "export * as ns from 'path'"
                    this->lexer.Next();
                    MaybeSubstring name = this->parseClauseAlias("export");
                    namespaceRef = this->storeNameInRef(name);
                    alias = std::make_shared<ExportStarAlias>();
                    alias->loc = this->lexer.Loc();
                    alias->original_name = name.str;
                    this->lexer.Next();
                    this->lexer.ExpectContextualKeyword("from");
                    std::tuple<logger::Range, std::string, compiler::ImportAssertOrWith *, compiler::ImportRecordFlags> path = this->parsePath();
                    pathRange = std::get<0>(path);
                    pathText = std::get<1>(path);
                    assertOrWith = std::get<2>(path);
                    flags = std::get<3>(path);
                } else {
                    // "export * from 'path'"
                    this->lexer.ExpectContextualKeyword("from");
                    std::tuple<logger::Range, std::string, compiler::ImportAssertOrWith *, compiler::ImportRecordFlags> path = this->parsePath();
                    pathRange = std::get<0>(path);
                    pathText = std::get<1>(path);
                    assertOrWith = std::get<2>(path);
                    flags = std::get<3>(path);
                    std::string name = GenerateNonUniqueNameFromPath(pathText) + "_star";
                    namespaceRef = this->storeNameInRef(MaybeSubstring{name});
                }
                uint32_t importRecordIndex = this->addImportRecord(compiler::ImportKind::kStmt, compiler::ImportPhase::kEvaluation, pathRange, pathText, std::shared_ptr<compiler::ImportAssertOrWith>(assertOrWith), flags);

                // Export-star statements anywhere in the file disable top-level const
                // local prefix because import cycles can be used to trigger TDZ
                this->currentScope->is_after_const_local_prefix = true;

                this->lexer.ExpectOrInsertSemicolon();
                std::shared_ptr<SExportStar> e = std::make_shared<SExportStar>();
                e->namespace_ref = namespaceRef;
                e->alias = alias;
                e->import_record_index = importRecordIndex;
                return Stmt{e, loc};
            }

            case T::kOpenBrace: {
                if (!opts.isModuleScope && (!opts.isNamespaceScope || !opts.isTypeScriptDeclare)) {
                    this->lexer.Unexpected();
                }

                std::pair<std::vector<ClauseItem>, bool> clause = this->parseExportClause();
                std::vector<ClauseItem> items = std::get<0>(clause);
                bool isSingleLine = std::get<1>(clause);
                if (this->lexer.IsContextualKeyword("from")) {
                    // "export {} from 'path'"
                    this->lexer.Next();
                    std::tuple<logger::Range, std::string, compiler::ImportAssertOrWith *, compiler::ImportRecordFlags> path = this->parsePath();
                    logger::Range pathLoc = std::get<0>(path);
                    std::string pathText = std::get<1>(path);
                    compiler::ImportAssertOrWith *assertOrWith = std::get<2>(path);
                    compiler::ImportRecordFlags flags = std::get<3>(path);
                    uint32_t importRecordIndex = this->addImportRecord(compiler::ImportKind::kStmt, compiler::ImportPhase::kEvaluation, pathLoc, pathText, std::shared_ptr<compiler::ImportAssertOrWith>(assertOrWith), flags);
                    std::string name = "import_" + GenerateNonUniqueNameFromPath(pathText);
                    compiler::Ref namespaceRef = this->storeNameInRef(MaybeSubstring{name});

                    // Export clause statements anywhere in the file disable top-level const
                    // local prefix because import cycles can be used to trigger TDZ
                    this->currentScope->is_after_const_local_prefix = true;

                    this->lexer.ExpectOrInsertSemicolon();
                    std::shared_ptr<SExportFrom> e = std::make_shared<SExportFrom>();
                    e->items = items;
                    e->namespace_ref = namespaceRef;
                    e->import_record_index = importRecordIndex;
                    e->is_single_line = isSingleLine;
                    return Stmt{e, loc};
                }

                this->lexer.ExpectOrInsertSemicolon();
                std::shared_ptr<SExportClause> e = std::make_shared<SExportClause>();
                e->items = items;
                e->is_single_line = isSingleLine;
                return Stmt{e, loc};
            }

            case T::kEquals:
                // "export = value;"
                this->esmExportKeyword = previousExportKeyword; // This wasn't an ESM export statement after all
                if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                    this->lexer.Next();
                    Expr value = this->parseExpr(L::kLowest);
                    this->lexer.ExpectOrInsertSemicolon();
                    std::shared_ptr<SExportEquals> e = std::make_shared<SExportEquals>();
                    e->value = value;
                    return Stmt{e, loc};
                }
                this->lexer.Unexpected();
                return Stmt();

            default:
                this->lexer.Unexpected();
                return Stmt();
            }
        }

        // ---- Function declaration ----
        case T::kFunction:
            this->lexer.Next();
            return this->parseFnStmt(loc, opts, false /* isAsync */, logger::Range());

        // ---- TypeScript enum declaration ----
        case T::kEnum:
            if (!this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                this->lexer.Unexpected();
            }
            return this->parseTypeScriptEnumStmt(loc, opts);

        // ---- Decorator before a class statement ----
        // Decorators are parsed eagerly, then the following statement is parsed.
        // If the statement is a class, the decorators are attached to it. If it's
        // a `declare class`, the decorator scopes are discarded since type
        // declarations are not visited. Any other statement triggers an error.
        case T::kAt: {
            // Parse decorators before class statements, which are potentially exported
            int scopeIndex = static_cast<int>(this->scopesInOrder.size());
            std::vector<Decorator> decorators = this->parseDecorators(this->currentScope, logger::Range(), static_cast<decoratorContextFlags>(0));

            // "@x export @y class Foo {}"
            if (opts.deferredDecorators != nullptr) {
                this->log.AddError(&this->tracker, logger::Range{loc, 1}, logger::FormatMsg(logger::MsgCat::kJS_DecoratorsNotValidHere));
                this->discardScopesUpTo(scopeIndex);
                return this->parseStmt(opts);
            }

            // If this turns out to be a "declare class" statement, we need to undo the
            // scopes that were potentially pushed while parsing the decorator arguments.
            // That can look like any one of the following:
            //
            //   "@decorator declare class Foo {}"
            //   "@decorator declare abstract class Foo {}"
            //   "@decorator export declare class Foo {}"
            //   "@decorator export declare abstract class Foo {}"
            //
            opts.deferredDecorators = new deferredDecorators();
            opts.deferredDecorators->decorators = decorators;

            Stmt stmt = this->parseStmt(opts);

            // Check for valid decorator targets
            if (Get<SClass>(stmt.data) != nullptr) {
                return stmt;
            }

            if (auto s = Get<SExportDefault>(stmt.data); s != nullptr) {
                if (Get<SClass>(s->value.data) != nullptr) {
                    return stmt;
                }
            }

            if (auto s = Get<STypeScript>(stmt.data); s != nullptr) {
                if (s->was_declare_class) {
                    // If this is a type declaration, discard any scopes that were pushed
                    // while parsing decorators. Unlike with the class statements above,
                    // these scopes won't end up being visited during the upcoming visit
                    // pass because type declarations aren't visited at all.
                    this->discardScopesUpTo(scopeIndex);
                    return stmt;
                }
            }

            // Forbid decorators on anything other than a class statement
            this->log.AddError(&this->tracker, logger::Range{loc, 1}, logger::FormatMsg(logger::MsgCat::kJS_DecoratorsNotValidHere));
            stmt.data = kSTypeScriptShared;
            this->discardScopesUpTo(scopeIndex);
            return stmt;
        }

        // ---- Class declaration ----
        case T::kClass:
            if (opts.lexicalDecl != lexicalDeclAllowAll) {
                this->forbidLexicalDecl(loc);
            }
            return this->parseClassStmt(loc, opts);

        case T::kVar: {
            this->lexer.Next();
            std::vector<Decl> decls = this->parseAndDeclareDecls(compiler::SymbolKind::kHoisted, opts);
            this->lexer.ExpectOrInsertSemicolon();
            std::shared_ptr<SLocal> s = std::make_shared<SLocal>();
            s->kind = LocalKind::kVar;
            s->decls = decls;
            s->is_export = opts.isExport;
            return Stmt{s, loc};
        }

        case T::kConst: {
            if (opts.lexicalDecl != lexicalDeclAllowAll) {
                this->forbidLexicalDecl(loc);
            }
            this->markSyntaxFeature(compat::JSFeature::kConstAndLet, this->lexer.Range());
            this->lexer.Next();

            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kEnum) {
                return this->parseTypeScriptEnumStmt(loc, opts);
            }

            std::vector<Decl> decls = this->parseAndDeclareDecls(compiler::SymbolKind::kConst, opts);
            this->lexer.ExpectOrInsertSemicolon();
            if (!opts.isTypeScriptDeclare) {
                this->requireInitializers(LocalKind::kConst, decls);
            }
            std::shared_ptr<SLocal> s = std::make_shared<SLocal>();
            s->kind = LocalKind::kConst;
            s->decls = decls;
            s->is_export = opts.isExport;
            return Stmt{s, loc};
        }

        // ---- if statement ----
        // Parses `if (test) yes [else no]`. The lexicalDecl option is set to
        // lexicalDeclAllowFnInsideIf for the branches, allowing function
        // declarations inside if/else bodies (which are valid in all browsers).
        case T::kIf: {
            this->lexer.Next();
            this->lexer.Expect(T::kOpenParen);
            Expr test = this->parseExpr(L::kLowest);
            this->lexer.Expect(T::kCloseParen);
            bool isSingleLineYes = !this->lexer.has_newline_before && this->lexer.token != T::kOpenBrace;
            parseStmtOpts branchOpts;
            branchOpts.lexicalDecl = lexicalDeclAllowFnInsideIf;
            Stmt yes = this->parseStmt(branchOpts);
            std::shared_ptr<SIf> e = std::make_shared<SIf>();
            e->test = test;
            e->yes = yes;
            e->is_single_line_yes = isSingleLineYes;
            if (this->lexer.token == T::kElse) {
                this->lexer.Next();
                e->is_single_line_no = !this->lexer.has_newline_before && this->lexer.token != T::kOpenBrace;
                e->no_or_nil = std::make_shared<Stmt>(this->parseStmt(branchOpts));
            }
            return Stmt{e, loc};
        }

        // ---- do-while statement ----
        // The trailing semicolon after `while(test)` is optional per ASI rules.
        case T::kDo: {
            this->lexer.Next();
            Stmt body = this->parseStmt(parseStmtOpts());
            this->lexer.Expect(T::kWhile);
            this->lexer.Expect(T::kOpenParen);
            Expr test = this->parseExpr(L::kLowest);
            this->lexer.Expect(T::kCloseParen);

            // This is a weird corner case where automatic semicolon insertion applies
            // even without a newline present
            if (this->lexer.token == T::kSemicolon) {
                this->lexer.Next();
            }
            std::shared_ptr<SDoWhile> e = std::make_shared<SDoWhile>();
            e->body = body;
            e->test = test;
            return Stmt{e, loc};
        }

        // ---- while statement ----
        case T::kWhile: {
            this->lexer.Next();
            this->lexer.Expect(T::kOpenParen);
            Expr test = this->parseExpr(L::kLowest);
            this->lexer.Expect(T::kCloseParen);
            bool isSingleLineBody = !this->lexer.has_newline_before && this->lexer.token != T::kOpenBrace;
            Stmt body = this->parseStmt(parseStmtOpts());
            std::shared_ptr<SWhile> e = std::make_shared<SWhile>();
            e->test = test;
            e->body = body;
            e->is_single_line_body = isSingleLineBody;
            return Stmt{e, loc};
        }

        // ---- with statement ----
        // Pushes a `kWith` scope so that bare identifiers inside the body are not
        // renamed, since `with(obj) { x }` could refer to obj.x at runtime.
        case T::kWith: {
            this->lexer.Next();
            this->lexer.Expect(T::kOpenParen);
            Expr test = this->parseExpr(L::kLowest);
            logger::Loc bodyLoc = this->lexer.Loc();
            this->lexer.Expect(T::kCloseParen);

            // Push a scope so we make sure to prevent any bare identifiers referenced
            // within the body from being renamed. Renaming them might change the
            // semantics of the code.
            this->pushScopeForParsePass(ScopeKind::kWith, bodyLoc);
            bool isSingleLineBody = !this->lexer.has_newline_before && this->lexer.token != T::kOpenBrace;
            Stmt body = this->parseStmt(parseStmtOpts());
            this->popScope();

            std::shared_ptr<SWith> e = std::make_shared<SWith>();
            e->value = test;
            e->body_loc = bodyLoc;
            e->body = body;
            e->is_single_line_body = isSingleLineBody;
            return Stmt{e, loc};
        }

        case T::kSwitch: {
            this->lexer.Next();
            this->lexer.Expect(T::kOpenParen);
            Expr test = this->parseExpr(L::kLowest);
            this->lexer.Expect(T::kCloseParen);

            logger::Loc bodyLoc = this->lexer.Loc();
            this->pushScopeForParsePass(ScopeKind::kBlock, bodyLoc);
            ParserScopePopper popper{this};

            this->lexer.Expect(T::kOpenBrace);
            std::vector<Case> cases;
            bool foundDefault = false;
            size_t switchScopeStart = this->scopesInOrder.size();
            std::unordered_set<Scope *> *caseScopeMap = nullptr;

            for (; this->lexer.token != T::kCloseBrace;) {
                Expr value;
                std::vector<Stmt> body;
                logger::Loc caseLoc = this->saveExprCommentsHere();
                size_t caseScopeStart = this->scopesInOrder.size();

                if (this->lexer.token == T::kDefault) {
                    if (foundDefault) {
                        this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_MultipleDefaultClauses));
                        throw LexerPanic();
                    }
                    foundDefault = true;
                    this->lexer.Next();
                    this->lexer.Expect(T::kColon);
                } else {
                    this->lexer.Expect(T::kCase);
                    value = this->parseExpr(L::kLowest);
                    this->lexer.Expect(T::kColon);
                }

                // Keep track of any scopes created by case values. This can happen if
                // code uses anonymous functions inside a case value. For example:
                //
                //   switch (x) {
                //     case y.map(z => -z).join(':'):
                //       return y
                //   }
                //
                if (caseScopeStart < this->scopesInOrder.size()) {
                    if (caseScopeMap == nullptr) {
                        caseScopeMap = new std::unordered_set<Scope *>();
                    }
                    for (;;) {
                        caseScopeMap->insert(this->scopesInOrder[caseScopeStart].scope);
                        caseScopeStart++;
                        if (caseScopeStart == this->scopesInOrder.size()) {
                            break;
                        }
                    }
                }

                bool done = false;
                while (!done) {
                    switch (this->lexer.token) {
                    case T::kCloseBrace:
                    case T::kCase:
                    case T::kDefault:
                        done = true;
                        break;
                    default:
                        parseStmtOpts caseBodyOpts;
                        caseBodyOpts.lexicalDecl = lexicalDeclAllowAll;
                        caseBodyOpts.isCaseBody = true;
                        body.push_back(this->parseStmt(caseBodyOpts));
                    }
                }

                Case c;
                c.value_or_nil = value;
                c.body = body;
                c.loc = caseLoc;
                cases.push_back(c);
            }

            // If any case contains values that create a scope, reorder those scopes to
            // come first before any scopes created by case bodies. This reflects the
            // order in which we will visit the AST in our second parsing pass.
            if (caseScopeMap != nullptr) {
                size_t caseScopeCount = caseScopeMap->size();
                std::vector<scopeOrder> caseScopes;
                caseScopes.reserve(caseScopeCount);
                std::vector<scopeOrder> bodyScopes;
                bodyScopes.reserve(this->scopesInOrder.size() - switchScopeStart - caseScopeCount);
                for (size_t i = switchScopeStart; i < this->scopesInOrder.size(); i++) {
                    scopeOrder it = this->scopesInOrder[i];
                    if (caseScopeMap->count(it.scope) != 0) {
                        caseScopes.push_back(it);
                    } else {
                        bodyScopes.push_back(it);
                    }
                }
                std::copy(caseScopes.begin(), caseScopes.end(), this->scopesInOrder.begin() + static_cast<std::ptrdiff_t>(switchScopeStart));
                std::copy(bodyScopes.begin(), bodyScopes.end(), this->scopesInOrder.begin() + static_cast<std::ptrdiff_t>(switchScopeStart + caseScopeCount));
            }

            logger::Loc closeBraceLoc = this->lexer.Loc();
            this->lexer.Expect(T::kCloseBrace);
            std::shared_ptr<SSwitch> e = std::make_shared<SSwitch>();
            e->test = test;
            e->cases = cases;
            e->body_loc = bodyLoc;
            e->close_brace_loc = closeBraceLoc;
            return Stmt{e, loc};
        }

        case T::kTry: {
            this->lexer.Next();
            logger::Loc blockLoc = this->lexer.Loc();
            this->lexer.Expect(T::kOpenBrace);
            this->pushScopeForParsePass(ScopeKind::kBlock, loc);
            std::vector<Stmt> body = this->parseStmtsUpTo(T::kCloseBrace, parseStmtOpts());
            this->popScope();
            logger::Loc closeBraceLoc = this->lexer.Loc();
            this->lexer.Next();

            std::shared_ptr<Catch> catchOrNil;
            std::shared_ptr<Finally> finallyOrNil;

            if (this->lexer.token == T::kCatch) {
                logger::Loc catchLoc = this->lexer.Loc();
                this->pushScopeForParsePass(ScopeKind::kCatchBinding, catchLoc);
                this->lexer.Next();
                Binding bindingOrNil;

                // The catch binding is optional, and can be omitted
                if (this->lexer.token == T::kOpenBrace) {
                    if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kOptionalCatchBinding)) {
                        // Generate a new symbol for the catch binding for older browsers
                        compiler::Ref ref = this->newSymbol(compiler::SymbolKind::kOther, "e");
                        this->currentScope->generated.push_back(ref);
                        std::shared_ptr<BIdentifier> b = std::make_shared<BIdentifier>();
                        b->ref = ref;
                        bindingOrNil = Binding{b, this->lexer.Loc()};
                    }
                } else {
                    this->lexer.Expect(T::kOpenParen);
                    bindingOrNil = this->parseBinding(parseBindingOpts());

                    // Skip over types
                    if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kColon) {
                        this->lexer.Expect(T::kColon);
                        this->skipTypeScriptType(L::kLowest);
                    }

                    this->lexer.Expect(T::kCloseParen);

                    // Bare identifiers are a special case
                    compiler::SymbolKind kind = compiler::SymbolKind::kOther;
                    if (Get<BIdentifier>(bindingOrNil.data) != nullptr) {
                        kind = compiler::SymbolKind::kCatchIdentifier;
                    }
                    this->declareBinding(kind, bindingOrNil, parseStmtOpts());
                }

                blockLoc = this->lexer.Loc();
                this->lexer.Expect(T::kOpenBrace);

                this->pushScopeForParsePass(ScopeKind::kBlock, blockLoc);
                std::vector<Stmt> stmts = this->parseStmtsUpTo(T::kCloseBrace, parseStmtOpts());
                this->popScope();

                closeBraceLoc = this->lexer.Loc();
                this->lexer.Next();
                catchOrNil = std::make_shared<Catch>();
                catchOrNil->loc = catchLoc;
                catchOrNil->binding_or_nil = bindingOrNil;
                catchOrNil->block_loc = blockLoc;
                catchOrNil->block.stmts = stmts;
                catchOrNil->block.close_brace_loc = closeBraceLoc;
                this->popScope();
            }

            if (this->lexer.token == T::kFinally || catchOrNil == nullptr) {
                logger::Loc finallyLoc = this->lexer.Loc();
                this->pushScopeForParsePass(ScopeKind::kBlock, finallyLoc);
                this->lexer.Expect(T::kFinally);
                this->lexer.Expect(T::kOpenBrace);
                std::vector<Stmt> stmts = this->parseStmtsUpTo(T::kCloseBrace, parseStmtOpts());
                closeBraceLoc = this->lexer.Loc();
                this->lexer.Next();
                finallyOrNil = std::make_shared<Finally>();
                finallyOrNil->loc = finallyLoc;
                finallyOrNil->block.stmts = stmts;
                finallyOrNil->block.close_brace_loc = closeBraceLoc;
                this->popScope();
            }

            std::shared_ptr<STry> e = std::make_shared<STry>();
            e->block_loc = blockLoc;
            e->block.stmts = body;
            e->block.close_brace_loc = closeBraceLoc;
            e->catch_block = catchOrNil;
            e->finally_block = finallyOrNil;
            return Stmt{e, loc};
        }

        case T::kFor: {
            this->pushScopeForParsePass(ScopeKind::kBlock, loc);
            ParserScopePopper popper{this};

            this->lexer.Next();

            // "for await (let x of y) {}"
            logger::Range awaitRange;
            if (this->lexer.IsContextualKeyword("await")) {
                awaitRange = this->lexer.Range();
                if (this->fnOrArrowDataParse.await != allowExpr) {
                    this->log.AddError(&this->tracker, awaitRange, logger::FormatMsg(logger::MsgCat::kJS_AwaitOutsideAsync));
                    awaitRange = logger::Range();
                } else {
                    bool didGenerateError = false;
                    if (this->fnOrArrowDataParse.isTopLevel) {
                        this->topLevelAwaitKeyword = awaitRange;
                    }
                    if (!didGenerateError && compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncAwait) && compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kGenerator)) {
                        // If for-await loops aren't supported, then we only support lowering
                        // if either async/await or generators is supported. Otherwise we
                        // cannot lower for-await loops.
                        this->markSyntaxFeature(compat::JSFeature::kForAwait, awaitRange);
                    }
                }
                this->lexer.Next();
            }

            this->lexer.Expect(T::kOpenParen);

            Stmt initOrNil;
            Expr testOrNil;
            Expr updateOrNil;

            // "in" expressions aren't allowed here
            this->allowIn = false;

            logger::Range badLetRange;
            if (this->lexer.IsContextualKeyword("let")) {
                badLetRange = this->lexer.Range();
            }
            std::vector<Decl> decls;
            logger::Loc initLoc = this->lexer.Loc();
            bool isVar = false;
            switch (this->lexer.token) {
        // ---- var declaration ----
        // var is hoisted to the enclosing function/module scope.
        case T::kVar: {
                isVar = true;
                this->lexer.Next();
                decls = this->parseAndDeclareDecls(compiler::SymbolKind::kHoisted, parseStmtOpts());
                std::shared_ptr<SLocal> s = std::make_shared<SLocal>();
                s->kind = LocalKind::kVar;
                s->decls = decls;
                initOrNil = Stmt{s, initLoc};
                break;
            }

        // ---- const declaration ----
        // const declares block-scoped constants that must have initializers.
        // In TypeScript, `const enum` is also handled here.
        case T::kConst: {
                this->markSyntaxFeature(compat::JSFeature::kConstAndLet, this->lexer.Range());
                this->lexer.Next();
                decls = this->parseAndDeclareDecls(compiler::SymbolKind::kConst, parseStmtOpts());
                std::shared_ptr<SLocal> s = std::make_shared<SLocal>();
                s->kind = LocalKind::kConst;
                s->decls = decls;
                initOrNil = Stmt{s, initLoc};
                break;
            }

            case T::kSemicolon:
                break;

            default: {
                Expr expr;
                Stmt stmt;
                parseStmtOpts forInitOpts;
                forInitOpts.lexicalDecl = lexicalDeclAllowAll;
                forInitOpts.isForLoopInit = true;
                forInitOpts.isForAwaitLoopInit = awaitRange.len > 0;
                std::tuple<Expr, Stmt, std::vector<Decl>> result = this->parseExprOrLetOrUsingStmt(forInitOpts);
                expr = std::get<0>(result);
                stmt = std::get<1>(result);
                decls = std::get<2>(result);
                if (!IsNil(stmt.data)) {
                    badLetRange = logger::Range();
                    initOrNil = stmt;
                } else {
                    std::shared_ptr<SExpr> s = std::make_shared<SExpr>();
                    s->value = expr;
                    initOrNil = Stmt{s, expr.loc};
                }
            }
            }

            // "in" expressions are allowed again
            this->allowIn = true;

            // Detect for-of loops
            if (this->lexer.IsContextualKeyword("of") || awaitRange.len > 0) {
                if (badLetRange.len > 0) {
                    this->log.AddError(&this->tracker, badLetRange, logger::FormatMsg(logger::MsgCat::kJS_LetWrappedInParens));
                }
                if (awaitRange.len > 0 && !this->lexer.IsContextualKeyword("of")) {
                    if (!IsNil(initOrNil.data)) {
                        this->lexer.ExpectedString("\"of\"");
                    } else {
                        this->lexer.Unexpected();
                    }
                }
                this->forbidInitializers(decls, "of", false);
                this->markSyntaxFeature(compat::JSFeature::kForOf, this->lexer.Range());
                this->lexer.Next();
                Expr value = this->parseExpr(L::kComma);
                this->lexer.Expect(T::kCloseParen);
                bool isSingleLineBody = !this->lexer.has_newline_before && this->lexer.token != T::kOpenBrace;
                Stmt body = this->parseStmt(parseStmtOpts());
                std::shared_ptr<SForOf> e = std::make_shared<SForOf>();
                e->await = awaitRange;
                e->init = std::make_shared<Stmt>(initOrNil);
                e->value = value;
                e->body = body;
                e->is_single_line_body = isSingleLineBody;
                return Stmt{e, loc};
            }

            // Detect for-in loops
            if (this->lexer.token == T::kIn) {
                this->forbidInitializers(decls, "in", isVar);
                if (decls.size() == 1) {
                    if (auto local = Get<SLocal>(initOrNil.data); local != nullptr) {
                        if (local->kind == LocalKind::kUsing) {
                            this->log.AddError(&this->tracker, RangeOfIdentifier(this->source, initOrNil.loc), logger::FormatMsg(logger::MsgCat::kJS_UsingDeclarationsNotAllowed));
                        } else if (local->kind == LocalKind::kAwaitUsing) {
                            this->log.AddError(&this->tracker, RangeOfIdentifier(this->source, initOrNil.loc), logger::FormatMsg(logger::MsgCat::kJS_AwaitUsingDeclarationsNotAllowed));
                        }
                    }
                }
                this->lexer.Next();
                Expr value = this->parseExpr(L::kLowest);
                this->lexer.Expect(T::kCloseParen);
                bool isSingleLineBody = !this->lexer.has_newline_before && this->lexer.token != T::kOpenBrace;
                Stmt body = this->parseStmt(parseStmtOpts());
                std::shared_ptr<SForIn> e = std::make_shared<SForIn>();
                e->init = std::make_shared<Stmt>(initOrNil);
                e->value = value;
                e->body = body;
                e->is_single_line_body = isSingleLineBody;
                return Stmt{e, loc};
            }

            this->lexer.Expect(T::kSemicolon);

            // "await using" declarations are only allowed in for-of loops
            if (auto local = Get<SLocal>(initOrNil.data); local != nullptr && local->kind == LocalKind::kAwaitUsing) {
                this->log.AddError(&this->tracker, RangeOfIdentifier(this->source, initOrNil.loc), logger::FormatMsg(logger::MsgCat::kJS_AwaitUsingDeclarationsNotAllowed));
            }

            // Only require "const" statement initializers when we know we're a normal for loop
            if (auto local = Get<SLocal>(initOrNil.data); local != nullptr && (local->kind == LocalKind::kConst || local->kind == LocalKind::kUsing)) {
                this->requireInitializers(local->kind, decls);
            }

            if (this->lexer.token != T::kSemicolon) {
                testOrNil = this->parseExpr(L::kLowest);
            }

            this->lexer.Expect(T::kSemicolon);

            if (this->lexer.token != T::kCloseParen) {
                updateOrNil = this->parseExpr(L::kLowest);
            }

            this->lexer.Expect(T::kCloseParen);
            bool isSingleLineBody = !this->lexer.has_newline_before && this->lexer.token != T::kOpenBrace;
            Stmt body = this->parseStmt(parseStmtOpts());
            std::shared_ptr<SFor> e = std::make_shared<SFor>();
            e->init_or_nil = IsNil(initOrNil.data) ? nullptr : std::make_shared<Stmt>(initOrNil);
            e->test_or_nil = testOrNil;
            e->update_or_nil = updateOrNil;
            e->body = body;
            e->is_single_line_body = isSingleLineBody;
            return Stmt{e, loc};
        }

        case T::kImport: {
            logger::Range previousImportStatementKeyword = this->esmImportStatementKeyword;
            this->esmImportStatementKeyword = this->lexer.Range();
            this->lexer.Next();
            SImport stmt;
            compiler::ImportPhase phase = compiler::ImportPhase::kEvaluation;
            bool wasOriginallyBareImport = false;
            logger::Loc starLoc;

            // "export import foo = bar"
            // "import foo = bar" in a namespace
            if ((opts.isExport || (opts.isNamespaceScope && !opts.isTypeScriptDeclare)) && this->lexer.token != T::kIdentifier) {
                this->lexer.Expected(T::kIdentifier);
            }

            switch (this->lexer.token) {
            case T::kOpenParen:
            case T::kDot: {
                // "import('path')"
                // "import.meta"
                this->esmImportStatementKeyword = previousImportStatementKeyword; // This wasn't an ESM import statement after all
                Expr expr = this->parseSuffix(this->parseImportExpr(loc, L::kLowest), L::kLowest, nullptr, static_cast<exprFlag>(0));
                this->lexer.ExpectOrInsertSemicolon();
                std::shared_ptr<SExpr> e = std::make_shared<SExpr>();
                e->value = expr;
                return Stmt{e, loc};
            }

            case T::kStringLiteral:
            case T::kNoSubstitutionTemplateLiteral:
                // "import 'path'"
                if (!opts.isModuleScope && (!opts.isNamespaceScope || !opts.isTypeScriptDeclare)) {
                    this->lexer.Unexpected();
                    return Stmt();
                }

                wasOriginallyBareImport = true;
                break;

            case T::kAsterisk:
                // "import * as ns from 'path'"
                if (!opts.isModuleScope && (!opts.isNamespaceScope || !opts.isTypeScriptDeclare)) {
                    this->lexer.Unexpected();
                    return Stmt();
                }

                this->lexer.Next();
                this->lexer.ExpectContextualKeyword("as");
                stmt.namespace_ref = this->storeNameInRef(this->lexer.identifier);
                starLoc = this->lexer.Loc();
                stmt.star_name_loc = std::make_shared<logger::Loc>(starLoc);
                this->lexer.Expect(T::kIdentifier);
                this->lexer.ExpectContextualKeyword("from");
                break;

            case T::kOpenBrace: {
                // "import {item1, item2} from 'path'"
                if (!opts.isModuleScope && (!opts.isNamespaceScope || !opts.isTypeScriptDeclare)) {
                    this->lexer.Unexpected();
                    return Stmt();
                }

                std::pair<std::vector<ClauseItem>, bool> clause = this->parseImportClause();
                stmt.items = std::make_shared<std::vector<ClauseItem>>(std::get<0>(clause));
                stmt.is_single_line = std::get<1>(clause);
                this->lexer.ExpectContextualKeyword("from");
                break;
            }

            case T::kIdentifier: {
                // "import defaultItem from 'path'"
                // "import foo = bar"
                if (!opts.isModuleScope && !opts.isNamespaceScope) {
                    this->lexer.Unexpected();
                    return Stmt();
                }

                MaybeSubstring defaultName = this->lexer.identifier;
                logger::Loc defaultLoc = this->lexer.Loc();
                bool isDeferName = this->lexer.Raw() == "defer";
                bool isSourceName = this->lexer.Raw() == "source";
                this->lexer.Next();

                if (isDeferName && this->lexer.token == T::kAsterisk) {
                    // "import defer * as foo from 'bar';"
                    this->markSyntaxFeature(compat::JSFeature::kImportDefer, RangeOfIdentifier(this->source, defaultLoc));
                    phase = compiler::ImportPhase::kDefer;
                    this->lexer.Next();
                    this->lexer.ExpectContextualKeyword("as");
                    stmt.namespace_ref = this->storeNameInRef(this->lexer.identifier);
                    starLoc = this->lexer.Loc();
                    stmt.star_name_loc = std::make_shared<logger::Loc>(starLoc);
                    this->lexer.Expect(T::kIdentifier);
                    this->lexer.ExpectContextualKeyword("from");
                    break;
                }

                if (isSourceName && this->lexer.token == T::kIdentifier) {
                    if (this->lexer.Raw() == "from") {
                        MaybeSubstring nameSubstring = this->lexer.identifier;
                        logger::Loc nameLoc = this->lexer.Loc();
                        this->lexer.Next();
                        if (this->lexer.IsContextualKeyword("from")) {
                            // "import source from from 'foo';"
                            this->markSyntaxFeature(compat::JSFeature::kImportSource, RangeOfIdentifier(this->source, defaultLoc));
                            phase = compiler::ImportPhase::kSource;
                            stmt.default_name = std::make_shared<compiler::LocRef>(compiler::LocRef{nameLoc, this->storeNameInRef(nameSubstring)});
                            this->lexer.Next();
                        } else {
                            // "import source from 'foo';"
                            stmt.default_name = std::make_shared<compiler::LocRef>(compiler::LocRef{defaultLoc, this->storeNameInRef(defaultName)});
                        }
                        break;
                    }

                    // "import source foo from 'bar';"
                    this->markSyntaxFeature(compat::JSFeature::kImportSource, RangeOfIdentifier(this->source, defaultLoc));
                    phase = compiler::ImportPhase::kSource;
                    stmt.default_name = std::make_shared<compiler::LocRef>(compiler::LocRef{this->lexer.Loc(), this->storeNameInRef(this->lexer.identifier)});
                    this->lexer.Next();
                    this->lexer.ExpectContextualKeyword("from");
                    break;
                }

                stmt.default_name = std::make_shared<compiler::LocRef>(compiler::LocRef{defaultLoc, this->storeNameInRef(defaultName)});

                if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                    // Skip over type-only imports
                    if (defaultName.str == "type") {
                        switch (this->lexer.token) {
                        case T::kIdentifier: {
                            MaybeSubstring nameSubstring = this->lexer.identifier;
                            logger::Loc nameLoc = this->lexer.Loc();
                            this->lexer.Next();
                            if (this->lexer.token == T::kEquals) {
                                // "import type foo = require('bar');"
                                // "import type foo = bar.baz;"
                                opts.isTypeScriptDeclare = true;
                                return this->parseTypeScriptImportEqualsStmt(loc, opts, nameLoc, nameSubstring.str);
                            } else if (this->lexer.token == T::kStringLiteral && nameSubstring.str == "from") {
                                // "import type from 'bar';"
                                goto syntaxBeforePath;
                            } else {
                                // "import type foo from 'bar';"
                                this->lexer.ExpectContextualKeyword("from");
                                this->parsePathAndDiscard();
                                this->lexer.ExpectOrInsertSemicolon();
                                return Stmt{kSTypeScriptShared, loc};
                            }
                        }

                        case T::kAsterisk:
                            // "import type * as foo from 'bar';"
                            this->lexer.Next();
                            this->lexer.ExpectContextualKeyword("as");
                            this->lexer.Expect(T::kIdentifier);
                            this->lexer.ExpectContextualKeyword("from");
                            this->parsePathAndDiscard();
                            this->lexer.ExpectOrInsertSemicolon();
                            return Stmt{kSTypeScriptShared, loc};

                        case T::kOpenBrace:
                            // "import type {foo} from 'bar';"
                            this->parseImportClause();
                            this->lexer.ExpectContextualKeyword("from");
                            this->parsePathAndDiscard();
                            this->lexer.ExpectOrInsertSemicolon();
                            return Stmt{kSTypeScriptShared, loc};

                        default:
                            break;
                        }
                    }

                    // Parse TypeScript import assignment statements
                    if (this->lexer.token == T::kEquals || opts.isExport || (opts.isNamespaceScope && !opts.isTypeScriptDeclare)) {
                        this->esmImportStatementKeyword = previousImportStatementKeyword; // This wasn't an ESM import statement after all
                        return this->parseTypeScriptImportEqualsStmt(loc, opts, stmt.default_name->loc, defaultName.str);
                    }
                }

                if (this->lexer.token == T::kComma) {
                    this->lexer.Next();
                    switch (this->lexer.token) {
                    case T::kAsterisk:
                        // "import defaultItem, * as ns from 'path'"
                        this->lexer.Next();
                        this->lexer.ExpectContextualKeyword("as");
                        stmt.namespace_ref = this->storeNameInRef(this->lexer.identifier);
                        starLoc = this->lexer.Loc();
                        stmt.star_name_loc = std::make_shared<logger::Loc>(starLoc);
                        this->lexer.Expect(T::kIdentifier);
                        break;

                    case T::kOpenBrace: {
                        // "import defaultItem, {item1, item2} from 'path'"
                        std::pair<std::vector<ClauseItem>, bool> clause = this->parseImportClause();
                        stmt.items = std::make_shared<std::vector<ClauseItem>>(std::get<0>(clause));
                        stmt.is_single_line = std::get<1>(clause);
                        break;
                    }

                    default:
                        this->lexer.Unexpected();
                        break;
                    }
                }

                this->lexer.ExpectContextualKeyword("from");
                break;
            }

            default:
                this->lexer.Unexpected();
                return Stmt();
            }

        syntaxBeforePath: {
            std::tuple<logger::Range, std::string, compiler::ImportAssertOrWith *, compiler::ImportRecordFlags> path = this->parsePath();
            logger::Range pathLoc = std::get<0>(path);
            std::string pathText = std::get<1>(path);
            compiler::ImportAssertOrWith *assertOrWith = std::get<2>(path);
            compiler::ImportRecordFlags flags = std::get<3>(path);
            this->lexer.ExpectOrInsertSemicolon();

            // If TypeScript's "preserveValueImports": true setting is active, TypeScript's
            // "importsNotUsedAsValues": "preserve" setting is NOT active, and the import
            // clause is present and empty (or is non-empty but filled with type-only
            // items), then the import statement should still be removed entirely to match
            // the behavior of the TypeScript compiler:
            //
            //   // Keep these
            //   import 'x'
            //   import { y } from 'x'
            //   import { y, type z } from 'x'
            //
            //   // Remove these
            //   import {} from 'x'
            //   import { type y } from 'x'
            //
            //   // Remove the items from these
            //   import d, {} from 'x'
            //   import d, { type y } from 'x'
            //
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && config::TSConfigUnusedImportFlags(this->options.optionsThatSupportStructuralEquality.ts.Config) == config::TSUnusedImportFlags::kKeepValues && stmt.items != nullptr && stmt.items->size() == 0) {
                if (stmt.default_name == nullptr) {
                    return Stmt{kSTypeScriptShared, loc};
                }
                stmt.items = nullptr;
            }

            if (wasOriginallyBareImport) {
                flags = static_cast<compiler::ImportRecordFlags>(static_cast<uint16_t>(flags) | static_cast<uint16_t>(compiler::ImportRecordFlags::kWasOriginallyBareImport));
            }
            stmt.import_record_index = this->addImportRecord(compiler::ImportKind::kStmt, phase, pathLoc, pathText, std::shared_ptr<compiler::ImportAssertOrWith>(assertOrWith), flags);

            if (stmt.star_name_loc != nullptr) {
                std::string name = this->loadNameFromRef(stmt.namespace_ref);
                stmt.namespace_ref = this->declareSymbol(compiler::SymbolKind::kImport, *stmt.star_name_loc, name);
            } else {
                // Generate a symbol for the namespace
                std::string name = "import_" + GenerateNonUniqueNameFromPath(pathText);
                stmt.namespace_ref = this->newSymbol(compiler::SymbolKind::kOther, name);
                this->currentScope->generated.push_back(stmt.namespace_ref);
            }
            std::unordered_map<std::string, compiler::LocRef> itemRefs;

            // Link the default item to the namespace
            if (stmt.default_name != nullptr) {
                std::string name = this->loadNameFromRef(stmt.default_name->ref);
                compiler::Ref ref = this->declareSymbol(compiler::SymbolKind::kImport, stmt.default_name->loc, name);
                this->isImportItem[ref] = true;
                stmt.default_name->ref = ref;
            }

            // Link each import item to the namespace
            if (stmt.items != nullptr) {
                for (size_t i = 0; i < stmt.items->size(); i++) {
                    ClauseItem &item = (*stmt.items)[i];
                    std::string name = this->loadNameFromRef(item.name.ref);
                    compiler::Ref ref = this->declareSymbol(compiler::SymbolKind::kImport, item.name.loc, name);
                    this->checkForUnrepresentableIdentifier(item.alias_loc, item.alias);
                    this->isImportItem[ref] = true;
                    item.name.ref = ref;
                    itemRefs[item.alias] = compiler::LocRef{item.name.loc, ref};
                }
            }

            // Track the items for this namespace
            namespaceImportItems nsItems;
            nsItems.entries = itemRefs;
            nsItems.importRecordIndex = stmt.import_record_index;
            this->importItemsForNamespace[stmt.namespace_ref] = nsItems;

            // Import statements anywhere in the file disable top-level const
            // local prefix because import cycles can be used to trigger TDZ
            this->currentScope->is_after_const_local_prefix = true;
            return Stmt{std::make_shared<SImport>(stmt), loc};
        }
        }

        case T::kBreak: {
            this->lexer.Next();
            compiler::LocRef *name = this->parseLabelName();
            this->lexer.ExpectOrInsertSemicolon();
            std::shared_ptr<SBreak> e = std::make_shared<SBreak>();
            e->label = std::shared_ptr<compiler::LocRef>(name);
            return Stmt{e, loc};
        }

        case T::kContinue: {
            this->lexer.Next();
            compiler::LocRef *name = this->parseLabelName();
            this->lexer.ExpectOrInsertSemicolon();
            std::shared_ptr<SContinue> e = std::make_shared<SContinue>();
            e->label = std::shared_ptr<compiler::LocRef>(name);
            return Stmt{e, loc};
        }

        case T::kReturn: {
            if (this->fnOrArrowDataParse.isReturnDisallowed) {
                this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_ReturnNotUsableHere));
            }
            this->lexer.Next();
            Expr value;
            if (this->lexer.token != T::kSemicolon &&
                !this->lexer.has_newline_before &&
                this->lexer.token != T::kCloseBrace &&
                this->lexer.token != T::kEndOfFile) {
                value = this->parseExpr(L::kLowest);
            }
            this->latestReturnHadSemicolon = this->lexer.token == T::kSemicolon;
            this->lexer.ExpectOrInsertSemicolon();
            std::shared_ptr<SReturn> e = std::make_shared<SReturn>();
            e->value_or_nil = value;
            return Stmt{e, loc};
        }

        case T::kThrow: {
            this->lexer.Next();
            if (this->lexer.has_newline_before) {
                logger::Loc endLoc = logger::Loc{loc.start + 5};
                this->log.AddError(&this->tracker, logger::Range{endLoc}, logger::FormatMsg(logger::MsgCat::kJS_NewlineAfterThrow));
                std::shared_ptr<SThrow> e = std::make_shared<SThrow>();
                e->value = Expr{kENullShared, endLoc};
                return Stmt{e, loc};
            }
            Expr expr = this->parseExpr(L::kLowest);
            this->lexer.ExpectOrInsertSemicolon();
            std::shared_ptr<SThrow> e = std::make_shared<SThrow>();
            e->value = expr;
            return Stmt{e, loc};
        }

        case T::kDebugger:
            this->lexer.Next();
            this->lexer.ExpectOrInsertSemicolon();
            return Stmt{kSDebuggerShared, loc};

        case T::kOpenBrace: {
            this->pushScopeForParsePass(ScopeKind::kBlock, loc);
            ParserScopePopper popper{this};

            this->lexer.Next();
            std::vector<Stmt> stmts = this->parseStmtsUpTo(T::kCloseBrace, parseStmtOpts());
            logger::Loc closeBraceLoc = this->lexer.Loc();
            this->lexer.Next();
            std::shared_ptr<SBlock> e = std::make_shared<SBlock>();
            e->stmts = stmts;
            e->close_brace_loc = closeBraceLoc;
            return Stmt{e, loc};
        }

        default: {
            bool isIdentifier = this->lexer.token == T::kIdentifier;
            logger::Range nameRange = this->lexer.Range();
            std::string name = this->lexer.identifier.str;

            // Parse either an async function, an async expression, or a normal expression
            Expr expr;
            if (isIdentifier && this->lexer.Raw() == "async") {
                this->lexer.Next();
                if (this->lexer.token == T::kFunction && !this->lexer.has_newline_before) {
                    this->lexer.Next();
                    return this->parseFnStmt(nameRange.loc, opts, true /* isAsync */, nameRange);
                }
                expr = this->parseSuffix(this->parseAsyncPrefixExpr(nameRange, L::kLowest, static_cast<exprFlag>(0)), L::kLowest, nullptr, static_cast<exprFlag>(0));
            } else {
                Stmt stmt;
                std::tuple<Expr, Stmt, std::vector<Decl>> result = this->parseExprOrLetOrUsingStmt(opts);
                expr = std::get<0>(result);
                stmt = std::get<1>(result);
                if (!IsNil(stmt.data)) {
                    this->lexer.ExpectOrInsertSemicolon();
                    return stmt;
                }
            }

            if (isIdentifier) {
                if (auto ident = Get<EIdentifier>(expr.data); ident != nullptr) {
                    if (this->lexer.token == T::kColon && opts.deferredDecorators == nullptr) {
                        this->pushScopeForParsePass(ScopeKind::kLabel, loc);
                        ParserScopePopper popper{this};

                        // Parse a labeled statement
                        this->lexer.Next();
                        compiler::LocRef labelName{expr.loc, ident->ref};
                        parseStmtOpts nestedOpts;
                        if (opts.lexicalDecl == lexicalDeclAllowAll || opts.lexicalDecl == lexicalDeclAllowFnInsideLabel) {
                            nestedOpts.lexicalDecl = lexicalDeclAllowFnInsideLabel;
                        }
                        bool isSingleLineStmt = !this->lexer.has_newline_before && this->lexer.token != T::kOpenBrace;
                        Stmt stmt = this->parseStmt(nestedOpts);
                        std::shared_ptr<SLabel> e = std::make_shared<SLabel>();
                        e->name = labelName;
                        e->stmt = stmt;
                        e->is_single_line_stmt = isSingleLineStmt;
                        return Stmt{e, loc};
                    }

                    if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                        if (name == "type") {
                            if (!this->lexer.has_newline_before && this->lexer.token == T::kIdentifier) {
                                // "type Foo = any"
                                parseStmtOpts tssOpts;
                                tssOpts.isModuleScope = opts.isModuleScope;
                                this->skipTypeScriptTypeStmt(tssOpts);
                                return Stmt{kSTypeScriptShared, loc};
                            }
                        } else if (name == "namespace" || name == "module") {
                            // "namespace Foo {}"
                            // "module Foo {}"
                            // "declare module 'fs' {}"
                            // "declare module 'fs';"
                            if (!this->lexer.has_newline_before && (opts.isModuleScope || opts.isNamespaceScope) && (this->lexer.token == T::kIdentifier ||
                                (this->lexer.token == T::kStringLiteral && opts.isTypeScriptDeclare))) {
                                return this->parseTypeScriptNamespaceStmt(loc, opts);
                            }
                        } else if (name == "interface") {
                            // "interface Foo {}"
                            // "export default interface Foo {}"
                            // "export default interface \n Foo {}"
                            if (!this->lexer.has_newline_before || opts.isExportDefault) {
                                parseStmtOpts tssOpts;
                                tssOpts.isModuleScope = opts.isModuleScope;
                                this->skipTypeScriptInterfaceStmt(tssOpts);
                                return Stmt{kSTypeScriptShared, loc};
                            }

                            // "interface \n Foo {}"
                            // "export interface \n Foo {}"
                            if (opts.isExport) {
                                this->log.AddError(&this->tracker, nameRange, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedInterface));
                                throw LexerPanic();
                            }
                        } else if (name == "abstract") {
                            if (!this->lexer.has_newline_before && this->lexer.token == T::kClass) {
                                return this->parseClassStmt(loc, opts);
                            }
                        } else if (name == "global") {
                            // "declare module 'fs' { global { namespace NodeJS {} } }"
                            if (opts.isNamespaceScope && opts.isTypeScriptDeclare && this->lexer.token == T::kOpenBrace) {
                                this->lexer.Next();
                                this->parseStmtsUpTo(T::kCloseBrace, opts);
                                this->lexer.Next();
                                return Stmt{kSTypeScriptShared, loc};
                            }
                        } else if (name == "declare") {
                            if (!this->lexer.has_newline_before) {
                                opts.lexicalDecl = lexicalDeclAllowAll;
                                opts.isTypeScriptDeclare = true;

                                // "declare global { ... }"
                                if (this->lexer.IsContextualKeyword("global")) {
                                    this->lexer.Next();
                                    this->lexer.Expect(T::kOpenBrace);
                                    this->parseStmtsUpTo(T::kCloseBrace, opts);
                                    this->lexer.Next();
                                    return Stmt{kSTypeScriptShared, loc};
                                }

                                // "declare const x: any"
                                int scopeIndex = static_cast<int>(this->scopesInOrder.size());
                                Lexer oldLexer = this->lexer;
                                Stmt stmt = this->parseStmt(opts);
                                S typeDeclarationData = kSTypeScriptShared;
                                if (Get<SEmpty>(stmt.data) != nullptr) {
                                    std::shared_ptr<SExpr> sv = std::make_shared<SExpr>();
                                    sv->value = expr;
                                    return Stmt{sv, loc};
                                } else if (Get<STypeScript>(stmt.data) != nullptr) {
                                    // Type declarations are expected. Propagate the "declare class"
                                    // status in case our caller is a decorator that needs to know
                                    // this was a "declare class" statement.
                                    typeDeclarationData = stmt.data;
                                } else if (Get<SLocal>(stmt.data) == nullptr) {
                                    // Anything that we don't expect is a syntax error. For example,
                                    // we consider this a syntax error:
                                    //
                                    //   declare let declare: any, foo: any
                                    //   declare foo
                                    //
                                    // Strangely TypeScript allows this code starting with version
                                    // 4.4, but I assume this is a bug. This bug was reported here:
                                    // https://github.com/microsoft/TypeScript/issues/54602
                                    this->lexer = oldLexer;
                                    this->lexer.Unexpected();
                                }
                                this->discardScopesUpTo(scopeIndex);

                                // Unlike almost all uses of "declare", statements that use
                                // "export declare" with "var/let/const" inside a namespace affect
                                // code generation. They cause any declared bindings to be
                                // considered exports of the namespace. Identifier references to
                                // those names must be converted into property accesses off the
                                // namespace object:
                                //
                                //   namespace ns {
                                //     export declare const x
                                //     export function y() { return x }
                                //   }
                                //
                                //   (ns as any).x = 1
                                //   console.log(ns.y())
                                //
                                // In this example, "return x" must be replaced with "return ns.x".
                                // This is handled by replacing each "export declare" statement
                                // inside a namespace with an "export var" statement containing all
                                // of the declared bindings. That "export var" statement will later
                                // cause identifiers to be transformed into property accesses.
                                if (opts.isNamespaceScope && opts.isExport) {
                                    std::vector<Decl> decls;
                                    if (auto s = Get<SLocal>(stmt.data); s != nullptr) {
                                        ForEachIdentifierBindingInDecls(s->decls, [&](logger::Loc bindingLoc, BIdentifier &b) {
                                            Decl decl;
                                            decl.binding.loc = bindingLoc;
                                            decl.binding.data = std::make_shared<BIdentifier>(b);
                                            decls.push_back(decl);
                                        });
                                    }
                                    if (decls.size() > 0) {
                                        std::shared_ptr<SLocal> sl = std::make_shared<SLocal>();
                                        sl->kind = LocalKind::kVar;
                                        sl->is_export = true;
                                        sl->decls = decls;
                                        return Stmt{sl, loc};
                                    }
                                }

                                return Stmt{typeDeclarationData, loc};
                            }
                        }
                    }
                }
            }

            this->lexer.ExpectOrInsertSemicolon();
            std::shared_ptr<SExpr> e = std::make_shared<SExpr>();
            e->value = expr;
            return Stmt{e, loc};
        }
        }
    }

    std::vector<Stmt> Parser::parseStmtsUpTo(T token, parseStmtOpts opts) {
        std::vector<Stmt> stmts;
        int32_t returnWithoutSemicolonStart = -1;
        opts.lexicalDecl = lexicalDeclAllowAll;
        bool isDirectivePrologue = opts.allowDirectivePrologue;

        for (;;) {
            // Preserve some statement-level comments
            const auto &comments = this->lexer.legal_comments_before_token;
            if (comments.size() > 0) {
                for (const auto &comment : comments) {
                    std::shared_ptr<SComment> s = std::make_shared<SComment>();
                    s->text = this->source.CommentTextWithoutIndent(comment);
                    s->is_legal_comment = true;
                    stmts.push_back(Stmt{s, comment.loc});
                }
            }

            if (this->lexer.token == token) {
                break;
            }

            Stmt stmt = this->parseStmt(opts);

            // Skip TypeScript types entirely
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                if (Get<STypeScript>(stmt.data) != nullptr) {
                    continue;
                }
            }

            // Parse one or more directives at the beginning
            if (isDirectivePrologue) {
                isDirectivePrologue = false;
                if (auto expr = Get<SExpr>(stmt.data); expr != nullptr) {
                    if (auto str = Get<EString>(expr->value.data); str != nullptr && !str->prefer_template) {
                        std::shared_ptr<SDirective> directive = std::make_shared<SDirective>();
                        directive->value = str->value;
                        directive->legacy_octal_loc = str->legacy_octal_loc;

                        // Snapshot the string value and location before "stmt.data" is
                        // replaced below, since the replacement releases the EString
                        // and SExpr that "str" and "expr" point into.
                        std::u16string strValue = str->value;
                        logger::Loc useStrictLoc = expr->value.loc;
                        bool isUseStrict = helpers::UTF16EqualsString(strValue, "use strict");
                        bool isUseAsm = !isUseStrict && helpers::UTF16EqualsString(strValue, "use asm");

                        stmt.data = directive;
                        isDirectivePrologue = true;

                        if (isUseStrict) {
                            // Track "use strict" directives
                            this->currentScope->strict_mode = StrictModeKind::kExplicitStrictMode;
                            this->currentScope->use_strict_loc = useStrictLoc;

                            // Inside a function, strict mode actually propagates from the child
                            // scope to the parent scope:
                            //
                            //   // This is a syntax error
                            //   function fn(arguments) {
                            //     "use strict";
                            //   }
                            //
                            if (this->currentScope->kind == ScopeKind::kFunctionBody &&
                                this->currentScope->parent->kind == ScopeKind::kFunctionArgs &&
                                this->currentScope->parent->strict_mode == StrictModeKind::kSloppyMode) {
                                this->currentScope->parent->strict_mode = StrictModeKind::kExplicitStrictMode;
                                this->currentScope->parent->use_strict_loc = useStrictLoc;
                            }
                        } else if (isUseAsm) {
                            // Deliberately remove "use asm" directives. The asm.js subset of
                            // JavaScript has complicated validation rules that are triggered
                            // by this directive. This parser is not designed with asm.js in
                            // mind and round-tripping asm.js code through guchho will very
                            // likely cause it to no longer validate as asm.js. When this
                            // happens, V8 prints a warning and people don't like seeing the
                            // warning.
                            //
                            // We deliberately do not attempt to preserve the validity of
                            // asm.js code because it's a complicated legacy format and it's
                            // obsolete now that WebAssembly exists. By removing this directive
                            // it will just become normal JavaScript, which will work fine and
                            // won't generate a warning (but will run slower). We don't generate
                            // a warning ourselves in this case because there isn't necessarily
                            // anything easy and actionable that the user can do to fix this.
                            stmt.data = std::make_shared<SEmpty>();
                        }
                    }
                }
            }

            stmts.push_back(stmt);

            // Warn about ASI and return statements. Here's an example of code with
            // this problem: https://github.com/rollup/rollup/issues/3729
            if (!this->suppressWarningsAboutWeirdCode) {
                bool isBareReturn = false;
                if (auto s = Get<SReturn>(stmt.data); s != nullptr && IsNil(s->value_or_nil.data) && !this->latestReturnHadSemicolon) {
                    returnWithoutSemicolonStart = stmt.loc.start;
                    isBareReturn = true;
                }
                if (!isBareReturn) {
                    if (returnWithoutSemicolonStart != -1) {
                        if (Get<SExpr>(stmt.data) != nullptr) {
                        this->log.AddID(logger::MsgID::kJS_SemicolonAfterReturn, logger::MsgKind::kWarning, &this->tracker, logger::Range{logger::Loc{returnWithoutSemicolonStart + 6}},
                            logger::FormatMsg(logger::MsgCat::kJS_ExpressionNotReturned));
                        }
                    }
                    returnWithoutSemicolonStart = -1;
                }
            }
        }

        return stmts;
    }

    Class Parser::parseClass(logger::Range classKeyword, compiler::LocRef *name, parseClassOpts classOpts) {
        Expr extendsOrNil;

        if (this->lexer.token == T::kExtends) {
            this->lexer.Next();
            extendsOrNil = this->parseExpr(L::kNew);

            // TypeScript's type argument parser inside expressions backtracks if the
            // first token after the end of the type parameter list is "{", so the
            // parsed expression above will have backtracked if there are any type
            // arguments. This means we have to re-parse for any type arguments here.
            // This seems kind of wasteful to me but it's what the official compiler
            // does and it probably doesn't have that high of a performance overhead
            // because "extends" clauses aren't that frequent, so it should be ok.
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                skipTypeScriptTypeArgumentsOpts typeOpts;
                this->skipTypeScriptTypeArguments(typeOpts);
            }
        }

        if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.IsContextualKeyword("implements")) {
            this->lexer.Next();
            for (;;) {
                this->skipTypeScriptType(L::kLowest);
                if (this->lexer.token != T::kComma) {
                    break;
                }
                this->lexer.Next();
            }
        }

        logger::Loc bodyLoc = this->lexer.Loc();
        this->lexer.Expect(T::kOpenBrace);
        std::vector<Property> properties;
        bool hasPropertyDecorator = false;

        // Allow "in" and private fields inside class bodies
        bool oldAllowIn = this->allowIn;
        this->allowIn = true;

        // A scope is needed for private identifiers
        int scopeIndex = this->pushScopeForParsePass(ScopeKind::kClassBody, bodyLoc);

        propertyOpts opts;
        opts.isClass = true;
        opts.decoratorScope = this->currentScope;
        opts.decoratorContext = classOpts.decoratorContext;
        opts.classHasExtends = !IsNil(extendsOrNil.data);
        opts.classKeyword = classKeyword;
        bool hasConstructor = false;

        while (this->lexer.token != T::kCloseBrace) {
            if (this->lexer.token == T::kSemicolon) {
                this->lexer.Next();
                continue;
            }

            // Parse decorators for this property
            logger::Loc firstDecoratorLoc = this->lexer.Loc();
            int innerScopeIndex = static_cast<int>(this->scopesInOrder.size());
            opts.decorators = this->parseDecorators(this->currentScope, classKeyword, opts.decoratorContext);
            if (!opts.decorators.empty()) {
                hasPropertyDecorator = true;
            }

            // This property may turn out to be a type in TypeScript, which should be ignored
            std::pair<Property, bool> parsed = this->parseProperty(this->saveExprCommentsHere(), PropertyKind::kField, opts, nullptr);
            if (parsed.second) {
                Property property = parsed.first;
                properties.push_back(property);

                // Forbid decorators on class constructors
                if (auto key = Get<EString>(property.key.data)) {
                    if (helpers::UTF16EqualsString(key->value, "constructor")) {
                        if (!opts.decorators.empty()) {
                            logger::Range r;
                            r.loc = firstDecoratorLoc;
                            this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_DecoratorsOnConstructors));
                        }
                        if (IsMethodDefinition(property.kind) && !Has(property.flags, PropertyFlags::kIsStatic) && !Has(property.flags, PropertyFlags::kIsComputed)) {
                            if (hasConstructor) {
                                this->log.AddError(&this->tracker, RangeOfIdentifier(this->source, property.key.loc), logger::FormatMsg(logger::MsgCat::kJS_MultipleConstructors));
                            }
                            hasConstructor = true;
                        }
                    }
                }
            } else if (!classOpts.isTypeScriptDeclare && !opts.decorators.empty()) {
                logger::Range r;
                r.loc = firstDecoratorLoc;
                r.len = 1;
                this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_DecoratorsNotValidHere));
                this->discardScopesUpTo(innerScopeIndex);
            }
        }

        // Discard the private identifier scope inside a TypeScript "declare class"
        if (classOpts.isTypeScriptDeclare) {
            this->popAndDiscardScope(scopeIndex);
        } else {
            this->popScope();
        }

        this->allowIn = oldAllowIn;

        logger::Loc closeBraceLoc = this->saveExprCommentsHere();
        this->lexer.Expect(T::kCloseBrace);

        // TypeScript has legacy behavior that uses assignment semantics instead of
        // define semantics for class fields when "useDefineForClassFields" is enabled
        // (in which case TypeScript behaves differently than JavaScript, which is
        // arguably "wrong").
        //
        // This legacy behavior exists because TypeScript added class fields to
        // TypeScript before they were added to JavaScript. They decided to go with
        // assignment semantics for whatever reason. Later on TC39 decided to go with
        // define semantics for class fields instead. This behaves differently if the
        // base class has a setter with the same name.
        //
        // The value of "useDefineForClassFields" defaults to false when it's not
        // specified and the target is earlier than "ES2022" since the class field
        // language feature was added in ES2022. However, TypeScript's "target"
        // setting currently defaults to "ES3" which unfortunately means that the
        // "useDefineForClassFields" setting defaults to false (i.e. to "wrong").
        //
        // We default "useDefineForClassFields" to true (i.e. to "correct") instead.
        // This is partially because our target defaults to "esnext", and partially
        // because this is a legacy behavior that no one should be using anymore.
        // Users that want the wrong behavior can either set "useDefineForClassFields"
        // to false in "tsconfig.json" explicitly, or set TypeScript's "target" to
        // "ES2021" or earlier in their in "tsconfig.json" file.
        bool useDefineForClassFields = !this->options.optionsThatSupportStructuralEquality.ts.Parse || this->options.optionsThatSupportStructuralEquality.ts.Config.UseDefineForClassFields == config::MaybeBool::kTrue ||
            (this->options.optionsThatSupportStructuralEquality.ts.Config.UseDefineForClassFields == config::MaybeBool::kUnspecified && this->options.optionsThatSupportStructuralEquality.ts.Config.Target != config::TSTarget::kBelowES2022);

        Class cls;
        cls.class_keyword = classKeyword;
        cls.decorators = classOpts.decorators;
        cls.name = std::shared_ptr<compiler::LocRef>(name);
        cls.extends_or_nil = extendsOrNil;
        cls.body_loc = bodyLoc;
        cls.properties = properties;
        cls.close_brace_loc = closeBraceLoc;

        // Always lower standard decorators if they are present and TypeScript's
        // "useDefineForClassFields" setting is false even if the configured target
        // environment supports decorators. This setting changes the behavior of
        // class fields, and so we must lower decorators so they behave correctly.
        cls.should_lower_standard_decorators = (!classOpts.decorators.empty() || hasPropertyDecorator) &&
            ((!this->options.optionsThatSupportStructuralEquality.ts.Parse && compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kDecorators)) ||
                (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->options.optionsThatSupportStructuralEquality.ts.Config.ExperimentalDecorators != config::MaybeBool::kTrue &&
                    (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kDecorators) || !useDefineForClassFields)));

        cls.use_define_for_class_fields = useDefineForClassFields;
        return cls;
    }

    Stmt Parser::parseClassStmt(logger::Loc loc, parseStmtOpts opts) {
        compiler::LocRef *name = nullptr;
        logger::Range classKeyword = this->lexer.Range();
        if (this->lexer.token == T::kClass) {
            this->markSyntaxFeature(compat::JSFeature::kClass, classKeyword);
            this->lexer.Next();
        } else {
            this->lexer.Expected(T::kClass);
        }

        if (!opts.isNameOptional || (this->lexer.token == T::kIdentifier && (!this->options.optionsThatSupportStructuralEquality.ts.Parse || this->lexer.identifier.str != "implements"))) {
            logger::Loc nameLoc = this->lexer.Loc();
            std::string nameText = this->lexer.identifier.str;
            this->lexer.Expect(T::kIdentifier);
            if (this->fnOrArrowDataParse.await != allowIdent && nameText == "await") {
                this->log.AddError(&this->tracker, RangeOfIdentifier(this->source, nameLoc), logger::FormatMsg(logger::MsgCat::kJS_AwaitAsIdentifier));
            }
            name = new compiler::LocRef{nameLoc, compiler::kInvalidRef};
            if (!opts.isTypeScriptDeclare) {
                name->ref = this->declareSymbol(compiler::SymbolKind::kClass, nameLoc, nameText);
            }
        }

        // Even anonymous classes can have TypeScript type parameters
        if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
            this->skipTypeScriptTypeParameters(static_cast<typeParameterFlags>(static_cast<uint8_t>(allowInOutVarianceAnnotations) | static_cast<uint8_t>(allowConstModifier)));
        }

        parseClassOpts classOpts;
        classOpts.isTypeScriptDeclare = opts.isTypeScriptDeclare;
        if (opts.deferredDecorators != nullptr) {
            classOpts.decorators = opts.deferredDecorators->decorators;
        }
        int scopeIndex = this->pushScopeForParsePass(ScopeKind::kClassName, loc);
        Class cls = this->parseClass(classKeyword, name, classOpts);

        if (opts.isTypeScriptDeclare) {
            this->popAndDiscardScope(scopeIndex);

            if (opts.isNamespaceScope && opts.isExport) {
                this->hasNonLocalExportDeclareInsideNamespace = true;
            }

            // Remember that this was a "declare class" so we can allow decorators on it
            return Stmt{kSTypeScriptSharedWasDeclareClass, loc};
        }

        this->popScope();
        std::shared_ptr<SClass> s = std::make_shared<SClass>();
        s->class_ = cls;
        s->is_export = opts.isExport;
        return Stmt{s, loc};
    }

    Expr Parser::parseClassExpr(std::vector<Decorator> decorators) {
        logger::Range classKeyword = this->lexer.Range();
        this->markSyntaxFeature(compat::JSFeature::kClass, classKeyword);
        this->lexer.Expect(T::kClass);
        compiler::LocRef *name = nullptr;

        parseClassOpts opts;
        opts.decorators = decorators;
        opts.decoratorContext = decoratorInClassExpr;
        this->pushScopeForParsePass(ScopeKind::kClassName, classKeyword.loc);

        // Parse an optional class name
        if (this->lexer.token == T::kIdentifier) {
            std::string nameText = this->lexer.identifier.str;
            if (!this->options.optionsThatSupportStructuralEquality.ts.Parse || nameText != "implements") {
                if (this->fnOrArrowDataParse.await != allowIdent && nameText == "await") {
                    this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_AwaitAsIdentifier));
                }
                name = new compiler::LocRef{this->lexer.Loc(), this->newSymbol(compiler::SymbolKind::kOther, nameText)};
                this->lexer.Next();
            }
        }

        // Even anonymous classes can have TypeScript type parameters
        if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
            this->skipTypeScriptTypeParameters(static_cast<typeParameterFlags>(static_cast<uint8_t>(allowInOutVarianceAnnotations) | static_cast<uint8_t>(allowConstModifier)));
        }

        Class cls = this->parseClass(classKeyword, name, opts);

        this->popScope();
        std::shared_ptr<EClass> e = std::make_shared<EClass>();
        e->class_ = cls;
        return Expr{e, classKeyword.loc};
    }

    std::pair<Fn, bool> Parser::parseFn(compiler::LocRef *name, logger::Range classKeyword, decoratorContextFlags decoratorContext, struct fnOrArrowDataParse data) {
        Fn fn;
        bool hadBody = false;
        fn.name = std::shared_ptr<compiler::LocRef>(name);
        fn.has_rest_arg = false;
        fn.is_async = data.await == allowExpr;
        fn.is_generator = data.yield == allowExpr;
        fn.arguments_ref = compiler::kInvalidRef;
        fn.open_paren_loc = this->lexer.Loc();
        this->lexer.Expect(T::kOpenParen);

        // Await and yield are not allowed in function arguments
        struct fnOrArrowDataParse oldFnOrArrowData = this->fnOrArrowDataParse;
        if (data.await == allowExpr) {
            this->fnOrArrowDataParse.await = forbidAll;
        } else {
            this->fnOrArrowDataParse.await = allowIdent;
        }
        if (data.yield == allowExpr) {
            this->fnOrArrowDataParse.yield = forbidAll;
        } else {
            this->fnOrArrowDataParse.yield = allowIdent;
        }

        // Don't suggest inserting "async" before anything if "await" is found
        this->fnOrArrowDataParse.needsAsyncLoc.start = -1;

        // If "super" is allowed in the body, it's allowed in the arguments
        this->fnOrArrowDataParse.allowSuperCall = data.allowSuperCall;
        this->fnOrArrowDataParse.allowSuperProperty = data.allowSuperProperty;

        while (this->lexer.token != T::kCloseParen) {
            // Skip over "this" type annotations
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kThis) {
                this->lexer.Next();
                if (this->lexer.token == T::kColon) {
                    this->lexer.Next();
                    this->skipTypeScriptType(L::kLowest);
                }
                if (this->lexer.token != T::kComma) {
                    break;
                }
                this->lexer.Next();
                continue;
            }

            std::vector<Decorator> decorators;
            if (data.decoratorScope != nullptr) {
                awaitOrYield oldAwait = this->fnOrArrowDataParse.await;
                logger::Loc oldNeedsAsyncLoc = this->fnOrArrowDataParse.needsAsyncLoc;

                // While TypeScript parameter decorators are expressions, they are not
                // evaluated where they exist in the code. They are moved to after the
                // class declaration and evaluated there instead. Specifically this
                // TypeScript code:
                //
                //   class Foo {
                //     foo(@bar() baz) {}
                //   }
                //
                // becomes this JavaScript code:
                //
                //   class Foo {
                //     foo(baz) {}
                //   }
                //   __decorate([
                //     __param(0, bar())
                //   ], Foo.prototype, "foo", null);
                //
                // One consequence of this is that whether "await" is allowed or not
                // depends on whether the class declaration itself is inside an "async"
                // function or not. The TypeScript compiler allows code that does this:
                //
                //   async function fn(foo) {
                //     class Foo {
                //       foo(@bar(await foo) baz) {}
                //     }
                //     return Foo
                //   }
                //
                // because that becomes the following valid JavaScript:
                //
                //   async function fn(foo) {
                //     class Foo {
                //       foo(baz) {}
                //     }
                //     __decorate([
                //       __param(0, bar(await foo))
                //     ], Foo.prototype, "foo", null);
                //     return Foo;
                //   }
                //
                if (oldFnOrArrowData.await == allowExpr) {
                    this->fnOrArrowDataParse.await = allowExpr;
                } else {
                    this->fnOrArrowDataParse.needsAsyncLoc = oldFnOrArrowData.needsAsyncLoc;
                }

                decorators = this->parseDecorators(data.decoratorScope, classKeyword, static_cast<decoratorContextFlags>(decoratorContext | decoratorInFnArgs));

                this->fnOrArrowDataParse.await = oldAwait;
                this->fnOrArrowDataParse.needsAsyncLoc = oldNeedsAsyncLoc;
            }

            if (!fn.has_rest_arg && this->lexer.token == T::kDotDotDot) {
                this->markSyntaxFeature(compat::JSFeature::kRestArgument, this->lexer.Range());
                this->lexer.Next();
                fn.has_rest_arg = true;
            }

            bool isTypeScriptCtorField = false;
            bool isIdentifier = this->lexer.token == T::kIdentifier;
            std::string text = this->lexer.identifier.str;
            parseBindingOpts bindingOpts;
            Binding binding = this->parseBinding(bindingOpts);

            if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                // Skip over TypeScript accessibility modifiers, which turn this argument
                // into a class field when used inside a class constructor. This is known
                // as a "parameter property" in TypeScript.
                if (isIdentifier && data.isConstructor) {
                    while (this->lexer.token == T::kIdentifier || this->lexer.token == T::kOpenBrace || this->lexer.token == T::kOpenBracket) {
                        if (text != "public" && text != "private" && text != "protected" && text != "readonly" && text != "override") {
                            break;
                        }
                        isTypeScriptCtorField = true;

                        // TypeScript requires an identifier binding
                        if (this->lexer.token != T::kIdentifier) {
                            this->lexer.Expect(T::kIdentifier);
                        }
                        text = this->lexer.identifier.str;

                        // Re-parse the binding (the current binding is the TypeScript keyword)
                        parseBindingOpts bindingOpts2;
                        binding = this->parseBinding(bindingOpts2);
                    }
                }

                // "function foo(a?) {}"
                if (this->lexer.token == T::kQuestion) {
                    this->lexer.Next();
                }

                // "function foo(a: any) {}"
                if (this->lexer.token == T::kColon) {
                    this->lexer.Next();
                    this->skipTypeScriptType(L::kLowest);
                }
            }

            parseStmtOpts declOpts;
            this->declareBinding(compiler::SymbolKind::kHoisted, binding, declOpts);

            Expr defaultValueOrNil;
            if (!fn.has_rest_arg && this->lexer.token == T::kEquals) {
                this->markSyntaxFeature(compat::JSFeature::kDefaultArgument, this->lexer.Range());
                this->lexer.Next();
                defaultValueOrNil = this->parseExpr(L::kComma);
            }

            Arg arg;
            arg.decorators = decorators;
            arg.binding = binding;
            arg.default_or_nil = defaultValueOrNil;

            // We need to track this because it affects code generation
            arg.is_typescript_ctor_field = isTypeScriptCtorField;

            fn.args.push_back(arg);

            if (this->lexer.token != T::kComma) {
                break;
            }
            if (fn.has_rest_arg) {
                // JavaScript does not allow a comma after a rest argument
                if (data.isTypeScriptDeclare) {
                    // TypeScript does allow a comma after a rest argument in a "declare" context
                    this->lexer.Next();
                } else {
                    this->lexer.Expect(T::kCloseParen);
                }
                break;
            }
            this->lexer.Next();
        }

        // Reserve the special name "arguments" in this scope. This ensures that it
        // shadows any variable called "arguments" in any parent scopes. But only do
        // this if it wasn't already declared above because arguments are allowed to
        // be called "arguments", in which case the real "arguments" is inaccessible.
        if (this->currentScope->members.count("arguments") == 0) {
            fn.arguments_ref = this->declareSymbol(compiler::SymbolKind::kArguments, fn.open_paren_loc, "arguments");
            this->symbols[fn.arguments_ref.inner_index].flags = compiler::SymbolFlags(this->symbols[fn.arguments_ref.inner_index].flags | compiler::SymbolFlags::kMustNotBeRenamed);
        }

        this->lexer.Expect(T::kCloseParen);
        this->fnOrArrowDataParse = oldFnOrArrowData;

        // "function foo(): any {}"
        if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kColon) {
            this->lexer.Next();
            this->skipTypeScriptReturnType();
        }

        // "function foo(): any;"
        if (data.allowMissingBodyForTypeScript && this->lexer.token != T::kOpenBrace) {
            this->lexer.ExpectOrInsertSemicolon();
            return std::make_pair(fn, hadBody);
        }

        fn.body = this->parseFnBody(data);
        hadBody = true;
        return std::make_pair(fn, hadBody);
    }


    Stmt Parser::parseFnStmt(logger::Loc loc, parseStmtOpts opts, bool isAsync, logger::Range asyncRange) {
        bool isGenerator = this->lexer.token == T::kAsterisk;
        bool hasError = false;
        if (isAsync) {
            hasError = this->markAsyncFn(asyncRange, isGenerator);
        }
        if (isGenerator) {
            if (!hasError) {
                this->markSyntaxFeature(compat::JSFeature::kGenerator, this->lexer.Range());
            }
            this->lexer.Next();
        }

        switch (opts.lexicalDecl) {
        case lexicalDeclForbid:
            this->forbidLexicalDecl(loc);
            break;

        // Allow certain function statements in certain single-statement contexts
        case lexicalDeclAllowFnInsideIf:
        case lexicalDeclAllowFnInsideLabel:
            if (opts.isTypeScriptDeclare || isGenerator || isAsync) {
                this->forbidLexicalDecl(loc);
            }
            break;

        case lexicalDeclAllowAll:
        case lexicalDeclAllowInCase:
            break;
        }

        compiler::LocRef *name = nullptr;
        std::string nameText;

        // The name is optional for "export default function() {}" pseudo-statements
        if (!opts.isNameOptional || this->lexer.token == T::kIdentifier) {
            logger::Loc nameLoc = this->lexer.Loc();
            nameText = this->lexer.identifier.str;
            if (!isAsync && this->fnOrArrowDataParse.await != allowIdent && nameText == "await") {
                this->log.AddError(&this->tracker, RangeOfIdentifier(this->source, nameLoc), logger::FormatMsg(logger::MsgCat::kJS_AwaitAsIdentifier));
            }
            this->lexer.Expect(T::kIdentifier);
            name = new compiler::LocRef{nameLoc, compiler::kInvalidRef};
        }

        // Even anonymous functions can have TypeScript type parameters
        if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
            this->skipTypeScriptTypeParameters(allowConstModifier);
        }

        // Introduce a fake block scope for function declarations inside if statements
        int ifStmtScopeIndex = 0;
        bool hasIfScope = opts.lexicalDecl == lexicalDeclAllowFnInsideIf;
        if (hasIfScope) {
            ifStmtScopeIndex = this->pushScopeForParsePass(ScopeKind::kBlock, loc);
        }

        int scopeIndex = this->pushScopeForParsePass(ScopeKind::kFunctionArgs, this->lexer.Loc());

        awaitOrYield await = allowIdent;
        awaitOrYield yield = allowIdent;
        if (isAsync) {
            await = allowExpr;
        }
        if (isGenerator) {
            yield = allowExpr;
        }

        struct fnOrArrowDataParse data;
        data.needsAsyncLoc = loc;
        data.asyncRange = asyncRange;
        data.await = await;
        data.yield = yield;
        data.isTypeScriptDeclare = opts.isTypeScriptDeclare;

        // Only allow omitting the body if we're parsing TypeScript
        data.allowMissingBodyForTypeScript = this->options.optionsThatSupportStructuralEquality.ts.Parse;

        std::pair<Fn, bool> parsedFn = this->parseFn(name, logger::Range(), static_cast<decoratorContextFlags>(0), data);
        Fn fn = parsedFn.first;
        bool hadBody = parsedFn.second;

        // Don't output anything if it's just a forward declaration of a function
        if (opts.isTypeScriptDeclare || !hadBody) {
            this->popAndDiscardScope(scopeIndex);

            // Balance the fake block scope introduced above
            if (hasIfScope) {
                this->popAndDiscardScope(ifStmtScopeIndex);
            }

            if (opts.isTypeScriptDeclare && opts.isNamespaceScope && opts.isExport) {
                this->hasNonLocalExportDeclareInsideNamespace = true;
            }

            return Stmt{kSTypeScriptShared, loc};
        }

        this->popScope();

        // Only declare the function after we know if it had a body or not. Otherwise
        // TypeScript code such as this will double-declare the symbol:
        //
        //     function foo(): void;
        //     function foo(): void {}
        //
        if (name != nullptr) {
            compiler::SymbolKind kind = compiler::SymbolKind::kHoistedFunction;
            if (isGenerator || isAsync) {
                kind = compiler::SymbolKind::kGeneratorOrAsyncFunction;
            }
            name->ref = this->declareSymbol(kind, name->loc, nameText);
        }

        // Balance the fake block scope introduced above
        if (hasIfScope) {
            this->popScope();
        }

        fn.has_if_scope = hasIfScope;
        this->validateFunctionName(fn, fnStmt);
        if (opts.hasNoSideEffectsComment && !this->options.optionsThatSupportStructuralEquality.ignoreDCEAnnotations) {
            fn.has_no_side_effects_comment = true;
            if (name != nullptr && !opts.isTypeScriptDeclare) {
                this->symbols[name->ref.inner_index].flags = compiler::SymbolFlags(this->symbols[name->ref.inner_index].flags | compiler::SymbolFlags::kCallCanBeUnwrappedIfUnused);
            }
        }
        std::shared_ptr<SFunction> s = std::make_shared<SFunction>();
        s->fn = fn;
        s->is_export = opts.isExport;
        return Stmt{s, loc};
    }    

    FnBody Parser::parseFnBody(struct fnOrArrowDataParse data) {
        struct fnOrArrowDataParse oldFnOrArrowData = this->fnOrArrowDataParse;
        bool oldAllowIn = this->allowIn;
        this->fnOrArrowDataParse = data;
        this->allowIn = true;

        logger::Loc loc = this->lexer.Loc();
        this->pushScopeForParsePass(ScopeKind::kFunctionBody, loc);
        struct ScopePopper {
            Parser *p;
            ~ScopePopper() { p->popScope(); }
        } popper{this};

        this->lexer.Expect(T::kOpenBrace);
        parseStmtOpts opts;
        opts.allowDirectivePrologue = true;
        std::vector<Stmt> stmts = this->parseStmtsUpTo(T::kCloseBrace, opts);
        logger::Loc closeBraceLoc = this->lexer.Loc();
        this->lexer.Next();

        this->allowIn = oldAllowIn;
        this->fnOrArrowDataParse = oldFnOrArrowData;

        FnBody body;
        body.loc = loc;
        body.block.stmts = stmts;
        body.block.close_brace_loc = closeBraceLoc;
        return body;
    }

    Binding Parser::parseBinding(parseBindingOpts opts) {
        logger::Loc loc = this->lexer.Loc();

        switch (this->lexer.token) {
        case T::kIdentifier: {
            MaybeSubstring name = this->lexer.identifier;

            // Forbid invalid identifiers
            if ((this->fnOrArrowDataParse.await != allowIdent && name.str == "await") ||
                (this->fnOrArrowDataParse.yield != allowIdent && name.str == "yield")) {
                this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_CannotUseNameIdentifier_2, name.str));
            }

            compiler::Ref ref = this->storeNameInRef(name);
            this->lexer.Next();
            std::shared_ptr<BIdentifier> b = std::make_shared<BIdentifier>();
            b->ref = ref;
            return Binding{b, loc};
        }

        case T::kOpenBracket: {
            if (opts.isUsingStmt) {
                break;
            }
            this->markSyntaxFeature(compat::JSFeature::kDestructuring, this->lexer.Range());
            this->lexer.Next();
            bool isSingleLine = !this->lexer.has_newline_before;
            std::vector<ArrayBinding> items;
            bool hasSpread = false;

            // "in" expressions are allowed
            bool oldAllowIn = this->allowIn;
            this->allowIn = true;

            while (this->lexer.token != T::kCloseBracket) {
                logger::Loc itemLoc = this->saveExprCommentsHere();

                if (this->lexer.token == T::kComma) {
                    ArrayBinding ab;
                    ab.binding = Binding{std::make_shared<BMissing>(), itemLoc};
                    ab.loc = itemLoc;
                    items.push_back(ab);
                } else {
                    if (this->lexer.token == T::kDotDotDot) {
                        this->lexer.Next();
                        hasSpread = true;

                        // This was a bug in the ES2015 spec that was fixed in ES2016
                        if (this->lexer.token != T::kIdentifier) {
                            this->markSyntaxFeature(compat::JSFeature::kNestedRestBinding, this->lexer.Range());
                        }
                    }

                    this->saveExprCommentsHere();
                    parseBindingOpts innerOpts;
                    Binding binding = this->parseBinding(innerOpts);

                    Expr defaultValueOrNil;
                    if (!hasSpread && this->lexer.token == T::kEquals) {
                        this->lexer.Next();
                        defaultValueOrNil = this->parseExpr(L::kComma);
                    }

                    ArrayBinding ab;
                    ab.binding = binding;
                    ab.default_value_or_nil = defaultValueOrNil;
                    ab.loc = itemLoc;
                    items.push_back(ab);

                    // Commas after spread elements are not allowed
                    if (hasSpread && this->lexer.token == T::kComma) {
                        this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_UnexpectedCommaAfterRest));
                        throw LexerPanic();
                    }
                }

                if (this->lexer.token != T::kComma) {
                    break;
                }
                if (this->lexer.has_newline_before) {
                    isSingleLine = false;
                }
                this->lexer.Next();
                if (this->lexer.has_newline_before) {
                    isSingleLine = false;
                }
            }

            this->allowIn = oldAllowIn;

            if (this->lexer.has_newline_before) {
                isSingleLine = false;
            }
            logger::Loc closeBracketLoc = this->saveExprCommentsHere();
            this->lexer.Expect(T::kCloseBracket);
            std::shared_ptr<BArray> b = std::make_shared<BArray>();
            b->items = items;
            b->has_spread = hasSpread;
            b->is_single_line = isSingleLine;
            b->close_bracket_loc = closeBracketLoc;
            return Binding{b, loc};
        }

        case T::kOpenBrace: {
            if (opts.isUsingStmt) {
                break;
            }
            this->markSyntaxFeature(compat::JSFeature::kDestructuring, this->lexer.Range());
            this->lexer.Next();
            bool isSingleLine = !this->lexer.has_newline_before;
            std::vector<PropertyBinding> properties;

            // "in" expressions are allowed
            bool oldAllowIn = this->allowIn;
            this->allowIn = true;

            while (this->lexer.token != T::kCloseBrace) {
                this->saveExprCommentsHere();
                PropertyBinding property = this->parsePropertyBinding();
                properties.push_back(property);

                // Commas after spread elements are not allowed
                if (property.is_spread && this->lexer.token == T::kComma) {
                    this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_UnexpectedCommaAfterRest));
                    throw LexerPanic();
                }

                if (this->lexer.token != T::kComma) {
                    break;
                }
                if (this->lexer.has_newline_before) {
                    isSingleLine = false;
                }
                this->lexer.Next();
                if (this->lexer.has_newline_before) {
                    isSingleLine = false;
                }
            }

            this->allowIn = oldAllowIn;

            if (this->lexer.has_newline_before) {
                isSingleLine = false;
            }
            logger::Loc closeBraceLoc = this->saveExprCommentsHere();
            this->lexer.Expect(T::kCloseBrace);
            std::shared_ptr<BObject> b = std::make_shared<BObject>();
            b->properties = properties;
            b->is_single_line = isSingleLine;
            b->close_brace_loc = closeBraceLoc;
            return Binding{b, loc};
        }

        default:
            break;
        }

        this->lexer.Expect(T::kIdentifier);
        return Binding();
    }

    std::pair<Property, bool> Parser::parseProperty(logger::Loc startLoc, PropertyKind kind, propertyOpts opts, deferredErrors *errors) {
        PropertyFlags flags = PropertyFlags::kNone;
        Expr key;
        logger::Loc closeBracketLoc;
        logger::Range keyRange = this->lexer.Range();

        switch (this->lexer.token) {
        case T::kNumericLiteral: {
            std::shared_ptr<ENumber> e = std::make_shared<ENumber>();
            e->value = this->lexer.number;
            key.loc = this->lexer.Loc();
            key.data = e;
            this->checkForLegacyOctalLiteral(key.data);
            this->lexer.Next();
            break;
        }

        case T::kStringLiteral:
            key = this->parseStringLiteral();
            if (!this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                flags |= PropertyFlags::kPreferQuotedKey;
            }
            break;

        case T::kBigIntegerLiteral:
            key = this->parseBigIntOrStringIfUnsupported();
            this->lexer.Next();
            break;

        case T::kPrivateIdentifier: {
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->options.optionsThatSupportStructuralEquality.ts.Config.ExperimentalDecorators == config::MaybeBool::kTrue && opts.decorators.size() > 0) {
                this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_TSDecoratorsPrivateIdentifier));
            } else if (!opts.isClass) {
                this->lexer.Expected(T::kIdentifier);
            } else if (opts.tsDeclareRange.len != 0) {
                this->log.AddError(&this->tracker, opts.tsDeclareRange, logger::FormatMsg(logger::MsgCat::kJS_DeclarePrivateIdentifier));
            }
            MaybeSubstring name = this->lexer.identifier;
            std::shared_ptr<EPrivateIdentifier> e = std::make_shared<EPrivateIdentifier>();
            e->ref = this->storeNameInRef(name);
            key.loc = this->lexer.Loc();
            key.data = e;
            this->reportPrivateNameUsage(name.str);
            this->lexer.Next();
            break;
        }

        case T::kOpenBracket: {
            flags |= PropertyFlags::kIsComputed;
            this->markSyntaxFeature(compat::JSFeature::kObjectExtensions, this->lexer.Range());
            this->lexer.Next();
            bool wasIdentifier = this->lexer.token == T::kIdentifier;
            Expr expr = this->parseExpr(L::kComma);

            // Handle index signatures
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kColon && wasIdentifier && opts.isClass) {
                if (Get<EIdentifier>(expr.data) != nullptr) {
                    if (opts.tsDeclareRange.len != 0) {
                        this->log.AddError(&this->tracker, opts.tsDeclareRange, logger::FormatMsg(logger::MsgCat::kJS_DeclareIndexSignature));
                    }

                    // "[key: string]: any;"
                    this->lexer.Next();
                    this->skipTypeScriptType(L::kLowest);
                    this->lexer.Expect(T::kCloseBracket);
                    this->lexer.Expect(T::kColon);
                    this->skipTypeScriptType(L::kLowest);
                    this->lexer.ExpectOrInsertSemicolon();

                    // Skip this property entirely
                    return {Property(), false};
                }
            }

            closeBracketLoc = this->saveExprCommentsHere();
            this->lexer.Expect(T::kCloseBracket);
            key = expr;
            break;
        }

        case T::kAsterisk:
            if (kind != PropertyKind::kField && (kind != PropertyKind::kMethod || opts.isGenerator)) {
                this->lexer.Unexpected();
            }
            opts.isGenerator = true;
            opts.generatorRange = this->lexer.Range();
            this->lexer.Next();
            return this->parseProperty(startLoc, PropertyKind::kMethod, opts, errors);

        default: {
            MaybeSubstring name = this->lexer.identifier;
            std::string raw = std::string(this->lexer.Raw());
            logger::Range nameRange = this->lexer.Range();
            if (!this->lexer.IsIdentifierOrKeyword()) {
                this->lexer.Expect(T::kIdentifier);
            }
            this->lexer.Next();

            // Support contextual keywords
            if (kind == PropertyKind::kField) {
                // Does the following token look like a key?
                bool couldBeModifierKeyword = this->lexer.IsIdentifierOrKeyword();
                if (!couldBeModifierKeyword) {
                    switch (this->lexer.token) {
                    case T::kOpenBracket:
                    case T::kNumericLiteral:
                    case T::kStringLiteral:
                    case T::kPrivateIdentifier:
                        couldBeModifierKeyword = true;
                        break;
                    case T::kAsterisk:
                        if (opts.isAsync || (raw != "get" && raw != "set")) {
                            couldBeModifierKeyword = true;
                        }
                        break;

                    default:
                        break;
                    }
                }

                // If so, check for a modifier keyword
                if (couldBeModifierKeyword) {
                    if (raw == "get") {
                        if (!opts.isAsync) {
                            this->markSyntaxFeature(compat::JSFeature::kObjectAccessors, nameRange);
                            return this->parseProperty(startLoc, PropertyKind::kGetter, opts, nullptr);
                        }
                    } else if (raw == "set") {
                        if (!opts.isAsync) {
                            this->markSyntaxFeature(compat::JSFeature::kObjectAccessors, nameRange);
                            return this->parseProperty(startLoc, PropertyKind::kSetter, opts, nullptr);
                        }
                    } else if (raw == "accessor") {
                        if (!this->lexer.has_newline_before && !opts.isAsync && opts.isClass) {
                            return this->parseProperty(startLoc, PropertyKind::kAutoAccessor, opts, nullptr);
                        }
                    } else if (raw == "async") {
                        if (!this->lexer.has_newline_before && !opts.isAsync) {
                            opts.isAsync = true;
                            opts.asyncRange = nameRange;
                            return this->parseProperty(startLoc, PropertyKind::kMethod, opts, nullptr);
                        }
                    } else if (raw == "static") {
                        if (!opts.isStatic && !opts.isAsync && opts.isClass) {
                            opts.isStatic = true;
                            return this->parseProperty(startLoc, kind, opts, nullptr);
                        }
                    } else if (raw == "declare") {
                        if (!this->lexer.has_newline_before && opts.isClass && this->options.optionsThatSupportStructuralEquality.ts.Parse && opts.tsDeclareRange.len == 0) {
                            opts.tsDeclareRange = nameRange;
                            int scopeIndex = static_cast<int>(this->scopesInOrder.size());

                            auto parsed = this->parseProperty(startLoc, kind, opts, nullptr);
                            if (parsed.second &&
                                parsed.first.kind == PropertyKind::kField && IsNil(parsed.first.value_or_nil.data) &&
                                (this->options.optionsThatSupportStructuralEquality.ts.Config.ExperimentalDecorators == config::MaybeBool::kTrue && opts.decorators.size() > 0)) {
                                // If this is a well-formed class field with the "declare" keyword,
                                // only keep the declaration to preserve its side-effects when
                                // there are TypeScript experimental decorators present:
                                //
                                //   class Foo {
                                //     // Remove this
                                //     declare [(console.log('side effect 1'), 'foo')]
                                //
                                //     // Keep this
                                //     @decorator(console.log('side effect 2')) declare bar
                                //   }
                                //
                                // This behavior is surprisingly somehow valid with TypeScript
                                // experimental decorators, which was possibly by accident.
                                // TypeScript does not allow this with JavaScript decorators.
                                //
                                // References:
                                //
                                //   https://github.com/evanw/esbuild/issues/1675
                                //   https://github.com/microsoft/TypeScript/issues/46345
                                //
                                parsed.first.kind = PropertyKind::kDeclareOrAbstract;
                                return parsed;
                            }

                            this->discardScopesUpTo(scopeIndex);
                            return {Property(), false};
                        }
                    } else if (raw == "abstract") {
                        if (!this->lexer.has_newline_before && opts.isClass && this->options.optionsThatSupportStructuralEquality.ts.Parse && !opts.isTSAbstract) {
                            opts.isTSAbstract = true;
                            int scopeIndex = static_cast<int>(this->scopesInOrder.size());

                            auto parsed = this->parseProperty(startLoc, kind, opts, nullptr);
                            if (parsed.second &&
                                parsed.first.kind == PropertyKind::kField && IsNil(parsed.first.value_or_nil.data) &&
                                (this->options.optionsThatSupportStructuralEquality.ts.Config.ExperimentalDecorators == config::MaybeBool::kTrue && opts.decorators.size() > 0)) {
                                // If this is a well-formed class field with the "abstract" keyword,
                                // only keep the declaration to preserve its side-effects when
                                // there are TypeScript experimental decorators present:
                                //
                                //   abstract class Foo {
                                //     // Remove this
                                //     abstract [(console.log('side effect 1'), 'foo')]
                                //
                                //     // Keep this
                                //     @decorator(console.log('side effect 2')) abstract bar
                                //   }
                                //
                                // This behavior is valid with TypeScript experimental decorators.
                                // TypeScript does not allow this with JavaScript decorators.
                                //
                                // References:
                                //
                                //   https://github.com/evanw/esbuild/issues/3684
                                //
                                parsed.first.kind = PropertyKind::kDeclareOrAbstract;
                                return parsed;
                            }

                            this->discardScopesUpTo(scopeIndex);
                            return {Property(), false};
                        }
                    } else if (raw == "private" || raw == "protected" || raw == "public" || raw == "readonly" || raw == "override") {
                        // Skip over TypeScript keywords
                        if (opts.isClass && this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                            return this->parseProperty(startLoc, kind, opts, nullptr);
                        }
                    }
                } else if (this->lexer.token == T::kOpenBrace && name.str == "static" && opts.decorators.size() == 0) {
                    logger::Loc loc = this->lexer.Loc();
                    this->lexer.Next();

                    struct fnOrArrowDataParse oldFnOrArrowDataParse = this->fnOrArrowDataParse;
                    this->fnOrArrowDataParse = {};                    this->fnOrArrowDataParse.isReturnDisallowed = true;
                    this->fnOrArrowDataParse.allowSuperProperty = true;
                    this->fnOrArrowDataParse.await = forbidAll;

                    this->pushScopeForParsePass(ScopeKind::kClassStaticInit, loc);
                    std::vector<Stmt> stmts = this->parseStmtsUpTo(T::kCloseBrace, parseStmtOpts());
                    this->popScope();

                    this->fnOrArrowDataParse = oldFnOrArrowDataParse;

                    logger::Loc closeBraceLoc = this->lexer.Loc();
                    this->lexer.Expect(T::kCloseBrace);
                    Property prop;
                    prop.kind = PropertyKind::kClassStaticBlock;
                    prop.loc = startLoc;
                    std::shared_ptr<ClassStaticBlock> block = std::make_shared<ClassStaticBlock>();
                    block->loc = loc;
                    block->block.stmts = stmts;
                    block->block.close_brace_loc = closeBraceLoc;
                    prop.class_static_block = block;
                    return {prop, true};
                }
            }

            if (this->isMangledProp(name.str)) {
                std::shared_ptr<ENameOfSymbol> e = std::make_shared<ENameOfSymbol>();
                e->ref = this->storeNameInRef(name);
                e->has_property_key_comment = true;
                key.loc = nameRange.loc;
                key.data = e;
            } else {
                std::shared_ptr<EString> e = std::make_shared<EString>();
                e->value = helpers::StringToUTF16(name.str);
                key.loc = nameRange.loc;
                key.data = e;
            }

            // Parse a shorthand property
            if (!opts.isClass && kind == PropertyKind::kField && this->lexer.token != T::kColon &&
                this->lexer.token != T::kOpenParen && this->lexer.token != T::kLessThan &&
                kKeywords.find(name.str) == kKeywords.end()) {

                // Forbid invalid identifiers
                if ((this->fnOrArrowDataParse.await != allowIdent && name.str == "await") ||
                    (this->fnOrArrowDataParse.yield != allowIdent && name.str == "yield")) {
                    this->log.AddError(&this->tracker, nameRange, logger::FormatMsg(logger::MsgCat::kJS_CannotUseNameIdentifier_2, name.str));
                }

                compiler::Ref ref = this->storeNameInRef(name);
                std::shared_ptr<EIdentifier> e = std::make_shared<EIdentifier>();
                e->ref = ref;
                Expr value;
                value.loc = key.loc;
                value.data = e;

                // Destructuring patterns have an optional default value
                Expr initializerOrNil;
                if (errors != nullptr && this->lexer.token == T::kEquals) {
                    errors->invalidExprDefaultValue = this->lexer.Range();
                    this->lexer.Next();
                    initializerOrNil = this->parseExpr(L::kComma);
                }

                Property prop;
                prop.kind = kind;
                prop.loc = startLoc;
                prop.key = key;
                prop.value_or_nil = value;
                prop.initializer_or_nil = initializerOrNil;
                prop.flags = PropertyFlags::kWasShorthand;
                return {prop, true};
            }
            break;
        }
        }

        bool hasTypeParameters = false;
        bool hasDefiniteAssignmentAssertionOperator = false;

        if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
            if (opts.isClass) {
                if (this->lexer.token == T::kQuestion) {
                    // "class X { foo?: number }"
                    // "class X { foo?(): number }"
                    this->lexer.Next();
                } else if (this->lexer.token == T::kExclamation && !this->lexer.has_newline_before &&
                    (kind == PropertyKind::kField || kind == PropertyKind::kAutoAccessor)) {
                    // "class X { foo!: number }"
                    this->lexer.Next();
                    hasDefiniteAssignmentAssertionOperator = true;
                }
            }

            // "class X { foo?<T>(): T }"
            // "const x = { foo<T>(): T {} }"
            if (!hasDefiniteAssignmentAssertionOperator && kind != PropertyKind::kAutoAccessor) {
                hasTypeParameters = this->skipTypeScriptTypeParameters(allowConstModifier) != didNotSkipAnything;
            }
        }

        // Parse a class field with an optional initial value
        if (kind == PropertyKind::kAutoAccessor || (opts.isClass && kind == PropertyKind::kField &&
            !hasTypeParameters && (this->lexer.token != T::kOpenParen || hasDefiniteAssignmentAssertionOperator))) {
            Expr initializerOrNil;

            // Forbid the names "constructor" and "prototype" in some cases
            if (!Has(flags, PropertyFlags::kIsComputed)) {
                if (auto str = Get<EString>(key.data)) {
                    if (helpers::UTF16EqualsString(str->value, "constructor") ||
                        (opts.isStatic && helpers::UTF16EqualsString(str->value, "prototype"))) {
                        this->log.AddError(&this->tracker, keyRange, logger::FormatMsg(logger::MsgCat::kJS_InvalidFieldName, helpers::UTF16ToString(str->value)));
                    }
                }
            }

            // Skip over types
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kColon) {
                this->lexer.Next();
                this->skipTypeScriptType(L::kLowest);
            }

            if (this->lexer.token == T::kEquals) {
                this->lexer.Next();

                // "this" and "super" property access is allowed in field initializers
                bool oldIsThisDisallowed = this->fnOrArrowDataParse.isThisDisallowed;
                bool oldAllowSuperProperty = this->fnOrArrowDataParse.allowSuperProperty;
                this->fnOrArrowDataParse.isThisDisallowed = false;
                this->fnOrArrowDataParse.allowSuperProperty = true;

                initializerOrNil = this->parseExpr(L::kComma);

                this->fnOrArrowDataParse.isThisDisallowed = oldIsThisDisallowed;
                this->fnOrArrowDataParse.allowSuperProperty = oldAllowSuperProperty;
            }

            // Special-case private identifiers
            if (auto private_ = Get<EPrivateIdentifier>(key.data)) {
                std::string name = this->loadNameFromRef(private_->ref);
                if (name == "#constructor") {
                    this->log.AddError(&this->tracker, keyRange, logger::FormatMsg(logger::MsgCat::kJS_InvalidFieldName, name));
                }
                compiler::SymbolKind declare;
                if (kind == PropertyKind::kAutoAccessor) {
                    if (opts.isStatic) {
                        declare = compiler::SymbolKind::kPrivateStaticGetSetPair;
                    } else {
                        declare = compiler::SymbolKind::kPrivateGetSetPair;
                    }
                    private_->ref = this->declareSymbol(declare, key.loc, name);
                    this->privateGetters[private_->ref] = this->newSymbol(compiler::SymbolKind::kOther, name.substr(1) + "_get");
                    this->privateSetters[private_->ref] = this->newSymbol(compiler::SymbolKind::kOther, name.substr(1) + "_set");
                } else {
                    if (opts.isStatic) {
                        declare = compiler::SymbolKind::kPrivateStaticField;
                    } else {
                        declare = compiler::SymbolKind::kPrivateField;
                    }
                    private_->ref = this->declareSymbol(declare, key.loc, name);
                }
            }

            this->lexer.ExpectOrInsertSemicolon();
            if (opts.isStatic) {
                flags |= PropertyFlags::kIsStatic;
            }
            Property prop;
            prop.decorators = opts.decorators;
            prop.loc = startLoc;
            prop.kind = kind;
            prop.flags = flags;
            prop.key = key;
            prop.initializer_or_nil = initializerOrNil;
            prop.close_bracket_loc = closeBracketLoc;
            return {prop, true};
        }

        // Parse a method expression
        if (this->lexer.token == T::kOpenParen || IsMethodDefinition(kind) || opts.isClass) {
            bool hasError = false;

            if (!hasError && opts.tsDeclareRange.len != 0) {
                std::string what = "method";
                if (kind == PropertyKind::kGetter) {
                    what = "getter";
                } else if (kind == PropertyKind::kSetter) {
                    what = "setter";
                }
                this->log.AddError(&this->tracker, opts.tsDeclareRange, logger::FormatMsg(logger::MsgCat::kJS_DeclareCannotBeUsedWith, what));
                hasError = true;
            }

            if (opts.isAsync && this->markAsyncFn(opts.asyncRange, opts.isGenerator)) {
                hasError = true;
            }

            if (!hasError && opts.isGenerator && this->markSyntaxFeature(compat::JSFeature::kGenerator, opts.generatorRange)) {
                hasError = true;
            }

            if (!hasError && this->lexer.token == T::kOpenParen && kind != PropertyKind::kGetter && kind != PropertyKind::kSetter && this->markSyntaxFeature(compat::JSFeature::kObjectExtensions, this->lexer.Range())) {
                hasError = true;
            }

            logger::Loc loc = this->lexer.Loc();
            int scopeIndex = this->pushScopeForParsePass(ScopeKind::kFunctionArgs, loc);
            bool isConstructor = false;

            // Forbid the names "constructor" and "prototype" in some cases
            if (opts.isClass && !Has(flags, PropertyFlags::kIsComputed)) {
                if (auto str = Get<EString>(key.data)) {
                    if (!opts.isStatic && helpers::UTF16EqualsString(str->value, "constructor")) {
                        if (kind == PropertyKind::kGetter) {
                            this->log.AddError(&this->tracker, keyRange, logger::FormatMsg(logger::MsgCat::kJS_ConstructorCannotBeGetter));
                        } else if (kind == PropertyKind::kSetter) {
                            this->log.AddError(&this->tracker, keyRange, logger::FormatMsg(logger::MsgCat::kJS_ConstructorCannotBeSetter));
                        } else if (opts.isAsync) {
                            this->log.AddError(&this->tracker, keyRange, logger::FormatMsg(logger::MsgCat::kJS_ConstructorCannotBeAsync));
                        } else if (opts.isGenerator) {
                            this->log.AddError(&this->tracker, keyRange, logger::FormatMsg(logger::MsgCat::kJS_ConstructorCannotBeGenerator));
                        } else {
                            isConstructor = true;
                        }
                    } else if (opts.isStatic && helpers::UTF16EqualsString(str->value, "prototype")) {
                        this->log.AddError(&this->tracker, keyRange, logger::FormatMsg(logger::MsgCat::kJS_InvalidStaticMethodName));
                    }
                }
            }

            awaitOrYield await = allowIdent;
            awaitOrYield yield = allowIdent;
            if (opts.isAsync) {
                await = allowExpr;
            }
            if (opts.isGenerator) {
                yield = allowExpr;
            }

            struct fnOrArrowDataParse data;
            data.needsAsyncLoc = key.loc;
            data.asyncRange = opts.asyncRange;
            data.await = await;
            data.yield = yield;
            data.allowSuperCall = opts.classHasExtends && isConstructor;
            data.allowSuperProperty = true;
            data.decoratorScope = opts.decoratorScope;
            data.isConstructor = isConstructor;

            // Only allow omitting the body if we're parsing TypeScript class
            data.allowMissingBodyForTypeScript = this->options.optionsThatSupportStructuralEquality.ts.Parse && opts.isClass;

            auto fnResult = this->parseFn(nullptr, opts.classKeyword, opts.decoratorContext, data);
            bool hadBody = fnResult.second;

            // "class Foo { foo(): void; foo(): void {} }"
            if (!hadBody) {
                // Skip this property entirely
                this->popAndDiscardScope(scopeIndex);
                return {Property(), false};
            }

            this->popScope();
            fnResult.first.is_unique_formal_parameters = true;
            std::shared_ptr<EFunction> fnExpr = std::make_shared<EFunction>();
            fnExpr->fn = fnResult.first;
            Expr value;
            value.loc = loc;
            value.data = fnExpr;

            // Enforce argument rules for accessors
            switch (kind) {
            case PropertyKind::kGetter:
                if (fnResult.first.args.size() > 0) {
                    logger::Range r = RangeOfIdentifier(this->source, fnResult.first.args[0].binding.loc);
                    this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_GetterZeroArgs, this->keyNameForError(key)));
                }
                break;

            case PropertyKind::kSetter:
                if (fnResult.first.args.size() != 1) {
                    logger::Range r = RangeOfIdentifier(this->source, key.loc);
                    if (fnResult.first.args.size() > 1) {
                        r = RangeOfIdentifier(this->source, fnResult.first.args[1].binding.loc);
                    }
                    this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_SetterExactlyOneArg, this->keyNameForError(key)));
                }
                break;

            default:
                kind = PropertyKind::kMethod;
                break;
            }

            // Special-case private identifiers
            if (auto private_ = Get<EPrivateIdentifier>(key.data)) {
                compiler::SymbolKind declare;
                std::string suffix;
                if (kind == PropertyKind::kGetter) {
                    if (opts.isStatic) {
                        declare = compiler::SymbolKind::kPrivateStaticGet;
                    } else {
                        declare = compiler::SymbolKind::kPrivateGet;
                    }
                    suffix = "_get";
                } else if (kind == PropertyKind::kSetter) {
                    if (opts.isStatic) {
                        declare = compiler::SymbolKind::kPrivateStaticSet;
                    } else {
                        declare = compiler::SymbolKind::kPrivateSet;
                    }
                    suffix = "_set";
                } else {
                    if (opts.isStatic) {
                        declare = compiler::SymbolKind::kPrivateStaticMethod;
                    } else {
                        declare = compiler::SymbolKind::kPrivateMethod;
                    }
                    suffix = "_fn";
                }
                std::string name = this->loadNameFromRef(private_->ref);
                if (name == "#constructor") {
                    this->log.AddError(&this->tracker, keyRange, logger::FormatMsg(logger::MsgCat::kJS_InvalidMethodName, name));
                }
                private_->ref = this->declareSymbol(declare, key.loc, name);
                compiler::Ref methodRef = this->newSymbol(compiler::SymbolKind::kOther, name.substr(1) + suffix);
                if (kind == PropertyKind::kSetter) {
                    this->privateSetters[private_->ref] = methodRef;
                } else {
                    this->privateGetters[private_->ref] = methodRef;
                }
            }

            if (opts.isStatic) {
                flags |= PropertyFlags::kIsStatic;
            }
            Property prop;
            prop.decorators = opts.decorators;
            prop.loc = startLoc;
            prop.kind = kind;
            prop.flags = flags;
            prop.key = key;
            prop.value_or_nil = value;
            prop.close_bracket_loc = closeBracketLoc;
            return {prop, true};
        }

        // Parse an object key/value pair
        this->lexer.Expect(T::kColon);
        Expr value = this->parseExprOrBindings(L::kComma, errors);
        Property prop;
        prop.loc = startLoc;
        prop.kind = kind;
        prop.flags = flags;
        prop.key = key;
        prop.value_or_nil = value;
        prop.close_bracket_loc = closeBracketLoc;
        return {prop, true};
    }

    PropertyBinding Parser::parsePropertyBinding() {
        Expr key;
        logger::Loc closeBracketLoc;
        bool isComputed = false;
        bool preferQuotedKey = false;
        logger::Loc loc = this->lexer.Loc();

        switch (this->lexer.token) {
        case T::kDotDotDot: {
            this->lexer.Next();
            std::shared_ptr<BIdentifier> identifier = std::make_shared<BIdentifier>();
            identifier->ref = this->storeNameInRef(this->lexer.identifier);
            Binding value;
            value.loc = this->saveExprCommentsHere();
            value.data = identifier;
            this->lexer.Expect(T::kIdentifier);
            PropertyBinding result;
            result.loc = loc;
            result.is_spread = true;
            result.value = value;
            return result;
        }

        case T::kNumericLiteral: {
            std::shared_ptr<ENumber> e = std::make_shared<ENumber>();
            e->value = this->lexer.number;
            key.loc = this->lexer.Loc();
            key.data = e;
            this->checkForLegacyOctalLiteral(key.data);
            this->lexer.Next();
            break;
        }

        case T::kStringLiteral:
            key = this->parseStringLiteral();
            preferQuotedKey = !this->options.optionsThatSupportStructuralEquality.minifySyntax;
            break;

        case T::kBigIntegerLiteral:
            key = this->parseBigIntOrStringIfUnsupported();
            this->lexer.Next();
            break;

        case T::kOpenBracket:
            isComputed = true;
            this->lexer.Next();
            key = this->parseExpr(L::kComma);
            closeBracketLoc = this->saveExprCommentsHere();
            this->lexer.Expect(T::kCloseBracket);
            break;

        default: {
            MaybeSubstring name = this->lexer.identifier;
            logger::Range nameRange = this->lexer.Range();
            if (!this->lexer.IsIdentifierOrKeyword()) {
                this->lexer.Expect(T::kIdentifier);
            }
            this->lexer.Next();
            if (this->isMangledProp(name.str)) {
                std::shared_ptr<ENameOfSymbol> e = std::make_shared<ENameOfSymbol>();
                e->ref = this->storeNameInRef(name);
                key.loc = nameRange.loc;
                key.data = e;
            } else {
                std::shared_ptr<EString> e = std::make_shared<EString>();
                e->value = helpers::StringToUTF16(name.str);
                key.loc = nameRange.loc;
                key.data = e;
            }

            if (this->lexer.token != T::kColon && this->lexer.token != T::kOpenParen) {
                // Forbid invalid identifiers
                if ((this->fnOrArrowDataParse.await != allowIdent && name.str == "await") ||
                    (this->fnOrArrowDataParse.yield != allowIdent && name.str == "yield")) {
                    this->log.AddError(&this->tracker, nameRange, logger::FormatMsg(logger::MsgCat::kJS_CannotUseNameIdentifier_2, name.str));
                }

                compiler::Ref ref = this->storeNameInRef(name);
                std::shared_ptr<BIdentifier> identifier = std::make_shared<BIdentifier>();
                identifier->ref = ref;
                Binding value;
                value.loc = nameRange.loc;
                value.data = identifier;

                Expr defaultValueOrNil;
                if (this->lexer.token == T::kEquals) {
                    this->lexer.Next();
                    defaultValueOrNil = this->parseExpr(L::kComma);
                }

                PropertyBinding result;
                result.loc = loc;
                result.key = key;
                result.value = value;
                result.default_value_or_nil = defaultValueOrNil;
                return result;
            }
            break;
        }
        }

        this->lexer.Expect(T::kColon);
        Binding value = this->parseBinding(parseBindingOpts());

        Expr defaultValueOrNil;
        if (this->lexer.token == T::kEquals) {
            this->lexer.Next();
            defaultValueOrNil = this->parseExpr(L::kComma);
        }

        PropertyBinding result;
        result.loc = loc;
        result.is_computed = isComputed;
        result.prefer_quoted_key = preferQuotedKey;
        result.key = key;
        result.value = value;
        result.default_value_or_nil = defaultValueOrNil;
        result.close_bracket_loc = closeBracketLoc;
        return result;
    }    

    MaybeSubstring Parser::parseClauseAlias(const std::string &kind) {
        logger::Loc loc = this->lexer.Loc();

        // The alias may now be a string (see https://github.com/tc39/ecma262/pull/2154)
        if (this->lexer.token == T::kStringLiteral) {
            logger::Range r = this->source.RangeOfString(loc);
            std::tuple<std::string, char16_t, bool> result = helpers::UTF16ToStringWithValidation(this->lexer.StringLiteral());
            std::string alias = std::get<0>(result);
            if (!std::get<2>(result)) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%X", static_cast<unsigned>(std::get<1>(result)));
                this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_AliasInvalidSurrogate, kind, buf));
            }
            return MaybeSubstring{alias};
        }

        // The alias may be a keyword
        if (!this->lexer.IsIdentifierOrKeyword()) {
            this->lexer.Expect(T::kIdentifier);
        }

        MaybeSubstring alias = this->lexer.identifier;
        this->checkForUnrepresentableIdentifier(loc, alias.str);
        return alias;
    }    

    std::pair<std::vector<ClauseItem>, bool> Parser::parseImportClause() {
        std::vector<ClauseItem> items;
        this->lexer.Expect(T::kOpenBrace);
        bool isSingleLine = !this->lexer.has_newline_before;

        while (this->lexer.token != T::kCloseBrace) {
            bool isIdentifier = this->lexer.token == T::kIdentifier;
            logger::Loc aliasLoc = this->lexer.Loc();
            MaybeSubstring alias = this->parseClauseAlias("import");
            compiler::LocRef name{aliasLoc, this->storeNameInRef(alias)};
            MaybeSubstring originalName = alias;
            this->lexer.Next();

            // "import { type xx } from 'mod'"
            // "import { type xx as yy } from 'mod'"
            // "import { type 'xx' as yy } from 'mod'"
            // "import { type as } from 'mod'"
            // "import { type as as } from 'mod'"
            // "import { type as as as } from 'mod'"
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && alias.str == "type" && this->lexer.token != T::kComma && this->lexer.token != T::kCloseBrace) {
                if (this->lexer.IsContextualKeyword("as")) {
                    this->lexer.Next();
                    if (this->lexer.IsContextualKeyword("as")) {
                        originalName = this->lexer.identifier;
                        name = compiler::LocRef{this->lexer.Loc(), this->storeNameInRef(originalName)};
                        this->lexer.Next();

                        if (this->lexer.token == T::kIdentifier) {
                            // "import { type as as as } from 'mod'"
                            // "import { type as as foo } from 'mod'"
                            this->lexer.Next();
                        } else {
                            // "import { type as as } from 'mod'"
                            ClauseItem item;
                            item.alias = alias.str;
                            item.alias_loc = aliasLoc;
                            item.name = name;
                            item.original_name = originalName.str;
                            items.push_back(item);
                        }
                    } else if (this->lexer.token == T::kIdentifier) {
                        // "import { type as xxx } from 'mod'"
                        originalName = this->lexer.identifier;
                        name = compiler::LocRef{this->lexer.Loc(), this->storeNameInRef(originalName)};
                        this->lexer.Expect(T::kIdentifier);

                        // Reject forbidden names
                        if (isEvalOrArguments(originalName.str)) {
                            logger::Range r = RangeOfIdentifier(this->source, name.loc);
                            this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_CannotUseNameIdentifier_2, originalName.str));
                        }

                        ClauseItem item;
                        item.alias = alias.str;
                        item.alias_loc = aliasLoc;
                        item.name = name;
                        item.original_name = originalName.str;
                        items.push_back(item);
                    }
                } else {
                    bool isIdentifier2 = this->lexer.token == T::kIdentifier;

                    // "import { type xx } from 'mod'"
                    // "import { type xx as yy } from 'mod'"
                    // "import { type if as yy } from 'mod'"
                    // "import { type 'xx' as yy } from 'mod'"
                    this->parseClauseAlias("import");
                    this->lexer.Next();

                    if (this->lexer.IsContextualKeyword("as")) {
                        this->lexer.Next();
                        this->lexer.Expect(T::kIdentifier);
                    } else if (!isIdentifier2) {
                        // An import where the name is a keyword must have an alias
                        this->lexer.ExpectedString("\"as\"");
                    }
                }
            } else {
                if (this->lexer.IsContextualKeyword("as")) {
                    this->lexer.Next();
                    originalName = this->lexer.identifier;
                    name = compiler::LocRef{this->lexer.Loc(), this->storeNameInRef(originalName)};
                    this->lexer.Expect(T::kIdentifier);
                } else if (!isIdentifier) {
                    // An import where the name is a keyword must have an alias
                    this->lexer.ExpectedString("\"as\"");
                }

                // Reject forbidden names
                if (isEvalOrArguments(originalName.str)) {
                    logger::Range r = RangeOfIdentifier(this->source, name.loc);
                    this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_CannotUseNameIdentifier_2, originalName.str));
                }

                ClauseItem item;
                item.alias = alias.str;
                item.alias_loc = aliasLoc;
                item.name = name;
                item.original_name = originalName.str;
                items.push_back(item);
            }

            if (this->lexer.token != T::kComma) {
                break;
            }
            if (this->lexer.has_newline_before) {
                isSingleLine = false;
            }
            this->lexer.Next();
            if (this->lexer.has_newline_before) {
                isSingleLine = false;
            }
        }

        if (this->lexer.has_newline_before) {
            isSingleLine = false;
        }
        this->lexer.Expect(T::kCloseBrace);
        return std::make_pair(items, isSingleLine);
    }

    std::pair<std::vector<ClauseItem>, bool> Parser::parseExportClause() {
        std::vector<ClauseItem> items;
        logger::Loc firstNonIdentifierLoc;
        this->lexer.Expect(T::kOpenBrace);
        bool isSingleLine = !this->lexer.has_newline_before;

        while (this->lexer.token != T::kCloseBrace) {
            MaybeSubstring alias = this->parseClauseAlias("export");
            logger::Loc aliasLoc = this->lexer.Loc();
            compiler::LocRef name{aliasLoc, this->storeNameInRef(alias)};
            MaybeSubstring originalName = alias;

            // The name can actually be a keyword if we're really an "export from"
            // statement. However, we won't know until later. Allow keywords as
            // identifiers for now and throw an error later if there's no "from".
            //
            //   // This is fine
            //   export { default } from 'path'
            //
            //   // This is a syntax error
            //   export { default }
            //
            if (this->lexer.token != T::kIdentifier && firstNonIdentifierLoc.start == 0) {
                firstNonIdentifierLoc = this->lexer.Loc();
            }
            this->lexer.Next();

            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && alias.str == "type" && this->lexer.token != T::kComma && this->lexer.token != T::kCloseBrace) {
                if (this->lexer.IsContextualKeyword("as")) {
                    this->lexer.Next();
                    if (this->lexer.IsContextualKeyword("as")) {
                        alias = this->parseClauseAlias("export");
                        aliasLoc = this->lexer.Loc();
                        this->lexer.Next();

                        if (this->lexer.token != T::kComma && this->lexer.token != T::kCloseBrace) {
                            // "export { type as as as }"
                            // "export { type as as foo }"
                            // "export { type as as 'foo' }"
                            this->parseClauseAlias("export");
                            this->lexer.Next();
                        } else {
                            // "export { type as as }"
                            ClauseItem item;
                            item.alias = alias.str;
                            item.alias_loc = aliasLoc;
                            item.name = name;
                            item.original_name = originalName.str;
                            items.push_back(item);
                        }
                    } else if (this->lexer.token != T::kComma && this->lexer.token != T::kCloseBrace) {
                        // "export { type as xxx }"
                        // "export { type as 'xxx' }"
                        alias = this->parseClauseAlias("export");
                        aliasLoc = this->lexer.Loc();
                        this->lexer.Next();

                        ClauseItem item;
                        item.alias = alias.str;
                        item.alias_loc = aliasLoc;
                        item.name = name;
                        item.original_name = originalName.str;
                        items.push_back(item);
                    }
                } else {
                    // The name can actually be a keyword if we're really an "export from"
                    // statement. However, we won't know until later. Allow keywords as
                    // identifiers for now and throw an error later if there's no "from".
                    //
                    //   // This is fine
                    //   export { type default } from 'path'
                    //
                    //   // This is a syntax error
                    //   export { type default }
                    //
                    if (this->lexer.token != T::kIdentifier && firstNonIdentifierLoc.start == 0) {
                        firstNonIdentifierLoc = this->lexer.Loc();
                    }

                    // "export { type xx }"
                    // "export { type xx as yy }"
                    // "export { type xx as if }"
                    // "export { type default } from 'path'"
                    // "export { type default as if } from 'path'"
                    // "export { type xx as 'yy' }"
                    // "export { type 'xx' } from 'mod'"
                    this->parseClauseAlias("export");
                    this->lexer.Next();

                    if (this->lexer.IsContextualKeyword("as")) {
                        this->lexer.Next();
                        this->parseClauseAlias("export");
                        this->lexer.Next();
                    }
                }
            } else {
                if (this->lexer.IsContextualKeyword("as")) {
                    this->lexer.Next();
                    alias = this->parseClauseAlias("export");
                    aliasLoc = this->lexer.Loc();
                    this->lexer.Next();
                }

                ClauseItem item;
                item.alias = alias.str;
                item.alias_loc = aliasLoc;
                item.name = name;
                item.original_name = originalName.str;
                items.push_back(item);
            }

            if (this->lexer.token != T::kComma) {
                break;
            }
            if (this->lexer.has_newline_before) {
                isSingleLine = false;
            }
            this->lexer.Next();
            if (this->lexer.has_newline_before) {
                isSingleLine = false;
            }
        }

        if (this->lexer.has_newline_before) {
            isSingleLine = false;
        }
        this->lexer.Expect(T::kCloseBrace);

        // Throw an error here if we found a keyword earlier and this isn't an
        // "export from" statement after all
        if (firstNonIdentifierLoc.start != 0 && !this->lexer.IsContextualKeyword("from")) {
            logger::Range r = RangeOfIdentifier(this->source, firstNonIdentifierLoc);
            this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_ExpectedButFound, this->source.TextForRange(r)));
            throw LexerPanic();
        }

        return std::make_pair(items, isSingleLine);
    }

    std::tuple<logger::Range, std::string, compiler::ImportAssertOrWith *, compiler::ImportRecordFlags> Parser::parsePath() {
        compiler::ImportRecordFlags flags = compiler::ImportRecordFlags::kNone;
        logger::Range pathRange = this->lexer.Range();
        std::string pathText = helpers::UTF16ToString(this->lexer.StringLiteral());
        if (this->lexer.token == T::kNoSubstitutionTemplateLiteral) {
            this->lexer.Next();
        } else {
            this->lexer.Expect(T::kStringLiteral);
        }

        // See https://github.com/tc39/proposal-import-attributes for more info
        compiler::ImportAssertOrWith *assertOrWith = nullptr;
        if (this->lexer.token == T::kWith || (!this->lexer.has_newline_before && this->lexer.IsContextualKeyword("assert"))) {
            // "import './foo.json' assert { type: 'json' }"
            // "import './foo.json' with { type: 'json' }"
            std::vector<compiler::AssertOrWithEntry> entries;
            std::unordered_map<std::string, logger::Range> duplicates;
            compiler::AssertOrWithKeyword keyword = compiler::AssertOrWithKeyword::kWith;
            if (this->lexer.token != T::kWith) {
                keyword = compiler::AssertOrWithKeyword::kAssert;
            }
            logger::Loc keywordLoc = this->saveExprCommentsHere();
            this->lexer.Next();
            logger::Loc openBraceLoc = this->saveExprCommentsHere();
            this->lexer.Expect(T::kOpenBrace);

            while (this->lexer.token != T::kCloseBrace) {
                // Parse the key
                logger::Loc keyLoc = this->saveExprCommentsHere();
                bool preferQuotedKey = false;
                std::u16string key;
                std::string keyText;
                if (this->lexer.IsIdentifierOrKeyword()) {
                    keyText = this->lexer.identifier.str;
                    key = helpers::StringToUTF16(keyText);
                } else if (this->lexer.token == T::kStringLiteral) {
                    key = this->lexer.StringLiteral();
                    keyText = helpers::UTF16ToString(key);
                    preferQuotedKey = !this->options.optionsThatSupportStructuralEquality.minifySyntax;
                } else {
                    this->lexer.Expect(T::kIdentifier);
                }
                if (auto it = duplicates.find(keyText); it != duplicates.end()) {
                    logger::Range prevRange = it->second;
                    std::string what = "attribute";
                    if (keyword == compiler::AssertOrWithKeyword::kAssert) {
                        what = "assertion";
                    }
                    this->log.AddErrorWithNotes(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_DuplicateImport, what, keyText),
                        {this->tracker.MakeMsgData(prevRange, logger::FormatMsg(logger::MsgCat::kJS_DuplicateImportFirstNote, keyText))});
                }
                duplicates[keyText] = this->lexer.Range();
                this->lexer.Next();
                this->lexer.Expect(T::kColon);

                // Parse the value
                logger::Loc valueLoc = this->saveExprCommentsHere();
                std::u16string value = this->lexer.StringLiteral();
                this->lexer.Expect(T::kStringLiteral);

                compiler::AssertOrWithEntry entry;
                entry.key = key;
                entry.key_loc = keyLoc;
                entry.value = value;
                entry.value_loc = valueLoc;
                entry.prefer_quoted_key = preferQuotedKey;
                entries.push_back(entry);

                // Using "assert: { type: 'json' }" triggers special behavior
                if (keyword == compiler::AssertOrWithKeyword::kAssert && helpers::UTF16EqualsString(key, "type") && helpers::UTF16EqualsString(value, "json")) {
                    flags = flags | compiler::ImportRecordFlags::kAssertTypeJSON;
                }

                if (this->lexer.token != T::kComma) {
                    break;
                }
                this->lexer.Next();
            }

            logger::Loc closeBraceLoc = this->saveExprCommentsHere();
            this->lexer.Expect(T::kCloseBrace);
            if (keyword == compiler::AssertOrWithKeyword::kAssert) {
                this->maybeWarnAboutAssertKeyword(keywordLoc);
            }
            compiler::ImportAssertOrWith *result = new compiler::ImportAssertOrWith();
            result->entries = std::move(entries);
            result->keyword = keyword;
            result->keyword_loc = keywordLoc;
            result->inner_open_brace_loc = openBraceLoc;
            result->inner_close_brace_loc = closeBraceLoc;
            assertOrWith = result;
        }

        return std::make_tuple(pathRange, pathText, assertOrWith, flags);
    }

    void Parser::parsePathAndDiscard() {
        // Some callers (e.g. TypeScript type-only imports) don't want the path
        // information. The caller of parsePath() must take ownership of the
        // returned ImportAssertOrWith (via addImportRecord or by deleting it), so
        // free it here instead of leaking it.
        std::tuple<logger::Range, std::string, compiler::ImportAssertOrWith *, compiler::ImportRecordFlags> path = this->parsePath();
        compiler::ImportAssertOrWith *assertOrWith = std::get<2>(path);
        if (assertOrWith != nullptr) {
            delete assertOrWith;
        }
    }

    std::vector<Decl> Parser::parseAndDeclareDecls(compiler::SymbolKind kind, parseStmtOpts opts) {
        std::vector<Decl> decls;

        for (;;) {
            // Forbid "let let" and "const let" but not "var let"
            if ((kind == compiler::SymbolKind::kOther || kind == compiler::SymbolKind::kConst) && this->lexer.IsContextualKeyword("let")) {
                this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_LetAsIdentifier));
            }

            Expr valueOrNil;
            parseBindingOpts bindingOpts;
            bindingOpts.isUsingStmt = opts.isUsingStmt;
            Binding local = this->parseBinding(bindingOpts);
            this->declareBinding(kind, local, opts);

            // Skip over types
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                // "let foo!"
                bool isDefiniteAssignmentAssertion = this->lexer.token == T::kExclamation && !this->lexer.has_newline_before;
                if (isDefiniteAssignmentAssertion) {
                    this->lexer.Next();
                }

                // "let foo: number"
                if (isDefiniteAssignmentAssertion || this->lexer.token == T::kColon) {
                    this->lexer.Expect(T::kColon);
                    this->skipTypeScriptType(L::kLowest);
                }
            }

            if (this->lexer.token == T::kEquals) {
                this->lexer.Next();
                valueOrNil = this->parseExpr(L::kComma);

                // Rollup (the tool that invented the "@__NO_SIDE_EFFECTS__" comment) only
                // applies this to the first declaration, and only when it's a "const".
                // For more info see: https://github.com/rollup/rollup/pull/5024/files
                if (!this->options.optionsThatSupportStructuralEquality.ignoreDCEAnnotations && kind == compiler::SymbolKind::kConst) {
                    if (auto arrow = Get<EArrow>(valueOrNil.data)) {
                        if (opts.hasNoSideEffectsComment) {
                            arrow->has_no_side_effects_comment = true;
                        }
                        if (arrow->has_no_side_effects_comment && !opts.isTypeScriptDeclare) {
                            if (auto b = Get<BIdentifier>(local.data)) {
                                this->symbols[b->ref.inner_index].flags = compiler::SymbolFlags(this->symbols[b->ref.inner_index].flags | compiler::SymbolFlags::kCallCanBeUnwrappedIfUnused);
                            }
                        }
                    } else if (auto fn = Get<EFunction>(valueOrNil.data)) {
                        if (opts.hasNoSideEffectsComment) {
                            fn->fn.has_no_side_effects_comment = true;
                        }
                        if (fn->fn.has_no_side_effects_comment && !opts.isTypeScriptDeclare) {
                            if (auto b = Get<BIdentifier>(local.data)) {
                                this->symbols[b->ref.inner_index].flags = compiler::SymbolFlags(this->symbols[b->ref.inner_index].flags | compiler::SymbolFlags::kCallCanBeUnwrappedIfUnused);
                            }
                        }
                    }

                    // Only apply this to the first declaration
                    opts.hasNoSideEffectsComment = false;
                }
            }

            Decl decl;
            decl.binding = local;
            decl.value_or_nil = valueOrNil;
            decls.push_back(decl);

            if (this->lexer.token != T::kComma) {
                break;
            }
            this->lexer.Next();
        }

        return decls;
    }    

    compiler::LocRef *Parser::parseLabelName() {
        if (this->lexer.token != T::kIdentifier || this->lexer.has_newline_before) {
            return nullptr;
        }

        compiler::LocRef *name = new compiler::LocRef{this->lexer.Loc(), this->storeNameInRef(this->lexer.identifier)};
        this->lexer.Next();
        return name;
    }


    std::pair<logger::Range, MaybeSubstring> Parser::parseJSXNamespacedName() {
        logger::Range nameRange = this->lexer.Range();
        MaybeSubstring name = this->lexer.identifier;
        this->lexer.ExpectInsideJSXElement(T::kIdentifier);

        // Parse JSX namespaces. These are not supported by React or TypeScript
        // but someone using JSX syntax in more obscure ways may find a use for
        // them. A namespaced name is just always turned into a string so you
        // can't use this feature to reference JavaScript identifiers.
        if (this->lexer.token == T::kColon) {
            // Parse the colon
            nameRange.len = this->lexer.Range().End() - nameRange.loc.start;
            std::string ns = name.str + ":";
            this->lexer.NextInsideJSXElement();

            // Parse the second identifier
            if (this->lexer.token == T::kIdentifier) {
                nameRange.len = this->lexer.Range().End() - nameRange.loc.start;
                ns += this->lexer.identifier.str;
                this->lexer.NextInsideJSXElement();
            } else {
                logger::Range r;
                r.loc = logger::Loc{nameRange.End()};
                this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_ExpectedIdentifierAfterNamespace, ns));
                throw LexerPanic();
            }
            return std::make_pair(nameRange, MaybeSubstring{ns});
        }

        return std::make_pair(nameRange, name);
    }

    std::string tagOrFragmentHelpText(const std::string &tag) {
        if (tag.empty()) {
            return "fragment tag";
        }
        return "\"" + tag + "\" tag";
    }

    std::tuple<logger::Range, std::string, Expr> Parser::parseJSXTag() {
        logger::Loc loc = this->lexer.Loc();

        // A missing tag is a fragment
        if (this->lexer.token == T::kGreaterThan) {
            return std::make_tuple(logger::Range{loc, 0}, std::string(), Expr());
        }

        // The tag is an identifier
        std::pair<logger::Range, MaybeSubstring> namespaced = this->parseJSXNamespacedName();
        logger::Range tagRange = namespaced.first;
        MaybeSubstring tagName = namespaced.second;

        // Certain identifiers are strings
        if (tagName.str.find_first_of("-:") != std::string::npos || (this->lexer.token != T::kDot && tagName.str[0] >= 'a' && tagName.str[0] <= 'z')) {
            std::shared_ptr<EString> e = std::make_shared<EString>();
            e->value = helpers::StringToUTF16(tagName.str);
            return std::make_tuple(tagRange, tagName.str, Expr{std::make_shared<EString>(*e), loc});
        }

        // Otherwise, this is an identifier
        std::shared_ptr<EIdentifier> e = std::make_shared<EIdentifier>();
        e->ref = this->storeNameInRef(tagName);
        Expr tag{e, loc};

        // Parse a member expression chain
        std::string chain;
        chain += tagName.str;
        while (this->lexer.token == T::kDot) {
            this->lexer.NextInsideJSXElement();
            logger::Range memberRange = this->lexer.Range();
            MaybeSubstring member = this->lexer.identifier;
            this->lexer.ExpectInsideJSXElement(T::kIdentifier);

            // Dashes are not allowed in member expression chains
            size_t index = member.str.find('-');
            if (index != std::string::npos) {
                logger::Range r;
                r.loc = logger::Loc{memberRange.loc.start + static_cast<int32_t>(index)};
                this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedDash));
                throw LexerPanic();
            }

            chain += "." + member.str;
            tag = Expr{*this->dotOrMangledPropParse(tag, member, memberRange.loc, OptionalChain::kNone, wasOriginallyDot), loc};
            tagRange.len = memberRange.loc.start + memberRange.len - tagRange.loc.start;
        }

        return std::make_tuple(tagRange, chain, tag);
    }

    Expr Parser::parseJSXElement(logger::Loc loc) {
        // Keep track of the location of the first JSX element for error messages
        if (this->firstJSXElementLoc.start == -1) {
            this->firstJSXElementLoc = loc;
        }

        // Parse the tag
        std::tuple<logger::Range, std::string, Expr> start = this->parseJSXTag();
        logger::Range startRange = std::get<0>(start);
        std::string startText = std::get<1>(start);
        Expr startTagOrNil = std::get<2>(start);

        // The tag may have TypeScript type arguments: "<Foo<T>/>"
        if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
            // Pass a flag to the type argument skipper because we need to call
            // js_lexer.NextInsideJSXElement() after we hit the closing ">". The next
            // token after the ">" might be an attribute name with a dash in it
            // like this: "<Foo<T> data-disabled/>"
            skipTypeScriptTypeArgumentsOpts opts;
            opts.isInsideJSXElement = true;
            this->skipTypeScriptTypeArguments(opts);
        }

        // Parse attributes
        logger::Loc previousStringWithBackslashLoc;
        std::vector<Property> properties;
        bool isSingleLine = true;
        if (!IsNil(startTagOrNil.data)) {
            for (;;) {
                if (this->lexer.has_newline_before) {
                    isSingleLine = false;
                }

                switch (this->lexer.token) {
                case T::kIdentifier: {
                    // Parse the key
                    std::pair<logger::Range, MaybeSubstring> keyInfo = this->parseJSXNamespacedName();
                    logger::Range keyRange = keyInfo.first;
                    MaybeSubstring keyName = keyInfo.second;
                    Expr key;
                    if (this->isMangledProp(keyName.str) && keyName.str.find(':') == std::string::npos) {
                        std::shared_ptr<ENameOfSymbol> e = std::make_shared<ENameOfSymbol>();
                        e->ref = this->storeNameInRef(keyName);
                        key = Expr{e, keyRange.loc};
                    } else {
                        std::shared_ptr<EString> s = std::make_shared<EString>();
                        s->value = helpers::StringToUTF16(keyName.str);
                        key = Expr{s, keyRange.loc};
                    }

                    // Parse the value
                    Expr value;
                    PropertyFlags flags = static_cast<PropertyFlags>(0);
                    if (this->lexer.token != T::kEquals) {
                        // Implicitly true value
                        flags = static_cast<PropertyFlags>(static_cast<uint8_t>(flags) | static_cast<uint8_t>(PropertyFlags::kWasShorthand));
                        std::shared_ptr<EBoolean> b = std::make_shared<EBoolean>();
                        b->value = true;
                        value = Expr{b, logger::Loc{keyRange.loc.start + keyRange.len}};
                    } else {
                        // Use NextInsideJSXElement() not Next() so we can parse a JSX-style string literal
                        this->lexer.NextInsideJSXElement();
                        if (this->lexer.token == T::kStringLiteral) {
                            logger::Loc stringLoc = this->lexer.Loc();
                            if (this->lexer.previous_backslash_quote_in_jsx.loc.start > stringLoc.start) {
                                previousStringWithBackslashLoc = stringLoc;
                            }
                            if (this->options.jsx.Preserve) {
                                std::shared_ptr<EJSXText> t = std::make_shared<EJSXText>();
                                t->raw = this->lexer.Raw();
                                value = Expr{t, stringLoc};
                            } else {
                                std::shared_ptr<EString> s = std::make_shared<EString>();
                                s->value = this->lexer.StringLiteral();
                                value = Expr{s, stringLoc};
                            }
                            this->lexer.NextInsideJSXElement();
                        } else if (this->lexer.token == T::kLessThan) {
                            // This may be removed in the future: https://github.com/facebook/jsx/issues/53
                            logger::Loc childLoc = this->lexer.Loc();
                            this->lexer.NextInsideJSXElement();
                            flags = static_cast<PropertyFlags>(static_cast<uint8_t>(flags) | static_cast<uint8_t>(PropertyFlags::kWasShorthand));
                            value = this->parseJSXElement(childLoc);

                            // The call to parseJSXElement() above doesn't consume the last
                            // TGreaterThan because the caller knows what Next() function to call.
                            // Use NextJSXElementChild() here since the next token is inside a JSX
                            // element.
                            this->lexer.NextInsideJSXElement();
                        } else {
                            // Use Expect() not ExpectInsideJSXElement() so we can parse expression tokens
                            this->lexer.Expect(T::kOpenBrace);
                            value = this->parseExpr(L::kLowest);
                            this->lexer.ExpectInsideJSXElement(T::kCloseBrace);
                        }
                    }

                    // Add a property
                    Property property;
                    property.loc = keyRange.loc;
                    property.key = key;
                    property.value_or_nil = value;
                    property.flags = flags;
                    properties.push_back(property);
                    break;
                }

                case T::kOpenBrace: {
                    // Use Next() not ExpectInsideJSXElement() so we can parse "..."
                    this->lexer.Next();
                    logger::Loc dotLoc = this->saveExprCommentsHere();
                    this->lexer.Expect(T::kDotDotDot);
                    Expr value = this->parseExpr(L::kComma);
                    Property property;
                    property.kind = PropertyKind::kSpread;
                    property.loc = dotLoc;
                    property.value_or_nil = value;
                    properties.push_back(property);

                    // Use NextInsideJSXElement() not Next() so we can parse ">>" as ">"
                    this->lexer.NextInsideJSXElement();
                    break;
                }

                default:
                    goto endParseAttributes;
                }
            }
        endParseAttributes:

            // Check for and warn about duplicate attributes
            if (properties.size() > 1 && !this->suppressWarningsAboutWeirdCode) {
                std::unordered_map<std::string, logger::Loc> keys;
                for (Property &property : properties) {
                    if (property.kind != PropertyKind::kSpread) {
                        if (auto str = Get<EString>(property.key.data)) {
                            std::string key = helpers::UTF16ToString(str->value);
                            auto prev = keys.find(key);
                            if (prev != keys.end()) {
                                logger::Range r = RangeOfIdentifier(this->source, property.key.loc);
                                logger::MsgData note = this->tracker.MakeMsgData(RangeOfIdentifier(this->source, prev->second), logger::FormatMsg(logger::MsgCat::kJS_DuplicateJSXAttributeNote, key));
                                this->log.AddIDWithNotes(logger::MsgID::kJS_DuplicateObjectKey, logger::MsgKind::kWarning, &this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_DuplicateJSXAttribute, key), std::vector<logger::MsgData>{note});
                            }
                            keys[key] = property.key.loc;
                        }
                    }
                }
            }
        }

        // People sometimes try to use the output of "JSON.stringify()" as a JSX
        // attribute when automatically-generating JSX code. Doing so is incorrect
        // because JSX strings work like XML instead of like JS (since JSX is XML-in-
        // JS). Specifically, using a backslash before a quote does not cause it to
        // be escaped:
        //
        //   JSX ends the "content" attribute here and sets "content" to 'some so-called \\'
        //                                          v
        //         <Button content="some so-called \"button text\"" />
        //                                                      ^
        //       There is no "=" after the JSX attribute "text", so we expect a ">"
        //
        // This code special-cases this error to provide a less obscure error message.
        if (this->lexer.token == T::kSyntaxError && this->lexer.Raw() == "\\" && previousStringWithBackslashLoc.start > 0) {
            logger::Msg msg;
            msg.kind = logger::MsgKind::kError;
            msg.data = this->tracker.MakeMsgData(this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_UnexpectedBackslashInJSX));

            // Option 1: Suggest using an XML escape
            std::string jsEscape = this->source.TextForRange(this->lexer.previous_backslash_quote_in_jsx);
            std::string xmlEscape;
            if (jsEscape == "\\\"") {
                xmlEscape = "&quot;";
            } else if (jsEscape == "\\'") {
                xmlEscape = "&apos;";
            }
            if (!xmlEscape.empty()) {
                logger::MsgData data = this->tracker.MakeMsgData(this->lexer.previous_backslash_quote_in_jsx, logger::FormatMsg(logger::MsgCat::kJS_BackslashEscapeNote));
                data.location->suggestion = xmlEscape;
                msg.notes.push_back(data);
            }

            // Option 2: Suggest using a JavaScript string
            logger::Range stringRange = this->source.RangeOfString(previousStringWithBackslashLoc);
            if (stringRange.len > 0) {
                logger::MsgData data = this->tracker.MakeMsgData(stringRange, logger::FormatMsg(logger::MsgCat::kJS_StringInsideBraceNote));
                data.location->suggestion = "{" + this->source.TextForRange(stringRange) + "}";
                msg.notes.push_back(data);
            }

            this->log.AddMsgID(logger::MsgID::kNone, msg);
            throw LexerPanic();
        }

        // A slash here is a self-closing element
        if (this->lexer.token == T::kSlash) {
            // Use NextInsideJSXElement() not Next() so we can parse ">>" as ">"
            logger::Loc closeLoc = this->lexer.Loc();
            this->lexer.NextInsideJSXElement();
            if (this->lexer.token != T::kGreaterThan) {
                this->lexer.Expected(T::kGreaterThan);
            }
            std::shared_ptr<EJSXElement> e = std::make_shared<EJSXElement>();
            e->tag_or_nil = startTagOrNil;
            e->properties = properties;
            e->close_loc = closeLoc;
            e->is_tag_single_line = isSingleLine;
            return Expr{e, loc};
        }

        // Attempt to provide a better error message for people incorrectly trying to
        // use arrow functions in TSX (which doesn't work because they are JSX elements)
        //
        // while the children of this element are being parsed.
        struct JSXBadArrowRestorer {
            Lexer *lexer = nullptr;
            logger::Range savedRange;
            std::string savedSuggestion;
            ~JSXBadArrowRestorer() {
                if (this->lexer != nullptr) {
                    this->lexer->could_be_bad_arrow_in_tsx--;
                    this->lexer->bad_arrow_in_tsx_range = savedRange;
                    this->lexer->bad_arrow_in_tsx_suggestion = savedSuggestion;
                }
            }
        } badArrowInTSXRestorer;

        if (this->options.optionsThatSupportStructuralEquality.ts.Parse && properties.size() == 0 && startText != "" && this->lexer.token == T::kGreaterThan &&
            this->source.contents.substr(static_cast<size_t>(this->lexer.Loc().start)).rfind(">(", static_cast<size_t>(0)) == 0) {
            badArrowInTSXRestorer.lexer = &this->lexer;
            badArrowInTSXRestorer.savedRange = this->lexer.bad_arrow_in_tsx_range;
            badArrowInTSXRestorer.savedSuggestion = this->lexer.bad_arrow_in_tsx_suggestion;

            this->lexer.could_be_bad_arrow_in_tsx++;
            this->lexer.bad_arrow_in_tsx_range = logger::Range{loc, this->lexer.Range().End() - loc.start};
            this->lexer.bad_arrow_in_tsx_suggestion = "<" + startText + ",>";
        }

        // Use ExpectJSXElementChild() so we parse child strings
        this->lexer.ExpectJSXElementChild(T::kGreaterThan);

        // Parse the children of this element
        std::vector<Expr> nullableChildren;
        for (;;) {
            switch (this->lexer.token) {
            case T::kStringLiteral:
                if (this->options.jsx.Preserve) {
                    std::shared_ptr<EJSXText> t = std::make_shared<EJSXText>();
                    t->raw = this->lexer.Raw();
                    nullableChildren.push_back(Expr{t, this->lexer.Loc()});
                } else {
                    std::u16string str = this->lexer.StringLiteral();
                    if (!str.empty()) {
                        std::shared_ptr<EString> s = std::make_shared<EString>();
                        s->value = str;
                        nullableChildren.push_back(Expr{s, this->lexer.Loc()});
                    } else {
                        // Skip this token if it turned out to be empty after trimming
                    }
                }
                this->lexer.NextJSXElementChild();
                break;

            case T::kOpenBrace:
                // Use Next() instead of NextJSXElementChild() here since the next token is an expression
                this->lexer.Next();

                // The expression is optional, and may be absent
                if (this->lexer.token == T::kCloseBrace) {
                    // Save comments even for absent expressions
                    nullableChildren.push_back(Expr{E{}, this->saveExprCommentsHere()});
                } else {
                    if (this->lexer.token == T::kDotDotDot) {
                        // TypeScript preserves "..." before JSX child expressions here.
                        // Babel gives the error "Spread children are not supported in React"
                        // instead, so it should be safe to support this TypeScript-specific
                        // behavior. Note that TypeScript's behavior changed in TypeScript 4.5.
                        // Before that, the "..." was omitted instead of being preserved.
                        logger::Loc itemLoc = this->lexer.Loc();
                this->markSyntaxFeature(compat::JSFeature::kRestArgument, this->lexer.Range());
                        this->lexer.Next();
                        std::shared_ptr<ESpread> s = std::make_shared<ESpread>();
                        s->value = this->parseExpr(L::kLowest);
                        nullableChildren.push_back(Expr{s, itemLoc});
                    } else {
                        nullableChildren.push_back(this->parseExpr(L::kLowest));
                    }
                }

                // Use ExpectJSXElementChild() so we parse child strings
                this->lexer.ExpectJSXElementChild(T::kCloseBrace);
                break;

            case T::kLessThan: {
                logger::Loc lessThanLoc = this->lexer.Loc();
                this->lexer.NextInsideJSXElement();

                if (this->lexer.token != T::kSlash) {
                    // This is a child element
                    nullableChildren.push_back(this->parseJSXElement(lessThanLoc));

                    // The call to parseJSXElement() above doesn't consume the last
                    // TGreaterThan because the caller knows what Next() function to call.
                    // Use NextJSXElementChild() here since the next token is an element
                    // child.
                    this->lexer.NextJSXElementChild();
                    continue;
                }

                // This is the closing element
                this->lexer.NextInsideJSXElement();
                std::tuple<logger::Range, std::string, Expr> end = this->parseJSXTag();
                logger::Range endRange = std::get<0>(end);
                std::string endText = std::get<1>(end);
                if (startText != endText) {
                    std::string startTag = tagOrFragmentHelpText(startText);
                    std::string endTag = tagOrFragmentHelpText(endText);
                    logger::Msg msg;
                    msg.kind = logger::MsgKind::kError;
                    msg.data = this->tracker.MakeMsgData(endRange, logger::FormatMsg(logger::MsgCat::kJS_ClosingTagMismatch, endTag, startTag));
                    msg.notes.push_back(this->tracker.MakeMsgData(startRange, logger::FormatMsg(logger::MsgCat::kJS_ClosingTagOpeningNote, startTag)));
                    msg.data.location->suggestion = startText;
                    this->log.AddMsgID(logger::MsgID::kNone, msg);
                }
                if (this->lexer.token != T::kGreaterThan) {
                    this->lexer.Expected(T::kGreaterThan);
                }

                std::shared_ptr<EJSXElement> e = std::make_shared<EJSXElement>();
                e->tag_or_nil = startTagOrNil;
                e->properties = properties;
                e->nullable_children = nullableChildren;
                e->close_loc = lessThanLoc;
                e->is_tag_single_line = isSingleLine;
                return Expr{e, loc};
            }

            case T::kEndOfFile: {
                std::string startTag = tagOrFragmentHelpText(startText);
                logger::Msg msg;
                msg.kind = logger::MsgKind::kError;
                msg.data = this->tracker.MakeMsgData(this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_UnexpectedEOFBeforeClosing, startTag));
                msg.notes.push_back(this->tracker.MakeMsgData(startRange, logger::FormatMsg(logger::MsgCat::kJS_OpeningTagNote, startTag)));
                msg.data.location->suggestion = "</" + startText + ">";
                this->log.AddMsgID(logger::MsgID::kNone, msg);
                throw LexerPanic();
            }

            default:
                this->lexer.Unexpected();
                break;
            }
        }
    }



}