#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_lexer.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/javascript/js_helpers.hpp"


namespace guchho::javascript {

    namespace {
        inline bool Has(skipTypeFlags flags, skipTypeFlags flag) {
            return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
        }

        enum class tsTypeIdentifierKind : uint8_t {
            normal,
            unique,
            abstract,
            asserts,
            prefix,
            primitive,
            infer,
        };

        // Use a map to improve lookup speed
        const std::unordered_map<std::string, tsTypeIdentifierKind> tsTypeIdentifierMap = {
            {"unique", tsTypeIdentifierKind::unique},
            {"abstract", tsTypeIdentifierKind::abstract},
            {"asserts", tsTypeIdentifierKind::asserts},

            {"keyof", tsTypeIdentifierKind::prefix},
            {"readonly", tsTypeIdentifierKind::prefix},

            {"any", tsTypeIdentifierKind::primitive},
            {"never", tsTypeIdentifierKind::primitive},
            {"unknown", tsTypeIdentifierKind::primitive},
            {"undefined", tsTypeIdentifierKind::primitive},
            {"object", tsTypeIdentifierKind::primitive},
            {"number", tsTypeIdentifierKind::primitive},
            {"string", tsTypeIdentifierKind::primitive},
            {"boolean", tsTypeIdentifierKind::primitive},
            {"bigint", tsTypeIdentifierKind::primitive},
            {"symbol", tsTypeIdentifierKind::primitive},

            {"infer", tsTypeIdentifierKind::infer},
        };
    }

    void Parser::skipTypeScriptBinding() {
        switch (this->lexer.token) {
        case T::kIdentifier:
        case T::kThis:
            this->lexer.Next();
            break;

        case T::kOpenBracket:
            this->lexer.Next();

            // "[, , a]"
            while (this->lexer.token == T::kComma) {
                this->lexer.Next();
            }

            // "[a, b]"
            while (this->lexer.token != T::kCloseBracket) {
                // "[...a]"
                if (this->lexer.token == T::kDotDotDot) {
                    this->lexer.Next();
                }

                this->skipTypeScriptBinding();
                if (this->lexer.token != T::kComma) {
                    break;
                }
                this->lexer.Next();
            }

            this->lexer.Expect(T::kCloseBracket);
            break;

        case T::kOpenBrace:
            this->lexer.Next();

            while (this->lexer.token != T::kCloseBrace) {
                bool foundIdentifier = false;

                switch (this->lexer.token) {
                case T::kDotDotDot:
                    this->lexer.Next();

                    if (this->lexer.token != T::kIdentifier) {
                        this->lexer.Unexpected();
                    }

                    // "{...x}"
                    foundIdentifier = true;
                    this->lexer.Next();
                    break;

                case T::kIdentifier:
                    // "{x}"
                    // "{x: y}"
                    foundIdentifier = true;
                    this->lexer.Next();
                    break;

                    // "{1: y}"
                    // "{'x': y}"
                case T::kStringLiteral:
                case T::kNumericLiteral:
                    this->lexer.Next();
                    break;

                default:
                    if (this->lexer.IsIdentifierOrKeyword()) {
                        // "{if: x}"
                        this->lexer.Next();
                    } else {
                        this->lexer.Unexpected();
                    }
                    break;
                }

                if (this->lexer.token == T::kColon || !foundIdentifier) {
                    this->lexer.Expect(T::kColon);
                    this->skipTypeScriptBinding();
                }

                if (this->lexer.token != T::kComma) {
                    break;
                }
                this->lexer.Next();
            }

            this->lexer.Expect(T::kCloseBrace);
            break;

        default:
            this->lexer.Unexpected();
            break;
        }
    }

    void Parser::skipTypeScriptFnArgs() {
        this->lexer.Expect(T::kOpenParen);

        while (this->lexer.token != T::kCloseParen) {
            // "(...a)"
            if (this->lexer.token == T::kDotDotDot) {
                this->lexer.Next();
            }

            this->skipTypeScriptBinding();

            // "(a?)"
            if (this->lexer.token == T::kQuestion) {
                this->lexer.Next();
            }

            // "(a: any)"
            if (this->lexer.token == T::kColon) {
                this->lexer.Next();
                this->skipTypeScriptType(L::kLowest);
            }

            // "(a, b)"
            if (this->lexer.token != T::kComma) {
                break;
            }
            this->lexer.Next();
        }

        this->lexer.Expect(T::kCloseParen);
    }

    // This is a spot where the TypeScript grammar is highly ambiguous. Here are
    // some cases that are valid:
    //
    //  let x = (y: any): (() => {}) => { };
    //  let x = (y: any): () => {} => { };
    //  let x = (y: any): (y) => {} => { };
    //  let x = (y: any): (y[]) => {};
    //  let x = (y: any): (a | b) => {};
    //
    // Here are some cases that aren't valid:
    //
    //  let x = (y: any): (y) => {};
    //  let x = (y: any): (y) => {return 0};
    //  let x = (y: any): asserts y is (y) => {};
    void Parser::skipTypeScriptParenOrFnType() {
        if (this->trySkipTypeScriptArrowArgsWithBacktracking()) {
            this->skipTypeScriptReturnType();
        } else {
            this->lexer.Expect(T::kOpenParen);
            this->skipTypeScriptType(L::kLowest);
            this->lexer.Expect(T::kCloseParen);
        }
    }

    void Parser::skipTypeScriptReturnType() {
        this->skipTypeScriptTypeWithFlags(L::kLowest, isReturnTypeFlag);
    }

    void Parser::skipTypeScriptType(L level) {
        this->skipTypeScriptTypeWithFlags(level, static_cast<skipTypeFlags>(0));
    }

    void Parser::skipTypeScriptTypeWithFlags(L level, skipTypeFlags flags) {
        for (;;) {
            switch (this->lexer.token) {
            case T::kNumericLiteral:
            case T::kBigIntegerLiteral:
            case T::kStringLiteral:
            case T::kNoSubstitutionTemplateLiteral:
            case T::kTrue:
            case T::kFalse:
            case T::kNull:
            case T::kVoid:
                this->lexer.Next();
                break;

            case T::kConst: {
                logger::Range r = this->lexer.Range();
                this->lexer.Next();

                // "[const: number]"
                if (Has(flags, allowTupleLabelsFlag) && this->lexer.token == T::kColon) {
                    this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kTS_UnexpectedConst));
                }
                break;
            }

            case T::kThis:
                this->lexer.Next();

                // "function check(): this is boolean"
                if (this->lexer.IsContextualKeyword("is") && !this->lexer.has_newline_before) {
                    this->lexer.Next();
                    this->skipTypeScriptType(L::kLowest);
                    return;
                }
                break;

            case T::kMinus:
                // "-123"
                // "-123n"
                this->lexer.Next();
                if (this->lexer.token == T::kBigIntegerLiteral) {
                    this->lexer.Next();
                } else {
                    this->lexer.Expect(T::kNumericLiteral);
                }
                break;

            case T::kAmpersand:
            case T::kBar:
                // Support things like "type Foo = | A | B" and "type Foo = & A & B"
                this->lexer.Next();
                continue;

            case T::kImport:
                // "import('fs')"
                this->lexer.Next();

                // "[import: number]"
                // "[import?: number]"
                if (Has(flags, allowTupleLabelsFlag) && (this->lexer.token == T::kColon || this->lexer.token == T::kQuestion)) {
                    return;
                }

                this->lexer.Expect(T::kOpenParen);
                this->lexer.Expect(T::kStringLiteral);

                // "import('./foo.json', { assert: { type: 'json' } })"
                if (this->lexer.token == T::kComma) {
                    this->lexer.Next();
                    this->skipTypeScriptObjectType();

                    // "import('./foo.json', { assert: { type: 'json' } }, )"
                    if (this->lexer.token == T::kComma) {
                        this->lexer.Next();
                    }
                }

                this->lexer.Expect(T::kCloseParen);
                break;

            case T::kNew:
                // "new () => Foo"
                // "new <T>() => Foo<T>"
                this->lexer.Next();

                // "[new: number]"
                // "[new?: number]"
                if (Has(flags, allowTupleLabelsFlag) && (this->lexer.token == T::kColon || this->lexer.token == T::kQuestion)) {
                    return;
                }

                this->skipTypeScriptTypeParameters(allowConstModifier);
                this->skipTypeScriptParenOrFnType();
                break;

            case T::kLessThan:
                // "<T>() => Foo<T>"
                this->skipTypeScriptTypeParameters(allowConstModifier);
                this->skipTypeScriptParenOrFnType();
                break;

            case T::kOpenParen:
                // "(number | string)"
                this->skipTypeScriptParenOrFnType();
                break;

            case T::kIdentifier: {
                tsTypeIdentifierKind kind = tsTypeIdentifierKind::normal;
                auto mapIt = tsTypeIdentifierMap.find(this->lexer.identifier.str);
                if (mapIt != tsTypeIdentifierMap.end()) {
                    kind = mapIt->second;
                }
                bool checkTypeParameters = true;

                switch (kind) {
                case tsTypeIdentifierKind::prefix:
                    this->lexer.Next();

                    // Valid:
                    //   "[keyof: string]"
                    //   "[keyof?: string]"
                    //   "{[keyof: string]: number}"
                    //   "{[keyof in string]: number}"
                    //
                    // Invalid:
                    //   "A extends B ? keyof : string"
                    //
                    if ((this->lexer.token != T::kColon && this->lexer.token != T::kQuestion && this->lexer.token != T::kIn) ||
                        (!Has(flags, isIndexSignatureFlag) && !Has(flags, allowTupleLabelsFlag))) {
                        this->skipTypeScriptType(L::kPrefix);
                    }
                    goto exitTypeAtomLoop;

                case tsTypeIdentifierKind::infer:
                    this->lexer.Next();

                    // "type Foo = Bar extends [infer T] ? T : null"
                    // "type Foo = Bar extends [infer T extends string] ? T : null"
                    // "type Foo = Bar extends [infer T extends string ? infer T : never] ? T : null"
                    // "type Foo = { [infer in Bar]: number }"
                    // "type Foo = [infer: number]"
                    // "type Foo = [infer?: number]"
                    if ((this->lexer.token != T::kColon && this->lexer.token != T::kQuestion && this->lexer.token != T::kIn) ||
                        (!Has(flags, isIndexSignatureFlag) && !Has(flags, allowTupleLabelsFlag))) {
                        this->lexer.Expect(T::kIdentifier);
                        if (this->lexer.token == T::kExtends) {
                            this->trySkipTypeScriptConstraintOfInferTypeWithBacktracking(flags);
                        }
                    }
                    goto exitTypeAtomLoop;

                case tsTypeIdentifierKind::unique:
                    this->lexer.Next();

                    // "let foo: unique symbol"
                    if (this->lexer.IsContextualKeyword("symbol")) {
                        this->lexer.Next();
                        goto exitTypeAtomLoop;
                    }
                    break;

                case tsTypeIdentifierKind::abstract:
                    this->lexer.Next();

                    // "let foo: abstract new () => {}" added in TypeScript 4.2
                    if (this->lexer.token == T::kNew) {
                        continue;
                    }
                    break;

                case tsTypeIdentifierKind::asserts:
                    this->lexer.Next();

                    // "function assert(x: boolean): asserts x"
                    // "function assert(x: boolean): asserts x is boolean"
                    if (Has(flags, isReturnTypeFlag) && !this->lexer.has_newline_before &&
                        (this->lexer.token == T::kIdentifier || this->lexer.token == T::kThis)) {
                        this->lexer.Next();
                    }
                    break;

                case tsTypeIdentifierKind::primitive:
                    this->lexer.Next();
                    checkTypeParameters = false;
                    break;

                default:
                    this->lexer.Next();
                    break;
                }

                // "function assert(x: any): x is boolean"
                if (this->lexer.IsContextualKeyword("is") && !this->lexer.has_newline_before) {
                    this->lexer.Next();
                    this->skipTypeScriptType(L::kLowest);
                    return;
                }

                // "let foo: any \n <number>foo" must not become a single type
                if (checkTypeParameters && !this->lexer.has_newline_before) {
                    this->skipTypeScriptTypeArguments(skipTypeScriptTypeArgumentsOpts{});
                }
                break;
            }

            case T::kTypeof:
                this->lexer.Next();

                // "[typeof: number]"
                // "[typeof?: number]"
                if (Has(flags, allowTupleLabelsFlag) && (this->lexer.token == T::kColon || this->lexer.token == T::kQuestion)) {
                    return;
                }

                if (this->lexer.token == T::kImport) {
                    // "typeof import('fs')"
                    continue;
                } else {
                    // "typeof x"
                    if (!this->lexer.IsIdentifierOrKeyword()) {
                        this->lexer.Expected(T::kIdentifier);
                    }
                    this->lexer.Next();

                    // "typeof x.y"
                    // "typeof x.#y"
                    while (this->lexer.token == T::kDot) {
                        this->lexer.Next();
                        if (!this->lexer.IsIdentifierOrKeyword() && this->lexer.token != T::kPrivateIdentifier) {
                            this->lexer.Expected(T::kIdentifier);
                        }
                        this->lexer.Next();
                    }

                    if (!this->lexer.has_newline_before) {
                        this->skipTypeScriptTypeArguments(skipTypeScriptTypeArgumentsOpts{});
                    }
                }
                break;

            case T::kOpenBracket:
                // "[number, string]"
                // "[first: number, second: string]"
                this->lexer.Next();
                while (this->lexer.token != T::kCloseBracket) {
                    if (this->lexer.token == T::kDotDotDot) {
                        this->lexer.Next();
                    }
                    this->skipTypeScriptTypeWithFlags(L::kLowest, allowTupleLabelsFlag);
                    if (this->lexer.token == T::kQuestion) {
                        this->lexer.Next();
                    }
                    if (this->lexer.token == T::kColon) {
                        this->lexer.Next();
                        this->skipTypeScriptType(L::kLowest);
                    }
                    if (this->lexer.token != T::kComma) {
                        break;
                    }
                    this->lexer.Next();
                }
                this->lexer.Expect(T::kCloseBracket);
                break;

            case T::kOpenBrace:
                this->skipTypeScriptObjectType();
                break;

            case T::kTemplateHead:
                // "`${'a' | 'b'}-${'c' | 'd'}`"
                for (;;) {
                    this->lexer.Next();
                    this->skipTypeScriptType(L::kLowest);
                    this->lexer.RescanCloseBraceAsTemplateToken();
                    if (this->lexer.token == T::kTemplateTail) {
                        this->lexer.Next();
                        break;
                    }
                }
                break;

            default:
                // "[function: number]"
                // "[function?: number]"
                if (Has(flags, allowTupleLabelsFlag) && this->lexer.IsIdentifierOrKeyword()) {
                    if (this->lexer.token != T::kFunction) {
                        this->log.AddError(&this->tracker, this->lexer.Range(),
                            logger::FormatMsg(logger::MsgCat::kTS_UnexpectedToken, std::string(this->lexer.Raw())));
                    }
                    this->lexer.Next();
                    if (this->lexer.token != T::kColon && this->lexer.token != T::kQuestion) {
                        this->lexer.Expect(T::kColon);
                    }
                    return;
                }

                this->lexer.Unexpected();
                break;
            }
            break;
        }
    exitTypeAtomLoop:
        // The type atom loop is done. Continue on to parse infix type operators.
        for (;;) {
            switch (this->lexer.token) {
            case T::kBar:
                if (level >= L::kBitwiseOr) {
                    return;
                }
                this->lexer.Next();
                this->skipTypeScriptTypeWithFlags(L::kBitwiseOr, flags);
                break;

            case T::kAmpersand:
                if (level >= L::kBitwiseAnd) {
                    return;
                }
                this->lexer.Next();
                this->skipTypeScriptTypeWithFlags(L::kBitwiseAnd, flags);
                break;

            case T::kExclamation:
                // A postfix "!" is allowed in JSDoc types in TypeScript, which are only
                // present in comments. While it's not valid in a non-comment position,
                // it's still parsed and turned into a soft error by the TypeScript
                // compiler. It turns out parsing this is important for correctness for
                // "as" casts because the "!" token must still be consumed.
                if (this->lexer.has_newline_before) {
                    return;
                }
                this->lexer.Next();
                break;

            case T::kDot:
                this->lexer.Next();
                if (!this->lexer.IsIdentifierOrKeyword()) {
                    this->lexer.Expect(T::kIdentifier);
                }
                this->lexer.Next();

                // "{ <A extends B>(): c.d \n <E extends F>(): g.h }" must not become a single type
                if (!this->lexer.has_newline_before) {
                    this->skipTypeScriptTypeArguments(skipTypeScriptTypeArgumentsOpts{});
                }
                break;

            case T::kOpenBracket:
                // "{ ['x']: string \n ['y']: string }" must not become a single type
                if (this->lexer.has_newline_before) {
                    return;
                }
                this->lexer.Next();
                if (this->lexer.token != T::kCloseBracket) {
                    this->skipTypeScriptType(L::kLowest);
                }
                this->lexer.Expect(T::kCloseBracket);
                break;

            case T::kExtends:
                // "{ x: number \n extends: boolean }" must not become a single type
                if (this->lexer.has_newline_before || Has(flags, disallowConditionalTypesFlag)) {
                    return;
                }
                this->lexer.Next();

                // The type following "extends" is not permitted to be another conditional type
                this->skipTypeScriptTypeWithFlags(L::kLowest, disallowConditionalTypesFlag);
                this->lexer.Expect(T::kQuestion);
                this->skipTypeScriptType(L::kLowest);
                this->lexer.Expect(T::kColon);
                this->skipTypeScriptType(L::kLowest);
                break;

            default:
                return;
            }
        }
    }

    void Parser::skipTypeScriptObjectType() {
        this->lexer.Expect(T::kOpenBrace);

        while (this->lexer.token != T::kCloseBrace) {
            // "{ -readonly [K in keyof T]: T[K] }"
            // "{ +readonly [K in keyof T]: T[K] }"
            if (this->lexer.token == T::kPlus || this->lexer.token == T::kMinus) {
                this->lexer.Next();
            }

            // Skip over modifiers and the property identifier
            bool foundKey = false;
            while (this->lexer.IsIdentifierOrKeyword() ||
                this->lexer.token == T::kStringLiteral ||
                this->lexer.token == T::kNumericLiteral) {
                this->lexer.Next();
                foundKey = true;
            }

            if (this->lexer.token == T::kOpenBracket) {
                // Index signature or computed property
                this->lexer.Next();
                this->skipTypeScriptTypeWithFlags(L::kLowest, isIndexSignatureFlag);

                // "{ [key: string]: number }"
                // "{ readonly [K in keyof T]: T[K] }"
                if (this->lexer.token == T::kColon) {
                    this->lexer.Next();
                    this->skipTypeScriptType(L::kLowest);
                } else if (this->lexer.token == T::kIn) {
                    this->lexer.Next();
                    this->skipTypeScriptType(L::kLowest);
                    if (this->lexer.IsContextualKeyword("as")) {
                        // "{ [K in keyof T as `get-${K}`]: T[K] }"
                        this->lexer.Next();
                        this->skipTypeScriptType(L::kLowest);
                    }
                }

                this->lexer.Expect(T::kCloseBracket);

                // "{ [K in keyof T]+?: T[K] }"
                // "{ [K in keyof T]-?: T[K] }"
                if (this->lexer.token == T::kPlus || this->lexer.token == T::kMinus) {
                    this->lexer.Next();
                }

                foundKey = true;
            }

            // "?" indicates an optional property
            // "!" indicates an initialization assertion
            if (foundKey && (this->lexer.token == T::kQuestion || this->lexer.token == T::kExclamation)) {
                this->lexer.Next();
            }

            // Type parameters come right after the optional mark
            this->skipTypeScriptTypeParameters(allowConstModifier);

            switch (this->lexer.token) {
            case T::kColon:
                // Regular property
                if (!foundKey) {
                    this->lexer.Expect(T::kIdentifier);
                }
                this->lexer.Next();
                this->skipTypeScriptType(L::kLowest);
                break;

            case T::kOpenParen:
                // Method signature
                this->skipTypeScriptFnArgs();
                if (this->lexer.token == T::kColon) {
                    this->lexer.Next();
                    this->skipTypeScriptReturnType();
                }
                break;

            default:
                if (!foundKey) {
                    this->lexer.Unexpected();
                }
                break;
            }

            switch (this->lexer.token) {
            case T::kCloseBrace:
                break;

            case T::kComma:
            case T::kSemicolon:
                this->lexer.Next();
                break;

            default:
                if (!this->lexer.has_newline_before) {
                    this->lexer.Unexpected();
                }
                break;
            }
        }

        this->lexer.Expect(T::kCloseBrace);
    }

    // This is the type parameter declarations that go with other symbol
    // declarations (class, function, type, etc.)
    ESkipTypeScript Parser::skipTypeScriptTypeParameters(typeParameterFlags flags) {
        if (this->lexer.token != T::kLessThan) {
            return didNotSkipAnything;
        }

        this->lexer.Next();
        ESkipTypeScript result = skippedTypeParameters;

        if ((flags & allowEmptyTypeParameters) != 0 && this->lexer.token == T::kGreaterThan) {
            this->lexer.Next();
            return definitelyTypeParameters;
        }

        for (;;) {
            bool hasIn = false;
            bool hasOut = false;
            bool expectIdentifier = true;
            logger::Range invalidModifierRange;

            // Scan over a sequence of "in" and "out" modifiers (a.k.a. optional
            // variance annotations) as well as "const" modifiers
            for (;;) {
                if (this->lexer.token == T::kConst) {
                    if (invalidModifierRange.len == 0 && (flags & allowConstModifier) == 0) {
                        // Valid:
                        //   "class Foo<const T> {}"
                        // Invalid:
                        //   "interface Foo<const T> {}"
                        invalidModifierRange = this->lexer.Range();
                    }
                    result = definitelyTypeParameters;
                    this->lexer.Next();
                    expectIdentifier = true;
                    continue;
                }

                if (this->lexer.token == T::kIn) {
                    if (invalidModifierRange.len == 0 && ((flags & allowInOutVarianceAnnotations) == 0 || hasIn || hasOut)) {
                        // Valid:
                        //   "type Foo<in T> = T"
                        // Invalid:
                        //   "type Foo<in in T> = T"
                        //   "type Foo<out in T> = T"
                        invalidModifierRange = this->lexer.Range();
                    }
                    this->lexer.Next();
                    hasIn = true;
                    expectIdentifier = true;
                    continue;
                }

                if (this->lexer.IsContextualKeyword("out")) {
                    logger::Range r = this->lexer.Range();
                    if (invalidModifierRange.len == 0 && (flags & allowInOutVarianceAnnotations) == 0) {
                        invalidModifierRange = r;
                    }
                    this->lexer.Next();
                    if (invalidModifierRange.len == 0 && hasOut && (this->lexer.token == T::kIn || this->lexer.token == T::kIdentifier)) {
                        // Valid:
                        //   "type Foo<out T> = T"
                        //   "type Foo<out out> = T"
                        //   "type Foo<out out, T> = T"
                        //   "type Foo<out out = T> = T"
                        //   "type Foo<out out extends T> = T"
                        // Invalid:
                        //   "type Foo<out out in T> = T"
                        //   "type Foo<out out T> = T"
                        invalidModifierRange = r;
                    }
                    hasOut = true;
                    expectIdentifier = false;
                    continue;
                }

                break;
            }

            // Only report an error for the first invalid modifier
            if (invalidModifierRange.len > 0) {
                this->log.AddError(&this->tracker, invalidModifierRange,
                    logger::FormatMsg(logger::MsgCat::kTS_ModifierNotValid, this->source.TextForRange(invalidModifierRange)));
            }

            // expectIdentifier => Mandatory identifier (e.g. after "type Foo <in ___")
            // !expectIdentifier => Optional identifier (e.g. after "type Foo <out ___" since "out" may be the identifier)
            if (expectIdentifier || this->lexer.token == T::kIdentifier) {
                this->lexer.Expect(T::kIdentifier);
            }

            // "class Foo<T extends number> {}"
            if (this->lexer.token == T::kExtends) {
                result = definitelyTypeParameters;
                this->lexer.Next();
                this->skipTypeScriptType(L::kLowest);
            }

            // "class Foo<T = void> {}"
            if (this->lexer.token == T::kEquals) {
                result = definitelyTypeParameters;
                this->lexer.Next();
                this->skipTypeScriptType(L::kLowest);
            }

            if (this->lexer.token != T::kComma) {
                break;
            }
            this->lexer.Next();
            if (this->lexer.token == T::kGreaterThan) {
                result = definitelyTypeParameters;
                break;
            }
        }

        this->lexer.ExpectGreaterThan(false);
        return result;
    }

    bool Parser::skipTypeScriptTypeArguments(skipTypeScriptTypeArgumentsOpts opts) {
        switch (this->lexer.token) {
        case T::kLessThan:
        case T::kLessThanEquals:
        case T::kLessThanLessThan:
        case T::kLessThanLessThanEquals:
            break;
        default:
            return false;
        }

        this->lexer.ExpectLessThan(false);

        for (;;) {
            this->skipTypeScriptType(L::kLowest);
            if (this->lexer.token != T::kComma) {
                break;
            }
            this->lexer.Next();
        }

        // This type argument list must end with a ">"
        if (!opts.isParseTypeArgumentsInExpression) {
            // Normally TypeScript allows any token starting with ">". For example,
            // "Array<Array<number>>()" is a type argument list even though there's a
            // ">>" token, because ">>" starts with ">".
            this->lexer.ExpectGreaterThan(opts.isInsideJSXElement);
        } else {
            // However, if we're emulating the TypeScript compiler's function called
            // "parseTypeArgumentsInExpression" function, then we must only allow the
            // ">" token itself. For example, "x < y >= z" is not a type argument list.
            //
            // This doesn't detect ">>" in "Array<Array<number>>()" because the inner
            // type argument list isn't a call to "parseTypeArgumentsInExpression"
            // because it's within a type context, not an expression context. So the
            // token that we see here is ">" in that case because the first ">" has
            // already been stripped off of the ">>" by the inner call.
            if (opts.isInsideJSXElement) {
                this->lexer.ExpectInsideJSXElement(T::kGreaterThan);
            } else {
                this->lexer.Expect(T::kGreaterThan);
            }
        }
        return true;
    }

    bool Parser::trySkipTypeArgumentsInExpressionWithBacktracking() {
        Lexer oldLexer = this->lexer;
        this->lexer.is_log_disabled = true;

        // Implement backtracking by restoring the lexer's memory to its original state.
        // makes the function return the zero value of its result type (i.e. "false").
        try {
            if (this->skipTypeScriptTypeArguments(skipTypeScriptTypeArgumentsOpts{false, true})) {
                // Check the token after the type argument list and backtrack if it's invalid
                if (!this->tsCanFollowTypeArgumentsInExpression()) {
                    this->lexer.Unexpected();
                }
            }

            // Restore the log disabled flag. Note that we can't just set it back to false
            // because it may have been true to start with.
            this->lexer.is_log_disabled = oldLexer.is_log_disabled;
        } catch (const LexerPanic &) {
            this->lexer = oldLexer;
            return false;
        }
        return true;
    }

    ESkipTypeScript Parser::trySkipTypeScriptTypeParametersThenOpenParenWithBacktracking() {
        ESkipTypeScript result = didNotSkipAnything;
        Lexer oldLexer = this->lexer;
        this->lexer.is_log_disabled = true;

        // Implement backtracking by restoring the lexer's memory to its original state.
        // makes the function return the zero value of its result type
        // (i.e. "didNotSkipAnything").
        try {
            result = this->skipTypeScriptTypeParameters(allowConstModifier);
            if (this->lexer.token != T::kOpenParen) {
                this->lexer.Unexpected();
            }

            // Restore the log disabled flag. Note that we can't just set it back to false
            // because it may have been true to start with.
            this->lexer.is_log_disabled = oldLexer.is_log_disabled;
        } catch (const LexerPanic &) {
            this->lexer = oldLexer;
            result = didNotSkipAnything;
        }
        return result;
    }

    bool Parser::trySkipTypeScriptArrowReturnTypeWithBacktracking() {
        Lexer oldLexer = this->lexer;
        this->lexer.is_log_disabled = true;

        // Implement backtracking by restoring the lexer's memory to its original state.
        // makes the function return the zero value of its result type (i.e. "false").
        try {
            this->lexer.Expect(T::kColon);
            this->skipTypeScriptReturnType();

            // Check the token after this and backtrack if it's the wrong one
            if (this->lexer.token != T::kEqualsGreaterThan) {
                this->lexer.Unexpected();
            }

            // Restore the log disabled flag. Note that we can't just set it back to false
            // because it may have been true to start with.
            this->lexer.is_log_disabled = oldLexer.is_log_disabled;
        } catch (const LexerPanic &) {
            this->lexer = oldLexer;
            return false;
        }
        return true;
    }

    // This is a very specific function that determines whether a colon token is a
    // TypeScript arrow function return type in the case where the arrow function
    // is the middle expression of a JavaScript ternary operator (i.e. is between
    // the "?" and ":" tokens). It's separate from the other function above called
    // "trySkipTypeScriptArrowReturnTypeWithBacktracking" because it's much more
    // expensive, and likely not as robust.
    bool Parser::isTypeScriptArrowReturnTypeAfterQuestionAndBeforeColon(awaitOrYield await) {
        // Implement "backtracking" by swallowing lexer errors on a temporary parser
        try {
            logger::Log deferLog = logger::NewDeferLog(logger::DeferLogKind::kDeferLogNoVerboseOrDebug, {});
            Parser *p = newParser(deferLog, this->source, this->lexer, &this->options);

            // Clone all state that the parser needs to parse this arrow function body
            p->allowIn = this->allowIn;
            p->lexer.is_log_disabled = true;
            p->pushScopeForParsePass(ScopeKind::kEntry, logger::Loc{0});
            p->pushScopeForParsePass(ScopeKind::kFunctionArgs, logger::Loc{1});

            // Parse the return type
            p->lexer.Expect(T::kColon);
            p->skipTypeScriptReturnType();

            // Parse the body and throw it out (with the side effect of maybe throwing an error)
            struct fnOrArrowDataParse data;
            data.await = await;
            p->parseArrowBody(std::vector<Arg>{}, data);

            // There must be a colon following the arrow function body to pair with the leading "?"
            p->lexer.Expect(T::kColon);

            // Parsing was successful if we get here
            return true;
        } catch (const LexerPanic &) {
            // Swallow this error
            return false;
        }
    }

    bool Parser::trySkipTypeScriptArrowArgsWithBacktracking() {
        Lexer oldLexer = this->lexer;
        this->lexer.is_log_disabled = true;

        // Implement backtracking by restoring the lexer's memory to its original state.
        // makes the function return the zero value of its result type (i.e. "false").
        try {
            this->skipTypeScriptFnArgs();
            this->lexer.Expect(T::kEqualsGreaterThan);

            // Restore the log disabled flag. Note that we can't just set it back to false
            // because it may have been true to start with.
            this->lexer.is_log_disabled = oldLexer.is_log_disabled;
        } catch (const LexerPanic &) {
            this->lexer = oldLexer;
            return false;
        }
        return true;
    }

    bool Parser::trySkipTypeScriptConstraintOfInferTypeWithBacktracking(skipTypeFlags flags) {
        Lexer oldLexer = this->lexer;
        this->lexer.is_log_disabled = true;

        // Implement backtracking by restoring the lexer's memory to its original state.
        // makes the function return the zero value of its result type (i.e. "false").
        try {
            this->lexer.Expect(T::kExtends);
            this->skipTypeScriptTypeWithFlags(L::kPrefix, disallowConditionalTypesFlag);
            if (!Has(flags, disallowConditionalTypesFlag) && this->lexer.token == T::kQuestion) {
                this->lexer.Unexpected();
            }

            // Restore the log disabled flag. Note that we can't just set it back to false
            // because it may have been true to start with.
            this->lexer.is_log_disabled = oldLexer.is_log_disabled;
        } catch (const LexerPanic &) {
            this->lexer = oldLexer;
            return false;
        }
        return true;
    }

    // This function is taken from the official TypeScript compiler source code:
    // https://github.com/microsoft/TypeScript/blob/master/src/compiler/parser.ts
    //
    // This function is pretty inefficient as written, and could be collapsed into
    // a single switch statement. But that would make it harder to keep this in
    // sync with the TypeScript compiler's source code, so we keep doing it the
    // slow way.
    bool Parser::tsCanFollowTypeArgumentsInExpression() {
        switch (this->lexer.token) {
        case
            // These tokens can follow a type argument list in a call expression.
            T::kOpenParen:
        case T::kNoSubstitutionTemplateLiteral:
        case T::kTemplateHead:
            return true;

        // A type argument list followed by `<` never makes sense, and a type argument list followed
        // by `>` is ambiguous with a (re-scanned) `>>` operator, so we disqualify both. Also, in
        // this context, `+` and `-` are unary operators, not binary operators.
        case T::kLessThan:
        case T::kGreaterThan:
        case T::kPlus:
        case T::kMinus:
            // TypeScript always sees "TGreaterThan" instead of these tokens since
            // their scanner works a little differently than our lexer. So since
            // "TGreaterThan" is forbidden above, we also forbid these too.
        case T::kGreaterThanEquals:
        case T::kGreaterThanGreaterThan:
        case T::kGreaterThanGreaterThanEquals:
        case T::kGreaterThanGreaterThanGreaterThan:
        case T::kGreaterThanGreaterThanGreaterThanEquals:
            return false;

        default:
            break;
        }

        // We favor the type argument list interpretation when it is immediately followed by
        // a line break, a binary operator, or something that can't start an expression.
        return this->lexer.has_newline_before || this->tsIsBinaryOperator() || !this->tsIsStartOfExpression();
    }

    // This function is taken from the official TypeScript compiler source code:
    // https://github.com/microsoft/TypeScript/blob/master/src/compiler/parser.ts
    bool Parser::tsIsBinaryOperator() {
        switch (this->lexer.token) {
        case T::kIn:
            return this->allowIn;

        case T::kQuestionQuestion:
        case T::kBarBar:
        case T::kAmpersandAmpersand:
        case T::kBar:
        case T::kCaret:
        case T::kAmpersand:
        case T::kEqualsEquals:
        case T::kExclamationEquals:
        case T::kEqualsEqualsEquals:
        case T::kExclamationEqualsEquals:
        case T::kLessThan:
        case T::kGreaterThan:
        case T::kLessThanEquals:
        case T::kGreaterThanEquals:
        case T::kInstanceof:
        case T::kLessThanLessThan:
        case T::kGreaterThanGreaterThan:
        case T::kGreaterThanGreaterThanGreaterThan:
        case T::kPlus:
        case T::kMinus:
        case T::kAsterisk:
        case T::kSlash:
        case T::kPercent:
        case T::kAsteriskAsterisk:
            return true;

        case T::kIdentifier:
            if (this->lexer.IsContextualKeyword("as") || this->lexer.IsContextualKeyword("satisfies")) {
                return true;
            }
            break;

        default:
            break;
        }

        return false;
    }

    // This function is taken from the official TypeScript compiler source code:
    // https://github.com/microsoft/TypeScript/blob/master/src/compiler/parser.ts
    bool Parser::tsIsStartOfExpression() {
        if (this->tsIsStartOfLeftHandSideExpression()) {
            return true;
        }

        switch (this->lexer.token) {
        case T::kPlus:
        case T::kMinus:
        case T::kTilde:
        case T::kExclamation:
        case T::kDelete:
        case T::kTypeof:
        case T::kVoid:
        case T::kPlusPlus:
        case T::kMinusMinus:
        case T::kLessThan:
        case T::kPrivateIdentifier:
        case T::kAt:
            return true;

        default:
            break;
        }

        if (this->lexer.token == T::kIdentifier && (this->lexer.identifier.str == "await" || this->lexer.identifier.str == "yield")) {
            // Yield/await always starts an expression.  Either it is an identifier (in which case
            // it is definitely an expression).  Or it's a keyword (either because we're in
            // a generator or async function, or in strict mode (or both)) and it started a yield or await expression.
            return true;
        }

        // Error tolerance.  If we see the start of some binary operator, we consider
        // that the start of an expression.  That way we'll parse out a missing identifier,
        // give a good message about an identifier being missing, and then consume the
        // rest of the binary expression.
        if (this->tsIsBinaryOperator()) {
            return true;
        }

        return this->tsIsIdentifier();
    }

    // This function is taken from the official TypeScript compiler source code:
    // https://github.com/microsoft/TypeScript/blob/master/src/compiler/parser.ts
    bool Parser::tsIsStartOfLeftHandSideExpression() {
        switch (this->lexer.token) {
        case T::kThis:
        case T::kSuper:
        case T::kNull:
        case T::kTrue:
        case T::kFalse:
        case T::kNumericLiteral:
        case T::kBigIntegerLiteral:
        case T::kStringLiteral:
        case T::kNoSubstitutionTemplateLiteral:
        case T::kTemplateHead:
        case T::kOpenParen:
        case T::kOpenBracket:
        case T::kOpenBrace:
        case T::kFunction:
        case T::kClass:
        case T::kNew:
        case T::kSlash:
        case T::kSlashEquals:
        case T::kIdentifier:
            return true;

        case T::kImport:
            return this->tsLookAheadNextTokenIsOpenParenOrLessThanOrDot();

        default:
            return this->tsIsIdentifier();
        }
    }

    // This function is taken from the official TypeScript compiler source code:
    // https://github.com/microsoft/TypeScript/blob/master/src/compiler/parser.ts
    bool Parser::tsLookAheadNextTokenIsOpenParenOrLessThanOrDot() {
        Lexer oldLexer = this->lexer;
        this->lexer.Next();

        bool result = this->lexer.token == T::kOpenParen ||
            this->lexer.token == T::kLessThan ||
            this->lexer.token == T::kDot;

        // Restore the lexer
        this->lexer = oldLexer;
        return result;
    }

    // This function is taken from the official TypeScript compiler source code:
    // https://github.com/microsoft/TypeScript/blob/master/src/compiler/parser.ts
    bool Parser::tsIsIdentifier() {
        if (this->lexer.token == T::kIdentifier) {
            // If we have a 'yield' keyword, and we're in the [yield] context, then 'yield' is
            // considered a keyword and is not an identifier.
            if (this->fnOrArrowDataParse.yield != allowIdent && this->lexer.identifier.str == "yield") {
                return false;
            }

            // If we have a 'await' keyword, and we're in the [Await] context, then 'await' is
            // considered a keyword and is not an identifier.
            if (this->fnOrArrowDataParse.await != allowIdent && this->lexer.identifier.str == "await") {
                return false;
            }

            return true;
        }

        return false;
    }

    void Parser::skipTypeScriptInterfaceStmt(parseStmtOpts opts) {
        std::string name = this->lexer.identifier.str;
        this->lexer.Expect(T::kIdentifier);

        if (opts.isModuleScope) {
            this->localTypeNames[name] = true;
        }

        this->skipTypeScriptTypeParameters(static_cast<typeParameterFlags>(allowInOutVarianceAnnotations | allowEmptyTypeParameters));

        if (this->lexer.token == T::kExtends) {
            this->lexer.Next();
            for (;;) {
                this->skipTypeScriptType(L::kLowest);
                if (this->lexer.token != T::kComma) {
                    break;
                }
                this->lexer.Next();
            }
        }

        if (this->lexer.IsContextualKeyword("implements")) {
            this->lexer.Next();
            for (;;) {
                this->skipTypeScriptType(L::kLowest);
                if (this->lexer.token != T::kComma) {
                    break;
                }
                this->lexer.Next();
            }
        }

        this->skipTypeScriptObjectType();
    }

    void Parser::skipTypeScriptTypeStmt(parseStmtOpts opts) {
        if (opts.isExport) {
            switch (this->lexer.token) {
            case T::kOpenBrace:
                // "export type {foo}"
                // "export type {foo} from 'bar'"
                (void)this->parseExportClause();
                if (this->lexer.IsContextualKeyword("from")) {
                    this->lexer.Next();
                    this->parsePathAndDiscard();
                }
                this->lexer.ExpectOrInsertSemicolon();
                return;

            // This is invalid TypeScript, and is rejected by the TypeScript compiler:
            //
            //   example.ts:1:1 - error TS1383: Only named exports may use 'export type'.
            //
            //   1 export type * from './types'
            //     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            //
            // However, people may not know this and then blame guchho for it not
            // working. So we parse it anyway and then discard it (since we always
            // discard all types). People who do this should be running the TypeScript
            // type checker when using TypeScript, which will then report this error.
            case T::kAsterisk:
                // "export type * from 'path'"
                this->lexer.Next();
                if (this->lexer.IsContextualKeyword("as")) {
                    // "export type * as ns from 'path'"
                    this->lexer.Next();
                    (void)this->parseClauseAlias("export");
                    this->lexer.Next();
                }
                this->lexer.ExpectContextualKeyword("from");
                this->parsePathAndDiscard();
                this->lexer.ExpectOrInsertSemicolon();
                return;

            default:
                break;
            }
        }

        std::string name = this->lexer.identifier.str;
        this->lexer.Expect(T::kIdentifier);

        if (opts.isModuleScope) {
            this->localTypeNames[name] = true;
        }

        this->skipTypeScriptTypeParameters(static_cast<typeParameterFlags>(allowInOutVarianceAnnotations | allowEmptyTypeParameters));
        this->lexer.Expect(T::kEquals);
        this->skipTypeScriptType(L::kLowest);
        this->lexer.ExpectOrInsertSemicolon();
    }

    Stmt Parser::parseTypeScriptEnumStmt(logger::Loc loc, parseStmtOpts opts) {
        this->lexer.Expect(T::kEnum);
        logger::Loc nameLoc = this->lexer.Loc();
        std::string nameText = this->lexer.identifier.str;
        this->lexer.Expect(T::kIdentifier);
        compiler::LocRef name;
        name.loc = nameLoc;
        name.ref = compiler::kInvalidRef;

        // Generate the namespace object
        std::shared_ptr<std::unordered_map<std::string, TSNamespaceMember>> exportedMembers =
            this->getOrCreateExportedNamespaceMembers(nameText, opts.isExport);
        TSNamespaceScope *tsNamespace = new TSNamespaceScope();
        tsNamespace->exported_members = exportedMembers;
        tsNamespace->arg_ref = compiler::kInvalidRef;
        tsNamespace->is_enum_scope = true;
        std::shared_ptr<TSNamespaceMemberNamespace> enumMemberData = std::make_shared<TSNamespaceMemberNamespace>();
        enumMemberData->exported_members = exportedMembers;

        // Declare the enum and create the scope
        int scopeIndex = 0;
        if (!opts.isTypeScriptDeclare) {
            name.ref = this->declareSymbol(compiler::SymbolKind::kTSEnum, nameLoc, nameText);
            scopeIndex = this->pushScopeForParsePass(ScopeKind::kEntry, loc);
            this->currentScope->ts_namespace = tsNamespace;
            this->refToTSNamespaceMemberData[name.ref] = enumMemberData;
        }

        this->lexer.Expect(T::kOpenBrace);
        std::vector<EnumValue> values;

        struct fnOrArrowDataParse oldFnOrArrowData = this->fnOrArrowDataParse;
        this->fnOrArrowDataParse = {};
        this->fnOrArrowDataParse.isThisDisallowed = true;
        this->fnOrArrowDataParse.needsAsyncLoc = logger::Loc{-1};

        // Parse the body
        while (this->lexer.token != T::kCloseBrace) {
            logger::Range nameRange = this->lexer.Range();
            EnumValue value;
            value.loc = nameRange.loc;
            value.ref = compiler::kInvalidRef;

            // Parse the name
            std::string valueNameText;
            if (this->lexer.token == T::kStringLiteral) {
                value.name = this->lexer.StringLiteral();
                valueNameText = helpers::UTF16ToString(value.name);
            } else if (this->lexer.IsIdentifierOrKeyword()) {
                valueNameText = this->lexer.identifier.str;
                value.name = helpers::StringToUTF16(valueNameText);
            } else {
                this->lexer.Expect(T::kIdentifier);
            }
            this->lexer.Next();

            // Identifiers can be referenced by other values
            if (!opts.isTypeScriptDeclare && IsIdentifierUTF16(
                    std::span<const uint16_t>(reinterpret_cast<const uint16_t *>(value.name.data()), value.name.size()))) {
                value.ref = this->declareSymbol(compiler::SymbolKind::kOther, value.loc, helpers::UTF16ToString(value.name));
            }

            // Parse the initializer
            if (this->lexer.token == T::kEquals) {
                this->lexer.Next();
                value.value_or_nil = this->parseExpr(L::kComma);
            }

            values.push_back(value);

            // Add this enum value as a member of the enum's namespace
            TSNamespaceMember member;
            member.loc = value.loc;
            member.data = std::make_shared<TSNamespaceMemberProperty>(TSNamespaceMemberProperty{});
            member.is_enum_value = true;
            (*exportedMembers)[valueNameText] = member;

            if (this->lexer.token != T::kComma && this->lexer.token != T::kSemicolon) {
                if (this->lexer.IsIdentifierOrKeyword() || this->lexer.token == T::kStringLiteral) {
                    logger::Loc errorLoc;
                    std::string errorText;

                    if (IsNil(value.value_or_nil.data)) {
                        errorLoc = logger::Loc{nameRange.End()};
                        errorText = logger::FormatMsg(logger::MsgCat::kTS_ExpectedCommaAfterValueInEnum, valueNameText);
                    } else {
                        std::string nextName;
                        if (this->lexer.token == T::kStringLiteral) {
                            nextName = helpers::UTF16ToString(this->lexer.StringLiteral());
                        } else {
                            nextName = this->lexer.identifier.str;
                        }
                        errorLoc = this->lexer.Loc();
                        errorText = logger::FormatMsg(logger::MsgCat::kTS_ExpectedCommaBeforeNextInEnum, nextName);
                    }

                    logger::MsgData data = this->tracker.MakeMsgData(logger::Range{errorLoc, 0}, errorText);
                    data.location->suggestion = ",";
                    logger::Msg msg;
                    msg.kind = logger::MsgKind::kError;
                    msg.data = data;
                    this->log.AddMsgID(logger::MsgID::kNone, msg);
                    std::fprintf(stderr, "LPANIC %s:%d\n", __FILE__, __LINE__); throw LexerPanic();
                }
                break;
            }
            this->lexer.Next();
        }

        this->fnOrArrowDataParse = oldFnOrArrowData;

        if (!opts.isTypeScriptDeclare) {
            // Avoid a collision with the enum closure argument variable if the
            // enum exports a symbol with the same name as the enum itself:
            //
            //   enum foo {
            //     foo = 123,
            //     bar = foo,
            //   }
            //
            // TypeScript generates the following code in this case:
            //
            //   var foo;
            //   (function (foo) {
            //     foo[foo["foo"] = 123] = "foo";
            //     foo[foo["bar"] = 123] = "bar";
            //   })(foo || (foo = {}));
            //
            // Whereas in this case:
            //
            //   enum foo {
            //     bar = foo as any,
            //   }
            //
            // TypeScript generates the following code:
            //
            //   var foo;
            //   (function (foo) {
            //     foo[foo["bar"] = foo] = "bar";
            //   })(foo || (foo = {}));
            //
            if (this->currentScope->members.count(nameText) > 0) {
                // Add a "_" to make tests easier to read, since non-bundler tests don't
                // run the renamer. For external-facing things the renamer will avoid
                // collisions automatically so this isn't important for correctness.
                tsNamespace->arg_ref = this->newSymbol(compiler::SymbolKind::kHoisted, "_" + nameText);
                this->currentScope->generated.push_back(tsNamespace->arg_ref);
            } else {
                tsNamespace->arg_ref = this->declareSymbol(compiler::SymbolKind::kHoisted, nameLoc, nameText);
            }
            this->refToTSNamespaceMemberData[tsNamespace->arg_ref] = enumMemberData;

            this->popScope();
        }

        this->lexer.Expect(T::kCloseBrace);

        if (opts.isTypeScriptDeclare) {
            if (opts.isNamespaceScope && opts.isExport) {
                this->hasNonLocalExportDeclareInsideNamespace = true;
            }

            return Stmt{kSTypeScriptShared, loc};
        }

        // Save these for when we do out-of-order enum visiting
        // Make a copy of "scopesInOrder" instead of a slice since the original
        // array may be flattened in the future by "popAndFlattenScope"
        this->scopesInOrderForEnum[loc] = std::vector<scopeOrder>(this->scopesInOrder.begin() + scopeIndex, this->scopesInOrder.end());

        // Share the final exported members map between the scope and the member data
        tsNamespace->exported_members = exportedMembers;
        enumMemberData->exported_members = exportedMembers;

        std::shared_ptr<SEnum> s = std::make_shared<SEnum>();
        s->name = name;
        s->arg = tsNamespace->arg_ref;
        s->values = values;
        s->is_export = opts.isExport;
        return Stmt{s, loc};
    }

    // This assumes the caller has already parsed the "import" token
    Stmt Parser::parseTypeScriptImportEqualsStmt(logger::Loc loc, parseStmtOpts opts, logger::Loc defaultNameLoc, const std::string &defaultName) {
        this->lexer.Expect(T::kEquals);

        LocalKind kind = this->selectLocalKind(LocalKind::kConst);
        MaybeSubstring name = this->lexer.identifier;
        Expr value{std::make_shared<EIdentifier>(EIdentifier{this->storeNameInRef(name)}), this->lexer.Loc()};
        this->lexer.Expect(T::kIdentifier);

        if (name.str == "require" && this->lexer.token == T::kOpenParen) {
            // "import ns = require('x')"
            this->lexer.Next();
            Expr path{std::make_shared<EString>(EString{this->lexer.StringLiteral(), logger::Loc{}}), this->lexer.Loc()};
            this->lexer.Expect(T::kStringLiteral);
            this->lexer.Expect(T::kCloseParen);
            std::shared_ptr<ECall> call = std::make_shared<ECall>();
            call->target = value;
            call->args = std::vector<Expr>{path};
            value.data = call;
        } else {
            // "import Foo = Bar"
            // "import Foo = Bar.Baz"
            while (this->lexer.token == T::kDot) {
                this->lexer.Next();
                std::shared_ptr<EDot> dot = std::make_shared<EDot>();
                dot->target = value;
                dot->name = this->lexer.identifier.str;
                dot->name_loc = this->lexer.Loc();
                dot->can_be_removed_if_unused = true;
                value.data = dot;
                this->lexer.Expect(T::kIdentifier);
            }
        }

        this->lexer.ExpectOrInsertSemicolon();

        if (opts.isTypeScriptDeclare) {
            // "import type foo = require('bar');"
            // "import type foo = bar.baz;"
            return Stmt{kSTypeScriptShared, loc};
        }

        compiler::Ref ref = this->declareSymbol(compiler::SymbolKind::kConst, defaultNameLoc, defaultName);
        std::vector<Decl> decls;
        Decl decl;
        decl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{ref}), defaultNameLoc};
        decl.value_or_nil = value;
        decls.push_back(decl);

        std::shared_ptr<SLocal> local = std::make_shared<SLocal>();
        local->kind = kind;
        local->decls = decls;
        local->is_export = opts.isExport;
        local->was_ts_import_equals = true;
        return Stmt{local, loc};
    }

    // Generate a TypeScript namespace object for this namespace's scope. If this
    // namespace is another block that is to be merged with an existing namespace,
    // use that earlier namespace's object instead.
    std::shared_ptr<std::unordered_map<std::string, TSNamespaceMember>> Parser::getOrCreateExportedNamespaceMembers(const std::string &name, bool isExport) {
        // Merge with a sibling namespace from the same scope
        auto existingMember = this->currentScope->members.find(name);
        if (existingMember != this->currentScope->members.end()) {
            auto memberEntry = this->refToTSNamespaceMemberData.find(existingMember->second.ref);
            if (memberEntry != this->refToTSNamespaceMemberData.end()) {
                if (auto *nsMemberData = std::get_if<std::shared_ptr<TSNamespaceMemberNamespace>>(&memberEntry->second); nsMemberData != nullptr) {
                    return (*nsMemberData)->exported_members;
                }
            }
        }

        // Merge with a sibling namespace from a different scope
        if (isExport) {
            if (TSNamespaceScope *parentNamespace = this->currentScope->ts_namespace; parentNamespace != nullptr) {
                auto existing = parentNamespace->exported_members->find(name);
                if (existing != parentNamespace->exported_members->end()) {
                    if (auto *existingData = std::get_if<std::shared_ptr<TSNamespaceMemberNamespace>>(&existing->second.data); existingData != nullptr) {
                        return (*existingData)->exported_members;
                    }
                }
            }
        }

        // Otherwise, generate a new namespace object
        return std::make_shared<std::unordered_map<std::string, TSNamespaceMember>>();
    }

    Stmt Parser::parseTypeScriptNamespaceStmt(logger::Loc loc, parseStmtOpts opts) {
        // "namespace Foo {}"
        logger::Loc nameLoc = this->lexer.Loc();
        std::string nameText = this->lexer.identifier.str;
        this->lexer.Next();

        // Generate the namespace object
        std::shared_ptr<std::unordered_map<std::string, TSNamespaceMember>> exportedMembers =
            this->getOrCreateExportedNamespaceMembers(nameText, opts.isExport);
        TSNamespaceScope *tsNamespace = new TSNamespaceScope();
        tsNamespace->exported_members = exportedMembers;
        tsNamespace->arg_ref = compiler::kInvalidRef;
        std::shared_ptr<TSNamespaceMemberNamespace> nsMemberData = std::make_shared<TSNamespaceMemberNamespace>();
        nsMemberData->exported_members = exportedMembers;

        // Declare the namespace and create the scope
        compiler::LocRef name;
        name.loc = nameLoc;
        name.ref = compiler::kInvalidRef;
        int scopeIndex = this->pushScopeForParsePass(ScopeKind::kEntry, loc);
        this->currentScope->ts_namespace = tsNamespace;

        bool oldHasNonLocalExportDeclareInsideNamespace = this->hasNonLocalExportDeclareInsideNamespace;
        struct fnOrArrowDataParse oldFnOrArrowData = this->fnOrArrowDataParse;
        this->hasNonLocalExportDeclareInsideNamespace = false;
        this->fnOrArrowDataParse = {};
        this->fnOrArrowDataParse.isThisDisallowed = true;
        this->fnOrArrowDataParse.isReturnDisallowed = true;
        this->fnOrArrowDataParse.needsAsyncLoc = logger::Loc{-1};

        // Parse the statements inside the namespace
        std::vector<Stmt> stmts;
        if (this->lexer.token == T::kDot) {
            logger::Loc dotLoc = this->lexer.Loc();
            this->lexer.Next();
            parseStmtOpts nestedOpts;
            nestedOpts.isExport = true;
            nestedOpts.isNamespaceScope = true;
            nestedOpts.isTypeScriptDeclare = opts.isTypeScriptDeclare;
            stmts = std::vector<Stmt>{this->parseTypeScriptNamespaceStmt(dotLoc, nestedOpts)};
        } else if (opts.isTypeScriptDeclare && this->lexer.token != T::kOpenBrace) {
            this->lexer.ExpectOrInsertSemicolon();
        } else {
            this->lexer.Expect(T::kOpenBrace);
            parseStmtOpts nestedOpts;
            nestedOpts.isNamespaceScope = true;
            nestedOpts.isTypeScriptDeclare = opts.isTypeScriptDeclare;
            stmts = this->parseStmtsUpTo(T::kCloseBrace, nestedOpts);
            this->lexer.Next();
        }

        bool savedHasNonLocalExportDeclareInsideNamespace = this->hasNonLocalExportDeclareInsideNamespace;
        this->hasNonLocalExportDeclareInsideNamespace = oldHasNonLocalExportDeclareInsideNamespace;
        this->fnOrArrowDataParse = oldFnOrArrowData;

        // Add any exported members from this namespace's body as members of the
        // associated namespace object.
        for (Stmt &stmt : stmts) {
            if (auto *sFn = Get<SFunction>(stmt.data); sFn != nullptr) {
                if (sFn->is_export) {
                    std::string fnName = this->symbols[sFn->fn.name->ref.inner_index].original_name;
                    TSNamespaceMember member;
                    member.loc = sFn->fn.name->loc;
                    member.data = std::make_shared<TSNamespaceMemberProperty>(TSNamespaceMemberProperty{});
                    (*exportedMembers)[fnName] = member;
                    this->refToTSNamespaceMemberData[sFn->fn.name->ref] = member.data;
                }
            } else if (auto *sCl = Get<SClass>(stmt.data); sCl != nullptr) {
                if (sCl->is_export) {
                    std::string clName = this->symbols[sCl->class_.name->ref.inner_index].original_name;
                    TSNamespaceMember member;
                    member.loc = sCl->class_.name->loc;
                    member.data = std::make_shared<TSNamespaceMemberProperty>(TSNamespaceMemberProperty{});
                    (*exportedMembers)[clName] = member;
                    this->refToTSNamespaceMemberData[sCl->class_.name->ref] = member.data;
                }
            } else if (auto *sNs = Get<SNamespace>(stmt.data); sNs != nullptr) {
                if (sNs->is_export) {
                    auto memberEntry = this->refToTSNamespaceMemberData.find(sNs->name.ref);
                    if (memberEntry != this->refToTSNamespaceMemberData.end()) {
                        if (auto *nsData = std::get_if<std::shared_ptr<TSNamespaceMemberNamespace>>(&memberEntry->second); nsData != nullptr) {
                            TSNamespaceMember member;
                            member.loc = sNs->name.loc;
                            std::shared_ptr<TSNamespaceMemberNamespace> child = std::make_shared<TSNamespaceMemberNamespace>();
                            child->exported_members = (*nsData)->exported_members;
                            member.data = child;
                            (*exportedMembers)[this->symbols[sNs->name.ref.inner_index].original_name] = member;
                            this->refToTSNamespaceMemberData[sNs->name.ref] = member.data;
                        }
                    }
                }
            } else if (auto *sEn = Get<SEnum>(stmt.data); sEn != nullptr) {
                if (sEn->is_export) {
                    auto memberEntry = this->refToTSNamespaceMemberData.find(sEn->name.ref);
                    if (memberEntry != this->refToTSNamespaceMemberData.end()) {
                        if (auto *nsData = std::get_if<std::shared_ptr<TSNamespaceMemberNamespace>>(&memberEntry->second); nsData != nullptr) {
                            TSNamespaceMember member;
                            member.loc = sEn->name.loc;
                            std::shared_ptr<TSNamespaceMemberNamespace> child = std::make_shared<TSNamespaceMemberNamespace>();
                            child->exported_members = (*nsData)->exported_members;
                            member.data = child;
                            (*exportedMembers)[this->symbols[sEn->name.ref.inner_index].original_name] = member;
                            this->refToTSNamespaceMemberData[sEn->name.ref] = member.data;
                        }
                    }
                }
            } else if (auto *sLoc = Get<SLocal>(stmt.data); sLoc != nullptr) {
                if (sLoc->is_export) {
                    ForEachIdentifierBindingInDecls(sLoc->decls, [this, exportedMembers](logger::Loc loc, BIdentifier &b) {
                        std::string name = this->symbols[b.ref.inner_index].original_name;
                        TSNamespaceMember member;
                        member.loc = loc;
                        member.data = std::make_shared<TSNamespaceMemberProperty>(TSNamespaceMemberProperty{});
                        (*exportedMembers)[name] = member;
                        this->refToTSNamespaceMemberData[b.ref] = member.data;
                    });
                }
            }
        }

        // Import assignments may be only used in type expressions, not value
        // expressions. If this is the case, the TypeScript compiler removes
        // them entirely from the output. That can cause the namespace itself
        // to be considered empty and thus be removed.
        int importEqualsCount = 0;
        for (Stmt &stmt : stmts) {
            if (auto *local = Get<SLocal>(stmt.data); local != nullptr && local->was_ts_import_equals && !local->is_export) {
                importEqualsCount++;
            }
        }

        // TypeScript omits namespaces without values. These namespaces
        // are only allowed to be used in type expressions. They are
        // allowed to be exported, but can also only be used in type
        // expressions when imported. So we shouldn't count them as a
        // real export either.
        //
        // TypeScript also strangely counts namespaces containing only
        // "export declare" statements as non-empty even though "declare"
        // statements are only type annotations. We cannot omit the namespace
        // in that case. See https://github.com/evanw/esbuild/issues/1158.
        if ((static_cast<int>(stmts.size()) == importEqualsCount && !savedHasNonLocalExportDeclareInsideNamespace) || opts.isTypeScriptDeclare) {
            this->popAndDiscardScope(scopeIndex);
            if (opts.isModuleScope) {
                this->localTypeNames[nameText] = true;
            }
            return Stmt{kSTypeScriptShared, loc};
        }

        if (!opts.isTypeScriptDeclare) {
            // Avoid a collision with the namespace closure argument variable if the
            // namespace exports a symbol with the same name as the namespace itself:
            //
            //   namespace foo {
            //     export let foo = 123
            //     console.log(foo)
            //   }
            //
            // TypeScript generates the following code in this case:
            //
            //   var foo;
            //   (function (foo_1) {
            //     foo_1.foo = 123;
            //     console.log(foo_1.foo);
            //   })(foo || (foo = {}));
            //
            if (this->currentScope->members.count(nameText) > 0) {
                // Add a "_" to make tests easier to read, since non-bundler tests don't
                // run the renamer. For external-facing things the renamer will avoid
                // collisions automatically so this isn't important for correctness.
                tsNamespace->arg_ref = this->newSymbol(compiler::SymbolKind::kHoisted, "_" + nameText);
                this->currentScope->generated.push_back(tsNamespace->arg_ref);
            } else {
                tsNamespace->arg_ref = this->declareSymbol(compiler::SymbolKind::kHoisted, nameLoc, nameText);
            }
            this->refToTSNamespaceMemberData[tsNamespace->arg_ref] = nsMemberData;
        }

        this->popScope();

        // Share the final exported members map between the scope and the member data
        tsNamespace->exported_members = exportedMembers;
        nsMemberData->exported_members = exportedMembers;

        if (!opts.isTypeScriptDeclare) {
            name.ref = this->declareSymbol(compiler::SymbolKind::kTSNamespace, nameLoc, nameText);
            this->refToTSNamespaceMemberData[name.ref] = nsMemberData;
        }

        std::shared_ptr<SNamespace> s = std::make_shared<SNamespace>();
        s->name = name;
        s->arg = tsNamespace->arg_ref;
        s->stmts = stmts;
        s->is_export = opts.isExport;
        return Stmt{s, loc};
    }

    std::vector<Stmt> Parser::generateClosureForTypeScriptNamespaceOrEnum(
        std::vector<Stmt> stmts, logger::Loc stmtLoc, bool isExport, logger::Loc nameLoc,
        compiler::Ref nameRef, compiler::Ref argRef, std::vector<Stmt> stmtsInsideClosure)
    {
        // Follow the link chain in case symbols were merged
        compiler::Symbol symbol = this->symbols[nameRef.inner_index];
        while (symbol.link != compiler::kInvalidRef) {
            nameRef = symbol.link;
            symbol = this->symbols[nameRef.inner_index];
        }

        // Make sure to only emit a variable once for a given namespace, since there
        // can be multiple namespace blocks for the same namespace
        if ((symbol.kind == compiler::SymbolKind::kTSNamespace || symbol.kind == compiler::SymbolKind::kTSEnum) &&
            this->emittedNamespaceVars.count(nameRef) == 0) {
            std::vector<Decl> decls;
            Decl decl;
            decl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{nameRef}), nameLoc};
            decls.push_back(decl);
            this->emittedNamespaceVars[nameRef] = true;
            if (this->currentScope == this->moduleScope) {
                // Top-level namespace: "var"
                std::shared_ptr<SLocal> local = std::make_shared<SLocal>();
                local->kind = LocalKind::kVar;
                local->decls = decls;
                local->is_export = isExport;
                stmts.push_back(Stmt{local, stmtLoc});
            } else {
                // Nested namespace: "let"
                std::shared_ptr<SLocal> local = std::make_shared<SLocal>();
                local->kind = LocalKind::kLet;
                local->decls = decls;
                stmts.push_back(Stmt{local, stmtLoc});
            }
        }

        Expr argExpr;
        if (this->options.optionsThatSupportStructuralEquality.minifySyntax &&
            !compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kLogicalAssignment)) {
            // If the "||=" operator is supported, our minified output can be slightly smaller
            if (isExport && this->enclosingNamespaceArgRef != nullptr) {
                // "name = (enclosing.name ||= {})"
                std::string name = this->symbols[nameRef.inner_index].original_name;
                E *prop = this->dotOrMangledPropVisit(
                    Expr{std::make_shared<EIdentifier>(EIdentifier{*this->enclosingNamespaceArgRef}), nameLoc},
                    name,
                    nameLoc);
                std::shared_ptr<EBinary> binary = std::make_shared<EBinary>();
                binary->op = OpCode::kBinOpLogicalOrAssign;
                binary->left = Expr{*prop, nameLoc};
                binary->right = Expr{std::make_shared<EObject>(EObject{}), nameLoc};
                argExpr = Assign(
                    Expr{std::make_shared<EIdentifier>(EIdentifier{nameRef}), nameLoc},
                    Expr{binary, nameLoc});
                this->recordUsage(*this->enclosingNamespaceArgRef);
                this->recordUsage(nameRef);
            } else {
                // "name ||= {}"
                std::shared_ptr<EBinary> binary = std::make_shared<EBinary>();
                binary->op = OpCode::kBinOpLogicalOrAssign;
                binary->left = Expr{std::make_shared<EIdentifier>(EIdentifier{nameRef}), nameLoc};
                binary->right = Expr{std::make_shared<EObject>(EObject{}), nameLoc};
                argExpr = Expr{binary, nameLoc};
                this->recordUsage(nameRef);
            }
        } else {
            if (isExport && this->enclosingNamespaceArgRef != nullptr) {
                // "name = enclosing.name || (enclosing.name = {})"
                std::string name = this->symbols[nameRef.inner_index].original_name;
                E *prop1 = this->dotOrMangledPropVisit(
                    Expr{std::make_shared<EIdentifier>(EIdentifier{*this->enclosingNamespaceArgRef}), nameLoc},
                    name,
                    nameLoc);
                E *prop2 = this->dotOrMangledPropVisit(
                    Expr{std::make_shared<EIdentifier>(EIdentifier{*this->enclosingNamespaceArgRef}), nameLoc},
                    name,
                    nameLoc);
                std::shared_ptr<EBinary> binary = std::make_shared<EBinary>();
                binary->op = OpCode::kBinOpLogicalOr;
                binary->left = Expr{*prop1, nameLoc};
                binary->right = Assign(Expr{*prop2, nameLoc}, Expr{std::make_shared<EObject>(EObject{}), nameLoc});
                argExpr = Assign(
                    Expr{std::make_shared<EIdentifier>(EIdentifier{nameRef}), nameLoc},
                    Expr{binary, nameLoc});
                this->recordUsage(*this->enclosingNamespaceArgRef);
                this->recordUsage(*this->enclosingNamespaceArgRef);
                this->recordUsage(nameRef);
            } else {
                // "name || (name = {})"
                std::shared_ptr<EBinary> binary = std::make_shared<EBinary>();
                binary->op = OpCode::kBinOpLogicalOr;
                binary->left = Expr{std::make_shared<EIdentifier>(EIdentifier{nameRef}), nameLoc};
                binary->right = Assign(
                    Expr{std::make_shared<EIdentifier>(EIdentifier{nameRef}), nameLoc},
                    Expr{std::make_shared<EObject>(EObject{}), nameLoc});
                argExpr = Expr{binary, nameLoc};
                this->recordUsage(nameRef);
                this->recordUsage(nameRef);
            }
        }

        // Try to use an arrow function if possible for compactness
        Expr targetExpr;
        std::vector<Arg> args;
        Arg arg;
        arg.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{argRef}), nameLoc};
        args.push_back(arg);
        if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kArrow)) {
            std::shared_ptr<EFunction> fn = std::make_shared<EFunction>();
            fn->fn.args = args;
            fn->fn.body.block.stmts = stmtsInsideClosure;
            fn->fn.body.loc = stmtLoc;
            targetExpr = Expr{fn, stmtLoc};
        } else {
            // "(() => { foo() })()" => "(() => foo())()"
            if (this->options.optionsThatSupportStructuralEquality.minifySyntax && stmtsInsideClosure.size() == 1) {
                if (auto *exprStmt = Get<SExpr>(stmtsInsideClosure[0].data); exprStmt != nullptr) {
                    std::shared_ptr<SReturn> ret = std::make_shared<SReturn>();
                    ret->value_or_nil = exprStmt->value;
                    stmtsInsideClosure[0].data = ret;
                }
            }
            std::shared_ptr<EArrow> arrow = std::make_shared<EArrow>();
            arrow->args = args;
            arrow->body.block.stmts = stmtsInsideClosure;
            arrow->body.loc = stmtLoc;
            arrow->prefer_expr = true;
            targetExpr = Expr{arrow, stmtLoc};
        }

        // Call the closure with the name object
        std::shared_ptr<ECall> call = std::make_shared<ECall>();
        call->target = targetExpr;
        call->args = std::vector<Expr>{argExpr};
        std::shared_ptr<SExpr> sexpr = std::make_shared<SExpr>();
        sexpr->value = Expr{call, stmtLoc};
        stmts.push_back(Stmt{sexpr, stmtLoc});

        return stmts;
    }

    std::vector<Stmt> Parser::generateClosureForTypeScriptEnum(
        std::vector<Stmt> stmts, logger::Loc stmtLoc, bool isExport, logger::Loc nameLoc,
        compiler::Ref nameRef, compiler::Ref argRef, std::vector<Expr> exprsInsideClosure,
        bool allValuesArePure)
    {
        // Bail back to the namespace code for enums that aren't at the top level.
        // Doing this for nested enums is problematic for two reasons. First of all
        // enums inside of namespaces must be property accesses off the namespace
        // object instead of variable declarations. Also we'd need to use "let"
        // instead of "var" which doesn't allow sibling declarations to be merged.
        if (this->currentScope != this->moduleScope) {
            std::vector<Stmt> stmtsInsideClosure;
            if (!exprsInsideClosure.empty()) {
                if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                    // "a; b; c;" => "a, b, c;"
                    Expr joined = JoinAllWithComma(exprsInsideClosure);
                    std::shared_ptr<SExpr> exprStmt = std::make_shared<SExpr>();
                    exprStmt->value = joined;
                    stmtsInsideClosure.push_back(Stmt{exprStmt, joined.loc});
                } else {
                    for (const Expr &expr : exprsInsideClosure) {
                        std::shared_ptr<SExpr> exprStmt = std::make_shared<SExpr>();
                        exprStmt->value = expr;
                        stmtsInsideClosure.push_back(Stmt{exprStmt, expr.loc});
                    }
                }
            }
            return this->generateClosureForTypeScriptNamespaceOrEnum(
                stmts, stmtLoc, isExport, nameLoc, nameRef, argRef, stmtsInsideClosure);
        }

        // Follow the link chain in case symbols were merged
        compiler::Symbol symbol = this->symbols[nameRef.inner_index];
        while (symbol.link != compiler::kInvalidRef) {
            nameRef = symbol.link;
            symbol = this->symbols[nameRef.inner_index];
        }

        // Generate the body of the closure, including a return statement at the end
        std::vector<Stmt> stmtsInsideClosure;
        Expr argExpr{std::make_shared<EIdentifier>(EIdentifier{argRef}), nameLoc};
        if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
            // "a; b; return c;" => "return a, b, c;"
            Expr joined = JoinAllWithComma(exprsInsideClosure);
            joined = JoinWithComma(joined, argExpr);
            std::shared_ptr<SReturn> ret = std::make_shared<SReturn>();
            ret->value_or_nil = joined;
            stmtsInsideClosure.push_back(Stmt{ret, joined.loc});
        } else {
            for (const Expr &expr : exprsInsideClosure) {
                std::shared_ptr<SExpr> exprStmt = std::make_shared<SExpr>();
                exprStmt->value = expr;
                stmtsInsideClosure.push_back(Stmt{exprStmt, expr.loc});
            }
            std::shared_ptr<SReturn> ret = std::make_shared<SReturn>();
            ret->value_or_nil = argExpr;
            stmtsInsideClosure.push_back(Stmt{ret, argExpr.loc});
        }

        // Try to use an arrow function if possible for compactness
        Expr targetExpr;
        std::vector<Arg> args;
        Arg arg;
        arg.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{argRef}), nameLoc};
        args.push_back(arg);
        if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kArrow)) {
            std::shared_ptr<EFunction> fn = std::make_shared<EFunction>();
            fn->fn.args = args;
            fn->fn.body.block.stmts = stmtsInsideClosure;
            fn->fn.body.loc = stmtLoc;
            targetExpr = Expr{fn, stmtLoc};
        } else {
            std::shared_ptr<EArrow> arrow = std::make_shared<EArrow>();
            arrow->args = args;
            arrow->body.block.stmts = stmtsInsideClosure;
            arrow->body.loc = stmtLoc;
            arrow->prefer_expr = this->options.optionsThatSupportStructuralEquality.minifySyntax;
            targetExpr = Expr{arrow, stmtLoc};
        }

        // Call the closure with the name object and store it to the variable
        std::shared_ptr<EBinary> orBinary = std::make_shared<EBinary>();
        orBinary->op = OpCode::kBinOpLogicalOr;
        orBinary->left = Expr{std::make_shared<EIdentifier>(EIdentifier{nameRef}), nameLoc};
        orBinary->right = Expr{std::make_shared<EObject>(EObject{}), nameLoc};

        std::shared_ptr<ECall> call = std::make_shared<ECall>();
        call->target = targetExpr;
        call->args = std::vector<Expr>{Expr{orBinary, nameLoc}};
        call->can_be_unwrapped_if_unused = allValuesArePure;

        std::vector<Decl> decls;
        Decl decl;
        decl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{nameRef}), nameLoc};
        decl.value_or_nil = Expr{call, stmtLoc};
        decls.push_back(decl);
        this->recordUsage(nameRef);

        // Use a "var" statement since this is a top-level enum, but only use "export" once
        std::shared_ptr<SLocal> local = std::make_shared<SLocal>();
        local->kind = LocalKind::kVar;
        local->decls = decls;
        local->is_export = isExport && this->emittedNamespaceVars.count(nameRef) == 0;
        stmts.push_back(Stmt{local, stmtLoc});
        this->emittedNamespaceVars[nameRef] = true;

        return stmts;
    }

}
