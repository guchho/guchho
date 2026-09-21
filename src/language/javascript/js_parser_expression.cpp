#include <string>
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

    // RAII wrapper that calls popScope() when it goes out of scope. Used throughout
    // the expression parser to ensure scopes are cleaned up even if an exception
    // (LexerPanic) is thrown during backtracking.
    struct ParserScopePopper {
        Parser *p;
        ~ParserScopePopper() { p->popScope(); }
    };


    // Marks an expression as having been wrapped in parentheses, and transfers any
    // comments that were attached to the open-paren location onto the inner expression
    // so they are not lost during printing. Currently only array, object, function,
    // and arrow expressions support this.
    //
    //   Input:  ( /* comment */ (foo) ), openParenLoc = position of outer (
    //   Output: The inner (foo) expression gets the "comment" attached to it.
    void Parser::markExprAsParenthesized(Expr value, logger::Loc openParenLoc, bool isAsync) {
        if (!isAsync) {
            if (auto it = this->exprComments.find(openParenLoc); it != this->exprComments.end()) {
                std::vector<std::string> comments = it->second;
                this->exprComments.erase(openParenLoc);
                std::vector<std::string> &existing = this->exprComments[value.loc];
                existing.insert(existing.end(), comments.begin(), comments.end());
            }
        }

        if (auto a = Get<EArray>(value.data)) {
            a->is_parenthesized = true;
        } else if (auto o = Get<EObject>(value.data)) {
            o->is_parenthesized = true;
        } else if (auto f = Get<EFunction>(value.data)) {
            f->is_parenthesized = true;
        } else if (auto arrow = Get<EArrow>(value.data)) {
            arrow->is_parenthesized = true;
        }
    }



    // Peeks ahead by one token (saving and restoring the lexer state) to check
    // whether "=>" follows the current token. Used to disambiguate the "async"
    // identifier in contexts like "for (async of ...)" where "async" could be
    // either a plain identifier or the start of an arrow function.
    //
    //   Input:  current token is "of", next token is "=>"
    //   Output: true
    //
    //   Input:  current token is "of", next token is "==="
    //   Output: false
    bool Parser::checkForArrowAfterTheCurrentToken() {
        Lexer oldLexer = this->lexer;
        this->lexer.is_log_disabled = true;

        LexerPanic caught;
        bool hadPanic = false;
        try {
            this->lexer.Next();
        } catch (const LexerPanic &) {
            caught = LexerPanic();
            hadPanic = true;
        }

        bool isArrowAfterThisToken;
        if (hadPanic) {
            this->lexer = oldLexer;
            isArrowAfterThisToken = false;
        } else {
            isArrowAfterThisToken = this->lexer.token == T::kEqualsGreaterThan;
            this->lexer = oldLexer;
        }
        return isArrowAfterThisToken;
    }



    // Parses a function expression: "function foo() {}", "function* gen() {}",
    // "async function() {}", etc. The "function" keyword has already been consumed.
    //
    // Handles optional function name (anonymous if absent), TypeScript type parameters,
    // generator/async flags, and declares the function name as a hoisted symbol.
    // The name "arguments" is special-cased since it is shadowed and inaccessible
    // inside the function body.
    //
    //   Input:  "foo() {}"  (after "function" consumed)
    //   Output: EFunction node with fn.name = "foo"
    //
    //   Input:  "() {}"  (after "function" consumed)
    //   Output: EFunction node with fn.name = nil (anonymous)
    Expr Parser::parseFnExpr(logger::Loc loc, bool isAsync, logger::Range asyncRange) {
        this->lexer.Next();
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
        compiler::LocRef *name = nullptr;

        this->pushScopeForParsePass(ScopeKind::kFunctionArgs, loc);
        ParserScopePopper popper{this};

        if (this->lexer.token == T::kIdentifier) {
            // The name "arguments" is special: it is shadowed and inaccessible inside the function body
            name = new compiler::LocRef{};
            name->loc = this->lexer.Loc();
            std::string text = this->lexer.identifier.str;
            if (text != "arguments") {
                name->ref = this->declareSymbol(compiler::SymbolKind::kHoistedFunction, name->loc, text);
            } else {
                name->ref = this->newSymbol(compiler::SymbolKind::kHoistedFunction, text);
            }
            this->lexer.Next();
        }

        if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
            this->skipTypeScriptTypeParameters(allowConstModifier);
        }

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

        auto fnResult = this->parseFn(name, logger::Range(), decoratorContextFlags(0), data);
        Fn fn = fnResult.first;
        this->validateFunctionName(fn, fnExpr);
        EFunction function;
        function.fn = fn;
        return Expr{std::make_shared<EFunction>(std::move(function)), loc};
    }

    // Converts an expression (possibly with a default value) into a binding pattern
    // and optional initializer. If the expression is "x = y", it splits into binding
    // "x" and initializer "y". Otherwise the initializer is nil.
    //
    // Used when parsing arrow function parameters where the syntax is ambiguous:
    // the parser initially parses arguments as expressions, then converts them to
    // bindings once it confirms the arrow function.
    //
    //   Input:  x = 5, isSpread=false
    //   Output: binding=BIdentifier("x"), initializer=5
    //
    //   Input:  ...rest, isSpread=true
    //   Output: binding=BIdentifier("rest"), initializer=nil
    std::tuple<Binding, Expr, invalidLog> Parser::convertExprToBindingAndInitializer(Expr expr, invalidLog invalidLog, bool isSpread) {
        Expr initializerOrNil;
        if (auto assign = Get<EBinary>(expr.data)) {
            if (assign->op == OpCode::kBinOpAssign) {
                initializerOrNil = assign->right;
                expr = assign->left;
            }
        }
        auto converted = this->convertExprToBinding(expr, invalidLog);
        Binding binding = converted.first;
        invalidLog = converted.second;
        if (!IsNil(initializerOrNil.data)) {
            logger::Range equalsRange = this->source.RangeOfOperatorBefore(initializerOrNil.loc, "=");
            if (isSpread) {
                this->log.AddError(&this->tracker, equalsRange, logger::FormatMsg(logger::MsgCat::kJS_RestArgDefaultInitializer));
            } else {
                syntaxFeature sf;
                sf.feature = compat::JSFeature::kDefaultArgument;
                sf.token = equalsRange;
                invalidLog.syntaxFeatures.push_back(sf);
            }
        }
        return {binding, initializerOrNil, invalidLog};
    }

    // Converts an expression AST node into a binding AST node. This is the core
    // of the expression-to-binding conversion used for arrow function parameters
    // and destructuring patterns. Errors are written to invalidLog (not this->log)
    // so that conversion can be retried if it turns out the expression is not needed
    // as a binding.
    //
    // Handles: EMissing (hole in array), EIdentifier, EArray (destructuring),
    // and EObject (destructuring). Other expression types are flagged as errors.
    //
    //   Input:  EIdentifier("x")
    //   Output: BIdentifier("x")
    //
    //   Input:  EArray([EIdentifier("a"), ESpread(EIdentifier("rest"))])
    //   Output: BArray with items [BIdentifier("a"), BIdentifier("rest")]
    std::pair<Binding, invalidLog> Parser::convertExprToBinding(Expr expr, invalidLog invalidLog) {
        E &e = expr.data;
        if (Get<EMissing>(e)) {
            Binding binding;
            binding.loc = expr.loc;
            binding.data = kBMissingShared;
            return {binding, invalidLog};
        }

        if (auto identifier = Get<EIdentifier>(e)) {
            BIdentifier b;
            b.ref = identifier->ref;
            Binding binding;
            binding.loc = expr.loc;
            binding.data = std::make_shared<BIdentifier>(std::move(b));
            return {binding, invalidLog};
        }

        if (auto array = Get<EArray>(e)) {
            if (array->comma_after_spread.start != 0) {
                logger::Range r;
                r.loc = array->comma_after_spread;
                r.len = 1;
                invalidLog.invalidTokens.push_back(r);
            }
            syntaxFeature sf;
            sf.feature = compat::JSFeature::kDestructuring;
            sf.token = this->source.RangeOfOperatorAfter(expr.loc, "[");
            invalidLog.syntaxFeatures.push_back(sf);
            std::vector<ArrayBinding> items;
            bool isSpread = false;
            for (Expr item : array->items) {
                if (auto spread = Get<ESpread>(item.data)) {
                    isSpread = true;
                    item = spread->value;
                    if (Get<EIdentifier>(item.data) == nullptr) {
                        this->markSyntaxFeature(compat::JSFeature::kNestedRestBinding, this->source.RangeOfOperatorAfter(item.loc, "["));
                    }
                }
                auto converted = this->convertExprToBindingAndInitializer(item, invalidLog, isSpread);
                Binding binding = std::get<0>(converted);
                Expr initializerOrNil = std::get<1>(converted);
                invalidLog = std::get<2>(converted);
                ArrayBinding ab;
                ab.binding = binding;
                ab.default_value_or_nil = initializerOrNil;
                ab.loc = item.loc;
                items.push_back(ab);
            }
            BArray b;
            b.items = items;
            b.has_spread = isSpread;
            b.is_single_line = array->is_single_line;
            b.close_bracket_loc = array->close_bracket_loc;
            Binding binding;
            binding.loc = expr.loc;
            binding.data = std::make_shared<BArray>(std::move(b));
            return {binding, invalidLog};
        }

        if (auto object = Get<EObject>(e)) {
            if (object->comma_after_spread.start != 0) {
                logger::Range r;
                r.loc = object->comma_after_spread;
                r.len = 1;
                invalidLog.invalidTokens.push_back(r);
            }
            syntaxFeature sf;
            sf.feature = compat::JSFeature::kDestructuring;
            sf.token = this->source.RangeOfOperatorAfter(expr.loc, "{");
            invalidLog.syntaxFeatures.push_back(sf);
            std::vector<PropertyBinding> properties;
            for (const Property &property : object->properties) {
                if (IsMethodDefinition(property.kind)) {
                    invalidLog.invalidTokens.push_back(RangeOfIdentifier(this->source, property.key.loc));
                    continue;
                }
                auto converted = this->convertExprToBindingAndInitializer(property.value_or_nil, invalidLog, false);
                Binding binding = std::get<0>(converted);
                Expr initializerOrNil = std::get<1>(converted);
                invalidLog = std::get<2>(converted);
                if (IsNil(initializerOrNil.data)) {
                    initializerOrNil = property.initializer_or_nil;
                }
                PropertyBinding pb;
                pb.loc = property.loc;
                pb.is_spread = property.kind == PropertyKind::kSpread;
                pb.is_computed = Has(property.flags, PropertyFlags::kIsComputed);
                pb.key = property.key;
                pb.value = binding;
                pb.default_value_or_nil = initializerOrNil;
                properties.push_back(pb);
            }
            BObject b;
            b.properties = properties;
            b.is_single_line = object->is_single_line;
            b.close_brace_loc = object->close_brace_loc;
            Binding binding;
            binding.loc = expr.loc;
            binding.data = std::make_shared<BObject>(std::move(b));
            return {binding, invalidLog};
        }

        logger::Range r;
        r.loc = expr.loc;
        invalidLog.invalidTokens.push_back(r);
        return {Binding(), invalidLog};
    }


    // Parses a parenthesized expression. This is the most complex disambiguation
    // function in the expression parser: it must decide whether the contents are
    // arrow function arguments, a call to a function named "async", or a simple
    // parenthesized expression / comma operator chain.
    //
    // The open parenthesis has already been consumed by the caller. This function
    // optimistically pushes a scope (in case it's an arrow function) and rolls
    // it back if it turns out not to be.
    //
    //   Input:  "x, y)"  (after "(" consumed)
    //   Output: EBinary comma of x and y
    //
    //   Input:  "x => x + 1)"  (after "(" consumed)
    //   Output: EArrow with single parameter x
    //
    //   Input:  "x: number, y: string = 'hello')"  (TypeScript)
    //   Output: EArrow with typed parameters
    Expr Parser::parseParenExpr(logger::Loc loc, L level, parenExprOpts opts) {
        std::vector<Expr> items;
        deferredErrors errors;
        deferredArrowArgErrors arrowArgErrors;
        logger::Range spreadRange;
        logger::Range typeColonRange;
        logger::Loc commaAfterSpread;
        bool isAsync = opts.asyncRange.len > 0;

        // Optimistically push a function-args scope. If this turns out to be an arrow
        // function, child scopes will be parented under it. If not, we undo this with
        // popAndFlattenScope later.
        int scopeIndex = this->pushScopeForParsePass(ScopeKind::kFunctionArgs, loc);

        bool oldAllowIn = this->allowIn;
        this->allowIn = true;

        struct fnOrArrowDataParse oldFnOrArrowData = this->fnOrArrowDataParse;
        this->fnOrArrowDataParse.arrowArgErrors = &arrowArgErrors;

        while (this->lexer.token != T::kCloseParen) {
            logger::Loc itemLoc = this->lexer.Loc();
            bool isSpread = this->lexer.token == T::kDotDotDot;

            if (isSpread) {
                spreadRange = this->lexer.Range();
                this->markSyntaxFeature(compat::JSFeature::kRestArgument, spreadRange);
                this->lexer.Next();
            }

            this->latestArrowArgLoc = this->lexer.Loc();
            Expr item = this->parseExprOrBindings(L::kComma, &errors);

            if (isSpread) {
                ESpread spread;
                spread.value = item;
                item = Expr{std::make_shared<ESpread>(std::move(spread)), itemLoc};
            }

            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kColon) {
                typeColonRange = this->lexer.Range();
                this->lexer.Next();
                this->skipTypeScriptType(L::kLowest);
            }

            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kEquals && this->lexer.Loc() != this->forbidSuffixAfterAsLoc) {
                this->lexer.Next();
                item = Assign(item, this->parseExpr(L::kComma));
            }

            items.push_back(item);
            if (this->lexer.token != T::kComma) {
                break;
            }

            if (isSpread) {
                commaAfterSpread = this->lexer.Loc();
            }

            this->lexer.Next();
        }

        this->lexer.Expect(T::kCloseParen);

        this->allowIn = oldAllowIn;

        this->fnOrArrowDataParse = oldFnOrArrowData;

        bool isArrowFn = this->lexer.token == T::kEqualsGreaterThan;
        if (isArrowFn || opts.forceArrowFn || (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kColon)) {
            if (level > L::kAssign) {
                this->lexer.Unexpected();
            }

            invalidLog invalidLog;
            std::vector<Arg> args;

            if (isAsync) {
                this->markAsyncFn(opts.asyncRange, false);
            }

            for (Expr item : items) {
                bool isSpread = false;
                if (auto spread = Get<ESpread>(item.data)) {
                    item = spread->value;
                    isSpread = true;
                }
                auto converted = this->convertExprToBindingAndInitializer(item, invalidLog, isSpread);
                Binding binding = std::get<0>(converted);
                Expr initializerOrNil = std::get<1>(converted);
                invalidLog = std::get<2>(converted);
                Arg arg;
                arg.binding = binding;
                arg.default_or_nil = initializerOrNil;
                args.push_back(arg);
            }

            awaitOrYield await = allowIdent;
            if (isAsync) {
                await = allowExpr;
            }

            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kColon && invalidLog.invalidTokens.size() == 0) {
                if (opts.isAfterQuestionAndBeforeColon) {
                    isArrowFn = this->isTypeScriptArrowReturnTypeAfterQuestionAndBeforeColon(await);
                    if (isArrowFn) {
                        this->lexer.Next();
                        this->skipTypeScriptReturnType();
                    }
                } else {
                    isArrowFn = this->trySkipTypeScriptArrowReturnTypeWithBacktracking();
                }
            }

            if (isArrowFn || opts.forceArrowFn) {
                if (commaAfterSpread.start != 0) {
                    logger::Range r;
                    r.loc = commaAfterSpread;
                    r.len = 1;
                    this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedCommaAfterRest));
                }
                this->logArrowArgErrors(&arrowArgErrors);
                this->logDeferredArrowArgErrors(&errors);

                if (invalidLog.invalidTokens.size() > 0) {
                    for (const logger::Range &token : invalidLog.invalidTokens) {
                        this->log.AddError(&this->tracker, token, logger::FormatMsg(logger::MsgCat::kJS_InvalidBindingPattern));
                    }

                    throw LexerPanic();
                }

                for (const syntaxFeature &entry : invalidLog.syntaxFeatures) {
                    this->markSyntaxFeature(entry.feature, entry.token);
                }

                struct fnOrArrowDataParse data;
                data.needsAsyncLoc = loc;
                data.await = await;
                EArrow *arrow = this->parseArrowBody(args, data);
                arrow->is_async = isAsync;
                arrow->has_rest_arg = spreadRange.len > 0;
                this->popScope();
                return Expr{std::shared_ptr<EArrow>(arrow), loc};
            }
        }

        this->popAndFlattenScope(scopeIndex);

        if (typeColonRange.len > 0) {
            this->log.AddError(&this->tracker, typeColonRange, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedColon));
            throw LexerPanic();
        }

        if (isAsync) {
            this->logExprErrors(&errors);
            EIdentifier identifier;
            identifier.ref = this->storeNameInRef(MaybeSubstring{std::string("async")});
            Expr async{std::make_shared<EIdentifier>(std::move(identifier)), loc};
            ECall call;
            call.target = async;
            call.args = items;
            return Expr{std::make_shared<ECall>(std::move(call)), loc};
        }

        if (items.size() > 0) {
            this->logExprErrors(&errors);
            if (spreadRange.len > 0) {
                this->log.AddError(&this->tracker, spreadRange, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedEllipsis));
                throw LexerPanic();
            }
            Expr value = JoinAllWithComma(items);
            this->markExprAsParenthesized(value, loc, isAsync);
            return value;
        }

        this->lexer.Expected(T::kEqualsGreaterThan);
        return Expr{};
    }

    // Parses "async" as a prefix expression. The ambiguity of "async" is resolved
    // here: it can be an async arrow function, an async function expression, or
    // just a plain identifier named "async".
    //
    // Disambiguation logic:
    //   "async function() {}"  -> async function expression
    //   "async => {}"          -> async arrow with no params
    //   "async x => {}"       -> async arrow with single param
    //   "async () => {}"      -> async arrow with params
    //   "async <T>() => {}"   -> async arrow with type params (TS)
    //   "async"               -> plain identifier
    //   "async + 1"           -> plain identifier in arithmetic
    Expr Parser::parseAsyncPrefixExpr(logger::Range asyncRange, L level, exprFlag flags) {
        if (!this->lexer.has_newline_before && this->lexer.token == T::kFunction) {
            return this->parseFnExpr(asyncRange.loc, true, asyncRange);
        }

        if (!this->lexer.has_newline_before && level < L::kMember) {
            switch (this->lexer.token) {
            case T::kEqualsGreaterThan:
                if (level <= L::kAssign) {
                    BIdentifier identifier;
                    identifier.ref = this->storeNameInRef(MaybeSubstring{std::string("async")});
                    Arg arg;
                    arg.binding.loc = asyncRange.loc;
                    arg.binding.data = std::make_shared<BIdentifier>(std::move(identifier));

                    this->pushScopeForParsePass(ScopeKind::kFunctionArgs, asyncRange.loc);
                    ParserScopePopper popper{this};

                    struct fnOrArrowDataParse data;
                    data.needsAsyncLoc = asyncRange.loc;
                    EArrow *arrow = this->parseArrowBody(std::vector<Arg>{arg}, data);
                    return Expr{std::shared_ptr<EArrow>(arrow), asyncRange.loc};
                }
                break;

            case T::kIdentifier: {
                if (level <= L::kAssign) {
                    bool isArrowFn = true;
                    if ((flags & exprFlagForLoopInit) != 0 && this->lexer.identifier.str == "of") {
                        isArrowFn = this->checkForArrowAfterTheCurrentToken();

                        if (!isArrowFn && (flags & exprFlagForAwaitLoopInit) == 0 && this->lexer.Raw() == "of") {
                            logger::Range r;
                            r.loc = asyncRange.loc;
                            r.len = this->lexer.Range().End() - asyncRange.loc.start;
                            this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_ForLoopAsyncOf));
                            throw LexerPanic();
                        }
                    } else if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->lexer.token == T::kIdentifier) {
                        isArrowFn = this->checkForArrowAfterTheCurrentToken();
                    }

                    if (isArrowFn) {
                        this->markAsyncFn(asyncRange, false);
                        compiler::Ref ref = this->storeNameInRef(this->lexer.identifier);
                        BIdentifier identifier;
                        identifier.ref = ref;
                        Arg arg;
                        arg.binding.loc = this->lexer.Loc();
                        arg.binding.data = std::make_shared<BIdentifier>(std::move(identifier));
                        this->lexer.Next();

                        this->pushScopeForParsePass(ScopeKind::kFunctionArgs, asyncRange.loc);
                        ParserScopePopper popper{this};

                        struct fnOrArrowDataParse data;
                        data.needsAsyncLoc = arg.binding.loc;
                        data.await = allowExpr;
                        EArrow *arrow = this->parseArrowBody(std::vector<Arg>{arg}, data);
                        arrow->is_async = true;
                        return Expr{std::shared_ptr<EArrow>(arrow), asyncRange.loc};
                    }
                }
                break;
            }

            case T::kOpenParen:
                this->lexer.Next();
                {
                    parenExprOpts opts;
                    opts.asyncRange = asyncRange;
                    return this->parseParenExpr(asyncRange.loc, level, opts);
                }

            case T::kLessThan:
                if (this->options.optionsThatSupportStructuralEquality.ts.Parse && (!this->options.jsx.Parse || this->isTSArrowFnJSX())) {
                    ESkipTypeScript result = this->trySkipTypeScriptTypeParametersThenOpenParenWithBacktracking();
                    if (result != didNotSkipAnything) {
                        this->lexer.Next();
                        parenExprOpts opts;
                        opts.asyncRange = asyncRange;
                        opts.forceArrowFn = result == definitelyTypeParameters;
                        return this->parseParenExpr(asyncRange.loc, level, opts);
                    }
                }
                break;

            default:
                break;
            }
        }

        EIdentifier identifier;
        identifier.ref = this->storeNameInRef(MaybeSubstring{std::string("async")});
        return Expr{std::make_shared<EIdentifier>(std::move(identifier)), asyncRange.loc};
    }


    // Creates either an EDot node (normal property access like "a.b") or an EIndex
    // node (for mangled properties via /* @__KEY__ */ comments). The mangled prop
    // path converts a string property name into a symbol reference that survives
    // property renaming during minification.
    //
    //   Input:  target=a, name="foo", isMangled=false
    //   Output: EDot(target=a, name="foo")
    //
    //   Input:  target=a, name="bar", isMangled=true
    //   Output: EIndex(target=a, index=ENameOfSymbol("bar"))
    E *Parser::dotOrMangledPropParse(
        Expr target,
        MaybeSubstring name,
        logger::Loc nameLoc,
        OptionalChain optionalChain,
        wasOriginallyDotOrIndex original) {
        if ((original != wasOriginallyIndex || this->options.optionsThatSupportStructuralEquality.mangleQuoted) && this->isMangledProp(name.str)) {
            EIndex index;
            index.target = std::move(target);
            ENameOfSymbol e;
            e.ref = this->storeNameInRef(name);
            index.index = Expr{std::make_shared<ENameOfSymbol>(std::move(e)), nameLoc};
            index.optional_chain = optionalChain;
            return new E(std::make_shared<EIndex>(std::move(index)));
        }

        EDot dot;
        dot.target = std::move(target);
        dot.name = name.str;
        dot.name_loc = nameLoc;
        dot.optional_chain = optionalChain;
        return new E(std::make_shared<EDot>(std::move(dot)));
    }

    // Records the location of legacy octal literals (like 0644) for later error
    // reporting. These are forbidden in strict mode and modern JavaScript.
    void Parser::checkForLegacyOctalLiteral(E e) {
        if (this->lexer.is_legacy_octal_literal) {
            this->legacyOctalLiterals[std::get<std::shared_ptr<ENumber>>(e).get()] = this->lexer.Range();
        }
    }


    // Parses a string literal or no-substitution template literal. The caller has
    // already verified the token type. Handles the /* @__KEY__ */ comment directive
    // that converts a string into a mangled property symbol reference.
    //
    //   Input:  '"hello"'
    //   Output: EString(value="hello")
    //
    //   Input:  '"foo" /* @__KEY__ */  (and "foo" is a mangled prop)
    //   Output: ENameOfSymbol(ref="foo")
    Expr Parser::parseStringLiteral() {
        logger::Loc legacyOctalLoc;
        logger::Loc loc = this->lexer.Loc();
        std::u16string text = this->lexer.StringLiteral();

        // Enable using a "/* @__KEY__ */" comment to turn a string into a key
        bool hasPropertyKeyComment = Has(this->lexer.has_comment_before, CommentBefore::kKey);
        if (hasPropertyKeyComment) {
            std::string name = helpers::UTF16ToString(text);
            if (this->isMangledProp(name)) {
                ENameOfSymbol value;
                value.ref = this->storeNameInRef(MaybeSubstring{name});
                value.has_property_key_comment = true;
                Expr result{std::make_shared<ENameOfSymbol>(std::move(value)), loc};
                this->lexer.Next();
                return result;
            }
        }

        if (this->lexer.legacy_octal_loc.start > loc.start) {
            legacyOctalLoc = this->lexer.legacy_octal_loc;
        }
        EString value;
        value.value = text;
        value.legacy_octal_loc = legacyOctalLoc;
        value.prefer_template = this->lexer.token == T::kNoSubstitutionTemplateLiteral;
        value.has_property_key_comment = hasPropertyKeyComment;
        Expr result{std::make_shared<EString>(std::move(value)), loc};
        this->lexer.Next();
        return result;
    }

    // Validates that const and using declarations have initializers. Reports an
    // error for each declaration that lacks one. For example, "const x;" is an
    // error because x has no initializer.
    //
    //   Input:  kind=kUsing, decls=[{binding=x, value=nil}]
    //   Output: error logged: "The name \"x\" must be initialized"
    void Parser::requireInitializers(LocalKind kind, std::vector<Decl> decls) {
        for (Decl &d : decls) {
            if (IsNil(d.value_or_nil.data)) {
                std::string what = "constant";
                if (kind == LocalKind::kUsing) {
                    what = "declaration";
                }
                if (auto id = Get<BIdentifier>(d.binding.data)) {
                    logger::Range r = RangeOfIdentifier(this->source, d.binding.loc);
                    this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_NameMustBeInitialized, what, this->symbols[id->ref.inner_index].original_name));
                } else {
                    this->log.AddError(&this->tracker, logger::Range{d.binding.loc}, logger::FormatMsg(logger::MsgCat::kJS_ThisMustBeInitialized, what));
                }
            }
        }
    }   

    // Parses a prefix expression. This is the first half of the Pratt parser:
    // it handles all tokens that can begin an expression (atoms and unary operators).
    //
    // Handles: super, parenthesized expressions, literals (true/false/null/number/string/
    // bigint/regex/template), this, private identifiers, identifiers (including async/
    // await/yield and arrow functions), unary operators (void/typeof/delete/+/-/~/!),
    // prefix ++/--, function expressions, class expressions, decorators, new (with
    // new.target), array literals, object literals, JSX elements, TypeScript angle-
    // bracket casts, and import expressions.
    //
    // Returns an expression node. The caller then calls parseSuffix to handle
    // binary operators and postfix expressions.
    Expr Parser::parsePrefix(L level, deferredErrors *errors, exprFlag flags) {
        logger::Loc loc = this->saveExprCommentsHere();

        switch (this->lexer.token) {
        case T::kSuper: {
            logger::Range superRange = this->lexer.Range();
            this->lexer.Next();

            switch (this->lexer.token) {
            case T::kOpenParen:
                if (level < L::kCall && this->fnOrArrowDataParse.allowSuperCall) {
                    return Expr{kESuperShared, loc};
                }
                break;

            case T::kDot:
            case T::kOpenBracket:
                if (this->fnOrArrowDataParse.allowSuperProperty) {
                    return Expr{kESuperShared, loc};
                }
                break;

            default:
                break;
            }

            this->log.AddError(&this->tracker, superRange, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedSuper));
            return Expr{kESuperShared, loc};
        }

        case T::kOpenParen: {
            if (errors != nullptr) {
                errors->invalidParens.push_back(this->lexer.Range());
            }

            this->lexer.Next();

            if (level > L::kAssign) {
                bool oldAllowIn = this->allowIn;
                this->allowIn = true;

                Expr value = this->parseExpr(L::kLowest);

                if ((flags & exprFlagDecorator) == 0) {
                    this->markExprAsParenthesized(value, loc, false);
                }

                this->lexer.Expect(T::kCloseParen);

                this->allowIn = oldAllowIn;
                return value;
            }

            parenExprOpts opts;
            opts.isAfterQuestionAndBeforeColon = (flags & exprFlagAfterQuestionAndBeforeColon) != 0;
            return this->parseParenExpr(loc, level, opts);
        }

        case T::kFalse:
            this->lexer.Next();
            return Expr{std::make_shared<EBoolean>(EBoolean{false}), loc};

        case T::kTrue:
            this->lexer.Next();
            return Expr{std::make_shared<EBoolean>(EBoolean{true}), loc};

        case T::kNull:
            this->lexer.Next();
            return Expr{kENullShared, loc};

        case T::kThis:
            if (this->fnOrArrowDataParse.isThisDisallowed) {
                this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_CannotUseThis));
            }
            this->lexer.Next();
            return Expr{kEThisShared, loc};

        case T::kPrivateIdentifier: {
            if (!this->allowIn || level >= L::kCompare) {
                this->lexer.Unexpected();
            }

            MaybeSubstring name = this->lexer.identifier;
            this->lexer.Next();

            // "#foo in bar" - check for the "in" keyword after the private identifier
            if (this->lexer.token != T::kIn) {
                this->lexer.Expected(T::kIn);
            }

            if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kClassPrivateBrandCheck)) {
                this->lowerAllOfThesePrivateNames[name.str] = true;
            }

            EPrivateIdentifier e;
            e.ref = this->storeNameInRef(name);
            return Expr{std::make_shared<EPrivateIdentifier>(std::move(e)), loc};
        }

        case T::kIdentifier: {
            MaybeSubstring name = this->lexer.identifier;
            logger::Range nameRange = this->lexer.Range();
            std::string_view raw = this->lexer.Raw();
            this->lexer.Next();

            if (name.str == "async") {
                if (raw == "async") {
                    return this->parseAsyncPrefixExpr(nameRange, level, flags);
                }
            } else if (name.str == "await") {
                switch (this->fnOrArrowDataParse.await) {
                case forbidAll:
                    this->log.AddError(&this->tracker, nameRange, logger::FormatMsg(logger::MsgCat::kJS_AwaitCannotBeUsed));
                    break;

                case allowExpr:
                    if (raw != "await") {
                        this->log.AddError(&this->tracker, nameRange, logger::FormatMsg(logger::MsgCat::kJS_AwaitCannotBeEscaped));
                    } else {
                        if (this->fnOrArrowDataParse.isTopLevel) {
                            this->topLevelAwaitKeyword = nameRange;
                        }
                        if (this->fnOrArrowDataParse.arrowArgErrors != nullptr) {
                            this->fnOrArrowDataParse.arrowArgErrors->invalidExprAwait = nameRange;
                        }
                        Expr value = this->parseExpr(L::kPrefix);
                        if (this->lexer.token == T::kAsteriskAsterisk) {
                            this->lexer.Unexpected();
                        }
                        EAwait e;
                        e.value = value;
                        return Expr{std::make_shared<EAwait>(std::move(e)), loc};
                    }
                    break;

                case allowIdent:
                    this->lexer.prev_token_was_await_keyword = true;
                    this->lexer.await_keyword_loc = loc;
                    this->lexer.fn_or_arrow_start_loc = this->fnOrArrowDataParse.needsAsyncLoc;
                    break;
                }
            } else if (name.str == "yield") {
                switch (this->fnOrArrowDataParse.yield) {
                case forbidAll:
                    this->log.AddError(&this->tracker, nameRange, logger::FormatMsg(logger::MsgCat::kJS_YieldCannotBeUsed));
                    break;

                case allowExpr:
                    if (raw != "yield") {
                        this->log.AddError(&this->tracker, nameRange, logger::FormatMsg(logger::MsgCat::kJS_YieldCannotBeEscaped));
                    } else {
                        if (level > L::kAssign) {
                            this->log.AddError(&this->tracker, nameRange, logger::FormatMsg(logger::MsgCat::kJS_YieldWithoutParens));
                        }
                        if (this->fnOrArrowDataParse.arrowArgErrors != nullptr) {
                            this->fnOrArrowDataParse.arrowArgErrors->invalidExprYield = nameRange;
                        }
                        return this->parseYieldExpr(loc);
                    }
                    break;

                case allowIdent:
                    if (!this->lexer.has_newline_before) {
                        switch (this->lexer.token) {
                        case T::kNull:
                        case T::kIdentifier:
                        case T::kFalse:
                        case T::kTrue:
                        case T::kNumericLiteral:
                        case T::kBigIntegerLiteral:
                        case T::kStringLiteral:
                            this->log.AddError(&this->tracker, nameRange, logger::FormatMsg(logger::MsgCat::kJS_YieldOutsideGenerator));
                            return this->parseYieldExpr(loc);

                        default:
                            break;
                        }
                    }
                    break;
                }
            }

            if (this->lexer.token == T::kEqualsGreaterThan && level <= L::kAssign) {
                compiler::Ref ref = this->storeNameInRef(name);
                BIdentifier b;
                b.ref = ref;
                Arg arg;
                arg.binding = Binding{std::make_shared<BIdentifier>(std::move(b)), loc};

                this->pushScopeForParsePass(ScopeKind::kFunctionArgs, loc);
                ParserScopePopper popper{this};

                struct fnOrArrowDataParse data;
                data.needsAsyncLoc = loc;
                EArrow *arrow = this->parseArrowBody(std::vector<Arg>{arg}, data);
                return Expr{std::shared_ptr<EArrow>(arrow), loc};
            }

            compiler::Ref ref = this->storeNameInRef(name);
            EIdentifier e;
            e.ref = ref;
            return Expr{std::make_shared<EIdentifier>(std::move(e)), loc};
        }

        case T::kStringLiteral:
        case T::kNoSubstitutionTemplateLiteral:
            return this->parseStringLiteral();

        case T::kTemplateHead: {
            logger::Loc legacyOctalLoc;
            logger::Loc headLoc = this->lexer.Loc();
            std::u16string head = this->lexer.StringLiteral();
            if (this->lexer.legacy_octal_loc.start > loc.start) {
                legacyOctalLoc = this->lexer.legacy_octal_loc;
            }
            std::pair<std::vector<TemplatePart>, logger::Loc> result = this->parseTemplateParts(false);
            if (result.second.start > 0) {
                legacyOctalLoc = result.second;
            }
            ETemplate e;
            e.head_loc = headLoc;
            e.head_cooked = head;
            e.parts = result.first;
            e.legacy_octal_loc = legacyOctalLoc;
            return Expr{std::make_shared<ETemplate>(std::move(e)), loc};
        }

        case T::kNumericLiteral: {
            ENumber e;
            e.value = this->lexer.number;
            Expr value{std::make_shared<ENumber>(std::move(e)), loc};
            this->checkForLegacyOctalLiteral(value.data);
            this->lexer.Next();
            return value;
        }

        case T::kBigIntegerLiteral: {
            MaybeSubstring value = this->lexer.identifier;
            this->lexer.Next();
            EBigInt e;
            e.value = value.str;
            return Expr{std::make_shared<EBigInt>(std::move(e)), loc};
        }

        case T::kSlash:
        case T::kSlashEquals: {
            this->lexer.ScanRegExp();
            std::string value{this->lexer.Raw()};
            this->lexer.Next();
            ERegExp e;
            e.value = value;
            return Expr{std::make_shared<ERegExp>(std::move(e)), loc};
        }

        case T::kVoid: {
            this->lexer.Next();
            Expr value = this->parseExpr(L::kPrefix);
            if (this->lexer.token == T::kAsteriskAsterisk) {
                this->lexer.Unexpected();
            }
            EUnary e;
            e.op = OpCode::kUnOpVoid;
            e.value = value;
            return Expr{std::make_shared<EUnary>(std::move(e)), loc};
        }

        case T::kTypeof: {
            this->lexer.Next();
            Expr value = this->parseExpr(L::kPrefix);
            if (this->lexer.token == T::kAsteriskAsterisk) {
                this->lexer.Unexpected();
            }
            bool valueIsIdentifier = Get<EIdentifier>(value.data) != nullptr;
            EUnary e;
            e.op = OpCode::kUnOpTypeof;
            e.value = value;
            e.was_originally_typeof_identifier = valueIsIdentifier;
            return Expr{std::make_shared<EUnary>(std::move(e)), loc};
        }

        case T::kDelete: {
            this->lexer.Next();
            Expr value = this->parseExpr(L::kPrefix);
            if (this->lexer.token == T::kAsteriskAsterisk) {
                this->lexer.Unexpected();
            }
            if (auto index = Get<EIndex>(value.data)) {
                if (auto private_ = Get<EPrivateIdentifier>(index->index.data)) {
                    std::string name = this->loadNameFromRef(private_->ref);
                    logger::Range r;
                    r.loc = index->index.loc;
                    r.len = static_cast<int32_t>(name.length());
                    this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_DeletePrivateName, name));
                }
            }
            bool valueIsIdentifier = Get<EIdentifier>(value.data) != nullptr;
            EUnary e;
            e.op = OpCode::kUnOpDelete;
            e.value = value;
            e.was_originally_delete_of_identifier_or_property_access = valueIsIdentifier || IsPropertyAccess(value);
            return Expr{std::make_shared<EUnary>(std::move(e)), loc};
        }

        case T::kPlus: {
            this->lexer.Next();
            Expr value = this->parseExpr(L::kPrefix);
            if (this->lexer.token == T::kAsteriskAsterisk) {
                this->lexer.Unexpected();
            }
            EUnary e;
            e.op = OpCode::kUnOpPos;
            e.value = value;
            return Expr{std::make_shared<EUnary>(std::move(e)), loc};
        }

        case T::kMinus: {
            this->lexer.Next();
            Expr value = this->parseExpr(L::kPrefix);
            if (this->lexer.token == T::kAsteriskAsterisk) {
                this->lexer.Unexpected();
            }
            EUnary e;
            e.op = OpCode::kUnOpNeg;
            e.value = value;
            return Expr{std::make_shared<EUnary>(std::move(e)), loc};
        }

        case T::kTilde: {
            this->lexer.Next();
            Expr value = this->parseExpr(L::kPrefix);
            if (this->lexer.token == T::kAsteriskAsterisk) {
                this->lexer.Unexpected();
            }
            EUnary e;
            e.op = OpCode::kUnOpCpl;
            e.value = value;
            return Expr{std::make_shared<EUnary>(std::move(e)), loc};
        }

        case T::kExclamation: {
            this->lexer.Next();
            Expr value = this->parseExpr(L::kPrefix);
            if (this->lexer.token == T::kAsteriskAsterisk) {
                this->lexer.Unexpected();
            }
            EUnary e;
            e.op = OpCode::kUnOpNot;
            e.value = value;
            return Expr{std::make_shared<EUnary>(std::move(e)), loc};
        }

        case T::kMinusMinus: {
            this->lexer.Next();
            EUnary e;
            e.op = OpCode::kUnOpPreDec;
            e.value = this->parseExpr(L::kPrefix);
            return Expr{std::make_shared<EUnary>(std::move(e)), loc};
        }

        case T::kPlusPlus: {
            this->lexer.Next();
            EUnary e;
            e.op = OpCode::kUnOpPreInc;
            e.value = this->parseExpr(L::kPrefix);
            return Expr{std::make_shared<EUnary>(std::move(e)), loc};
        }

        case T::kFunction:
            return this->parseFnExpr(loc, false, logger::Range());

        case T::kClass:
            return this->parseClassExpr(std::vector<Decorator>());

        case T::kAt: {
            std::vector<Decorator> decorators = this->parseDecorators(this->currentScope, logger::Range(), decoratorBeforeClassExpr);
            return this->parseClassExpr(decorators);
        }

        case T::kNew: {
            this->lexer.Next();

            if (this->lexer.token == T::kDot) {
                this->lexer.Next();
                if (this->lexer.token != T::kIdentifier || this->lexer.Raw() != "target") {
                    this->lexer.Unexpected();
                }
                logger::Range r;
                r.loc = loc;
                r.len = this->lexer.Range().End() - loc.start;
                this->markSyntaxFeature(compat::JSFeature::kNewTarget, r);
                this->lexer.Next();
                ENewTarget e;
                e.range = r;
                return Expr{std::make_shared<ENewTarget>(std::move(e)), loc};
            }

            Expr target = this->parseExprWithFlags(L::kMember, static_cast<exprFlag>(flags | exprFlagIsNewTarget));
            std::vector<Expr> args;
            logger::Loc closeParenLoc;
            bool isMultiLine = false;

            if (this->lexer.token == T::kOpenParen) {
                std::tuple<std::vector<Expr>, logger::Loc, bool> callArgs = this->parseCallArgs();
                args = std::get<0>(callArgs);
                closeParenLoc = std::get<1>(callArgs);
                isMultiLine = std::get<2>(callArgs);
            }

            ENew e;
            e.target = target;
            e.args = args;
            e.close_paren_loc = closeParenLoc;
            e.is_multi_line = isMultiLine;
            return Expr{std::make_shared<ENew>(std::move(e)), loc};
        }

        case T::kOpenBracket: {
            this->lexer.Next();
            bool isSingleLine = !this->lexer.has_newline_before;
            std::vector<Expr> items;
            deferredErrors selfErrors;
            logger::Loc commaAfterSpread;

            bool oldAllowIn = this->allowIn;
            this->allowIn = true;

            while (this->lexer.token != T::kCloseBracket) {
                switch (this->lexer.token) {
                case T::kComma:
                    items.push_back(Expr{kEMissingShared, this->lexer.Loc()});
                    break;

                case T::kDotDotDot: {
                    if (errors != nullptr) {
                        errors->arraySpreadFeature = this->lexer.Range();
                    } else {
                        this->markSyntaxFeature(compat::JSFeature::kArraySpread, this->lexer.Range());
                    }
                    logger::Loc dotsLoc = this->saveExprCommentsHere();
                    this->lexer.Next();
                    Expr item = this->parseExprOrBindings(L::kComma, &selfErrors);
                    ESpread spread;
                    spread.value = item;
                    items.push_back(Expr{std::make_shared<ESpread>(std::move(spread)), dotsLoc});

                    // Commas are not allowed here when destructuring
                    if (this->lexer.token == T::kComma) {
                        commaAfterSpread = this->lexer.Loc();
                    }
                    break;
                }

                default: {
                    Expr item = this->parseExprOrBindings(L::kComma, &selfErrors);
                    items.push_back(item);
                    break;
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

            if (this->lexer.has_newline_before) {
                isSingleLine = false;
            }
            logger::Loc closeBracketLoc = this->saveExprCommentsHere();
            this->lexer.Expect(T::kCloseBracket);
            this->allowIn = oldAllowIn;

            if (this->willNeedBindingPattern()) {
                // Is this a binding pattern?
            } else if (errors == nullptr) {
                // Is this an expression?
                this->logExprErrors(&selfErrors);
            } else {
                // In this case, we can't distinguish between the two yet
                selfErrors.mergeInto(errors);
            }

            EArray e;
            e.items = items;
            e.comma_after_spread = commaAfterSpread;
            e.is_single_line = isSingleLine;
            e.close_bracket_loc = closeBracketLoc;
            return Expr{std::make_shared<EArray>(std::move(e)), loc};
        }

        case T::kOpenBrace: {
            this->lexer.Next();
            bool isSingleLine = !this->lexer.has_newline_before;
            std::vector<Property> properties;
            deferredErrors selfErrors;
            logger::Loc commaAfterSpread;

            // Allow "in" inside object literals
            bool oldAllowIn = this->allowIn;
            this->allowIn = true;

            while (this->lexer.token != T::kCloseBrace) {
                if (this->lexer.token == T::kDotDotDot) {
                    logger::Loc dotLoc = this->saveExprCommentsHere();
                    this->lexer.Next();
                    Expr value = this->parseExprOrBindings(L::kComma, &selfErrors);
                    Property property;
                    property.kind = PropertyKind::kSpread;
                    property.loc = dotLoc;
                    property.value_or_nil = value;
                    properties.push_back(property);

                    // Commas are not allowed here when destructuring
                    if (this->lexer.token == T::kComma) {
                        commaAfterSpread = this->lexer.Loc();
                    }
                } else {
                    // This property may turn out to be a type in TypeScript, which should be ignored
                    propertyOpts opts;
                    std::pair<Property, bool> property = this->parseProperty(this->saveExprCommentsHere(), PropertyKind::kField, opts, &selfErrors);
                    if (property.second) {
                        properties.push_back(property.first);
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

            if (this->lexer.has_newline_before) {
                isSingleLine = false;
            }
            logger::Loc closeBraceLoc = this->saveExprCommentsHere();
            this->lexer.Expect(T::kCloseBrace);
            this->allowIn = oldAllowIn;

            if (this->willNeedBindingPattern()) {
                // Is this a binding pattern?
            } else if (errors == nullptr) {
                // Is this an expression?
                this->logExprErrors(&selfErrors);
            } else {
                // In this case, we can't distinguish between the two yet
                selfErrors.mergeInto(errors);
            }

            EObject e;
            e.properties = properties;
            e.comma_after_spread = commaAfterSpread;
            e.is_single_line = isSingleLine;
            e.close_brace_loc = closeBraceLoc;
            return Expr{std::make_shared<EObject>(std::move(e)), loc};
        }

        case T::kLessThan: {
            // This is a very complicated and highly ambiguous area of TypeScript
            // syntax. Many similar-looking things are overloaded.
            //
            // TS:
            //
            //   A type cast:
            //     <A>(x)
            //     <[]>(x)
            //     <A[]>(x)
            //     <const>(x)
            //
            //   An arrow function with type parameters:
            //     <A>(x) => {}
            //     <A, B>(x) => {}
            //     <A = B>(x) => {}
            //     <A extends B>(x) => {}
            //     <const A>(x) => {}
            //     <const A extends B>(x) => {}
            //
            //   A syntax error:
            //     <>() => {}
            //
            // TSX:
            //
            //   A JSX element:
            //     <>() => {}</>
            //     <A>(x) => {}</A>
            //     <A extends/>
            //     <A extends>(x) => {}</A>
            //     <A extends={false}>(x) => {}</A>
            //     <const A extends/>
            //     <const A extends>(x) => {}</const>
            //
            //   An arrow function with type parameters:
            //     <A,>(x) => {}
            //     <A, B>(x) => {}
            //     <A = B>(x) => {}
            //     <A extends B>(x) => {}
            //     <const>(x)</const>
            //     <const A extends B>(x) => {}
            //
            //   A syntax error:
            //     <[]>(x)
            //     <A[]>(x)
            //     <>() => {}
            //     <A>(x) => {}
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->options.jsx.Parse && this->isTSArrowFnJSX()) {
                this->skipTypeScriptTypeParameters(allowConstModifier);
                this->lexer.Expect(T::kOpenParen);
                parenExprOpts opts;
                opts.forceArrowFn = true;
                return this->parseParenExpr(loc, level, opts);
            }

            // Print a friendly error message when parsing JSX as JavaScript
            if (!this->options.jsx.Parse && !this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                logger::MsgData note = {nullptr,nullptr,
                    std::string(
                        "The guchho loader for this file is currently set to \"js\" but it must be set to \"jsx\" to be able to parse JSX syntax. "
                        "You can use '--loader:.js=jsx' to do that."
                    )
                };
                this->log.AddErrorWithNotes(&this->tracker, this->lexer.Range(), "The JSX syntax extension is not currently enabled", std::vector<logger::MsgData>{note});
                this->options.jsx.Parse = true;
            }

            if (this->options.jsx.Parse) {
                // Use NextInsideJSXElement() instead of Next() so we parse "<<" as "<"
                this->lexer.NextInsideJSXElement();
                Expr element = this->parseJSXElement(loc);

                // The call to parseJSXElement() above doesn't consume the last
                // TGreaterThan because the caller knows what Next() function to call.
                // Use Next() instead of NextInsideJSXElement() here since the next
                // token is an expression.
                this->lexer.Next();
                return element;
            }

            if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                // This is either an old-style type cast or a generic lambda function

                // TypeScript 4.5 introduced the ".mts" and ".cts" extensions that forbid
                // the use of an expression starting with "<" that would be ambiguous
                // when the file is in JSX mode.
                if (this->options.optionsThatSupportStructuralEquality.ts.NoAmbiguousLessThan && !this->isTSArrowFnJSX()) {
                    this->log.AddError(&this->tracker, this->lexer.Range(),
                        "This syntax is not allowed in files with the \".mts\" or \".cts\" extension");
                }

                // "<T>(x)"
                // "<T>(x) => {}"
                if (ESkipTypeScript result = this->trySkipTypeScriptTypeParametersThenOpenParenWithBacktracking(); result != didNotSkipAnything) {
                    this->lexer.Expect(T::kOpenParen);
                    parenExprOpts opts;
                    opts.forceArrowFn = result == definitelyTypeParameters;
                    return this->parseParenExpr(loc, level, opts);
                }

                // "<T>x"
                this->lexer.Next();
                this->skipTypeScriptType(L::kLowest);
                this->lexer.ExpectGreaterThan(false /* isInsideJSXElement */);
                return this->parsePrefix(level, errors, flags);
            }

            this->lexer.Unexpected();
            return Expr();
        }

        case T::kImport:
            this->lexer.Next();
            return this->parseImportExpr(loc, level);

        default:
            this->lexer.Unexpected();
            return Expr();
        }
    }

    // Parses a "yield" or "yield*" expression. The "yield" keyword has already
    // been consumed. In a generator function, yield pauses iteration and optionally
    // returns a value. "yield* delegates to another iterator.
    //
    // The value is only parsed if the next token is not a terminator (close paren,
    // comma, semicolon, etc.) and there is no newline before it.
    //
    //   Input:  "yield 42"   -> EYield(value=42, is_star=false)
    //   Input:  "yield* gen" -> EYield(value=gen, is_star=true)
    //   Input:  "yield"      -> EYield(value=nil, is_star=false)
    Expr Parser::parseYieldExpr(logger::Loc loc) {
        // Parse a yield-from expression, which yields from an iterator
        bool isStar = this->lexer.token == T::kAsterisk;
        if (isStar && !this->lexer.has_newline_before) {
            this->lexer.Next();
        }

        Expr valueOrNil;

        // The yield expression only has a value in certain cases
        if (isStar) {
            valueOrNil = this->parseExpr(L::kYield);
        } else {
            switch (this->lexer.token) {
            case T::kCloseBrace:
            case T::kCloseBracket:
            case T::kCloseParen:
            case T::kColon:
            case T::kComma:
            case T::kSemicolon:
                break;

            default:
                if (!this->lexer.has_newline_before) {
                    valueOrNil = this->parseExpr(L::kYield);
                }
                break;
            }
        }

        EYield e;
        e.value_or_nil = valueOrNil;
        e.is_star = isStar;
        return Expr{std::make_shared<EYield>(std::move(e)), loc};
    }

    // Parses a dynamic import() expression. The "import" keyword has already been
    // consumed. Handles import.meta, import.defer, and import.source phases as
    // well as the standard import('./module') syntax with optional assertions.
    //
    //   Input:  "./foo.json" )  (after "import(" consumed)
    //   Output: EImportCall(expr=EString("./foo.json"))
    //
    //   Input:  "meta"  (after "import." consumed)
    //   Output: EImportMeta
    Expr Parser::parseImportExpr(logger::Loc loc, L level) {
        compiler::ImportPhase phase = compiler::ImportPhase::kEvaluation;
        if (this->lexer.token == T::kDot) {
            this->lexer.Next();
            if (this->lexer.IsContextualKeyword("meta")) {
                this->esmImportMeta.loc = loc;
                this->esmImportMeta.len = this->lexer.Range().End() - loc.start;
                this->lexer.Next();
                EImportMeta e;
                e.range_len = this->esmImportMeta.len;
                return Expr{std::make_shared<EImportMeta>(std::move(e)), loc};
            } else if (this->lexer.IsContextualKeyword("defer")) {
                this->markSyntaxFeature(compat::JSFeature::kImportDefer, this->lexer.Range());
                phase = compiler::ImportPhase::kDefer;
                this->lexer.Next();
            } else if (this->lexer.IsContextualKeyword("source")) {
                this->markSyntaxFeature(compat::JSFeature::kImportSource, this->lexer.Range());
                phase = compiler::ImportPhase::kSource;
                this->lexer.Next();
            } else {
                this->lexer.Unexpected();
            }
        }

        if (level > L::kCall) {
            logger::Range r = RangeOfIdentifier(this->source, loc);
            this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_ImportWithoutParens));
        }

        bool oldAllowIn = this->allowIn;
        this->allowIn = true;

        this->lexer.Expect(T::kOpenParen);

        Expr value = this->parseExpr(L::kComma);
        Expr optionsOrNil;

        if (this->lexer.token == T::kComma) {
            this->lexer.Next();

            if (this->lexer.token != T::kCloseParen) {
                optionsOrNil = this->parseExpr(L::kComma);

                if (this->lexer.token == T::kComma) {
                    this->lexer.Next();
                }
            }
        }

        logger::Loc closeParenLoc = this->saveExprCommentsHere();
        this->lexer.Expect(T::kCloseParen);

        this->allowIn = oldAllowIn;
        EImportCall e;
        e.expr = value;
        e.options_or_nil = optionsOrNil;
        e.close_paren_loc = closeParenLoc;
        e.phase = phase;
        return Expr{std::make_shared<EImportCall>(std::move(e)), loc};
    }

    // Parses an expression or binding, deferring errors until the ambiguity is resolved.
    // Used in contexts where the parser doesn't yet know if the code is an expression
    // (like a function call argument) or a binding pattern (like an arrow function
    // parameter).
    Expr Parser::parseExprOrBindings(L level, deferredErrors *errors) {
        return this->parseExprCommon(level, errors, static_cast<exprFlag>(0));
    }

    // Parses a standard expression with no deferred errors and no special flags.
    // This is the most common entry point for expression parsing.
    Expr Parser::parseExpr(L level) {
        return this->parseExprCommon(level, nullptr, static_cast<exprFlag>(0));
    }

    // Parses an expression with flags but no deferred errors. Flags control behavior
    // like whether this is a for-loop initializer or a new.target expression.
    Expr Parser::parseExprWithFlags(L level, exprFlag flags) {
        return this->parseExprCommon(level, nullptr, flags);
    }

    // Core expression parser: calls parsePrefix to get the initial expression atom,
    // then handles @__PURE__ and @__NO_SIDE_EFFECTS__ comments by setting flags on
    // call/new nodes, and finally calls parseSuffix to handle binary operators and
    // postfix expressions.
    //
    //   Input:  "a + b" at level=kLowest
    //   Output: EBinary(op=+, left=a, right=b)
    Expr Parser::parseExprCommon(L level, deferredErrors *errors, exprFlag flags) {
        CommentBefore lexerCommentFlags = this->lexer.has_comment_before;
        Expr expr = this->parsePrefix(level, errors, flags);

        if ((Has(lexerCommentFlags, CommentBefore::kPure) || Has(lexerCommentFlags, CommentBefore::kNoSideEffects)) && !this->options.optionsThatSupportStructuralEquality.ignoreDCEAnnotations) {
            if (Has(lexerCommentFlags, CommentBefore::kNoSideEffects)) {
                if (EArrow *arrow = Get<EArrow>(expr.data)) {
                    arrow->has_no_side_effects_comment = true;
                } else if (EFunction *fn = Get<EFunction>(expr.data)) {
                    fn->fn.has_no_side_effects_comment = true;
                }
            }

            // @__PURE__ comments apply to the next CallExpression or NewExpression.
            // For example, in "/* @__PURE__ */ a().b() + c()", the comment applies
            // to "a().b()", marking it as safe to remove if unused.
            if (Has(lexerCommentFlags, CommentBefore::kPure) && level < L::kCall) {
                expr = this->parseSuffix(expr, static_cast<L>(static_cast<uint8_t>(L::kCall) - 1), errors, flags);
                if (ECall *call = Get<ECall>(expr.data)) {
                    call->can_be_unwrapped_if_unused = true;
                } else if (ENew *new_ = Get<ENew>(expr.data)) {
                    new_->can_be_unwrapped_if_unused = true;
                }
            }
        }

        return this->parseSuffix(expr, level, errors, flags);
    }

    // Parses suffix and infix operators after an expression atom. This is the second
    // half of the Pratt parser, handling all binary operators, postfix operators,
    // member access, optional chaining, and TypeScript type assertions.
    //
    // Precedence is controlled by the level parameter: operators at higher precedence
    // levels bind tighter. Assignment operators right-associate by parsing at
    // level - 1.
    //
    // Handles: property access (.), optional chaining (?.), computed index ([]),
    // function calls (()), tagged templates, ternary (? :), postfix ++/--,
    // comma, arithmetic (+ - * / % **), equality (== != === !==),
    // comparison (< <= > >=), shifts (<< >> >>>), nullish coalescing (??),
    // logical (|| &&), bitwise (| & ^), assignment (= += etc.),
    // in, instanceof, and TypeScript as/satisfies casts.
    Expr Parser::parseSuffix(Expr left, L level, deferredErrors *errors, exprFlag flags) {
        OptionalChain optionalChain = OptionalChain::kNone;

        for (;;) {
            if (this->lexer.Loc() == this->afterArrowBodyLoc) {
                for (;;) {
                    switch (this->lexer.token) {
                    case T::kComma:
                        if (level >= L::kComma) {
                            return left;
                        }
                        this->lexer.Next();
                        {
                            EBinary b;
                            b.op = OpCode::kBinOpComma;
                            b.left = left;
                            b.right = this->parseExpr(L::kComma);
                            left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                        }
                        break;

                    default:
                        return left;
                    }
                }
            }

            if (this->lexer.Loc() == this->forbidSuffixAfterAsLoc) {
                return left;
            }

            OptionalChain oldOptionalChain = optionalChain;
            optionalChain = OptionalChain::kNone;

            switch (this->lexer.token) {
            case T::kDot:
                this->lexer.Next();

                if (this->lexer.token == T::kPrivateIdentifier) {
                if (Get<ESuper>(left.data)) {
                    this->lexer.Expected(T::kIdentifier);
                }
                MaybeSubstring name = this->lexer.identifier;
                    logger::Loc nameLoc = this->lexer.Loc();
                    this->reportPrivateNameUsage(name.str);
                    this->lexer.Next();
                    compiler::Ref ref = this->storeNameInRef(name);
                    EIndex e;
                    e.target = left;
                    EPrivateIdentifier pi;
                    pi.ref = ref;
                    e.index = Expr{std::make_shared<EPrivateIdentifier>(std::move(pi)), nameLoc};
                    e.optional_chain = oldOptionalChain;
                    left = Expr{std::make_shared<EIndex>(std::move(e)), left.loc};
                } else {
                    if (!this->lexer.IsIdentifierOrKeyword()) {
                        this->lexer.Expect(T::kIdentifier);
                    }
                    MaybeSubstring name = this->lexer.identifier;
                    logger::Loc nameLoc = this->lexer.Loc();
                    this->lexer.Next();
                    left = Expr{*this->dotOrMangledPropParse(left, name, nameLoc, oldOptionalChain, wasOriginallyDot), left.loc};
                }

                optionalChain = oldOptionalChain;
                break;

            case T::kQuestionDot: {
                if ((flags & exprFlagIsNewTarget) != 0) {
                    this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_UnparenthesizedOptionalChainNew));
                    flags = static_cast<exprFlag>(flags & ~exprFlagIsNewTarget); // Don't report this error more than once in this spot
                }

                this->lexer.Next();
                OptionalChain optionalStart = OptionalChain::kStart;

                if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                    if (std::optional<std::pair<bool, SideEffects>> result = ToNullOrUndefinedWithSideEffects(left.data); result && !result->first) {
                        optionalStart = OptionalChain::kNone;
                    }
                }

                switch (this->lexer.token) {
                case T::kOpenBracket: {
                    this->lexer.Next();

                    bool oldAllowIn = this->allowIn;
                    this->allowIn = true;

                    Expr index = this->parseExpr(L::kLowest);

                    this->allowIn = oldAllowIn;

                    logger::Loc closeBracketLoc = this->saveExprCommentsHere();
                    this->lexer.Expect(T::kCloseBracket);
                    EIndex e;
                    e.target = left;
                    e.index = index;
                    e.optional_chain = optionalStart;
                    e.close_bracket_loc = closeBracketLoc;
                    left = Expr{std::make_shared<EIndex>(std::move(e)), left.loc};
                    break;
                }

                case T::kOpenParen: {
                    if (level >= L::kCall) {
                        return left;
                    }
                    CallKind kind = CallKind::kNormal;
                    if (IsPropertyAccess(left)) {
                        kind = CallKind::kTargetWasOriginallyPropertyAccess;
                    }
                    std::tuple<std::vector<Expr>, logger::Loc, bool> callArgs = this->parseCallArgs();
                    ECall e;
                    e.target = left;
                    e.args = std::get<0>(callArgs);
                    e.close_paren_loc = std::get<1>(callArgs);
                    e.optional_chain = optionalStart;
                    e.is_multi_line = std::get<2>(callArgs);
                    e.kind = kind;
                    left = Expr{std::make_shared<ECall>(std::move(e)), left.loc};
                    break;
                }

                case T::kLessThan:
                case T::kLessThanLessThan: {
                    if (!this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                        this->lexer.Expected(T::kIdentifier);
                    }
                    skipTypeScriptTypeArgumentsOpts opts;
                    this->skipTypeScriptTypeArguments(opts);
                    if (this->lexer.token != T::kOpenParen) {
                        this->lexer.Expected(T::kOpenParen);
                    }
                    if (level >= L::kCall) {
                        return left;
                    }
                    CallKind kind = CallKind::kNormal;
                    if (IsPropertyAccess(left)) {
                        kind = CallKind::kTargetWasOriginallyPropertyAccess;
                    }
                    std::tuple<std::vector<Expr>, logger::Loc, bool> callArgs = this->parseCallArgs();
                    ECall e;
                    e.target = left;
                    e.args = std::get<0>(callArgs);
                    e.close_paren_loc = std::get<1>(callArgs);
                    e.optional_chain = optionalStart;
                    e.is_multi_line = std::get<2>(callArgs);
                    e.kind = kind;
                    left = Expr{std::make_shared<ECall>(std::move(e)), left.loc};
                    break;
                }

                default:
                    if (this->lexer.token == T::kPrivateIdentifier) {
                        MaybeSubstring name = this->lexer.identifier;
                        logger::Loc nameLoc = this->lexer.Loc();
                        this->reportPrivateNameUsage(name.str);
                        this->lexer.Next();
                        compiler::Ref ref = this->storeNameInRef(name);
                        EIndex e;
                        e.target = left;
                        EPrivateIdentifier pi;
                        pi.ref = ref;
                        e.index = Expr{std::make_shared<EPrivateIdentifier>(std::move(pi)), nameLoc};
                        e.optional_chain = optionalStart;
                        left = Expr{std::make_shared<EIndex>(std::move(e)), left.loc};
                    } else {
                        if (!this->lexer.IsIdentifierOrKeyword()) {
                            this->lexer.Expect(T::kIdentifier);
                        }
                        MaybeSubstring name = this->lexer.identifier;
                        logger::Loc nameLoc = this->lexer.Loc();
                        this->lexer.Next();
                        left = Expr{*this->dotOrMangledPropParse(left, name, nameLoc, optionalStart, wasOriginallyDot), left.loc};
                    }
                    break;
                }

                if (optionalStart == OptionalChain::kStart) {
                    optionalChain = OptionalChain::kContinue;
                }
                break;
            }

            case T::kNoSubstitutionTemplateLiteral: {
                if (oldOptionalChain != OptionalChain::kNone) {
                    this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_TemplateLiteralsOptionalChainTag));
                }
                logger::Loc headLoc = this->lexer.Loc();
                Lexer::CookedAndRawTemplateContentsResult head = this->lexer.CookedAndRawTemplateContents();
                this->lexer.Next();
                ETemplate e;
                e.tag_or_nil = left;
                e.head_loc = headLoc;
                e.head_cooked = head.cooked;
                e.head_cooked_is_valid = head.is_valid;
                e.head_raw = head.raw;
                e.tag_was_originally_property_access = IsPropertyAccess(left);
                left = Expr{std::make_shared<ETemplate>(std::move(e)), left.loc};
                break;
            }

            case T::kTemplateHead: {
                if (oldOptionalChain != OptionalChain::kNone) {
                    this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_TemplateLiteralsOptionalChainTag));
                }
                logger::Loc headLoc = this->lexer.Loc();
                Lexer::CookedAndRawTemplateContentsResult head = this->lexer.CookedAndRawTemplateContents();
                std::pair<std::vector<TemplatePart>, logger::Loc> parts = this->parseTemplateParts(true);
                ETemplate e;
                e.tag_or_nil = left;
                e.head_loc = headLoc;
                e.head_cooked = head.cooked;
                e.head_cooked_is_valid = head.is_valid;
                e.head_raw = head.raw;
                e.parts = parts.first;
                e.tag_was_originally_property_access = IsPropertyAccess(left);
                left = Expr{std::make_shared<ETemplate>(std::move(e)), left.loc};
                break;
            }

            case T::kOpenBracket: {
                if ((flags & exprFlagDecorator) != 0) {
                    return left;
                }

                this->lexer.Next();

                bool oldAllowIn = this->allowIn;
                this->allowIn = true;

                Expr index = this->parseExpr(L::kLowest);

                this->allowIn = oldAllowIn;

                logger::Loc closeBracketLoc = this->saveExprCommentsHere();
                this->lexer.Expect(T::kCloseBracket);
                EIndex e;
                e.target = left;
                e.index = index;
                e.optional_chain = oldOptionalChain;
e.close_bracket_loc = closeBracketLoc;
                left = Expr{std::make_shared<EIndex>(std::move(e)), left.loc};
                optionalChain = oldOptionalChain;
                break;
            }

            case T::kOpenParen: {
                if (level >= L::kCall) {
                    return left;
                }
                CallKind kind = CallKind::kNormal;
                if (IsPropertyAccess(left)) {
                    kind = CallKind::kTargetWasOriginallyPropertyAccess;
                }
                std::tuple<std::vector<Expr>, logger::Loc, bool> callArgs = this->parseCallArgs();
                ECall e;
                e.target = left;
                e.args = std::get<0>(callArgs);
                e.close_paren_loc = std::get<1>(callArgs);
                e.optional_chain = oldOptionalChain;
                e.is_multi_line = std::get<2>(callArgs);
                e.kind = kind;
                left = Expr{std::make_shared<ECall>(std::move(e)), left.loc};
                optionalChain = oldOptionalChain;
                break;
            }

            case T::kQuestion: {
                if (level >= L::kConditional) {
                    return left;
                }
                this->lexer.Next();

                if (this->options.optionsThatSupportStructuralEquality.ts.Parse && left.loc == this->latestArrowArgLoc && (this->lexer.token == T::kColon ||
                    this->lexer.token == T::kCloseParen || this->lexer.token == T::kComma)) {
                    if (errors == nullptr) {
                        this->lexer.Unexpected();
                    }
                    errors->invalidExprAfterQuestion = this->lexer.Range();
                    return left;
                }

                bool oldAllowIn = this->allowIn;
                this->allowIn = true;

                Expr yes = this->parseExprWithFlags(L::kComma, exprFlagAfterQuestionAndBeforeColon);

                this->allowIn = oldAllowIn;

                this->lexer.Expect(T::kColon);
                Expr no = this->parseExprWithFlags(L::kComma, static_cast<exprFlag>(flags & exprFlagAfterQuestionAndBeforeColon));
                EIf e;
                e.test = left;
                e.yes = yes;
                e.no = no;
                left = Expr{std::make_shared<EIf>(std::move(e)), left.loc};
                break;
            }

            case T::kExclamation:
                if (this->lexer.has_newline_before) {
                    return left;
                }
                if (!this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                    this->lexer.Unexpected();
                }
                this->lexer.Next();
                optionalChain = oldOptionalChain;
                break;

            case T::kMinusMinus:
                if (this->lexer.has_newline_before || level >= L::kPostfix) {
                    return left;
                }
                this->lexer.Next();
                {
                    EUnary e;
                    e.op = OpCode::kUnOpPostDec;
                    e.value = left;
                    left = Expr{std::make_shared<EUnary>(std::move(e)), left.loc};
                }
                break;

            case T::kPlusPlus:
                if (this->lexer.has_newline_before || level >= L::kPostfix) {
                    return left;
                }
                this->lexer.Next();
                {
                    EUnary e;
                    e.op = OpCode::kUnOpPostInc;
                    e.value = left;
                    left = Expr{std::make_shared<EUnary>(std::move(e)), left.loc};
                }
                break;

            case T::kComma:
                if (level >= L::kComma) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpComma;
                    b.left = left;
                    b.right = this->parseExpr(L::kComma);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kPlus:
                if (level >= L::kAdd) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpAdd;
                    b.left = left;
                    b.right = this->parseExpr(L::kAdd);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kPlusEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpAddAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kMinus:
                if (level >= L::kAdd) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpSub;
                    b.left = left;
                    b.right = this->parseExpr(L::kAdd);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kMinusEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpSubAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kAsterisk:
                if (level >= L::kMultiply) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpMul;
                    b.left = left;
                    b.right = this->parseExpr(L::kMultiply);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kAsteriskAsterisk:
                if (level >= L::kExponentiation) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpPow;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kExponentiation) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kAsteriskAsteriskEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpPowAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kAsteriskEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpMulAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kPercent:
                if (level >= L::kMultiply) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpRem;
                    b.left = left;
                    b.right = this->parseExpr(L::kMultiply);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kPercentEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpRemAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kSlash:
                if (level >= L::kMultiply) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpDiv;
                    b.left = left;
                    b.right = this->parseExpr(L::kMultiply);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kSlashEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpDivAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kEqualsEquals:
                if (level >= L::kEquals) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpLooseEq;
                    b.left = left;
                    b.right = this->parseExpr(L::kEquals);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kExclamationEquals:
                if (level >= L::kEquals) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpLooseNe;
                    b.left = left;
                    b.right = this->parseExpr(L::kEquals);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kEqualsEqualsEquals:
                if (level >= L::kEquals) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpStrictEq;
                    b.left = left;
                    b.right = this->parseExpr(L::kEquals);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kExclamationEqualsEquals:
                if (level >= L::kEquals) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpStrictNe;
                    b.left = left;
                    b.right = this->parseExpr(L::kEquals);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kLessThan:
                if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->trySkipTypeArgumentsInExpressionWithBacktracking()) {
                    optionalChain = oldOptionalChain;
                    continue;
                }

                if (level >= L::kCompare) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpLt;
                    b.left = left;
                    b.right = this->parseExpr(L::kCompare);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kLessThanEquals:
                if (level >= L::kCompare) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpLe;
                    b.left = left;
                    b.right = this->parseExpr(L::kCompare);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kGreaterThan:
                if (level >= L::kCompare) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpGt;
                    b.left = left;
                    b.right = this->parseExpr(L::kCompare);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kGreaterThanEquals:
                if (level >= L::kCompare) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpGe;
                    b.left = left;
                    b.right = this->parseExpr(L::kCompare);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kLessThanLessThan:
                if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->trySkipTypeArgumentsInExpressionWithBacktracking()) {
                    optionalChain = oldOptionalChain;
                    continue;
                }

                if (level >= L::kShift) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpShl;
                    b.left = left;
                    b.right = this->parseExpr(L::kShift);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kLessThanLessThanEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpShlAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kGreaterThanGreaterThan:
                if (level >= L::kShift) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpShr;
                    b.left = left;
                    b.right = this->parseExpr(L::kShift);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kGreaterThanGreaterThanEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpShrAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kGreaterThanGreaterThanGreaterThan:
                if (level >= L::kShift) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpUShr;
                    b.left = left;
                    b.right = this->parseExpr(L::kShift);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kGreaterThanGreaterThanGreaterThanEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpUShrAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kQuestionQuestion:
                if (level >= L::kNullishCoalescing) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpNullishCoalescing;
                    b.left = left;
                    b.right = this->parseExpr(L::kNullishCoalescing);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kQuestionQuestionEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpNullishCoalescingAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kBarBar: {
                if (level >= L::kLogicalOr) {
                    return left;
                }

                if (level == L::kNullishCoalescing) {
                    this->logNullishCoalescingErrorPrecedenceError("||");
                }

                this->lexer.Next();
                Expr right = this->parseExpr(L::kLogicalOr);
                EBinary b;
                b.op = OpCode::kBinOpLogicalOr;
                b.left = left;
                b.right = right;
                left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};

                if (level < L::kNullishCoalescing) {
                    left = this->parseSuffix(left, static_cast<L>(static_cast<uint8_t>(L::kNullishCoalescing) + 1), nullptr, flags);
                    if (this->lexer.token == T::kQuestionQuestion) {
                        this->logNullishCoalescingErrorPrecedenceError("||");
                    }
                }
                break;
            }

            case T::kBarBarEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpLogicalOrAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kAmpersandAmpersand: {
                if (level >= L::kLogicalAnd) {
                    return left;
                }

                if (level == L::kNullishCoalescing) {
                    this->logNullishCoalescingErrorPrecedenceError("&&");
                }

                this->lexer.Next();
                EBinary b;
                b.op = OpCode::kBinOpLogicalAnd;
                b.left = left;
                b.right = this->parseExpr(L::kLogicalAnd);
                left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};

                if (level < L::kNullishCoalescing) {
                    left = this->parseSuffix(left, static_cast<L>(static_cast<uint8_t>(L::kNullishCoalescing) + 1), nullptr, flags);
                    if (this->lexer.token == T::kQuestionQuestion) {
                        this->logNullishCoalescingErrorPrecedenceError("&&");
                    }
                }
                break;
            }

            case T::kAmpersandAmpersandEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpLogicalAndAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kBar:
                if (level >= L::kBitwiseOr) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpBitwiseOr;
                    b.left = left;
                    b.right = this->parseExpr(L::kBitwiseOr);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kBarEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpBitwiseOrAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kAmpersand:
                if (level >= L::kBitwiseAnd) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpBitwiseAnd;
                    b.left = left;
                    b.right = this->parseExpr(L::kBitwiseAnd);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kAmpersandEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpBitwiseAndAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kCaret:
                if (level >= L::kBitwiseXor) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpBitwiseXor;
                    b.left = left;
                    b.right = this->parseExpr(L::kBitwiseXor);
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kCaretEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                {
                    EBinary b;
                    b.op = OpCode::kBinOpBitwiseXorAssign;
                    b.left = left;
                    b.right = this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1));
                    left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                }
                break;

            case T::kEquals:
                if (level >= L::kAssign) {
                    return left;
                }
                this->lexer.Next();
                left = Assign(left, this->parseExpr(static_cast<L>(static_cast<uint8_t>(L::kAssign) - 1)));
                break;

            case T::kIn: {
                if (level >= L::kCompare || !this->allowIn) {
                    return left;
                }

                logger::MsgKind kind = logger::MsgKind::kWarning;
                if (this->suppressWarningsAboutWeirdCode) {
                    kind = logger::MsgKind::kDebug;
                }
                if (EUnary *e = Get<EUnary>(left.data)) {
                    if (e->op == OpCode::kUnOpNot) {
                        logger::Range r;
                        r.loc = left.loc;
                        r.len = this->source.LocBeforeWhitespace(this->lexer.Loc()).start - left.loc.start;
                        logger::MsgData data = this->tracker.MakeMsgData(r, "Suspicious use of the \"!\" operator inside the \"in\" operator");
                        data.location->suggestion = "(" + this->source.TextForRange(r) + ")";
                        logger::MsgData note;
                        note.text = "The code \"!x in y\" is parsed as \"(!x) in y\". You need to insert parentheses to get \"!(x in y)\" instead.";
                        logger::Msg msg;
                        msg.kind = kind;
                        msg.data = data;
                        msg.notes.push_back(note);
                        this->log.AddMsgID(logger::MsgID::kJS_SuspiciousBooleanNot, msg);
                    }
                }

                this->lexer.Next();
                EBinary b;
                b.op = OpCode::kBinOpIn;
                b.left = left;
                b.right = this->parseExpr(L::kCompare);
                left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                break;
            }

            case T::kInstanceof: {
                if (level >= L::kCompare) {
                    return left;
                }

                logger::MsgKind kind = logger::MsgKind::kWarning;
                if (this->suppressWarningsAboutWeirdCode) {
                    kind = logger::MsgKind::kDebug;
                }
                if (EUnary *e = Get<EUnary>(left.data)) {
                    if (e->op == OpCode::kUnOpNot) {
                        logger::Range r;
                        r.loc = left.loc;
                        r.len = this->source.LocBeforeWhitespace(this->lexer.Loc()).start - left.loc.start;
                        logger::MsgData data = this->tracker.MakeMsgData(r, "Suspicious use of the \"!\" operator inside the \"instanceof\" operator");
                        data.location->suggestion = "(" + this->source.TextForRange(r) + ")";
                        logger::MsgData note;
                        note.text = "The code \"!x instanceof y\" is parsed as \"(!x) instanceof y\". You need to insert parentheses to get \"!(x instanceof y)\" instead.";
                        logger::Msg msg;
                        msg.kind = kind;
                        msg.data = data;
                        msg.notes.push_back(note);
                        this->log.AddMsgID(logger::MsgID::kJS_SuspiciousBooleanNot, msg);
                    }
                }

                this->lexer.Next();
                EBinary b;
                b.op = OpCode::kBinOpInstanceof;
                b.left = left;
                b.right = this->parseExpr(L::kCompare);
                left = Expr{std::make_shared<EBinary>(std::move(b)), left.loc};
                break;
            }

            default:
                if (this->options.optionsThatSupportStructuralEquality.ts.Parse && level < L::kCompare && !this->lexer.has_newline_before && (this->lexer.IsContextualKeyword("as") || this->lexer.IsContextualKeyword("satisfies"))) {
                    this->lexer.Next();
                    this->skipTypeScriptType(L::kLowest);

                    switch (this->lexer.token) {
                    case T::kPlusPlus:
                    case T::kMinusMinus:
                    case T::kNoSubstitutionTemplateLiteral:
                    case T::kTemplateHead:
                    case T::kOpenParen:
                    case T::kOpenBracket:
                    case T::kQuestionDot:
                        this->forbidSuffixAfterAsLoc = this->lexer.Loc();
                        return left;

                    default:
                        break;
                    }
                    switch (this->lexer.token) {
                    case T::kEquals:
                    case T::kPlusEquals:
                    case T::kMinusEquals:
                    case T::kAsteriskAsteriskEquals:
                    case T::kAsteriskEquals:
                    case T::kPercentEquals:
                    case T::kSlashEquals:
                    case T::kBarBarEquals:
                    case T::kAmpersandAmpersandEquals:
                    case T::kQuestionQuestionEquals:
                    case T::kBarEquals:
                    case T::kAmpersandEquals:
                    case T::kCaretEquals:
                    case T::kLessThanLessThanEquals:
                    case T::kGreaterThanGreaterThanEquals:
                    case T::kGreaterThanGreaterThanGreaterThanEquals:
                        this->forbidSuffixAfterAsLoc = this->lexer.Loc();
                        return left;

                    default:
                        break;
                    }
                    continue;
                }

                return left;
            }
        }
    }
    // Disambiguates between an expression, a "let" declaration, a "using" declaration,
    // and an "await using" declaration. The parser sees the leading identifier and must
    // decide which form it is based on the token that follows.
    //
    // Returns a tuple of (expression, statement, declarations). If this is a pure
    // expression, the statement and declarations are empty. If this is a declaration,
    // the expression is empty.
    //
    //   Input:  "x + 1"
    //   Output: expr=EBinary(+, x, 1), stmt=nil
    //
    //   Input:  "let x = 5"
    //   Output: expr=nil, stmt=SLocal(kind=let, decls=[{x, 5}])
    std::tuple<Expr, Stmt, std::vector<Decl>> Parser::parseExprOrLetOrUsingStmt(parseStmtOpts opts) {
        bool couldBeLet = false;
        bool couldBeUsing = false;
        bool couldBeAwaitUsing = false;
        logger::Range tokenRange = this->lexer.Range();

        if (this->lexer.token == T::kIdentifier) {
            std::string_view raw = this->lexer.Raw();
            couldBeLet = raw == "let";
            couldBeUsing = raw == "using";
            couldBeAwaitUsing = raw == "await" && this->fnOrArrowDataParse.await == allowExpr;
        }

        if (!couldBeLet && !couldBeUsing && !couldBeAwaitUsing) {
            exprFlag flags = static_cast<exprFlag>(0);
            if (opts.isForLoopInit) {
                flags = static_cast<exprFlag>(flags | exprFlagForLoopInit);
            }
            if (opts.isForAwaitLoopInit) {
                flags = static_cast<exprFlag>(flags | exprFlagForAwaitLoopInit);
            }
            return std::make_tuple(this->parseExprCommon(L::kLowest, nullptr, flags), Stmt(), std::vector<Decl>());
        }

        MaybeSubstring name = this->lexer.identifier;
        this->lexer.Next();

        if (couldBeLet) {
            bool isLet = opts.isExport;
            switch (this->lexer.token) {
            case T::kIdentifier:
            case T::kOpenBracket:
            case T::kOpenBrace:
                if (opts.lexicalDecl == lexicalDeclAllowAll || !this->lexer.has_newline_before || this->lexer.token == T::kOpenBracket) {
                    isLet = true;
                }
                break;

            default:
                break;
            }
            if (isLet) {
                if (opts.lexicalDecl != lexicalDeclAllowAll) {
                    this->forbidLexicalDecl(tokenRange.loc);
                }
                this->markSyntaxFeature(compat::JSFeature::kConstAndLet, tokenRange);
                std::vector<Decl> decls = this->parseAndDeclareDecls(compiler::SymbolKind::kOther, opts);
                SLocal s;
                s.kind = LocalKind::kLet;
                s.decls = decls;
                s.is_export = opts.isExport;
                return std::make_tuple(Expr(), Stmt{std::make_shared<SLocal>(std::move(s)), tokenRange.loc}, decls);
            }
        } else if (couldBeUsing && this->lexer.token == T::kIdentifier && !this->lexer.has_newline_before && (!opts.isForLoopInit || this->lexer.Raw() != "of")) {
            if (opts.isCaseBody) {
                this->forbidUsingInSwitch(tokenRange.loc);
            } else if (opts.lexicalDecl != lexicalDeclAllowAll) {
                this->forbidLexicalDecl(tokenRange.loc);
            }
            opts.isUsingStmt = true;
            std::vector<Decl> decls = this->parseAndDeclareDecls(compiler::SymbolKind::kConst, opts);
            if (!opts.isForLoopInit) {
                this->requireInitializers(LocalKind::kUsing, decls);
            }
            SLocal s;
            s.kind = LocalKind::kUsing;
            s.decls = decls;
            s.is_export = opts.isExport;
            return std::make_tuple(Expr(), Stmt{std::make_shared<SLocal>(std::move(s)), tokenRange.loc}, decls);
        } else if (couldBeAwaitUsing) {
            if (this->fnOrArrowDataParse.isTopLevel) {
                this->topLevelAwaitKeyword = tokenRange;
            }
            Expr value;
            if (this->lexer.token == T::kIdentifier && this->lexer.Raw() == "using") {
                logger::Loc usingLoc = this->saveExprCommentsHere();
                logger::Range usingRange = this->lexer.Range();
                this->lexer.Next();
                if (this->lexer.token == T::kIdentifier && !this->lexer.has_newline_before) {
                    if (opts.isCaseBody) {
                        this->forbidUsingInSwitch(usingRange.loc);
                    } else if (opts.lexicalDecl != lexicalDeclAllowAll) {
                        this->forbidLexicalDecl(usingRange.loc);
                    }
                    opts.isUsingStmt = true;
                    std::vector<Decl> decls = this->parseAndDeclareDecls(compiler::SymbolKind::kConst, opts);
                    if (!opts.isForLoopInit) {
                        this->requireInitializers(LocalKind::kAwaitUsing, decls);
                    }
                    SLocal s;
                    s.kind = LocalKind::kAwaitUsing;
                    s.decls = decls;
                    s.is_export = opts.isExport;
                    return std::make_tuple(Expr(), Stmt{std::make_shared<SLocal>(std::move(s)), tokenRange.loc}, decls);
                }
                EIdentifier e;
                e.ref = this->storeNameInRef(MaybeSubstring{"using"});
                value = Expr{std::make_shared<EIdentifier>(std::move(e)), usingLoc};
            } else {
                value = this->parseExpr(L::kPrefix);
            }
            if (this->lexer.token == T::kAsteriskAsterisk) {
                this->lexer.Unexpected();
            }
            value = this->parseSuffix(value, L::kPrefix, nullptr, static_cast<exprFlag>(0));
            EAwait e;
            e.value = value;
            Expr expr{std::make_shared<EAwait>(std::move(e)), tokenRange.loc};
            return std::make_tuple(this->parseSuffix(expr, L::kLowest, nullptr, static_cast<exprFlag>(0)), Stmt(), std::vector<Decl>());
        }

        // Parse the remainder of this expression that starts with an identifier
        EIdentifier e;
        e.ref = this->storeNameInRef(name);
        Expr expr{std::make_shared<EIdentifier>(std::move(e)), tokenRange.loc};
        return std::make_tuple(this->parseSuffix(expr, L::kLowest, nullptr, static_cast<exprFlag>(0)), Stmt(), std::vector<Decl>());
    }

    // Parses comma-separated function call arguments between "(" and ")".
    // Handles spread arguments (...expr), tracks whether the call spans multiple
    // lines (for formatting), and restores the allowIn flag afterward.
    //
    //   Input:  "1, 2, 3" )   (after "(" consumed)
    //   Output: args=[1, 2, 3], closeParenLoc=loc of ")", isMultiLine=false
    //
    //   Input:  "...x" )      (after "(" consumed)
    //   Output: args=[ESpread(x)], closeParenLoc=loc of ")", isMultiLine=false
    std::tuple<std::vector<Expr>, logger::Loc, bool> Parser::parseCallArgs() {
        std::vector<Expr> args;
        logger::Loc closeParenLoc;
        bool isMultiLine = false;

        bool oldAllowIn = this->allowIn;
        this->allowIn = true;

        this->lexer.Expect(T::kOpenParen);

        while (this->lexer.token != T::kCloseParen) {
            if (this->lexer.has_newline_before) {
                isMultiLine = true;
            }
            logger::Loc loc = this->lexer.Loc();
            bool isSpread = this->lexer.token == T::kDotDotDot;
            if (isSpread) {
                this->markSyntaxFeature(compat::JSFeature::kRestArgument, this->lexer.Range());
                this->lexer.Next();
            }
            Expr arg = this->parseExpr(L::kComma);
            if (isSpread) {
                ESpread s;
                s.value = arg;
                arg = Expr{std::make_shared<ESpread>(std::move(s)), loc};
            }
            args.push_back(arg);
            if (this->lexer.token != T::kComma) {
                break;
            }
            if (this->lexer.has_newline_before) {
                isMultiLine = true;
            }
            this->lexer.Next();
        }

        if (this->lexer.has_newline_before) {
            isMultiLine = true;
        }
        closeParenLoc = this->saveExprCommentsHere();
        this->lexer.Expect(T::kCloseParen);
        this->allowIn = oldAllowIn;
        return std::make_tuple(args, closeParenLoc, isMultiLine);
    }


    // Parses the interpolated parts of a template literal: ${expr1}...${expr2}...
    // Loops until the template tail (backtick), parsing each embedded expression
    // and the text segments between them. If includeRaw is true, also captures
    // the raw (uncooked) text for each segment (used for tagged templates).
    //
    //   Input:  "${x + 1} hello ${y}"   (template head already consumed)
    //   Output: parts=[{value=x+1, tail=" hello "}, {value=y, tail=""}]
    std::pair<std::vector<TemplatePart>, logger::Loc> Parser::parseTemplateParts(bool includeRaw) {
        std::vector<TemplatePart> parts;
        logger::Loc legacyOctalLoc;

        bool oldAllowIn = this->allowIn;
        this->allowIn = true;

        for (;;) {
            this->lexer.Next();
            Expr value = this->parseExpr(L::kLowest);
            logger::Loc tailLoc = this->lexer.Loc();
            this->lexer.RescanCloseBraceAsTemplateToken();
            if (includeRaw) {
                Lexer::CookedAndRawTemplateContentsResult tail = this->lexer.CookedAndRawTemplateContents();
                TemplatePart part;
                part.value = value;
                part.tail_loc = tailLoc;
                part.tail_cooked = tail.cooked;
                part.tail_cooked_is_valid = tail.is_valid;
                part.tail_raw = tail.raw;
                parts.push_back(part);
            } else {
                TemplatePart part;
                part.value = value;
                part.tail_loc = tailLoc;
                part.tail_cooked = this->lexer.StringLiteral();
                parts.push_back(part);
                if (this->lexer.legacy_octal_loc.start > tailLoc.start) {
                    legacyOctalLoc = this->lexer.legacy_octal_loc;
                }
            }
            if (this->lexer.token == T::kTemplateTail) {
                this->lexer.Next();
                break;
            }
        }

        this->allowIn = oldAllowIn;

        return std::make_pair(parts, legacyOctalLoc);
    }

}