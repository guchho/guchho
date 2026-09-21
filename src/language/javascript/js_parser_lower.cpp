#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/config.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/javascript/js_helpers.hpp"


namespace guchho::javascript {

namespace {

// Returns the address of the object stored inside an expression variant. Copies
// of the same expression share the same address, so this can be used as a map
// key to identify a specific AST node.
const void *EDataPtr(const E &data) {
    return std::visit([](const auto &ptr) -> const void * { return ptr.get(); }, data);
}


bool bindingHasObjectRest(const Binding &binding) {
    if (const BArray *b = Get<BArray>(binding.data)) {
        for (const ArrayBinding &item : b->items) {
            if (bindingHasObjectRest(item.binding)) {
                return true;
            }
        }
    } else if (const BObject *bo = Get<BObject>(binding.data)) {
        for (const PropertyBinding &property : bo->properties) {
            if (property.is_spread || bindingHasObjectRest(property.value)) {
                return true;
            }
        }
    }
    return false;
}

bool exprHasObjectRest(const Expr &expr) {
    if (const EBinary *e = Get<EBinary>(expr.data)) {
        if (e->op == OpCode::kBinOpAssign && exprHasObjectRest(e->left)) {
            return true;
        }
    } else if (const EArray *e2 = Get<EArray>(expr.data)) {
        for (const Expr &item : e2->items) {
            if (exprHasObjectRest(item)) {
                return true;
            }
        }
    } else if (const EObject *e3 = Get<EObject>(expr.data)) {
        for (const Property &property : e3->properties) {
            if (property.kind == PropertyKind::kSpread || exprHasObjectRest(property.value_or_nil)) {
                return true;
            }
        }
    }
    return false;
}


bool couldPotentiallyThrow(const E &data) {
    if (Get<ENull>(data) != nullptr || Get<EUndefined>(data) != nullptr ||
        Get<EBoolean>(data) != nullptr || Get<ENumber>(data) != nullptr ||
        Get<EBigInt>(data) != nullptr || Get<EString>(data) != nullptr ||
        Get<EFunction>(data) != nullptr || Get<EArrow>(data) != nullptr) {
        return false;
    }
    return true;
}

} // namespace


bool Parser::isStrictModeOutputFormat() {
    return this->options.optionsThatSupportStructuralEquality.outputFormat == config::Format::kESModule;
}

void Parser::lowerFunction(
    bool *isAsync,
    bool *isGenerator,
    std::vector<Arg> *args,
    logger::Loc bodyLoc,
    SBlock *bodyBlock,
    bool *preferExpr,
    bool *hasRestArg,
    bool isArrow) {

    // Lower object rest binding patterns in function arguments
    if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kObjectRestSpread)) {
        std::vector<Stmt> prefixStmts;

        // Lower each argument individually instead of lowering all arguments
        // together. There is a correctness tradeoff here around default values
        // for function arguments, with no right answer.
        //
        // Lowering all arguments together will preserve the order of side effects
        // for default values, but will mess up their scope:
        //
        //   // Side effect order: a(), b(), c()
        //   function foo([{[a()]: w, ...x}, y = b()], z = c()) {}
        //
        //   // Side effect order is correct but scope is wrong
        //   function foo(_a, _b) {
        //     var [[{[a()]: w, ...x}, y = b()], z = c()] = [_a, _b]
        //   }
        //
        // Lowering each argument individually will preserve the scope for default
        // values that don't contain object rest binding patterns, but will mess up
        // the side effect order:
        //
        //   // Side effect order: a(), b(), c()
        //   function foo([{[a()]: w, ...x}, y = b()], z = c()) {}
        //
        //   // Side effect order is wrong but scope for c() is correct
        //   function foo(_a, z = c()) {
        //     var [{[a()]: w, ...x}, y = b()] = _a
        //   }
        //
        // This transform chooses to lower each argument individually with the
        // thinking that perhaps scope matters more in real-world code than side
        // effect order.
        for (size_t i = 0; i < args->size(); i++) {
            Arg &arg = (*args)[i];
            if (bindingHasObjectRest(arg.binding)) {
                compiler::Ref ref = this->generateTempRef(tempRefNoDeclare, "");
                Expr target = ConvertBindingToExpr(arg.binding, std::function<Expr(logger::Loc, compiler::Ref)>());
                Expr init{std::make_shared<EIdentifier>(EIdentifier{ref}), arg.binding.loc};
                this->recordUsage(ref);

                std::vector<Decl> decls;
                bool ok = false;
                std::tie(decls, ok) = this->lowerObjectRestToDecls(target, init, std::vector<Decl>());
                if (ok) {
                    // Replace the binding but leave the default value intact
                    arg.binding.data = std::make_shared<BIdentifier>(BIdentifier{ref});

                    // Append a variable declaration to the function body
                    SLocal local;
                    local.kind = LocalKind::kVar;
                    local.decls = decls;
                    prefixStmts.push_back(Stmt{std::make_shared<SLocal>(local), arg.binding.loc});
                }
            }
        }

        if (prefixStmts.size() > 0) {
            bodyBlock->stmts.insert(bodyBlock->stmts.begin(), prefixStmts.begin(), prefixStmts.end());
        }
    }

    // Lower async functions and async generator functions
    if (*isAsync && (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncAwait) ||
                     (isGenerator != nullptr && *isGenerator && compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncGenerator)))) {
        // Use the shortened form if we're an arrow function
        if (preferExpr != nullptr) {
            *preferExpr = true;
        }

        // Determine the value for "this"
        Expr thisValue;
        bool hasThisValue = false;
        std::tie(thisValue, hasThisValue) = this->valueForThis(bodyLoc, false, AssignTarget::kNone, false, false);

        if (isArrow && !this->fnOnlyDataVisit.hasThisUsage) {
            thisValue = Expr{kENullShared, bodyLoc};
        } else if (!hasThisValue) {
            thisValue = Expr{kEThisShared, bodyLoc};
        }

        // Move the code into a nested generator function
        Fn fn;
        fn.is_generator = true;
        fn.body = FnBody{*bodyBlock, bodyLoc};
        bodyBlock->stmts.clear();

        // Errors thrown during argument evaluation must reject the
        // resulting promise, which needs more complex code to handle
        bool couldThrowErrors = false;
        for (const Arg &arg : *args) {
            if (Get<BIdentifier>(arg.binding.data) == nullptr ||
                (!IsNil(arg.default_or_nil.data) && couldPotentiallyThrow(arg.default_or_nil.data))) {
                couldThrowErrors = true;
                break;
            }
        }

        // Forward the arguments to the wrapper function
        bool usesArgumentsRef = !isArrow && this->fnOnlyDataVisit.argumentsRef != nullptr &&
            this->symbolUses[*this->fnOnlyDataVisit.argumentsRef].count_estimate > 0;
        Expr forwardedArgs;
        if (!couldThrowErrors && !usesArgumentsRef) {
            // Simple case: the arguments can stay on the outer function. It's
            // worth separating out the simple case because it's the common case
            // and it generates smaller code.
            forwardedArgs = Expr{kENullShared, bodyLoc};
        } else {
            // If code uses "arguments" then we must move the arguments to the inner
            // function. This is because you can modify arguments by assigning to
            // elements in the "arguments" object:
            //
            //   async function foo(x) {
            //     arguments[0] = 1;
            //     // "x" must be 1 here
            //   }
            //

            // Complex case: the arguments must be moved to the inner function
            fn.args = *args;
            fn.has_rest_arg = *hasRestArg;
            args->clear();
            *hasRestArg = false;

            // Make sure to not change the value of the "length" property. This is
            // done by generating dummy arguments for the outer function equal to
            // the expected length of the function:
            //
            //   async function foo(a, b, c = d, ...e) {
            //   }
            //
            // This turns into:
            //
            //   function foo(_0, _1) {
            //     return __async(this, arguments, function* (a, b, c = d, ...e) {
            //     });
            //   }
            //
            // The "_0" and "_1" are dummy variables to ensure "foo.length" is 2.
            for (size_t i = 0; i < fn.args.size(); i++) {
                Arg &arg = fn.args[i];
                if (!IsNil(arg.default_or_nil.data) || (fn.has_rest_arg && i + 1 == fn.args.size())) {
                    // Arguments from here on don't add to the "length"
                    break;
                }

                // Generate a dummy variable
                compiler::Ref argRef = this->newSymbol(compiler::SymbolKind::kOther, "_" + std::to_string(i));
                this->currentScope->generated.push_back(argRef);
                Arg dummy;
                dummy.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{argRef}), arg.binding.loc};
                args->push_back(dummy);
            }

            // Forward all arguments from the outer function to the inner function
            if (!isArrow) {
                // Normal functions can just use "arguments" to forward everything
                forwardedArgs = Expr{std::make_shared<EIdentifier>(EIdentifier{*this->fnOnlyDataVisit.argumentsRef}), bodyLoc};
            } else {
                // Arrow functions can't use "arguments", so we need to forward
                // the arguments manually.
                //
                // Note that if the arrow function references "arguments" in its body
                // (even if it's inside another nested arrow function), that reference
                // to "arguments" will have to be substituted with a captured variable.
                // This is because we're changing the arrow function into a generator
                // function, which introduces a variable named "arguments". This is
                // handled separately during symbol resolution instead of being handled
                // here so we don't need to re-traverse the arrow function body.

                // If we need to forward more than the current number of arguments,
                // add a rest argument to the set of forwarding variables. This is the
                // case if the arrow function has rest or default arguments.
                if (args->size() < fn.args.size()) {
                    compiler::Ref argRef = this->newSymbol(compiler::SymbolKind::kOther, "_" + std::to_string(args->size()));
                    this->currentScope->generated.push_back(argRef);
                    Arg dummy;
                    dummy.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{argRef}), bodyLoc};
                    args->push_back(dummy);
                    *hasRestArg = true;
                }

                // Forward all of the arguments
                std::vector<Expr> items;
                items.reserve(args->size());
                for (size_t i = 0; i < args->size(); i++) {
                    const Arg &arg = (*args)[i];
                    const BIdentifier *id = Get<BIdentifier>(arg.binding.data);
                    Expr item{std::make_shared<EIdentifier>(EIdentifier{id->ref}), arg.binding.loc};
                    if (*hasRestArg && i + 1 == args->size()) {
                        item.data = std::make_shared<ESpread>(ESpread{item});
                    }
                    items.push_back(item);
                }
                EArray array;
                array.items = items;
                array.is_single_line = true;
                forwardedArgs = Expr{std::make_shared<EArray>(array), bodyLoc};
            }
        }

        std::string name;
        if (isGenerator != nullptr && *isGenerator) {
            // "async function* foo(a, b) { stmts }" => "function foo(a, b) { return __asyncGenerator(this, null, function* () { stmts }) }"
            name = "__asyncGenerator";
            *isGenerator = false;
        } else {
            // "async function foo(a, b) { stmts }" => "function foo(a, b) { return __async(this, null, function* () { stmts }) }"
            name = "__async";
        }
        *isAsync = false;
        std::vector<Expr> callAsyncArgs;
        callAsyncArgs.push_back(thisValue);
        callAsyncArgs.push_back(forwardedArgs);
        EFunction fnExpr;
        fnExpr.fn = fn;
        callAsyncArgs.push_back(Expr{std::make_shared<EFunction>(fnExpr), bodyLoc});
        Expr callAsync = this->callRuntime(bodyLoc, name, callAsyncArgs);
        SReturn ret;
        ret.value_or_nil = callAsync;
        bodyBlock->stmts = std::vector<Stmt>{Stmt{std::make_shared<SReturn>(ret), bodyLoc}};
    }
}

std::pair<Expr, exprOut> Parser::lowerOptionalChain(Expr expr, exprIn in, exprOut childOut) {
    Expr valueWhenUndefined{kEUndefinedShared, expr.loc};
    bool endsWithPropertyAccess = false;
    bool containsPrivateName = false;
    bool startsWithCall = false;
    Expr originalExpr = expr;
    std::vector<Expr> chain;
    logger::Loc loc = expr.loc;

    // Step 1: Get an array of all expressions in the chain. We're traversing the
    // chain from the outside in, so the array will be filled in "backwards".
    for (;;) {
        chain.push_back(expr);

        if (EDot *e = Get<EDot>(expr.data)) {
            expr = e->target;
            if (chain.size() == 1) {
                endsWithPropertyAccess = true;
            }
            if (e->optional_chain == OptionalChain::kStart) {
                break;
            }
        } else if (EIndex *e2 = Get<EIndex>(expr.data)) {
            expr = e2->target;
            if (chain.size() == 1) {
                endsWithPropertyAccess = true;
            }

            // If this is a private name that needs to be lowered, the entire chain
            // itself will have to be lowered even if the language target supports
            // optional chaining. This is because there's no way to use our shim
            // function for private names with optional chaining syntax.
            if (EPrivateIdentifier *private_ = Get<EPrivateIdentifier>(e2->index.data);
                private_ != nullptr && this->privateSymbolNeedsToBeLowered(private_)) {
                containsPrivateName = true;
            }

            if (e2->optional_chain == OptionalChain::kStart) {
                break;
            }
        } else if (ECall *e3 = Get<ECall>(expr.data)) {
            expr = e3->target;
            if (e3->optional_chain == OptionalChain::kStart) {
                startsWithCall = true;
                break;
            }
        } else if (EUnary *e4 = Get<EUnary>(expr.data)) { // UnOpDelete
            valueWhenUndefined = Expr{std::make_shared<EBoolean>(EBoolean{true}), loc};
            expr = e4->value;
        } else {
            break;
        }
    }

    // Stop now if we can strip the whole chain as dead code. Since the chain is
    // lazily evaluated, it's safe to just drop the code entirely.
    if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
        if (std::optional<std::pair<bool, SideEffects>> isNullOrUndefined =
                ToNullOrUndefinedWithSideEffects(expr.data);
            isNullOrUndefined && isNullOrUndefined->first) {
            if (isNullOrUndefined->second == SideEffects::kCouldHaveSideEffects) {
                return {JoinWithComma(this->astHelpers.SimplifyUnusedExpr(expr, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures), valueWhenUndefined), exprOut{}};
            }
            return {valueWhenUndefined, exprOut{}};
        }
    } else {
        if (Get<ENull>(expr.data) != nullptr || Get<EUndefined>(expr.data) != nullptr) {
            return {valueWhenUndefined, exprOut{}};
        }
    }

    // We need to lower this if this is an optional call off of a private name
    // such as "foo.#bar?.()" because the value of "this" must be captured.
    std::tuple<Expr, logger::Loc, EPrivateIdentifier *> extracted = this->extractPrivateIndex(expr);
    if (std::get<2>(extracted) != nullptr) {
        containsPrivateName = true;
    }

    // Don't lower this if we don't need to. This check must be done here instead
    // of earlier so we can do the dead code elimination above when the target is
    // null or undefined.
    if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kOptionalChain) && !containsPrivateName) {
        return {originalExpr, exprOut{}};
    }

    // Step 2: Figure out if we need to capture the value for "this" for the
    // initial ECall. This will be passed to ".call(this, ...args)" later.
    Expr thisArg;
    std::function<Expr(Expr)> targetWrapFunc;
    if (startsWithCall) {
        if (childOut.thisArgFunc) {
            // The initial value is a nested optional chain that ended in a property
            // access. The nested chain was processed first and has saved the
            // appropriate value for "this". The callback here will return a
            // reference to that saved location.
            thisArg = childOut.thisArgFunc();
        } else {
            // The initial value is a normal expression. If it's a property access,
            // strip the property off and save the target of the property access to
            // be used as the value for "this".
            if (EDot *e = Get<EDot>(expr.data)) {
                if (Get<ESuper>(e->target.data) != nullptr) {
                    // Lower "super.prop" if necessary
                    if (this->shouldLowerSuperPropertyAccess(e->target)) {
                        EString key;
                        key.value = helpers::StringToUTF16(e->name);
                        expr = this->lowerSuperPropertyGet(expr.loc, Expr{std::make_shared<EString>(key), e->name_loc});
                    }

                    // Special-case "super.foo?.()" to avoid a syntax error. Without this,
                    // we would generate:
                    //
                    //   (_b = (_a = super).foo) == null ? void 0 : _b.call(_a)
                    //
                    // which is a syntax error. Now we generate this instead:
                    //
                    //   (_a = super.foo) == null ? void 0 : _a.call(this)
                    //
                    thisArg = Expr{kEThisShared, loc};
                } else {
                    std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
                        this->captureValueWithPossibleSideEffects(loc, 2, e->target, valueDefinitelyNotMutated);
                    EDot dot;
                    dot.target = captured.first();
                    dot.name = e->name;
                    dot.name_loc = e->name_loc;
                    expr = Expr{std::make_shared<EDot>(dot), loc};
                    thisArg = captured.first();
                    targetWrapFunc = captured.second;
                }
            } else if (EIndex *e2 = Get<EIndex>(expr.data)) {
                if (Get<ESuper>(e2->target.data) != nullptr) {
                    // Lower "super[prop]" if necessary
                    if (this->shouldLowerSuperPropertyAccess(e2->target)) {
                        expr = this->lowerSuperPropertyGet(expr.loc, e2->index);
                    }

                    // See the comment above about a similar special case for EDot
                    thisArg = Expr{kEThisShared, loc};
                } else {
                    std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
                        this->captureValueWithPossibleSideEffects(loc, 2, e2->target, valueDefinitelyNotMutated);
                    targetWrapFunc = captured.second;

                    // Capture the value of "this" if the target of the starting call
                    // expression is a private property access
                    if (EPrivateIdentifier *private_ = Get<EPrivateIdentifier>(e2->index.data);
                        private_ != nullptr && this->privateSymbolNeedsToBeLowered(private_)) {
                        // "foo().#bar?.()" must capture "foo()" for "this"
                        expr = this->lowerPrivateGet(captured.first(), e2->index.loc, private_);
                        thisArg = captured.first();
                    } else {
                        EIndex index;
                        index.target = captured.first();
                        index.index = e2->index;
                        expr = Expr{std::make_shared<EIndex>(index), loc};
                        thisArg = captured.first();
                    }
                }
            }
        }
    }

    // Step 3: Figure out if we need to capture the starting value. We don't need
    // to capture it if it doesn't have any side effects (e.g. it's just a bare
    // identifier). Skipping the capture reduces code size and matches the output
    // of the TypeScript compiler.
    std::pair<std::function<Expr()>, std::function<Expr(Expr)>> exprCaptured =
        this->captureValueWithPossibleSideEffects(loc, 2, expr, valueDefinitelyNotMutated);
    std::function<Expr()> exprFunc = exprCaptured.first;
    std::function<Expr(Expr)> exprWrapFunc = exprCaptured.second;
    expr = exprFunc();
    Expr result = exprFunc();

    // Step 4: Wrap the starting value by each expression in the chain. We
    // traverse the chain in reverse because we want to go from the inside out
    // and the chain was built from the outside in.
    std::function<Expr()> parentThisArgFunc;
    std::function<Expr(Expr)> parentThisArgWrapFunc;
    std::function<Expr()> privateThisFunc;
    std::function<Expr(Expr)> privateThisWrapFunc;
    for (int32_t i = static_cast<int32_t>(chain.size()) - 1; i >= 0; i--) {
        // Save a reference to the value of "this" for our parent ECall
        if (i == 0 && in.storeThisArgForParentOptionalChain && endsWithPropertyAccess) {
            std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
                this->captureValueWithPossibleSideEffects(result.loc, 2, result, valueDefinitelyNotMutated);
            parentThisArgFunc = captured.first;
            parentThisArgWrapFunc = captured.second;
            result = captured.first();
        }

        if (EDot *e = Get<EDot>(chain[static_cast<size_t>(i)].data)) {
            EDot dot;
            dot.target = result;
            dot.name = e->name;
            dot.name_loc = e->name_loc;
            result = Expr{std::make_shared<EDot>(dot), loc};
        } else if (EIndex *e2 = Get<EIndex>(chain[static_cast<size_t>(i)].data)) {
            if (EPrivateIdentifier *private_ = Get<EPrivateIdentifier>(e2->index.data);
                private_ != nullptr && this->privateSymbolNeedsToBeLowered(private_)) {
                // If this is private name property access inside a call expression and
                // the call expression is part of this chain, then the call expression
                // is going to need a copy of the property access target as the value
                // for "this" for the call. Example for this case: "foo.#bar?.()"
                if (i > 0) {
                    if (Get<ECall>(chain[static_cast<size_t>(i - 1)].data) != nullptr) {
                        std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
                            this->captureValueWithPossibleSideEffects(loc, 2, result, valueDefinitelyNotMutated);
                        privateThisFunc = captured.first;
                        privateThisWrapFunc = captured.second;
                        result = captured.first();
                    }
                }

                result = this->lowerPrivateGet(result, e2->index.loc, private_);
                continue;
            }

            EIndex index;
            index.target = result;
            index.index = e2->index;
            result = Expr{std::make_shared<EIndex>(index), loc};
        } else if (ECall *e3 = Get<ECall>(chain[static_cast<size_t>(i)].data)) {
            // If this is the initial ECall in the chain and it's being called off of
            // a property access, invoke the function using ".call(this, ...args)" to
            // explicitly provide the value for "this".
            if (i == static_cast<int32_t>(chain.size()) - 1 && !IsNil(thisArg.data)) {
                EDot dot;
                dot.target = result;
                dot.name = "call";
                dot.name_loc = loc;
                ECall call;
                call.target = Expr{std::make_shared<EDot>(dot), loc};
                call.args.push_back(thisArg);
                call.args.insert(call.args.end(), e3->args.begin(), e3->args.end());
                call.can_be_unwrapped_if_unused = e3->can_be_unwrapped_if_unused;
                call.is_multi_line = e3->is_multi_line;
                call.kind = CallKind::kTargetWasOriginallyPropertyAccess;
                result = Expr{std::make_shared<ECall>(call), loc};
                continue;
            }

            // If the target of this call expression is a private name property
            // access that's also part of this chain, then we must use the copy of
            // the property access target that was stashed away earlier as the value
            // for "this" for the call. Example for this case: "foo.#bar?.()"
            if (privateThisFunc) {
                EDot dot;
                dot.target = result;
                dot.name = "call";
                dot.name_loc = loc;
                ECall call;
                call.target = Expr{std::make_shared<EDot>(dot), loc};
                call.args.push_back(privateThisFunc());
                call.args.insert(call.args.end(), e3->args.begin(), e3->args.end());
                call.can_be_unwrapped_if_unused = e3->can_be_unwrapped_if_unused;
                call.is_multi_line = e3->is_multi_line;
                call.kind = CallKind::kTargetWasOriginallyPropertyAccess;
                result = privateThisWrapFunc(Expr{std::make_shared<ECall>(call), loc});
                privateThisFunc = nullptr;
                continue;
            }

            ECall call;
            call.target = result;
            call.args = e3->args;
            call.can_be_unwrapped_if_unused = e3->can_be_unwrapped_if_unused;
            call.is_multi_line = e3->is_multi_line;
            call.kind = e3->kind;
            result = Expr{std::make_shared<ECall>(call), loc};
        } else if (EUnary *e4 = Get<EUnary>(chain[static_cast<size_t>(i)].data)) {
            EUnary unary;
            unary.op = OpCode::kUnOpDelete;
            unary.value = result;

            // If a delete of an optional chain takes place, it behaves as if the
            // optional chain isn't there with regard to the "delete" semantics.
            unary.was_originally_delete_of_identifier_or_property_access = e4->was_originally_delete_of_identifier_or_property_access;
            result = Expr{std::make_shared<EUnary>(unary), loc};
        } else {
            // panic("Internal error")
            break;
        }
    }

    // Step 5: Wrap it all in a conditional that returns the chain or the default
    // value if the initial value is null/undefined. The default value is usually
    // "undefined" but is "true" if the chain ends in a "delete" operator.
    // "x?.y" => "x == null ? void 0 : x.y"
    // "x()?.y()" => "(_a = x()) == null ? void 0 : _a.y()"
    EBinary test;
    test.op = OpCode::kBinOpLooseEq;
    test.left = expr;
    test.right = Expr{kENullShared, loc};
    EIf if_;
    if_.test = Expr{std::make_shared<EBinary>(test), loc};
    if_.yes = valueWhenUndefined;
    if_.no = result;
    result = Expr{std::make_shared<EIf>(if_), loc};
    if (exprWrapFunc) {
        result = exprWrapFunc(result);
    }
    if (targetWrapFunc) {
        result = targetWrapFunc(result);
    }
    if (childOut.thisArgWrapFunc) {
        result = childOut.thisArgWrapFunc(result);
    }
    exprOut out;
    out.thisArgFunc = parentThisArgFunc;
    out.thisArgWrapFunc = parentThisArgWrapFunc;
    return {result, out};
}

Expr Parser::lowerParenthesizedOptionalChain(logger::Loc loc, ECall *e, exprOut childOut) {
    EDot dot;
    dot.target = e->target;
    dot.name = "call";
    dot.name_loc = loc;
    ECall call;
    call.target = Expr{std::make_shared<EDot>(dot), loc};
    call.args.push_back(childOut.thisArgFunc());
    call.args.insert(call.args.end(), e->args.begin(), e->args.end());
    call.is_multi_line = e->is_multi_line;
    call.kind = CallKind::kTargetWasOriginallyPropertyAccess;
    return childOut.thisArgWrapFunc(Expr{std::make_shared<ECall>(call), loc});
}

Expr Parser::lowerAssignmentOperator(Expr value, const std::function<Expr(Expr, Expr)> &callback) {
    if (EDot *left = Get<EDot>(value.data)) {
        if (left->optional_chain == OptionalChain::kNone) {
            std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
                this->captureValueWithPossibleSideEffects(value.loc, 2, left->target, valueDefinitelyNotMutated);
            EDot a;
            a.target = captured.first();
            a.name = left->name;
            a.name_loc = left->name_loc;
            EDot b;
            b.target = captured.first();
            b.name = left->name;
            b.name_loc = left->name_loc;
            return captured.second(callback(
                Expr{std::make_shared<EDot>(a), value.loc},
                Expr{std::make_shared<EDot>(b), value.loc}));
        }
    } else if (EIndex *left2 = Get<EIndex>(value.data)) {
        if (left2->optional_chain == OptionalChain::kNone) {
            std::pair<std::function<Expr()>, std::function<Expr(Expr)>> targetCaptured =
                this->captureValueWithPossibleSideEffects(value.loc, 2, left2->target, valueDefinitelyNotMutated);
            std::pair<std::function<Expr()>, std::function<Expr(Expr)>> indexCaptured =
                this->captureValueWithPossibleSideEffects(value.loc, 2, left2->index, valueDefinitelyNotMutated);
            EIndex a;
            a.target = targetCaptured.first();
            a.index = indexCaptured.first();
            EIndex b;
            b.target = targetCaptured.first();
            b.index = indexCaptured.first();
            return targetCaptured.second(indexCaptured.second(callback(
                Expr{std::make_shared<EIndex>(a), value.loc},
                Expr{std::make_shared<EIndex>(b), value.loc})));
        }
    } else if (EIdentifier *left3 = Get<EIdentifier>(value.data)) {
        return callback(
            Expr{std::make_shared<EIdentifier>(EIdentifier{left3->ref}), value.loc},
            value);
    }

    // We shouldn't get here with valid syntax? Just let this through for now
    // since there's currently no assignment target validation. Garbage in,
    // garbage out.
    return value;
}

Expr Parser::lowerExponentiationAssignmentOperator(logger::Loc loc, EBinary *e) {
    std::tuple<Expr, logger::Loc, EPrivateIdentifier *> extracted = this->extractPrivateIndex(e->left);
    Expr target = std::get<0>(extracted);
    logger::Loc privateLoc = std::get<1>(extracted);
    EPrivateIdentifier *private_ = std::get<2>(extracted);
    if (private_ != nullptr) {
        // "a.#b **= c" => "__privateSet(a, #b, __pow(__privateGet(a, #b), c))"
        std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
            this->captureValueWithPossibleSideEffects(loc, 2, target, valueDefinitelyNotMutated);
        // The target of the setter is evaluated first, so give "__privateSet"
        // the temporary ("_a = target") and use the bare temporary in the get:
        // "__privateSet(_a = target, #b, __pow(__privateGet(_a, #b), c))"
        Expr setTarget = captured.first();
        std::vector<Expr> args;
        args.push_back(this->lowerPrivateGet(captured.first(), privateLoc, private_));
        args.push_back(e->right);
        return captured.second(this->lowerPrivateSet(setTarget, privateLoc, private_,
            this->callRuntime(loc, "__pow", args)));
    }

    return this->lowerAssignmentOperator(e->left, [&](Expr a, Expr b) -> Expr {
        // "a **= b" => "a = __pow(a, b)"
        std::vector<Expr> args;
        args.push_back(b);
        args.push_back(e->right);
        return Assign(a, this->callRuntime(loc, "__pow", args));
    });
}

std::pair<Expr, bool> Parser::lowerNullishCoalescingAssignmentOperator(logger::Loc loc, EBinary *e) {
    std::tuple<Expr, logger::Loc, EPrivateIdentifier *> extracted = this->extractPrivateIndex(e->left);
    Expr target = std::get<0>(extracted);
    logger::Loc privateLoc = std::get<1>(extracted);
    EPrivateIdentifier *private_ = std::get<2>(extracted);
    if (private_ != nullptr) {
        std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
            this->captureValueWithPossibleSideEffects(loc, 2, target, valueDefinitelyNotMutated);
        if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kNullishCoalescing)) {
            // "a.#b ??= c" => "(_a = __privateGet(a, #b)) != null ? _a : __privateSet(a, #b, c)"
            Expr left = this->lowerPrivateGet(captured.first(), privateLoc, private_);
            Expr right = this->lowerPrivateSet(captured.first(), privateLoc, private_, e->right);
            return {captured.second(this->lowerNullishCoalescing(loc, left, right)), true};
        }

        // "a.#b ??= c" => "__privateGet(a, #b) ?? __privateSet(a, #b, c)"
        EBinary binary;
        binary.op = OpCode::kBinOpNullishCoalescing;
        binary.left = this->lowerPrivateGet(captured.first(), privateLoc, private_);
        binary.right = this->lowerPrivateSet(captured.first(), privateLoc, private_, e->right);
        return {captured.second(Expr{std::make_shared<EBinary>(binary), loc}), true};
    }

    if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kLogicalAssignment)) {
        return {this->lowerAssignmentOperator(e->left, [&](Expr a, Expr b) -> Expr {
            if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kNullishCoalescing)) {
                // "a ??= b" => "(_a = a) != null ? _a : a = b"
                return this->lowerNullishCoalescing(loc, a, Assign(b, e->right));
            }

            // "a ??= b" => "a ?? (a = b)"
            EBinary binary;
            binary.op = OpCode::kBinOpNullishCoalescing;
            binary.left = a;
            binary.right = Assign(b, e->right);
            return Expr{std::make_shared<EBinary>(binary), loc};
        }), true};
    }

    return {Expr{}, false};
}

std::pair<Expr, bool> Parser::lowerLogicalAssignmentOperator(logger::Loc loc, EBinary *e, OpCode op) {
    std::tuple<Expr, logger::Loc, EPrivateIdentifier *> extracted = this->extractPrivateIndex(e->left);
    Expr target = std::get<0>(extracted);
    logger::Loc privateLoc = std::get<1>(extracted);
    EPrivateIdentifier *private_ = std::get<2>(extracted);
    if (private_ != nullptr) {
        // "a.#b &&= c" => "__privateGet(a, #b) && __privateSet(a, #b, c)"
        // "a.#b ||= c" => "__privateGet(a, #b) || __privateSet(a, #b, c)"
        std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
            this->captureValueWithPossibleSideEffects(loc, 2, target, valueDefinitelyNotMutated);
        EBinary binary;
        binary.op = op;
        binary.left = this->lowerPrivateGet(captured.first(), privateLoc, private_);
        binary.right = this->lowerPrivateSet(captured.first(), privateLoc, private_, e->right);
        return {captured.second(Expr{std::make_shared<EBinary>(binary), loc}), true};
    }

    if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kLogicalAssignment)) {
        return {this->lowerAssignmentOperator(e->left, [&](Expr a, Expr b) -> Expr {
            // "a &&= b" => "a && (a = b)"
            // "a ||= b" => "a || (a = b)"
            EBinary binary;
            binary.op = op;
            binary.left = a;
            binary.right = Assign(b, e->right);
            return Expr{std::make_shared<EBinary>(binary), loc};
        }), true};
    }

    return {Expr{}, false};
}

Expr Parser::lowerNullishCoalescing(logger::Loc loc, Expr left, Expr right) {
    // "x ?? y" => "x != null ? x : y"
    // "x() ?? y()" => "_a = x(), _a != null ? _a : y"
    std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
        this->captureValueWithPossibleSideEffects(loc, 2, left, valueDefinitelyNotMutated);
    EBinary test;
    test.op = OpCode::kBinOpLooseNe;
    test.left = captured.first();
    test.right = Expr{kENullShared, loc};
    EIf if_;
    if_.test = Expr{std::make_shared<EBinary>(test), loc};
    if_.yes = captured.first();
    if_.no = right;
    return captured.second(Expr{std::make_shared<EIf>(if_), loc});
}

// Lower object spread for environments that don't support them. Non-spread
// properties are grouped into object literals and then passed to the
// "__spreadValues" and "__spreadProps" functions like this:
//
//	"{a, b, ...c, d, e}" => "__spreadProps(__spreadValues(__spreadProps({a, b}, c), {d, e})"
//
// If the object literal starts with a spread, then we pass an empty object
// literal to "__spreadValues" to make sure we clone the object:
//
//	"{...a, b}" => "__spreadProps(__spreadValues({}, a), {b})"
//
// It's not immediately obvious why we don't compile everything to a single
// call to a function that takes any number of arguments, since that would be
// shorter. The reason is to preserve the order of side effects. Consider
// this code:
//
//	let a = {
//	  get x() {
//	    b = {y: 2}
//	    return 1
//	  }
//	}
//	let b = {}
//	let c = {...a, ...b}
//
// Converting the above code to "let c = __spreadFn({}, a, null, b)" means "c"
// becomes "{x: 1}" which is incorrect. Converting the above code instead to
// "let c = __spreadProps(__spreadProps({}, a), b)" means "c" becomes
// "{x: 1, y: 2}" which is correct.
Expr Parser::lowerObjectSpread(logger::Loc loc, EObject *e) {
    bool needsLowering = false;

    if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kObjectRestSpread)) {
        for (const Property &property : e->properties) {
            if (property.kind == PropertyKind::kSpread) {
                needsLowering = true;
                break;
            }
        }
    }

    if (!needsLowering) {
        return Expr{std::make_shared<EObject>(EObject{*e}), loc};
    }

    Expr result;
    std::vector<Property> properties;

    for (const Property &property : e->properties) {
        if (property.kind != PropertyKind::kSpread) {
            properties.push_back(property);
            continue;
        }

        if (properties.size() > 0 || IsNil(result.data)) {
            if (IsNil(result.data)) {
                // "{a, ...b}" => "__spreadValues({a}, b)"
                EObject object;
                object.properties = properties;
                object.is_single_line = e->is_single_line;
                result = Expr{std::make_shared<EObject>(object), loc};
            } else {
                // "{...a, b, ...c}" => "__spreadValues(__spreadProps(__spreadValues({}, a), {b}), c)"
                EObject object;
                object.properties = properties;
                object.is_single_line = e->is_single_line;
                std::vector<Expr> args;
                args.push_back(result);
                args.push_back(Expr{std::make_shared<EObject>(object), loc});
                result = this->callRuntime(loc, "__spreadProps", args);
            }
            properties.clear();
        }

        // "{a, ...b}" => "__spreadValues({a}, b)"
        std::vector<Expr> args;
        args.push_back(result);
        args.push_back(property.value_or_nil);
        result = this->callRuntime(loc, "__spreadValues", args);
    }

    if (properties.size() > 0) {
        // "{...a, b}" => "__spreadProps(__spreadValues({}, a), {b})"
        EObject object;
        object.properties = properties;
        object.is_single_line = e->is_single_line;
        object.close_brace_loc = e->close_brace_loc;
        std::vector<Expr> args;
        args.push_back(result);
        args.push_back(Expr{std::make_shared<EObject>(object), loc});
        result = this->callRuntime(loc, "__spreadProps", args);
    }

    return result;
}

std::pair<Expr, exprOut> Parser::maybeLowerAwait(logger::Loc loc, EAwait *e) {
    // "await x" turns into "yield __await(x)" when lowering async generator functions
    if (this->fnOrArrowDataVisit.isGenerator &&
        (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncAwait) ||
            compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncGenerator))) {
        std::vector<Expr> args;
        args.push_back(e->value);
        ENew new_;
        new_.target = this->importFromRuntime(loc, "__await");
        new_.args = args;
        EYield yield_;
        yield_.value_or_nil = Expr{std::make_shared<ENew>(new_), loc};
        return {Expr{std::make_shared<EYield>(yield_), loc}, exprOut{}};
    }

    // "await x" turns into "yield x" when lowering async functions
    if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncAwait)) {
        EYield yield_;
        yield_.value_or_nil = e->value;
        return {Expr{std::make_shared<EYield>(yield_), loc}, exprOut{}};
    }

    return {Expr{std::make_shared<EAwait>(EAwait{*e}), loc}, exprOut{}};
}

std::vector<Stmt> Parser::lowerForAwaitLoop(logger::Loc loc, SForOf *loop, std::vector<Stmt> stmts) {
    // This code:
    //
    //   for await (let x of y) z()
    //
    // is transformed into the following code:
    //
    //   try {
    //     for (var iter = __forAwait(y), more, temp, error; more = !(temp = await iter.next()).done; more = false) {
    //       let x = temp.value;
    //       z();
    //     }
    //   } catch (temp) {
    //     error = [temp]
    //   } finally {
    //     try {
    //       more && (temp = iter.return) && (await temp.call(iter))
    //     } finally {
    //       if (error) throw error[0]
    //     }
    //   }
    //
    // except that "yield" is used instead of "await" if await is unsupported.
    // This mostly follows TypeScript's implementation of the syntax transform.

    compiler::Ref iterRef = this->generateTempRef(tempRefNoDeclare, "iter");
    compiler::Ref moreRef = this->generateTempRef(tempRefNoDeclare, "more");
    compiler::Ref tempRef = this->generateTempRef(tempRefNoDeclare, "temp");
    compiler::Ref errorRef = this->generateTempRef(tempRefNoDeclare, "error");

    this->recordUsage(iterRef);
    this->recordUsage(moreRef);
    this->recordUsage(tempRef);
    this->recordUsage(errorRef);

    Stmt *initStmt = loop->init.get();
    if (SLocal *init = Get<SLocal>(initStmt->data)) {
        if (init->decls.size() == 1) {
            EDot dot;
            dot.target = Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), loc};
            dot.name_loc = loc;
            dot.name = "value";
            init->decls[0].value_or_nil = Expr{std::make_shared<EDot>(dot), loc};
        }
    } else if (SExpr *initExpr = Get<SExpr>(initStmt->data)) {
        EBinary binary;
        binary.op = OpCode::kBinOpAssign;
        binary.left = initExpr->value;
        EDot dot;
        dot.target = Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), loc};
        dot.name_loc = loc;
        dot.name = "value";
        binary.right = Expr{std::make_shared<EDot>(dot), loc};
        initExpr->value.data = std::make_shared<EBinary>(binary);
    }

    std::vector<Stmt> body;
    logger::Loc closeBraceLoc;
    body.push_back(*loop->init);

    if (SBlock *block = Get<SBlock>(loop->body.data)) {
        body.insert(body.end(), block->stmts.begin(), block->stmts.end());
        closeBraceLoc = block->close_brace_loc;
    } else {
        body.push_back(loop->body);
    }

    Expr awaitIterNext;
    {
        EDot dot;
        dot.target = Expr{std::make_shared<EIdentifier>(EIdentifier{iterRef}), loc};
        dot.name_loc = loc;
        dot.name = "next";
        ECall call;
        call.target = Expr{std::make_shared<EDot>(dot), loc};
        call.kind = CallKind::kTargetWasOriginallyPropertyAccess;
        awaitIterNext = Expr{std::make_shared<ECall>(call), loc};
    }
    Expr awaitTempCallIter;
    {
        EDot dot;
        dot.target = Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), loc};
        dot.name_loc = loc;
        dot.name = "call";
        ECall call;
        call.target = Expr{std::make_shared<EDot>(dot), loc};
        call.args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{iterRef}), loc});
        call.kind = CallKind::kTargetWasOriginallyPropertyAccess;
        awaitTempCallIter = Expr{std::make_shared<ECall>(call), loc};
    }

    // "await" expressions turn into "yield" expressions when lowering
    EAwait awaitNextNode;
    awaitNextNode.value = awaitIterNext;
    awaitIterNext = this->maybeLowerAwait(awaitIterNext.loc, &awaitNextNode).first;
    EAwait awaitCallNode;
    awaitCallNode.value = awaitTempCallIter;
    awaitTempCallIter = this->maybeLowerAwait(awaitTempCallIter.loc, &awaitCallNode).first;

    SFor sfor;
    sfor.is_lowered_for_await = true;

    // "for (var iter = __forAwait(y), more, temp, error; ...)"
    {
        SLocal local;
        local.kind = LocalKind::kVar;
        Decl d0;
        d0.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{iterRef}), loc};
        std::vector<Expr> forAwaitArgs;
        forAwaitArgs.push_back(loop->value);
        d0.value_or_nil = this->callRuntime(loc, "__forAwait", forAwaitArgs);
        Decl d1;
        d1.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{moreRef}), loc};
        Decl d2;
        d2.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{tempRef}), loc};
        Decl d3;
        d3.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{errorRef}), loc};
        local.decls = {d0, d1, d2, d3};
        sfor.init_or_nil = std::make_shared<Stmt>(Stmt{std::make_shared<SLocal>(local), loc});
    }

    // "; more = !(temp = await iter.next()).done;"
    {
        EBinary tempAssign;
        tempAssign.op = OpCode::kBinOpAssign;
        tempAssign.left = Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), loc};
        tempAssign.right = awaitIterNext;
        EDot doneDot;
        doneDot.target = Expr{std::make_shared<EBinary>(tempAssign), loc};
        doneDot.name_loc = loc;
        doneDot.name = "done";
        EUnary unaryNot;
        unaryNot.op = OpCode::kUnOpNot;
        unaryNot.value = Expr{std::make_shared<EDot>(doneDot), loc};
        EBinary test;
        test.op = OpCode::kBinOpAssign;
        test.left = Expr{std::make_shared<EIdentifier>(EIdentifier{moreRef}), loc};
        test.right = Expr{std::make_shared<EUnary>(unaryNot), loc};
        sfor.test_or_nil = Expr{std::make_shared<EBinary>(test), loc};
    }

    // "; more = false"
    {
        EBinary update;
        update.op = OpCode::kBinOpAssign;
        update.left = Expr{std::make_shared<EIdentifier>(EIdentifier{moreRef}), loc};
        update.right = Expr{std::make_shared<EBoolean>(EBoolean{false}), loc};
        sfor.update_or_nil = Expr{std::make_shared<EBinary>(update), loc};
    }

    // Body
    {
        SBlock block;
        block.stmts = body;
        block.close_brace_loc = closeBraceLoc;
        sfor.body = Stmt{std::make_shared<SBlock>(block), loop->body.loc};
    }

    // "try { for (...) { ... } } catch (temp) { error = [temp] } finally { ... }"
    STry try_;
    try_.block_loc = loc;
    {
        SBlock block;
        block.stmts.push_back(Stmt{std::make_shared<SFor>(sfor), loc});
        try_.block = block;
    }

    // Catch block: "catch (temp) { error = [temp] }"
    {
        Catch catch_;
        catch_.loc = loc;
        catch_.block_loc = loc;
        catch_.binding_or_nil = Binding{std::make_shared<BIdentifier>(BIdentifier{tempRef}), loc};
        EBinary errorAssign;
        errorAssign.op = OpCode::kBinOpAssign;
        errorAssign.left = Expr{std::make_shared<EIdentifier>(EIdentifier{errorRef}), loc};
        EArray array;
        array.items.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), loc});
        array.is_single_line = true;
        errorAssign.right = Expr{std::make_shared<EArray>(array), loc};
        SExpr sexpr;
        sexpr.value = Expr{std::make_shared<EBinary>(errorAssign), loc};
        SBlock block;
        block.stmts.push_back(Stmt{std::make_shared<SExpr>(sexpr), loc});
        catch_.block = block;
        try_.catch_block = std::make_shared<Catch>(catch_);
    }

    // Finally block: "try { more && (temp = iter.return) && (await temp.call(iter)) } finally { if (error) throw error[0] }"
    {
        Finally outerFinally;
        outerFinally.loc = loc;

        STry innerTry;
        innerTry.block_loc = loc;
        {
            // "more && (temp = iter.return) && (await temp.call(iter))"
            EBinary returnAssign;
            returnAssign.op = OpCode::kBinOpAssign;
            returnAssign.left = Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), loc};
            EDot returnDot;
            returnDot.target = Expr{std::make_shared<EIdentifier>(EIdentifier{iterRef}), loc};
            returnDot.name_loc = loc;
            returnDot.name = "return";
            returnAssign.right = Expr{std::make_shared<EDot>(returnDot), loc};
            EBinary moreAndAssign;
            moreAndAssign.op = OpCode::kBinOpLogicalAnd;
            moreAndAssign.left = Expr{std::make_shared<EIdentifier>(EIdentifier{moreRef}), loc};
            moreAndAssign.right = Expr{std::make_shared<EBinary>(returnAssign), loc};
            EBinary whole;
            whole.op = OpCode::kBinOpLogicalAnd;
            whole.left = Expr{std::make_shared<EBinary>(moreAndAssign), loc};
            whole.right = awaitTempCallIter;
            SExpr sexpr;
            sexpr.value = Expr{std::make_shared<EBinary>(whole), loc};
            SBlock block;
            block.stmts.push_back(Stmt{std::make_shared<SExpr>(sexpr), loc});
            innerTry.block = block;
        }
        {
            // "if (error) throw error[0]"
            Finally innerFinally;
            innerFinally.loc = loc;
            SIf if_;
            if_.test = Expr{std::make_shared<EIdentifier>(EIdentifier{errorRef}), loc};
            EIndex errorIndex;
            errorIndex.target = Expr{std::make_shared<EIdentifier>(EIdentifier{errorRef}), loc};
            errorIndex.index = Expr{std::make_shared<ENumber>(ENumber{0}), loc};
            SThrow throw_;
            throw_.value = Expr{std::make_shared<EIndex>(errorIndex), loc};
            if_.yes = Stmt{std::make_shared<SThrow>(throw_), loc};
            SBlock block;
            block.stmts.push_back(Stmt{std::make_shared<SIf>(if_), loc});
            innerFinally.block = block;
            innerTry.finally_block = std::make_shared<Finally>(innerFinally);
        }

        SBlock block;
        block.stmts.push_back(Stmt{std::make_shared<STry>(innerTry), loc});
        outerFinally.block = block;
        try_.finally_block = std::make_shared<Finally>(outerFinally);
    }

    stmts.push_back(Stmt{std::make_shared<STry>(try_), loc});
    return stmts;
}

std::vector<Decl> Parser::lowerObjectRestInDecls(std::vector<Decl> decls) {
    if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kObjectRestSpread)) {
        return decls;
    }

    // Don't do any allocations if there are no object rest patterns. We want as
    // little overhead as possible in the common case.
    for (size_t i = 0; i < decls.size(); i++) {
        const Decl &decl = decls[i];
        if (!IsNil(decl.value_or_nil.data) && bindingHasObjectRest(decl.binding)) {
            std::vector<Decl> clone(decls.begin(), decls.begin() + static_cast<std::ptrdiff_t>(i));
            for (size_t j = i; j < decls.size(); j++) {
                const Decl &d = decls[j];
                if (!IsNil(d.value_or_nil.data)) {
                    Expr target = ConvertBindingToExpr(d.binding, std::function<Expr(logger::Loc, compiler::Ref)>());
                    std::vector<Decl> result;
                    bool ok = false;
                    std::tie(result, ok) = this->lowerObjectRestToDecls(target, d.value_or_nil, clone);
                    if (ok) {
                        clone = result;
                        continue;
                    }
                }
                clone.push_back(d);
            }

            return clone;
        }
    }

    return decls;
}

void Parser::lowerObjectRestInForLoopInit(Stmt init, Stmt *body) {
    if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kObjectRestSpread)) {
        return;
    }

    Stmt bodyPrefixStmt;

    if (SExpr *s = Get<SExpr>(init.data)) {
        // "for ({...x} in y) {}"
        // "for ({...x} of y) {}"
        if (exprHasObjectRest(s->value)) {
            compiler::Ref ref = this->generateTempRef(tempRefNeedsDeclare, "");
            Expr resultExpr;
            bool ok = false;
            std::tie(resultExpr, ok) = this->lowerAssign(s->value, Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), init.loc}, objRestReturnValueIsUnused);
            if (ok) {
                this->recordUsage(ref);
                s->value.data = std::make_shared<EIdentifier>(EIdentifier{ref});
                SExpr sexpr;
                sexpr.value = resultExpr;
                bodyPrefixStmt = Stmt{std::make_shared<SExpr>(sexpr), resultExpr.loc};
            }
        }
    } else if (SLocal *s2 = Get<SLocal>(init.data)) {
        // "for (let {...x} in y) {}"
        // "for (let {...x} of y) {}"
        if (s2->decls.size() == 1 && bindingHasObjectRest(s2->decls[0].binding)) {
            compiler::Ref ref = this->generateTempRef(tempRefNoDeclare, "");
            Decl decl;
            decl.binding = s2->decls[0].binding;
            decl.value_or_nil = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), init.loc};
            this->recordUsage(ref);
            std::vector<Decl> decls = this->lowerObjectRestInDecls(std::vector<Decl>{decl});
            s2->decls[0].binding.data = std::make_shared<BIdentifier>(BIdentifier{ref});
            SLocal local;
            local.kind = s2->kind;
            local.decls = decls;
            bodyPrefixStmt = Stmt{std::make_shared<SLocal>(local), init.loc};
        }
    }

    if (!IsNil(bodyPrefixStmt.data)) {
        if (SBlock *block = Get<SBlock>(body->data)) {
            // If there's already a block, insert at the front
            std::vector<Stmt> stmts;
            stmts.reserve(1 + block->stmts.size());
            stmts.push_back(bodyPrefixStmt);
            stmts.insert(stmts.end(), block->stmts.begin(), block->stmts.end());
            block->stmts = stmts;
        } else {
            // Otherwise, make a block and insert at the front
            SBlock blockValue;
            blockValue.stmts.push_back(bodyPrefixStmt);
            blockValue.stmts.push_back(*body);
            body->data = std::make_shared<SBlock>(blockValue);
        }
    }
}

void Parser::lowerObjectRestInCatchBinding(Catch *catch_) {
    if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kObjectRestSpread)) {
        return;
    }

    if (!IsNil(catch_->binding_or_nil.data) && bindingHasObjectRest(catch_->binding_or_nil)) {
        compiler::Ref ref = this->generateTempRef(tempRefNoDeclare, "");
        Decl decl;
        decl.binding = catch_->binding_or_nil;
        decl.value_or_nil = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), catch_->binding_or_nil.loc};
        this->recordUsage(ref);
        std::vector<Decl> decls = this->lowerObjectRestInDecls(std::vector<Decl>{decl});
        catch_->binding_or_nil.data = std::make_shared<BIdentifier>(BIdentifier{ref});
        std::vector<Stmt> stmts;
        stmts.reserve(1 + catch_->block.stmts.size());
        SLocal local;
        local.kind = LocalKind::kLet;
        local.decls = decls;
        stmts.push_back(Stmt{std::make_shared<SLocal>(local), catch_->binding_or_nil.loc});
        stmts.insert(stmts.end(), catch_->block.stmts.begin(), catch_->block.stmts.end());
        catch_->block.stmts = stmts;
    }
}

std::pair<Expr, bool> Parser::lowerAssign(Expr rootExpr, Expr rootInit, objRestMode mode) {
    std::pair<Expr, bool> result = this->lowerSuperPropertyOrPrivateInAssign(rootExpr);
    rootExpr = result.first;
    bool didLower = result.second;

    Expr expr;
    std::function<void(Expr, Expr)> assign = [&expr](Expr left, Expr right) {
        expr = JoinWithComma(expr, Assign(left, right));
    };

    std::function<Expr(Expr)> initWrapFunc;
    bool ok = false;
    std::tie(initWrapFunc, ok) = this->lowerObjectRestHelper(rootExpr, rootInit, assign, tempRefNeedsDeclare, mode);
    if (ok) {
        if (initWrapFunc) {
            expr = initWrapFunc(expr);
        }
        return {expr, true};
    }

    if (didLower) {
        return {Assign(rootExpr, rootInit), true};
    }

    return {Expr{}, false};
}

std::pair<std::vector<Decl>, bool> Parser::lowerObjectRestToDecls(Expr rootExpr, Expr rootInit, std::vector<Decl> decls) {
    std::function<void(Expr, Expr)> assign = [this, &decls](Expr left, Expr right) {
        std::pair<Binding, invalidLog> result = this->convertExprToBinding(left, invalidLog{});
        if (result.second.invalidTokens.size() > 0) {
            // panic("Internal error")
        }
        decls.push_back(Decl{result.first, right});
    };

    std::function<Expr(Expr)> wrapFunc;
    bool ok = false;
    std::tie(wrapFunc, ok) = this->lowerObjectRestHelper(rootExpr, rootInit, assign, tempRefNoDeclare, objRestReturnValueIsUnused);
    if (ok) {
        return {decls, true};
    }

    return {std::vector<Decl>(), false};
}

std::pair<std::function<Expr(Expr)>, bool> Parser::lowerObjectRestHelper(
    Expr rootExpr,
    Expr rootInit,
    const std::function<void(Expr, Expr)> &assign,
    generateTempRefArg declare,
    objRestMode mode) {
    if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kObjectRestSpread)) {
        return {std::function<Expr(Expr)>(), false};
    }

    // Check if this could possibly contain an object rest binding
    if (Get<EArray>(rootExpr.data) == nullptr && Get<EObject>(rootExpr.data) == nullptr) {
        return {std::function<Expr(Expr)>(), false};
    }

    // Scan for object rest bindings and initialize rest binding containment
    std::unordered_map<const void *, bool> containsRestBinding;
    std::function<bool(Expr)> findRestBindings;
    findRestBindings = [&](Expr expr) -> bool {
        bool found = false;
        if (const EBinary *e = Get<EBinary>(expr.data)) {
            if (e->op == OpCode::kBinOpAssign && findRestBindings(e->left)) {
                found = true;
            }
        } else if (const EArray *e2 = Get<EArray>(expr.data)) {
            for (const Expr &item : e2->items) {
                if (findRestBindings(item)) {
                    found = true;
                }
            }
        } else if (const EObject *e3 = Get<EObject>(expr.data)) {
            for (const Property &property : e3->properties) {
                if (property.kind == PropertyKind::kSpread || findRestBindings(property.value_or_nil)) {
                    found = true;
                }
            }
        }
        if (found) {
            containsRestBinding[EDataPtr(expr.data)] = true;
        }
        return found;
    };
    findRestBindings(rootExpr);
    if (containsRestBinding.size() == 0) {
        return {std::function<Expr(Expr)>(), false};
    }

    // If there is at least one rest binding, lower the whole expression
    std::function<void(Expr, Expr, std::vector<std::function<Expr()>>)> visit;

    std::function<compiler::Ref(Expr)> captureIntoRef = [this, &assign, declare](Expr expr) -> compiler::Ref {
        compiler::Ref ref = this->generateTempRef(declare, "");
        assign(Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), expr.loc}, expr);
        this->recordUsage(ref);
        return ref;
    };

    std::function<void(std::vector<Property>, Expr, Expr, std::vector<std::function<Expr()>>, bool)> lowerObjectRestPattern;
    lowerObjectRestPattern = [this, &assign, &captureIntoRef](std::vector<Property> before, Expr binding, Expr init, std::vector<std::function<Expr()>> capturedKeys, bool isSingleLine) {
        // If there are properties before this one, store the initializer in a
        // temporary so we can reference it multiple times, then create a new
        // destructuring assignment for these properties
        if (before.size() > 0) {
            // "let {a, ...b} = c"
            compiler::Ref ref = captureIntoRef(init);
            EObject object;
            object.properties = before;
            object.is_single_line = isSingleLine;
            assign(Expr{std::make_shared<EObject>(object), before[0].key.loc},
                Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), init.loc});
            init = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), init.loc};
            this->recordUsage(ref);
            this->recordUsage(ref);
        }

        // Call "__objRest" to clone the initializer without the keys for previous
        // properties, then assign the result to the binding for the rest pattern
        std::vector<Expr> keysToExclude;
        keysToExclude.reserve(capturedKeys.size());
        for (std::function<Expr()> &capturedKey : capturedKeys) {
            keysToExclude.push_back(capturedKey());
        }
        EArray array;
        array.items = keysToExclude;
        array.is_single_line = isSingleLine;
        std::vector<Expr> args;
        args.push_back(init);
        args.push_back(Expr{std::make_shared<EArray>(array), binding.loc});
        assign(binding, this->callRuntime(binding.loc, "__objRest", args));
    };

    std::function<void(std::vector<Expr>, Expr, std::vector<Expr>, Expr, bool)> splitArrayPattern;
    splitArrayPattern = [this, &assign, &visit, declare](std::vector<Expr> before, Expr split, std::vector<Expr> after, Expr init, bool isSingleLine) {
        // If this has a default value, skip the value to target the binding
        Expr *binding = &split;
        if (EBinary *binary = Get<EBinary>(split.data); binary != nullptr && binary->op == OpCode::kBinOpAssign) {
            binding = &binary->left;
        }

        // Swap the binding with a temporary
        compiler::Ref splitRef = this->generateTempRef(declare, "");
        Expr deferredBinding = *binding;
        binding->data = std::make_shared<EIdentifier>(EIdentifier{splitRef});
        std::vector<Expr> items = before;
        items.push_back(split);

        // If there are any items left over, defer them until later too
        Expr tailExpr;
        Expr tailInit;
        if (after.size() > 0) {
            compiler::Ref tailRef = this->generateTempRef(declare, "");
            logger::Loc tailLoc = after[0].loc;
            EArray tailArray;
            tailArray.items = after;
            tailArray.is_single_line = isSingleLine;
            tailExpr = Expr{std::make_shared<EArray>(tailArray), tailLoc};
            tailInit = Expr{std::make_shared<EIdentifier>(EIdentifier{tailRef}), tailLoc};
            ESpread spread;
            spread.value = Expr{std::make_shared<EIdentifier>(EIdentifier{tailRef}), tailLoc};
            items.push_back(Expr{std::make_shared<ESpread>(spread), tailLoc});
            this->recordUsage(tailRef);
            this->recordUsage(tailRef);
        }

        // The original destructuring assignment must come first
        EArray array;
        array.items = items;
        array.is_single_line = isSingleLine;
        assign(Expr{std::make_shared<EArray>(array), split.loc}, init);

        // Then the deferred split is evaluated
        visit(deferredBinding, Expr{std::make_shared<EIdentifier>(EIdentifier{splitRef}), split.loc}, std::vector<std::function<Expr()>>());
        this->recordUsage(splitRef);

        // Then anything after the split
        if (after.size() > 0) {
            visit(tailExpr, tailInit, std::vector<std::function<Expr()>>());
        }
    };

    std::function<void(std::vector<Property>, std::vector<Property>, Expr, std::vector<std::function<Expr()>>, bool)> splitObjectPattern;
    splitObjectPattern = [this, &assign, &visit, &captureIntoRef, declare](std::vector<Property> upToSplit, std::vector<Property> afterSplit, Expr init, std::vector<std::function<Expr()>> capturedKeys, bool isSingleLine) {
        // If there are properties after the split, store the initializer in a
        // temporary so we can reference it multiple times
        Expr afterSplitInit;
        if (afterSplit.size() > 0) {
            compiler::Ref ref = captureIntoRef(init);
            init = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), init.loc};
            afterSplitInit = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), init.loc};
        }

        Property &split = upToSplit[upToSplit.size() - 1];
        Expr *binding = &split.value_or_nil;

        // Swap the binding with a temporary
        compiler::Ref splitRef = this->generateTempRef(declare, "");
        Expr deferredBinding = *binding;
        binding->data = std::make_shared<EIdentifier>(EIdentifier{splitRef});
        this->recordUsage(splitRef);

        // Use a destructuring assignment to unpack everything up to and including
        // the split point
        EObject object;
        object.properties = upToSplit;
        object.is_single_line = isSingleLine;
        assign(Expr{std::make_shared<EObject>(object), binding->loc}, init);

        // Handle any nested rest binding patterns inside the split point
        visit(deferredBinding, Expr{std::make_shared<EIdentifier>(EIdentifier{splitRef}), binding->loc}, std::vector<std::function<Expr()>>());
        this->recordUsage(splitRef);

        // Then continue on to any properties after the split
        if (afterSplit.size() > 0) {
            EObject afterObject;
            afterObject.properties = afterSplit;
            afterObject.is_single_line = isSingleLine;
            visit(Expr{std::make_shared<EObject>(afterObject), binding->loc}, afterSplitInit, capturedKeys);
        }
    };

    // This takes an expression representing a binding pattern as input and
    // returns that binding pattern with any object rest patterns stripped out.
    // The object rest patterns are lowered and appended to "exprChain" along
    // with any child binding patterns that came after the binding pattern
    // containing the object rest pattern.
    //
    // This transform must be very careful to preserve the exact evaluation
    // order of all assignments, default values, and computed property keys.
    //
    // Unlike the Babel and TypeScript compilers, this transform does not
    // lower binding patterns other than object rest patterns. For example,
    // array spread patterns are preserved.
    //
    // Certain patterns such as "{a: {...a}, b: {...b}, ...c}" may need to be
    // split multiple times. In this case the "capturedKeys" argument allows
    // the visitor to pass on captured keys to the tail-recursive call that
    // handles the properties after the split.
    visit = [this, &assign, &containsRestBinding, &splitArrayPattern, &splitObjectPattern, &lowerObjectRestPattern](Expr expr, Expr init, std::vector<std::function<Expr()>> capturedKeys) {
        if (EArray *e = Get<EArray>(expr.data)) {
            // Split on the first binding with a nested rest binding pattern
            for (size_t i = 0; i < e->items.size(); i++) {
                // "let [a, {...b}, c] = d"
                if (containsRestBinding.count(EDataPtr(e->items[i].data)) > 0) {
                    std::vector<Expr> before(e->items.begin(), e->items.begin() + static_cast<std::ptrdiff_t>(i));
                    std::vector<Expr> after(e->items.begin() + static_cast<std::ptrdiff_t>(i + 1), e->items.end());
                    splitArrayPattern(before, e->items[i], after, init, e->is_single_line);
                    return;
                }
            }
        } else if (EObject *e2 = Get<EObject>(expr.data)) {
            int32_t last = static_cast<int32_t>(e2->properties.size()) - 1;
            bool endsWithRestBinding = last >= 0 && e2->properties[static_cast<size_t>(last)].kind == PropertyKind::kSpread;

            // Split on the first binding with a nested rest binding pattern
            for (int32_t i = 0; i < static_cast<int32_t>(e2->properties.size()); i++) {
                Property &property = e2->properties[static_cast<size_t>(i)];

                // "let {a, ...b} = c"
                if (property.kind == PropertyKind::kSpread) {
                    std::vector<Property> before(e2->properties.begin(), e2->properties.begin() + static_cast<std::ptrdiff_t>(i));
                    lowerObjectRestPattern(before, property.value_or_nil, init, capturedKeys, e2->is_single_line);
                    return;
                }

                // Save a copy of this key so the rest binding can exclude it
                if (endsWithRestBinding) {
                    std::pair<Expr, std::function<Expr()>> captured = this->captureKeyForObjectRest(property.key);
                    property.key = captured.first;
                    capturedKeys.push_back(captured.second);
                }

                // "let {a: {...b}, c} = d"
                if (containsRestBinding.count(EDataPtr(property.value_or_nil.data)) > 0) {
                    std::vector<Property> upToSplit(e2->properties.begin(), e2->properties.begin() + static_cast<std::ptrdiff_t>(i + 1));
                    std::vector<Property> afterSplit(e2->properties.begin() + static_cast<std::ptrdiff_t>(i + 1), e2->properties.end());
                    splitObjectPattern(upToSplit, afterSplit, init, capturedKeys, e2->is_single_line);
                    return;
                }
            }
        }

        assign(expr, init);
    };

    // Capture and return the value of the initializer if this is an assignment
    // expression and the return value is used:
    //
    //   // Input:
    //   console.log({...x} = x);
    //
    //   // Output:
    //   var _a;
    //   console.log((x = __objRest(_a = x, []), _a));
    //
    // This isn't necessary if the return value is unused:
    //
    //   // Input:
    //   ({...x} = x);
    //
    //   // Output:
    //   x = __objRest(x, []);
    //
    std::function<Expr(Expr)> wrapFunc;
    if (mode == objRestMustReturnInitExpr) {
        std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
            this->captureValueWithPossibleSideEffects(rootInit.loc, 2, rootInit, valueCouldBeMutated);
        rootInit = captured.first();
        wrapFunc = [captured](Expr expr) -> Expr {
            return captured.second(JoinWithComma(expr, captured.first()));
        };
    }

    visit(rootExpr, rootInit, std::vector<std::function<Expr()>>());
    return {wrapFunc, true};
}

// Save a copy of the key for the call to "__objRest" later on. Certain
// expressions can be converted to keys more efficiently than others.
std::pair<Expr, std::function<Expr()>> Parser::captureKeyForObjectRest(Expr originalKey) {
    logger::Loc loc = originalKey.loc;
    Expr finalKey = originalKey;
    std::function<Expr()> capturedKey;

    if (const EString *k = Get<EString>(originalKey.data)) {
        capturedKey = [loc, k]() -> Expr {
            return Expr{std::make_shared<EString>(EString{k->value, logger::Loc{}}), loc};
        };
    } else if (const ENumber *k2 = Get<ENumber>(originalKey.data)) {
        // Emit it as the number plus a string (i.e. call toString() on it).
        capturedKey = [loc, k2]() -> Expr {
            EBinary binary;
            binary.op = OpCode::kBinOpAdd;
            binary.left = Expr{std::make_shared<ENumber>(ENumber{k2->value}), loc};
            binary.right = Expr{std::make_shared<EString>(EString{std::u16string(), logger::Loc{}}), loc};
            return Expr{std::make_shared<EBinary>(binary), loc};
        };
    } else if (const EIdentifier *k3 = Get<EIdentifier>(originalKey.data)) {
        capturedKey = [this, loc, k3]() -> Expr {
            this->recordUsage(k3->ref);
            std::vector<Expr> args;
            args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{k3->ref}), loc});
            return this->callRuntime(loc, "__restKey", args);
        };
    } else {
        // If it's an arbitrary expression, it probably has a side effect.
        // Stash it in a temporary reference so we don't evaluate it twice.
        compiler::Ref tempRef = this->generateTempRef(tempRefNeedsDeclare, "");
        finalKey = Assign(Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), loc}, originalKey);
        capturedKey = [this, loc, tempRef]() -> Expr {
            this->recordUsage(tempRef);
            std::vector<Expr> args;
            args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), loc});
            return this->callRuntime(loc, "__restKey", args);
        };
    }

    return {finalKey, capturedKey};
}

Expr Parser::lowerTemplateLiteral(logger::Loc loc, ETemplate *e, std::function<Expr()> tagThisFunc, std::function<Expr(Expr)> tagWrapFunc) {
    // If there is no tag, turn this into normal string concatenation
    if (IsNil(e->tag_or_nil.data)) {
        Expr value;

        // Handle the head
        EString head;
        head.value = e->head_cooked;
        head.legacy_octal_loc = e->legacy_octal_loc;
        value = Expr{std::make_shared<EString>(head), loc};

        // Handle the tail. Each one is handled with a separate call to ".concat()"
        // to handle various corner cases in the specification including:
        //
        //   * For objects, "toString" must be called instead of "valueOf"
        //   * Side effects must happen inline instead of at the end
        //   * Passing a "Symbol" instance should throw
        //
        for (const TemplatePart &part : e->parts) {
            std::vector<Expr> args;
            if (part.tail_cooked.size() > 0) {
                EString tail;
                tail.value = part.tail_cooked;
                args.push_back(part.value);
                args.push_back(Expr{std::make_shared<EString>(tail), part.tail_loc});
            } else {
                args.push_back(part.value);
            }
            EDot dot;
            dot.target = value;
            dot.name = "concat";
            dot.name_loc = part.value.loc;
            ECall call;
            call.target = Expr{std::make_shared<EDot>(dot), loc};
            call.args = args;
            call.kind = CallKind::kTargetWasOriginallyPropertyAccess;
            value = Expr{std::make_shared<ECall>(call), loc};
        }

        return value;
    }

    // Otherwise, call the tag with the template object
    bool needsRaw = false;
    std::vector<Expr> cooked;
    std::vector<Expr> raw;
    std::vector<Expr> args;
    args.reserve(1 + e->parts.size());
    args.push_back(Expr{});

    if (!e->head_cooked_is_valid) {
        cooked.push_back(Expr{kEUndefinedShared, e->head_loc});
        needsRaw = true;
    } else {
        EString cookedString;
        cookedString.value = e->head_cooked;
        cooked.push_back(Expr{std::make_shared<EString>(cookedString), e->head_loc});
        if (!helpers::UTF16EqualsString(e->head_cooked, e->head_raw)) {
            needsRaw = true;
        }
    }
    EString rawString;
    rawString.value = helpers::StringToUTF16(e->head_raw);
    raw.push_back(Expr{std::make_shared<EString>(rawString), e->head_loc});

    // Handle the tail
    for (const TemplatePart &part : e->parts) {
        args.push_back(part.value);
        if (!part.tail_cooked_is_valid) {
            cooked.push_back(Expr{kEUndefinedShared, part.tail_loc});
            needsRaw = true;
        } else {
            EString cookedString;
            cookedString.value = part.tail_cooked;
            cooked.push_back(Expr{std::make_shared<EString>(cookedString), part.tail_loc});
            if (!helpers::UTF16EqualsString(part.tail_cooked, part.tail_raw)) {
                needsRaw = true;
            }
        }
        EString rawPart;
        rawPart.value = helpers::StringToUTF16(part.tail_raw);
        raw.push_back(Expr{std::make_shared<EString>(rawPart), part.tail_loc});
    }

    // Construct the template object
    EArray cookedArray;
    cookedArray.items = cooked;
    cookedArray.is_single_line = true;
    std::vector<Expr> arrays;
    if (needsRaw) {
        EArray rawArray;
        rawArray.items = raw;
        rawArray.is_single_line = true;
        arrays.push_back(Expr{std::make_shared<EArray>(cookedArray), e->head_loc});
        arrays.push_back(Expr{std::make_shared<EArray>(rawArray), e->head_loc});
    } else {
        arrays.push_back(Expr{std::make_shared<EArray>(cookedArray), e->head_loc});
    }
    Expr templateObj = this->callRuntime(e->head_loc, "__template", arrays);

    // Cache it in a temporary object (required by the specification)
    compiler::Ref tempRef = this->generateTopLevelTempRef();
    this->recordUsage(tempRef);
    this->recordUsage(tempRef);
    EBinary cache;
    cache.op = OpCode::kBinOpLogicalOr;
    cache.left = Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), loc};
    EBinary assignCache;
    assignCache.op = OpCode::kBinOpAssign;
    assignCache.left = Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), loc};
    assignCache.right = templateObj;
    cache.right = Expr{std::make_shared<EBinary>(assignCache), loc};
    args[0] = Expr{std::make_shared<EBinary>(cache), loc};

    // If this optional chain was used as a template tag, then also forward the value for "this"
    if (tagThisFunc) {
        EDot dot;
        dot.target = e->tag_or_nil;
        dot.name = "call";
        dot.name_loc = e->head_loc;
        ECall call;
        call.target = Expr{std::make_shared<EDot>(dot), loc};
        call.args.push_back(tagThisFunc());
        call.args.insert(call.args.end(), args.begin(), args.end());
        call.kind = CallKind::kTargetWasOriginallyPropertyAccess;
        return tagWrapFunc(Expr{std::make_shared<ECall>(call), loc});
    }

    // Call the tag function
    ECall call;
    call.target = e->tag_or_nil;
    call.args = args;
    call.kind = e->tag_was_originally_property_access ? CallKind::kTargetWasOriginallyPropertyAccess : CallKind::kNormal;
    return Expr{std::make_shared<ECall>(call), loc};
}

Expr Parser::maybeLowerSetBinOp(Expr left, OpCode op, Expr right) {
    std::tuple<Expr, logger::Loc, EPrivateIdentifier *> extracted = this->extractPrivateIndex(left);
    if (std::get<2>(extracted) != nullptr) {
        return this->lowerPrivateSetBinOp(std::get<0>(extracted), std::get<1>(extracted), std::get<2>(extracted), op, right);
    }
    Expr property = this->extractSuperProperty(left);
    if (!IsNil(property.data)) {
        return this->lowerSuperPropertySetBinOp(left.loc, property, op, right);
    }
    return Expr{};
}

struct lowerUsingDeclarationContext Parser::lowerUsingDeclarationContext() {
    struct lowerUsingDeclarationContext ctx;
    ctx.stack_ref = this->newSymbol(compiler::SymbolKind::kOther, "_stack");
    return ctx;
}

void lowerUsingDeclarationContext::scanStmts(Parser *p, std::vector<Stmt> stmts) {
    for (Stmt &stmt : stmts) {
        if (SLocal *local = Get<SLocal>(stmt.data); local != nullptr && IsUsing(local->kind)) {
            // Wrap each "using" initializer in a call to the "__using" helper function
            if (this->first_using_loc.start == 0) {
                this->first_using_loc = stmt.loc;
            }
            if (local->kind == LocalKind::kAwaitUsing) {
                this->has_await_using = true;
            }
            for (size_t i = 0; i < local->decls.size(); i++) {
                Decl &decl = local->decls[i];
                if (!IsNil(decl.value_or_nil.data)) {
                    logger::Loc valueLoc = decl.value_or_nil.loc;
                    p->recordUsage(this->stack_ref);
                    std::vector<Expr> args;
                    args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{this->stack_ref}), valueLoc});
                    args.push_back(decl.value_or_nil);
                    if (local->kind == LocalKind::kAwaitUsing) {
                        args.push_back(Expr{std::make_shared<EBoolean>(EBoolean{true}), valueLoc});
                    }
                    decl.value_or_nil = p->callRuntime(valueLoc, "__using", args);
                }
            }
            if (p->willWrapModuleInTryCatchForUsing && p->currentScope->parent == nullptr) {
                local->kind = LocalKind::kVar;
            } else {
                local->kind = p->selectLocalKind(LocalKind::kConst);
            }
        }
    }
}

std::vector<Stmt> lowerUsingDeclarationContext::finalize(Parser *p, std::vector<Stmt> stmts, bool shouldHoistFunctions) {
    std::vector<Stmt> result;
    std::vector<ClauseItem> exports;
    size_t end = 0;

    // Filter out statements that can't go in a try/catch block
    for (size_t i = 0; i < stmts.size(); i++) {
        Stmt &stmt = stmts[i];
        // Note: We don't need to handle class declarations here because they
        // should have been already converted into local "var" declarations
        // before this point. It's done in "lowerClass" instead of here because
        // "lowerClass" already does this sometimes for other reasons, and it's
        // more straightforward to do it in one place because it's complicated.

        if (Get<SDirective>(stmt.data) != nullptr || Get<SImport>(stmt.data) != nullptr ||
            Get<SExportFrom>(stmt.data) != nullptr || Get<SExportStar>(stmt.data) != nullptr) {
            // These can't go in a try/catch block
            result.push_back(stmt);
            continue;
        }

        if (SExportClause *s = Get<SExportClause>(stmt.data)) {
            // Merge export clauses together
            exports.insert(exports.end(), s->items.begin(), s->items.end());
            continue;
        }

        if (Get<SFunction>(stmt.data) != nullptr) {
            if (shouldHoistFunctions) {
                // Hoist function declarations for cross-file ESM references
                result.push_back(stmt);
                continue;
            }
        }

        if (SExportDefault *s = Get<SExportDefault>(stmt.data)) {
            if (Get<SFunction>(s->value.data) != nullptr && shouldHoistFunctions) {
                // Hoist function declarations for cross-file ESM references
                result.push_back(stmt);
                continue;
            }
        }

        if (SLocal *s = Get<SLocal>(stmt.data)) {
            // If any of these are exported, turn it into a "var" and add export clauses
            if (s->is_export) {
                ForEachIdentifierBindingInDecls(s->decls, [p, &exports, s](logger::Loc loc, BIdentifier &b) {
                    ClauseItem item;
                    item.alias = p->symbols[b.ref.inner_index].original_name;
                    item.alias_loc = loc;
                    item.name = compiler::LocRef{loc, b.ref};
                    exports.push_back(item);
                    s->kind = LocalKind::kVar;
                });
                s->is_export = false;
            }
        }

        stmts[end] = stmt;
        end++;
    }
    stmts.resize(end);

    // Generate the variables we'll need
    compiler::Ref caughtRef = p->newSymbol(compiler::SymbolKind::kOther, "_");
    compiler::Ref errorRef = p->newSymbol(compiler::SymbolKind::kOther, "_error");
    compiler::Ref hasErrorRef = p->newSymbol(compiler::SymbolKind::kOther, "_hasError");

    // Generated variables are declared with "var", so hoist them up
    Scope *scope = p->currentScope;
    while (!StopsHoisting(scope->kind)) {
        scope = scope->parent;
    }
    bool isTopLevel = scope == p->moduleScope;
    scope->generated.push_back(this->stack_ref);
    scope->generated.push_back(caughtRef);
    scope->generated.push_back(errorRef);
    scope->generated.push_back(hasErrorRef);
    p->declaredSymbols.push_back(DeclaredSymbol{this->stack_ref, isTopLevel});
    p->declaredSymbols.push_back(DeclaredSymbol{caughtRef, isTopLevel});
    p->declaredSymbols.push_back(DeclaredSymbol{errorRef, isTopLevel});
    p->declaredSymbols.push_back(DeclaredSymbol{hasErrorRef, isTopLevel});

    // Call the "__callDispose" helper function at the end of the scope
    logger::Loc loc = this->first_using_loc;
    p->recordUsage(this->stack_ref);
    p->recordUsage(errorRef);
    p->recordUsage(hasErrorRef);
    std::vector<Expr> callDisposeArgs;
    callDisposeArgs.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{this->stack_ref}), loc});
    callDisposeArgs.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{errorRef}), loc});
    callDisposeArgs.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{hasErrorRef}), loc});
    Expr callDispose = p->callRuntime(loc, "__callDispose", callDisposeArgs);

    // If there was an "await using", optionally await the returned promise
    std::vector<Stmt> finallyStmts;
    if (this->has_await_using) {
        compiler::Ref promiseRef = p->generateTempRef(tempRefNoDeclare, "_promise");
        scope->generated.push_back(promiseRef);
        p->declaredSymbols.push_back(DeclaredSymbol{promiseRef, isTopLevel});

        // "await" expressions turn into "yield" expressions when lowering
        p->recordUsage(promiseRef);
        EAwait await;
        await.value = Expr{std::make_shared<EIdentifier>(EIdentifier{promiseRef}), loc};
        Expr awaitExpr = p->maybeLowerAwait(loc, &await).first;

        p->recordUsage(promiseRef);
        SLocal local;
        Decl d;
        d.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{promiseRef}), loc};
        d.value_or_nil = callDispose;
        local.decls = std::vector<Decl>{d};
        finallyStmts.push_back(Stmt{std::make_shared<SLocal>(local), loc});

        // The "await" must not happen if an error was thrown before the
        // "await using", so we conditionally await here:
        //
        //   var promise = __callDispose(stack, error, hasError);
        //   promise && await promise;
        //
        EBinary and_;
        and_.op = OpCode::kBinOpLogicalAnd;
        and_.left = Expr{std::make_shared<EIdentifier>(EIdentifier{promiseRef}), loc};
        and_.right = awaitExpr;
        SExpr sexpr;
        sexpr.value = Expr{std::make_shared<EBinary>(and_), loc};
        finallyStmts.push_back(Stmt{std::make_shared<SExpr>(sexpr), loc});
    } else {
        SExpr sexpr;
        sexpr.value = callDispose;
        finallyStmts.push_back(Stmt{std::make_shared<SExpr>(sexpr), loc});
    }

    // Wrap everything in a try/catch/finally block
    p->recordUsage(caughtRef);

    SLocal stackLocal;
    Decl stackDecl;
    stackDecl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{this->stack_ref}), loc};
    stackDecl.value_or_nil = Expr{std::make_shared<EArray>(EArray{}), loc};
    stackLocal.decls = std::vector<Decl>{stackDecl};
    result.push_back(Stmt{std::make_shared<SLocal>(stackLocal), loc});

    STry try_;
    try_.block_loc = loc;
    SBlock block;
    block.stmts = stmts;
    try_.block = block;

    Catch catch_;
    catch_.loc = loc;
    catch_.block_loc = loc;
    catch_.binding_or_nil = Binding{std::make_shared<BIdentifier>(BIdentifier{caughtRef}), loc};
    SBlock catchBlock;
    SLocal catchLocal;
    Decl e1;
    e1.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{errorRef}), loc};
    e1.value_or_nil = Expr{std::make_shared<EIdentifier>(EIdentifier{caughtRef}), loc};
    Decl e2;
    e2.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{hasErrorRef}), loc};
    e2.value_or_nil = Expr{std::make_shared<EBoolean>(EBoolean{true}), loc};
    catchLocal.decls = std::vector<Decl>{e1, e2};
    catchBlock.stmts.push_back(Stmt{std::make_shared<SLocal>(catchLocal), loc});
    catch_.block = catchBlock;
    try_.catch_block = std::make_shared<Catch>(catch_);

    Finally finally_;
    finally_.loc = loc;
    finally_.block = SBlock{finallyStmts, logger::Loc{}};
    try_.finally_block = std::make_shared<Finally>(finally_);
    result.push_back(Stmt{std::make_shared<STry>(try_), loc});

    if (exports.size() > 0) {
        SExportClause clause;
        clause.items = exports;
        result.push_back(Stmt{std::make_shared<SExportClause>(clause), loc});
    }
    return result;
}

void Parser::lowerUsingDeclarationInForOf(logger::Loc loc, SLocal *init, Stmt *body) {
    Binding binding = init->decls[0].binding;
    BIdentifier *id = Get<BIdentifier>(binding.data);
    compiler::Ref tempRef = this->generateTempRef(tempRefNoDeclare, "_" + this->symbols[id->ref.inner_index].original_name);

    SBlock *block = Get<SBlock>(body->data);
    std::shared_ptr<SBlock> newBlock;
    if (block == nullptr) {
        newBlock = std::make_shared<SBlock>();
        if (Get<SEmpty>(body->data) == nullptr) {
            newBlock->stmts.push_back(*body);
        }
        body->data = newBlock;
        block = newBlock.get();
    }

    std::vector<Stmt> blockStmts;
    blockStmts.reserve(1 + block->stmts.size());
    SLocal local;
    local.kind = init->kind;
    Decl d;
    d.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{id->ref}), binding.loc};
    d.value_or_nil = Expr{std::make_shared<EIdentifier>(EIdentifier{tempRef}), binding.loc};
    local.decls = std::vector<Decl>{d};
    blockStmts.push_back(Stmt{std::make_shared<SLocal>(local), loc});
    blockStmts.insert(blockStmts.end(), block->stmts.begin(), block->stmts.end());

    struct lowerUsingDeclarationContext ctx = this->lowerUsingDeclarationContext();
    ctx.scanStmts(this, blockStmts);
    block->stmts = ctx.finalize(this, blockStmts, this->willWrapModuleInTryCatchForUsing && this->currentScope->parent == nullptr);
    init->kind = LocalKind::kVar;
    id->ref = tempRef;
}

} // namespace guchho::javascript
