#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
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

    namespace {

        bool IsSameNodeAs(const E& a, const E& b) {
            return std::visit(
                [](const auto& pa, const auto& pb) {
                    using UA = typename std::decay_t<decltype(pa)>;
                    using UB = typename std::decay_t<decltype(pb)>;
                    if constexpr (std::is_same_v<UA, UB>) {
                        return pa != nullptr && pb != nullptr && pa.get() == pb.get();
                    }
                    return false;
                },
                a, b);
        }

        bool isUnsightlyPrimitive(const E& data) {
            return Get<EBoolean>(data) != nullptr || Get<ENull>(data) != nullptr ||
                Get<EUndefined>(data) != nullptr || Get<ENumber>(data) != nullptr ||
                Get<EBigInt>(data) != nullptr || Get<EString>(data) != nullptr;
        }

    }

    Expr Parser::instantiateDefineExpr(logger::Loc loc, config::DefineExpr expr, identifierOpts opts) {
        if (expr.HasConstant()) {
            return Expr{expr.Constant, loc};
        }

        if (expr.InjectedDefineIndex.IsValid()) {
            compiler::Ref ref = this->injectedDefineSymbols[expr.InjectedDefineIndex.GetIndex()];
            this->recordUsage(ref);
            return Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), loc};
        }

        std::vector<std::string> parts = expr.Parts;
        if (parts.empty()) {
            return Expr{};
        }

        // Check both user-specified defines and known globals
        if (opts.matchAgainstDefines) {
            // Make sure define resolution is not recursive
            opts.matchAgainstDefines = false;

            // Substitute user-specified defines
            if (auto definesIt = this->options.defines->DotDefines.find(parts[parts.size() - 1]); definesIt != this->options.defines->DotDefines.end()) {
                for (const auto &define : definesIt->second) {
                    if (define.DefineExprData != nullptr && helpers::StringArraysEqual(define.KeyParts, parts)) {
                        return this->instantiateDefineExpr(loc, *define.DefineExprData, opts);
                    }
                }
            }
        }

        // Check injected dot names
        if (auto namesIt = this->injectedDotNames.find(parts[parts.size() - 1]); namesIt != this->injectedDotNames.end()) {
            for (const injectedDotName &name : namesIt->second) {
                if (helpers::StringArraysEqual(name.parts, parts)) {
                    return this->instantiateInjectDotName(loc, name, opts.assignTarget);
                }
            }
        }

        // Generate an identifier for the first part
        Expr value;
        std::string firstPart = parts[0];
        parts = std::vector<std::string>(parts.begin() + 1, parts.end());
        if (firstPart == "NaN") {
            value = Expr{std::make_shared<ENumber>(ENumber{NAN}), loc};
        } else if (firstPart == "Infinity") {
            value = Expr{std::make_shared<ENumber>(ENumber{INFINITY}), loc};
        } else if (firstPart == "null") {
            value = Expr{kENullShared, loc};
        } else if (firstPart == "undefined") {
            value = Expr{kEUndefinedShared, loc};
        } else if (firstPart == "this") {
            std::pair<Expr, bool> thisValue = this->valueForThis(loc, false /* shouldLog */, AssignTarget::kNone, false, false);
            if (thisValue.second) {
                value = thisValue.first;
            } else {
                value = Expr{kEThisShared, loc};
            }
        } else {
            if (firstPart == "import" && parts.size() > 0 && parts[0] == "meta") {
                std::pair<Expr, bool> importMeta = this->valueForImportMeta(loc);
                if (importMeta.second) {
                    value = importMeta.first;
                } else {
                    value = Expr{std::make_shared<EImportMeta>(), loc};
                }
                parts = std::vector<std::string>(parts.begin() + 1, parts.end());
            } else {
                findSymbolResult result = this->findSymbol(loc, firstPart);
                auto identifier = std::make_shared<EIdentifier>();
                identifier->ref = result.ref;
                identifier->must_keep_due_to_with_stmt = result.isInsideWithScope;

                // Enable tree shaking
                identifier->can_be_removed_if_unused = true;
                value = this->handleIdentifier(loc, identifier, opts);
            }
        }

        // Build up a chain of property access expressions for subsequent parts
        for (const std::string &part : parts) {
            std::pair<Expr, bool> rewritten = this->maybeRewritePropertyAccess(loc, AssignTarget::kNone, false, value, part, loc, false, false, false);
            if (rewritten.second) {
                value = rewritten.first;
            } else if (this->isMangledProp(part)) {
                auto nameOfSymbol = std::make_shared<ENameOfSymbol>();
                nameOfSymbol->ref = this->symbolForMangledProp(part);
                auto index = std::make_shared<EIndex>();
                index->target = value;
                index->index = Expr{nameOfSymbol, loc};
                value = Expr{index, loc};
            } else {
                auto dot = std::make_shared<EDot>();
                dot->target = value;
                dot->name = part;
                dot->name_loc = loc;

                // Enable tree shaking
                dot->can_be_removed_if_unused = true;
                value = Expr{dot, loc};
            }
        }

        return value;
    }

    bool Parser::isDotOrIndexDefineMatch(Expr expr, std::vector<std::string> parts) {
        const E &e = expr.data;
        if (auto dot = Get<EDot>(e)) {
            if (parts.size() > 1) {
                // Intermediates must be dot expressions
                size_t last = parts.size() - 1;
                return parts[last] == dot->name && this->isDotOrIndexDefineMatch(dot->target, std::vector<std::string>(parts.begin(), parts.begin() + static_cast<std::ptrdiff_t>(last)));
            }
        } else if (auto index = Get<EIndex>(e)) {
            if (parts.size() > 1) {
                if (auto str = Get<EString>(index->index.data)) {
                    // Intermediates must be dot expressions
                    size_t last = parts.size() - 1;
                    return parts[last] == helpers::UTF16ToString(str->value) && this->isDotOrIndexDefineMatch(index->target, std::vector<std::string>(parts.begin(), parts.begin() + static_cast<std::ptrdiff_t>(last)));
                }
            }
        } else if (Get<EThis>(e)) {
            // Allow matching on top-level "this"
            if (!this->fnOnlyDataVisit.isThisNested) {
                return parts.size() == 1 && parts[0] == "this";
            }
        } else if (Get<EImportMeta>(e)) {
            // Allow matching on "import.meta"
            return parts.size() == 2 && parts[0] == "import" && parts[1] == "meta";
        } else if (auto ident = Get<EIdentifier>(e)) {
            // The last expression must be an identifier
            if (parts.size() == 1) {
                // The name must match
                std::string name = this->loadNameFromRef(ident->ref);
                if (name != parts[0]) {
                    return false;
                }

                findSymbolResult result = this->findSymbol(expr.loc, name);

                // The "findSymbol" function also marks this symbol as used. But that's
                // never what we want here because we're just peeking to see what kind of
                // symbol it is to see if it's a match. If it's not a match, it will be
                // re-resolved again later and marked as used there. So we don't want to
                // mark it as used twice.
                this->ignoreUsage(result.ref);

                // We must not be in a "with" statement scope
                if (result.isInsideWithScope) {
                    return false;
                }

                // The last symbol must be unbound or injected
                return compiler::SymbolKindIsUnboundOrInjected(this->symbols[result.ref.inner_index].kind);
            }
        }

        return false;
    }

    bool defineValueCanBeUsedInAssignTarget(const E &data) {
        if (Get<EIdentifier>(data) != nullptr || Get<EDot>(data) != nullptr) {
            return true;
        }

        // Substituting a constant into an assignment target (e.g. "x = 1" becomes
        // "0 = 1") will cause a syntax error, so we avoid doing this. The caller
        // will log a warning instead.
        return false;
    }

    // Returns true if a statement needs its own scope and cannot be trivially merged
    // with adjacent statements. Used by block optimization to decide whether a block
    // containing a single statement can be unwrapped. Statements that introduce
    // lexically-scoped bindings (let/const, class, function in strict mode) return true.
    bool statementCaresAboutScope(Stmt stmt) {
        if (Get<SBlock>(stmt.data) != nullptr || Get<SEmpty>(stmt.data) != nullptr || Get<SDebugger>(stmt.data) != nullptr || Get<SExpr>(stmt.data) != nullptr || Get<SIf>(stmt.data) != nullptr ||
            Get<SFor>(stmt.data) != nullptr || Get<SForIn>(stmt.data) != nullptr || Get<SForOf>(stmt.data) != nullptr || Get<SDoWhile>(stmt.data) != nullptr || Get<SWhile>(stmt.data) != nullptr ||
            Get<SWith>(stmt.data) != nullptr || Get<STry>(stmt.data) != nullptr || Get<SSwitch>(stmt.data) != nullptr || Get<SReturn>(stmt.data) != nullptr || Get<SThrow>(stmt.data) != nullptr ||
            Get<SBreak>(stmt.data) != nullptr || Get<SContinue>(stmt.data) != nullptr || Get<SDirective>(stmt.data) != nullptr || Get<SLabel>(stmt.data) != nullptr) {
            return false;
        }
        if (const SLocal *s = Get<SLocal>(stmt.data); s != nullptr) {
            return s->kind != LocalKind::kVar;
        }
        return true;
    }

    // Removes the first statement from a block body, optionally replacing it.
    // Used when rewriting for-loop bodies (e.g. "for (;;) if (x) break;" => "for (;!x;);").
    // Returns the body unchanged if it's not a block, or an empty statement if there's
    // nothing to replace with.
    Stmt dropFirstStatement(Stmt body, Stmt replaceOrNil) {
        if (SBlock *block = Get<SBlock>(body.data); block != nullptr && block->stmts.size() > 0) {
            if (!IsNil(replaceOrNil.data)) {
                block->stmts[0] = replaceOrNil;
            } else if (block->stmts.size() == 2 && !statementCaresAboutScope(block->stmts[1])) {
                return block->stmts[1];
            } else {
                block->stmts.erase(block->stmts.begin());
            }
            return body;
        }
        if (!IsNil(replaceOrNil.data)) {
            return replaceOrNil;
        }
        return Stmt{kSEmptyShared, body.loc};
    }

    // Returns the source location immediately after the operator in a binary expression.
    // Used for locating diagnostic messages (e.g. equality check warnings) that should
    // point to the operator between two operands. Handles transposed operands.
    logger::Loc locAfterOp(EBinary *e) {
        if (e->left.loc.start < e->right.loc.start) {
            return e->right.loc;
        } else {
            // Handle the case when we have transposed the operands
            return e->left.loc;
        }
    }

    // Resets the duplicate-case checker, clearing the bloom filter and stored cases.
    // Preserves vector capacity to avoid reallocation across multiple switch statements.
    void duplicateCaseChecker::reset() {
        // Preserve capacity
        this->cases.clear();

        // This should be optimized by the compiler. See this for more information:
        for (uint8_t &byte : this->bloomFilter) {
            byte = 0;
        }
    }

    // Checks whether a case expression duplicates an earlier case in a switch statement.
    // Uses a bloom filter for fast rejection: if the hash bucket is not set, the case is
    // definitely new. If it is set, performs exact comparison against all stored cases with
    // the same hash. Warns about duplicates (or "likely" duplicates for uncertain matches).
    // Input: case expression from a switch statement.
    // Side effect: logs a warning if a duplicate is found.
    void duplicateCaseChecker::check(Parser *p, Expr expr) {
        std::pair<uint32_t, bool> hashResult = duplicateCaseHash(expr);
        if (!hashResult.second) {
            return;
        }
        uint32_t hash = hashResult.first;
        uint32_t bucket = hash % bloomFilterSize;
        uint8_t *entry = &this->bloomFilter[bucket / 8];
        uint8_t mask = static_cast<uint8_t>(1u << (bucket % 8));

        // Check for collisions
        if ((*entry & mask) != 0) {
            for (const duplicateCaseValue &c : this->cases) {
                if (c.hash == hash) {
                    std::pair<bool, bool> equalsResult = duplicateCaseEquals(c.value, expr);
                    if (equalsResult.first) {
                        logger::Range laterRange;
                        logger::Range earlierRange;
                        if (Get<EString>(expr.data) != nullptr) {
                            laterRange = p->source.RangeOfString(expr.loc);
                        } else {
                            laterRange = p->source.RangeOfOperatorBefore(expr.loc, "case");
                        }
                        if (Get<EString>(c.value.data) != nullptr) {
                            earlierRange = p->source.RangeOfString(c.value.loc);
                        } else {
                            earlierRange = p->source.RangeOfOperatorBefore(c.value.loc, "case");
                        }
                        std::string text = std::string(logger::MsgTemplate(logger::MsgCat::kJS_DuplicateCaseNever));
                        if (equalsResult.second) {
                            text = std::string(logger::MsgTemplate(logger::MsgCat::kJS_DuplicateCaseMay));
                        }
                        logger::MsgKind kind = logger::MsgKind::kWarning;
                        if (p->suppressWarningsAboutWeirdCode) {
                            kind = logger::MsgKind::kDebug;
                        }
                        p->log.AddIDWithNotes(logger::MsgID::kJS_DuplicateCase, kind, &p->tracker, laterRange, text,
                            std::vector<logger::MsgData>{p->tracker.MakeMsgData(earlierRange, "The earlier case clause is here:")});
                    }
                    return;
                }
            }
        }

        *entry |= mask;
        this->cases.push_back(duplicateCaseValue{/*value=*/expr, /*hash=*/hash});
    }

    // Warns about duplicate property keys in object literals or class bodies.
    // Tracks both static and instance keys separately. A getter+setter pair on the same
    // key is allowed and merged; duplicate getters or duplicate setters trigger a warning.
    // Ignores "__proto__" in object literals and "constructor" in classes (these are allowed).
    void Parser::warnAboutDuplicateProperties(std::vector<Property> &properties, duplicatePropertiesIn in) {
        if (properties.size() < 2) {
            return;
        }

        enum keyKind : uint8_t {
            keyMissing = 0,
            keyNormal,
            keyGet,
            keySet,
            keyGetAndSet,
        };

        struct existingKey {
            logger::Loc loc;
            keyKind kind;
        };

        std::unordered_map<std::string, existingKey> instanceKeys;
        std::unordered_map<std::string, existingKey> staticKeys;

        for (const Property &property : properties) {
            if (property.kind != PropertyKind::kSpread) {
                if (const EString *str = Get<EString>(property.key.data); str != nullptr) {
                    std::unordered_map<std::string, existingKey> *keys;
                    if (Has(property.flags, PropertyFlags::kIsStatic)) {
                        keys = &staticKeys;
                    } else {
                        keys = &instanceKeys;
                    }
                    std::string key = helpers::UTF16ToString(str->value);
                    existingKey prevKey;
                    prevKey.kind = keyMissing;
                    auto it = keys->find(key);
                    if (it != keys->end()) {
                        prevKey = it->second;
                    }
                    existingKey nextKey;
                    nextKey.kind = keyNormal;
                    nextKey.loc = property.key.loc;

                    if (property.kind == PropertyKind::kGetter) {
                        nextKey.kind = keyGet;
                    } else if (property.kind == PropertyKind::kSetter) {
                        nextKey.kind = keySet;
                    }

                    if (prevKey.kind != keyMissing && (in != duplicatePropertiesInObject || key != "__proto__") && (in != duplicatePropertiesInClass || key != "constructor")) {
                        if ((prevKey.kind == keyGet && nextKey.kind == keySet) || (prevKey.kind == keySet && nextKey.kind == keyGet)) {
                            nextKey.kind = keyGetAndSet;
                        } else {
                            logger::MsgID id = {};
                            std::string what;
                            std::string where;
                            switch (in) {
                            case duplicatePropertiesInObject:
                                id = logger::MsgID::kJS_DuplicateObjectKey;
                                what = "key";
                                where = "object literal";
                                break;
                            case duplicatePropertiesInClass:
                                id = logger::MsgID::kJS_DuplicateClassMember;
                                what = "member";
                                where = "class body";
                                break;
                            }
                            logger::Range r = RangeOfIdentifier(this->source, property.key.loc);
                            this->log.AddIDWithNotes(id, logger::MsgKind::kWarning, &this->tracker, r,
                                "Duplicate " + what + " \"" + key + "\" in " + where,
                                std::vector<logger::MsgData>{this->tracker.MakeMsgData(RangeOfIdentifier(this->source, prevKey.loc), "The original " + what + " \"" + key + "\" is here:")});
                        }
                    }

                    (*keys)[key] = nextKey;
                }
            }
        }
    }

    // This is a helper function to use when you need to capture a value that may
    // have side effects so you can use it multiple times. It guarantees that the
    // side effects take place exactly once.
    //
    // Example usage:
    //
    //	// "value" => "value + value"
    //	// "value()" => "(_a = value(), _a + _a)"
    //	valueFunc, wrapFunc := p.captureValueWithPossibleSideEffects(loc, 2, value)
    //	return wrapFunc(js_ast.Expr{Loc: loc, Data: &js_ast.EBinary{
    //	  Op: js_ast.BinOpAdd,
    //	  Left: valueFunc(),
    //	  Right: valueFunc(),
    //	}})
    //
    // This returns a function for generating references instead of a raw reference
    // because AST nodes are supposed to be unique in memory, not aliases of other
    // AST nodes. That way you can mutate one during lowering without having to
    // worry about messing up other nodes.
    std::pair<std::function<Expr()>, std::function<Expr(Expr)>> Parser::captureValueWithPossibleSideEffects(
        logger::Loc loc,
        int count,
        Expr value,
        captureValueMode mode) {

        std::function<Expr(Expr)> wrapFunc = [value](Expr expr) -> Expr {
            // Make sure side effects still happen if no expression was generated
            if (IsNil(expr.data)) {
                return value;
            }
            return expr;
        };

        // Referencing certain expressions more than once has no side effects, so we
        // can just create them inline without capturing them in a temporary variable
        std::function<Expr()> valueFunc;
        if (Get<ENull>(value.data) != nullptr) {
            valueFunc = [loc]() -> Expr { return Expr{kENullShared, loc}; };
        } else if (Get<EUndefined>(value.data) != nullptr) {
            valueFunc = [loc]() -> Expr { return Expr{kEUndefinedShared, loc}; };
        } else if (Get<EThis>(value.data) != nullptr) {
            valueFunc = [loc]() -> Expr { return Expr{kEThisShared, loc}; };
        } else if (auto e = Get<EBoolean>(value.data)) {
            valueFunc = [loc, e]() -> Expr {
                return Expr{std::make_shared<EBoolean>(EBoolean{e->value}), loc};
            };
        } else if (auto en = Get<ENumber>(value.data)) {
            valueFunc = [loc, en]() -> Expr {
                return Expr{std::make_shared<ENumber>(ENumber{en->value}), loc};
            };
        } else if (auto eb = Get<EBigInt>(value.data)) {
            valueFunc = [loc, eb]() -> Expr {
                return Expr{std::make_shared<EBigInt>(EBigInt{eb->value}), loc};
            };
        } else if (auto es = Get<EString>(value.data)) {
            valueFunc = [loc, es]() -> Expr {
                return Expr{std::make_shared<EString>(EString{es->value, logger::Loc{}}), loc};
            };
        } else if (auto ep = Get<EPrivateIdentifier>(value.data)) {
            valueFunc = [loc, ep]() -> Expr {
                return Expr{std::make_shared<EPrivateIdentifier>(EPrivateIdentifier{ep->ref}), loc};
            };
        } else if (auto eid = Get<EIdentifier>(value.data)) {
            if (mode == valueDefinitelyNotMutated) {
                valueFunc = [this, loc, eid]() -> Expr {
                    // Make sure we record this usage in the usage count so that duplicating
                    // a single-use reference means it's no longer considered a single-use
                    // reference. Otherwise the single-use reference inlining code may
                    // incorrectly inline the initializer into the first reference, leaving
                    // the second reference without a definition.
                    this->recordUsage(eid->ref);
                    return Expr{std::make_shared<EIdentifier>(EIdentifier{eid->ref}), loc};
                };
            }
        }
        if (valueFunc) {
            return {valueFunc, wrapFunc};
        }

        // We don't need to worry about side effects if the value won't be used
        // multiple times. This special case lets us avoid generating a temporary
        // reference.
        if (count < 2) {
            valueFunc = [value]() -> Expr {
                return value;
            };
            return {valueFunc, wrapFunc};
        }

        // Otherwise, fall back to generating a temporary reference
        std::shared_ptr<compiler::Ref> tempRef = std::make_shared<compiler::Ref>(compiler::kInvalidRef);

        // If we're in a function argument scope, then we won't be able to generate
        // symbols in this scope to store stuff, since there's nowhere to put the
        // variable declaration. We don't want to put the variable declaration
        // outside the function since some code in the argument list may cause the
        // function to be reentrant, and we can't put the variable declaration in
        // the function body since that's not accessible by the argument list.
        //
        // Instead, we use an immediately-invoked arrow function to create a new
        // symbol inline by introducing a new scope. Make sure to only use it for
        // symbol declaration and still initialize the variable inline to preserve
        // side effect order.
        if (this->currentScope->kind == ScopeKind::kFunctionArgs) {
            std::function<Expr()> valueFuncInArgs = [this, loc, value, tempRef]() -> Expr {
                if (*tempRef == compiler::kInvalidRef) {
                    *tempRef = this->generateTempRef(tempRefNoDeclare, "");

                    // Assign inline so the order of side effects remains the same
                    this->recordUsage(*tempRef);
                    return Assign(Expr{std::make_shared<EIdentifier>(EIdentifier{*tempRef}), loc}, value);
                }
                this->recordUsage(*tempRef);
                return Expr{std::make_shared<EIdentifier>(EIdentifier{*tempRef}), loc};
            };
            std::function<Expr(Expr)> wrapFuncInArgs = [loc, value, tempRef](Expr expr) -> Expr {
                // Make sure side effects still happen if no expression was generated
                if (IsNil(expr.data)) {
                    return value;
                }

                // Generate a new variable using an arrow function to avoid messing with "this"
                std::shared_ptr<EArrow> arrow = std::make_shared<EArrow>(EArrow{});
                Arg arg;
                arg.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{*tempRef}), loc};
                arrow->args.push_back(arg);
                arrow->prefer_expr = true;
                arrow->body.loc = loc;
                std::shared_ptr<SReturn> returnStmt = std::make_shared<SReturn>(SReturn{expr});
                arrow->body.block.stmts.push_back(Stmt{returnStmt, loc});
                std::shared_ptr<ECall> call = std::make_shared<ECall>(ECall{});
                call->target = Expr{arrow, loc};
                return Expr{call, loc};
            };
            return {valueFuncInArgs, wrapFuncInArgs};
        }

        std::function<Expr()> valueFuncFallback = [this, loc, value, tempRef]() -> Expr {
            if (*tempRef == compiler::kInvalidRef) {
                *tempRef = this->generateTempRef(tempRefNeedsDeclare, "");
                this->recordUsage(*tempRef);
                return Assign(Expr{std::make_shared<EIdentifier>(EIdentifier{*tempRef}), loc}, value);
            }
            this->recordUsage(*tempRef);
            return Expr{std::make_shared<EIdentifier>(EIdentifier{*tempRef}), loc};
        };
        return {valueFuncFallback, wrapFunc};
    }

    compiler::Ref Parser::declareCommonJSSymbol(compiler::SymbolKind kind, const std::string &name) {
        auto memberIt = this->moduleScope->members.find(name);
        bool ok = memberIt != this->moduleScope->members.end();

        // If the code declared this symbol using "var name", then this is actually
        // not a collision. For example, node will let you do this:
        //
        //   var exports;
        //   module.exports.foo = 123;
        //   console.log(exports.foo);
        //
        // This works because node's implementation of CommonJS wraps the entire
        // source file like this:
        //
        //   (function(require, exports, module, __filename, __dirname) {
        //     var exports;
        //     module.exports.foo = 123;
        //     console.log(exports.foo);
        //   })
        //
        // Both the "exports" argument and "var exports" are hoisted variables, so
        // they don't collide.
        if (ok && this->symbols[memberIt->second.ref.inner_index].kind == compiler::SymbolKind::kHoisted &&
            kind == compiler::SymbolKind::kHoisted && !this->isFileConsideredToHaveESMExports) {
            return memberIt->second.ref;
        }

        // Create a new symbol if we didn't merge with an existing one above
        compiler::Ref ref = this->newSymbol(kind, name);

        // If the variable wasn't declared, declare it now. This means any references
        // to this name will become bound to this symbol after this (since we haven't
        // run the visit pass yet).
        if (!ok) {
            this->moduleScope->members[name] = ScopeMember{ref, logger::Loc{/*start=*/-1}};
            return ref;
        }

        // If the variable was declared, then it shadows this symbol. The code in
        // this module will be unable to reference this symbol. However, we must
        // still add the symbol to the scope so it gets minified (automatically-
        // generated code may still reference the symbol).
        this->moduleScope->generated.push_back(ref);
        return ref;
    }

    
    // First phase of binary expression visiting. Processes the left operand of a binary
    // expression and determines whether special handling is needed (e.g. private brand
    // checks with "in" operator). Sets up the leftIn flags for the caller to use when
    // visiting the left child. Returns a non-nil expression only if this is a special
    // case that short-circuits the normal binary expression visiting.
    Expr binaryExprVisitor::checkAndPrepare(Parser *p) {
        EBinary *bin = this->e.get();

        // Special-case EPrivateIdentifier to allow it here
        if (EPrivateIdentifier *privateId = Get<EPrivateIdentifier>(bin->left.data); privateId != nullptr && bin->op == OpCode::kBinOpIn) {
            std::string name = p->loadNameFromRef(privateId->ref);
            findSymbolResult result = p->findSymbol(bin->left.loc, name);
            privateId->ref = result.ref;

            // Unlike regular identifiers, there are no unbound private identifiers
            compiler::Symbol *symbol = &p->symbols[result.ref.inner_index];
            if (!compiler::SymbolKindIsPrivate(symbol->kind)) {
                logger::Range r = logger::Range{/*loc=*/bin->left.loc, /*len=*/static_cast<int32_t>(name.size())};
                p->log.AddError(&p->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_PrivateNameNotInEnclosing, name));
            }

            bin->right = p->visitExpr(bin->right);

            if (p->privateSymbolNeedsToBeLowered(privateId)) {
                return p->lowerPrivateBrandCheck(bin->right, this->loc, privateId);
            }
            return Expr{/*data=*/this->e, /*loc=*/this->loc};
        }

        this->isStmtExpr = !IsNil(p->stmtExprValue) && Get<EBinary>(p->stmtExprValue) == bin;
        this->oldSilenceWarningAboutThisBeingUndefined = p->fnOnlyDataVisit.silenceMessageAboutThisBeingUndefined;

        if (Get<EThis>(bin->left.data) != nullptr && bin->op == OpCode::kBinOpLogicalAnd) {
            p->fnOnlyDataVisit.silenceMessageAboutThisBeingUndefined = true;
        }
        this->leftIn = exprIn{
            /*isMethod=*/false,
            /*isLoweredPrivateMethod=*/false,
            /*hasChainParent=*/false,
            /*storeThisArgForParentOptionalChain=*/false,
            /*shouldMangleStringsAsProps=*/bin->op == OpCode::kBinOpIn,
            /*assignTarget=*/BinaryAssignTarget(bin->op),
        };
        return Expr{};
    }

    // Second phase of binary expression visiting. Processes the right operand and
    // performs operator-specific transformations:
    //   - Logical operators (&&, ||, ??): dead-code marking based on constant evaluation
    //   - Equality operators (==, ===, !=, !==): constant folding, type warnings
    //   - Nullish coalescing: associativity simplification, lowering
    //   - Addition: string concatenation folding
    //   - All assignment operators: private member lowering, super property handling,
    //     destructuring pattern lowering, CommonJS-in-ESM warnings
    //   - Comma: unused expression simplification
    // Also handles operand reordering for equality comparisons (constants to the right)
    // and comma expression optimization under minification.
    Expr binaryExprVisitor::visitRightAndFinish(Parser *p) {
        EBinary *ebin = this->e.get();

        // Mark the control flow as dead if the branch is never taken
        switch (ebin->op) {
        case OpCode::kBinOpLogicalOr:
            if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(ebin->left.data); boolean && boolean->first) {
                // "true || dead"
                bool old = p->isControlFlowDead;
                p->isControlFlowDead = true;
                ebin->right = p->visitExpr(ebin->right);
                p->isControlFlowDead = old;
            } else {
                ebin->right = p->visitExpr(ebin->right);
            }
            break;

        case OpCode::kBinOpLogicalAnd:
            if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(ebin->left.data); boolean && !boolean->first) {
                // "false && dead"
                bool old = p->isControlFlowDead;
                p->isControlFlowDead = true;
                ebin->right = p->visitExpr(ebin->right);
                p->isControlFlowDead = old;
            } else {
                ebin->right = p->visitExpr(ebin->right);
            }
            break;

        case OpCode::kBinOpNullishCoalescing:
            if (std::optional<std::pair<bool, SideEffects>> isNullOrUndefined = ToNullOrUndefinedWithSideEffects(ebin->left.data); isNullOrUndefined && !isNullOrUndefined->first) {
                // "notNullOrUndefined ?? dead"
                bool old = p->isControlFlowDead;
                p->isControlFlowDead = true;
                ebin->right = p->visitExpr(ebin->right);
                p->isControlFlowDead = old;
            } else {
                ebin->right = p->visitExpr(ebin->right);
            }
            break;

        case OpCode::kBinOpComma:
            ebin->right = p->visitExprInOut(ebin->right, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/this->in.shouldMangleStringsAsProps, /*assignTarget=*/AssignTarget::kNone}).first;
            break;

        case OpCode::kBinOpAssign:
        case OpCode::kBinOpLogicalOrAssign:
        case OpCode::kBinOpLogicalAndAssign:
        case OpCode::kBinOpNullishCoalescingAssign:
            // Check for a propagated name to keep from the parent context
            if (EIdentifier *id = Get<EIdentifier>(ebin->left.data); id != nullptr) {
                p->nameToKeep = p->symbols[id->ref.inner_index].original_name;
                p->nameToKeepIsFor = ebin->right.data;
            }

            ebin->right = p->visitExpr(ebin->right);
            break;

        default:
            ebin->right = p->visitExpr(ebin->right);
            break;
        }
        p->fnOnlyDataVisit.silenceMessageAboutThisBeingUndefined = this->oldSilenceWarningAboutThisBeingUndefined;

        // Always put constants consistently on the same side for equality
        // comparisons to help improve compression. In theory, dictionary-based
        // compression methods may already have a dictionary entry for code that
        // is similar to previous code. Note that we can only reorder expressions
        // that do not have any side effects.
        //
        // Constants are currently ordered on the right instead of the left because
        // it results in slightly smalller gzip size on our primary benchmark
        // (although slightly larger uncompressed size). The size difference is
        // less than 0.1% so it really isn't that important an optimization.
        if (p->options.optionsThatSupportStructuralEquality.minifySyntax) {
            switch (ebin->op) {
            case OpCode::kBinOpLooseEq:
            case OpCode::kBinOpLooseNe:
            case OpCode::kBinOpStrictEq:
            case OpCode::kBinOpStrictNe:
                // "1 === x" => "x === 1"
                if (IsPrimitiveLiteral(ebin->left.data) && !IsPrimitiveLiteral(ebin->right.data)) {
                    Expr temp = ebin->left;
                    ebin->left = ebin->right;
                    ebin->right = temp;
                }
                break;
            default:
                break;  
            }

        }

        if (p->shouldFoldTypeScriptConstantExpressions || (p->options.optionsThatSupportStructuralEquality.minifySyntax && ShouldFoldBinaryOperatorWhenMinifying(*ebin))) {
            if (std::optional<Expr> result = FoldBinaryOperator(this->loc, *ebin); result) {
                return *result;
            }
        }

        // Post-process the binary expression
        switch (ebin->op) {
        case OpCode::kBinOpComma:
            // "(1, 2)" => "2"
            // "(sideEffects(), 2)" => "(sideEffects(), 2)"
            if (p->options.optionsThatSupportStructuralEquality.minifySyntax) {
                ebin->left = p->astHelpers.SimplifyUnusedExpr(ebin->left, p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures);
                if (IsNil(ebin->left.data)) {
                    return ebin->right;
                }
            }
            break;

        case OpCode::kBinOpLooseEq:
            if (std::optional<bool> result = CheckEqualityIfNoSideEffects(ebin->left.data, ebin->right.data, EqualityKind::kLooseEquality); result) {
                return Expr{/*data=*/std::make_shared<EBoolean>(EBoolean{*result}), /*loc=*/this->loc};
            }
            {
                logger::Loc afterOpLoc = locAfterOp(ebin);
                if (!p->warnAboutEqualityCheck("==", ebin->left, afterOpLoc)) {
                    p->warnAboutEqualityCheck("==", ebin->right, afterOpLoc);
                }
                p->warnAboutTypeofAndString(ebin->left, ebin->right, checkBothOrders);

                if (p->options.optionsThatSupportStructuralEquality.minifySyntax) {
                    // "x == void 0" => "x == null"
                    if (Get<EUndefined>(ebin->left.data) != nullptr) {
                        ebin->left.data = kENullShared;
                    } else if (Get<EUndefined>(ebin->right.data) != nullptr) {
                        ebin->right.data = kENullShared;
                    }

                    if (std::optional<Expr> result = MaybeSimplifyEqualityComparison(this->loc, *ebin, p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures); result) {
                        return *result;
                    }
                }
            }
            break;

        case OpCode::kBinOpStrictEq:
            if (std::optional<bool> result = CheckEqualityIfNoSideEffects(ebin->left.data, ebin->right.data, EqualityKind::kStrictEquality); result) {
                return Expr{/*data=*/std::make_shared<EBoolean>(EBoolean{*result}), /*loc=*/this->loc};
            }
            {
                logger::Loc afterOpLoc = locAfterOp(ebin);
                if (!p->warnAboutEqualityCheck("===", ebin->left, afterOpLoc)) {
                    p->warnAboutEqualityCheck("===", ebin->right, afterOpLoc);
                }
                p->warnAboutTypeofAndString(ebin->left, ebin->right, checkBothOrders);

                if (p->options.optionsThatSupportStructuralEquality.minifySyntax) {
                    // "typeof x === 'undefined'" => "typeof x == 'undefined'"
                    if (CanChangeStrictToLoose(ebin->left, ebin->right)) {
                        ebin->op = OpCode::kBinOpLooseEq;
                    }

                    if (std::optional<Expr> result = MaybeSimplifyEqualityComparison(this->loc, *ebin, p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures); result) {
                        return *result;
                    }
                }
            }
            break;

        case OpCode::kBinOpLooseNe:
            if (std::optional<bool> result = CheckEqualityIfNoSideEffects(ebin->left.data, ebin->right.data, EqualityKind::kLooseEquality); result) {
                return Expr{/*data=*/std::make_shared<EBoolean>(EBoolean{!*result}), /*loc=*/this->loc};
            }
            {
                logger::Loc afterOpLoc = locAfterOp(ebin);
                if (!p->warnAboutEqualityCheck("!=", ebin->left, afterOpLoc)) {
                    p->warnAboutEqualityCheck("!=", ebin->right, afterOpLoc);
                }
                p->warnAboutTypeofAndString(ebin->left, ebin->right, checkBothOrders);

                if (p->options.optionsThatSupportStructuralEquality.minifySyntax) {
                    // "x != void 0" => "x != null"
                    if (Get<EUndefined>(ebin->left.data) != nullptr) {
                        ebin->left.data = kENullShared;
                    } else if (Get<EUndefined>(ebin->right.data) != nullptr) {
                        ebin->right.data = kENullShared;
                    }

                    if (std::optional<Expr> result = MaybeSimplifyEqualityComparison(this->loc, *ebin, p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures); result) {
                        return *result;
                    }
                }
            }
            break;

        case OpCode::kBinOpStrictNe:
            if (std::optional<bool> result = CheckEqualityIfNoSideEffects(ebin->left.data, ebin->right.data, EqualityKind::kStrictEquality); result) {
                return Expr{/*data=*/std::make_shared<EBoolean>(EBoolean{!*result}), /*loc=*/this->loc};
            }
            {
                logger::Loc afterOpLoc = locAfterOp(ebin);
                if (!p->warnAboutEqualityCheck("!==", ebin->left, afterOpLoc)) {
                    p->warnAboutEqualityCheck("!==", ebin->right, afterOpLoc);
                }
                p->warnAboutTypeofAndString(ebin->left, ebin->right, checkBothOrders);

                if (p->options.optionsThatSupportStructuralEquality.minifySyntax) {
                    // "typeof x !== 'undefined'" => "typeof x != 'undefined'"
                    if (CanChangeStrictToLoose(ebin->left, ebin->right)) {
                        ebin->op = OpCode::kBinOpLooseNe;
                    }

                    if (std::optional<Expr> result = MaybeSimplifyEqualityComparison(this->loc, *ebin, p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures); result) {
                        return *result;
                    }
                }
            }
            break;

        case OpCode::kBinOpNullishCoalescing:
            if (std::optional<std::pair<bool, SideEffects>> isNullOrUndefined = ToNullOrUndefinedWithSideEffects(ebin->left.data); isNullOrUndefined) {
                bool isNullOrUndefinedValue = isNullOrUndefined->first;
                SideEffects sideEffects = isNullOrUndefined->second;
                // Warn about potential bugs
                if (!IsPrimitiveLiteral(ebin->left.data)) {
                    // "return props.flag === flag ?? true" is "return (props.flag === flag) ?? true" not "return props.flag === (flag ?? true)"
                    std::string which;
                    std::string leftIsNullOrUndefined;
                    std::string leftIsReturned;
                    if (!isNullOrUndefinedValue) {
                        which = "left";
                        leftIsNullOrUndefined = "never";
                        leftIsReturned = "always";
                    } else {
                        which = "right";
                        leftIsNullOrUndefined = "always";
                        leftIsReturned = "never";
                    }
                    logger::MsgKind kind = logger::MsgKind::kWarning;
                    if (p->suppressWarningsAboutWeirdCode) {
                        kind = logger::MsgKind::kDebug;
                    }
                    logger::Range rOp = p->source.RangeOfOperatorBefore(ebin->right.loc, "??");
                    logger::Range rLeft = logger::Range{/*loc=*/ebin->left.loc, /*len=*/static_cast<int32_t>(p->source.LocBeforeWhitespace(rOp.loc).start - ebin->left.loc.start)};
                    p->log.AddIDWithNotes(logger::MsgID::kJS_SuspiciousNullishCoalescing, kind, &p->tracker, rOp,
                        logger::FormatMsg(logger::MsgCat::kJS_NullishCoalescingAlwaysReturns, which),
                        std::vector<logger::MsgData>{p->tracker.MakeMsgData(rLeft,
                            logger::FormatMsg(logger::MsgCat::kJS_NullishCoalescingLeftNote, leftIsNullOrUndefined, leftIsReturned))});
                }

                if (!isNullOrUndefinedValue) {
                    return ebin->left;
                } else if (sideEffects == SideEffects::kNoSideEffects) {
                    return ebin->right;
                }
            }

            if (p->options.optionsThatSupportStructuralEquality.minifySyntax) {
                // "a ?? (b ?? c)" => "a ?? b ?? c"
                if (EBinary *right = Get<EBinary>(ebin->right.data); right != nullptr && right->op == OpCode::kBinOpNullishCoalescing) {
                    ebin->left = JoinWithLeftAssociativeOp(OpCode::kBinOpNullishCoalescing, ebin->left, right->left);
                    Expr newRight = right->right;
                    ebin->right = newRight;
                }
            }

            if (compat::Has(p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kNullishCoalescing)) {
                return p->lowerNullishCoalescing(this->loc, ebin->left, ebin->right);
            }
            break;

        case OpCode::kBinOpLogicalOr:
            if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(ebin->left.data); boolean) {
                // Warn about potential bugs
                if (p->suspiciousLogicalOperatorInsideArrow != E{} && Get<EBinary>(p->suspiciousLogicalOperatorInsideArrow) == this->e.get()) {
                    logger::Range arrowLoc = p->source.RangeOfOperatorBefore(this->loc, "=>");
                    if (arrowLoc.loc.start + 2 == p->source.LocBeforeWhitespace(this->loc).start) {
                        // "return foo => 1 || foo <= 0"
                        std::string which;
                        if (boolean->first) {
                            which = "left";
                        } else {
                            which = "right";
                        }
                        logger::MsgKind kind = logger::MsgKind::kWarning;
                        if (p->suppressWarningsAboutWeirdCode) {
                            kind = logger::MsgKind::kDebug;
                        }
                        logger::MsgData note = p->tracker.MakeMsgData(arrowLoc,
                            std::string(logger::MsgTemplate(logger::MsgCat::kJS_SuspiciousArrowNote)));
                        note.location->suggestion = ">=";
                        logger::Range rOp = p->source.RangeOfOperatorBefore(ebin->right.loc, "||");
                        p->log.AddIDWithNotes(logger::MsgID::kJS_SuspiciousLogicalOperator, kind, &p->tracker, rOp,
                            logger::FormatMsg(logger::MsgCat::kJS_LogicalAlwaysReturns, std::string("||"), which),
                            std::vector<logger::MsgData>{note});
                    }
                }

                if (boolean->first) {
                    return ebin->left;
                } else if (boolean->second == SideEffects::kNoSideEffects) {
                    return ebin->right;
                }
            }

            if (p->options.optionsThatSupportStructuralEquality.minifySyntax) {
                // "a || (b || c)" => "a || b || c"
                if (EBinary *right = Get<EBinary>(ebin->right.data); right != nullptr && right->op == OpCode::kBinOpLogicalOr) {
                    ebin->left = JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, ebin->left, right->left);
                    Expr newRight = right->right;
                    ebin->right = newRight;
                }

                // "a === null || a === undefined" => "a == null"
                if (std::optional<std::pair<Expr, Expr>> leftRight = IsBinaryNullAndUndefined(ebin->left, ebin->right, OpCode::kBinOpStrictEq); leftRight) {
                    ebin->op = OpCode::kBinOpLooseEq;
                    ebin->left = leftRight->first;
                    ebin->right = leftRight->second;
                }
            }
            break;

        case OpCode::kBinOpLogicalAnd:
            if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(ebin->left.data); boolean) {
                // Warn about potential bugs
                if (p->suspiciousLogicalOperatorInsideArrow != E{} && Get<EBinary>(p->suspiciousLogicalOperatorInsideArrow) == this->e.get()) {
                    logger::Range arrowLoc = p->source.RangeOfOperatorBefore(this->loc, "=>");
                    if (arrowLoc.loc.start + 2 == p->source.LocBeforeWhitespace(this->loc).start) {
                        // Warn about `a && b => a && b`
                        std::string which;
                        if (boolean->first) {
                            which = "left";
                        } else {
                            which = "right";
                        }
                        logger::MsgKind kind = logger::MsgKind::kWarning;
                        if (p->suppressWarningsAboutWeirdCode) {
                            kind = logger::MsgKind::kDebug;
                        }
                        logger::MsgData note = p->tracker.MakeMsgData(arrowLoc,
                            std::string(logger::MsgTemplate(logger::MsgCat::kJS_SuspiciousArrowNote)));
                        note.location->suggestion = ">=";
                        logger::Range rOp = p->source.RangeOfOperatorBefore(ebin->right.loc, "&&");
                        p->log.AddIDWithNotes(logger::MsgID::kJS_SuspiciousLogicalOperator, kind, &p->tracker, rOp,
                            logger::FormatMsg(logger::MsgCat::kJS_LogicalAlwaysReturns, std::string("&&"), which),
                            std::vector<logger::MsgData>{note});
                    }
                }

                if (!boolean->first) {
                    return ebin->left;
                } else if (boolean->second == SideEffects::kNoSideEffects) {
                    return ebin->right;
                }
            }

            if (p->options.optionsThatSupportStructuralEquality.minifySyntax) {
                // "a && (b && c)" => "a && b && c"
                if (EBinary *right = Get<EBinary>(ebin->right.data); right != nullptr && right->op == OpCode::kBinOpLogicalAnd) {
                    ebin->left = JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, ebin->left, right->left);
                    Expr newRight = right->right;
                    ebin->right = newRight;
                }

                // "a !== null && a !== undefined" => "a != null"
                if (std::optional<std::pair<Expr, Expr>> leftRight = IsBinaryNullAndUndefined(ebin->left, ebin->right, OpCode::kBinOpStrictNe); leftRight) {
                    ebin->op = OpCode::kBinOpLooseNe;
                    ebin->left = leftRight->first;
                    ebin->right = leftRight->second;
                }
            }
            break;

        case OpCode::kBinOpAdd:
            // "'abc' + 'xyz'" => "'abcxyz'"
            if (std::optional<Expr> result = FoldStringAddition(ebin->left, ebin->right, StringAdditionKind::kNormal); result) {
                return *result;
            }

            if (EBinary *left = Get<EBinary>(ebin->left.data); left != nullptr && left->op == OpCode::kBinOpAdd) {
                // "x + 'abc' + 'xyz'" => "x + 'abcxyz'"
                if (std::optional<Expr> result = FoldStringAddition(left->right, ebin->right, StringAdditionKind::kWithNestedLeft); result) {
                    EBinary *b = new EBinary();
                    b->op = left->op;
                    b->left = left->left;
                    b->right = *result;
                    return Expr{/*data=*/std::shared_ptr<EBinary>(b), /*loc=*/this->loc};
                }
            }
            break;

        case OpCode::kBinOpPow:
            // Lower the exponentiation operator for browsers that don't support it
            if (compat::Has(p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kExponentOperator)) {
                std::vector<Expr> args = {ebin->left, ebin->right};
                return p->callRuntime(this->loc, "__pow", args);
            }
            break;

            ////////////////////////////////////////////////////////////////////////////////
            // All assignment operators below here

        case OpCode::kBinOpAssign:
            if (auto extracted = p->extractPrivateIndex(ebin->left); std::get<2>(extracted) != nullptr) {
                return p->lowerPrivateSet(std::get<0>(extracted), std::get<1>(extracted), std::get<2>(extracted), ebin->right);
            }

            if (Expr property = p->extractSuperProperty(ebin->left); !IsNil(property.data)) {
                return p->lowerSuperPropertySet(ebin->left.loc, property, ebin->right);
            }

            // Lower assignment destructuring patterns for browsers that don't
            // support them. Note that assignment expressions are used to represent
            // initializers in binding patterns, so only do this if we're not
            // ourselves the target of an assignment. Example: "[a = b] = c"
            if (this->in.assignTarget == AssignTarget::kNone) {
                objRestMode mode = objRestMustReturnInitExpr;
                if (this->isStmtExpr) {
                    mode = objRestReturnValueIsUnused;
                }
                if (std::pair<Expr, bool> result = p->lowerAssign(ebin->left, ebin->right, mode); result.second) {
                    return result.first;
                }

                // If CommonJS-style exports are disabled, then references to them are
                // treated as global variable references. This is consistent with how
                // they work in node and the browser, so it's the correct interpretation.
                //
                // However, people sometimes try to use both types of exports within the
                // same module and expect it to work. We warn about this when module
                // format conversion is enabled.
                //
                // Only warn about this for uses in assignment position since there are
                // some legitimate other uses. For example, some people do "typeof module"
                // to check for a CommonJS environment, and we shouldn't warn on that.
                if (p->options.optionsThatSupportStructuralEquality.mode != config::Mode::kPassThrough && p->isFileConsideredToHaveESMExports && !p->isControlFlowDead) {
                    if (EDot *dot = Get<EDot>(ebin->left.data); dot != nullptr) {
                        std::string name;
                        logger::Loc dotLoc;

                        if (EIdentifier *target = Get<EIdentifier>(dot->target.data); target != nullptr) {
                            compiler::Symbol *symbol = &p->symbols[target->ref.inner_index];
                            if (symbol->kind == compiler::SymbolKind::kUnbound &&
                                ((symbol->original_name == "module" && dot->name == "exports") || symbol->original_name == "exports") &&
                                !compiler::Has(symbol->flags, compiler::SymbolFlags::kDidWarnAboutCommonJSInESM)) {
                                // "module.exports = ..."
                                // "exports.something = ..."
                                name = symbol->original_name;
                                dotLoc = dot->target.loc;
                                symbol->flags = symbol->flags | compiler::SymbolFlags::kDidWarnAboutCommonJSInESM;
                            }
                        } else if (EDot *targetDot = Get<EDot>(dot->target.data); targetDot != nullptr) {
                            if (targetDot->name == "exports") {
                                if (EIdentifier *id = Get<EIdentifier>(targetDot->target.data); id != nullptr) {
                                    compiler::Symbol *symbol = &p->symbols[id->ref.inner_index];
                                    if (symbol->kind == compiler::SymbolKind::kUnbound &&
                                        symbol->original_name == "module" && !compiler::Has(symbol->flags, compiler::SymbolFlags::kDidWarnAboutCommonJSInESM)) {
                                        // "module.exports.foo = ..."
                                        name = symbol->original_name;
                                        dotLoc = targetDot->target.loc;
                                        symbol->flags = symbol->flags | compiler::SymbolFlags::kDidWarnAboutCommonJSInESM;
                                    }
                                }
                            }
                        }

                        if (name != "") {
                            logger::MsgKind kind = logger::MsgKind::kWarning;
                            if (p->suppressWarningsAboutWeirdCode) {
                                kind = logger::MsgKind::kDebug;
                            }
                            std::pair<whyESM, std::vector<logger::MsgData>> whyResult = p->whyESModule();
                            whyESM why = whyResult.first;
                            std::vector<logger::MsgData> notes = whyResult.second;
                            if (why == whyESMTypeModulePackageJSON) {
                                std::string text = std::string(logger::MsgTemplate(logger::MsgCat::kJS_CommonJSVariableCJSNote));
                                if (p->options.optionsThatSupportStructuralEquality.ts.Parse) {
                                    text += logger::MsgTemplate(logger::MsgCat::kJS_CommonJSVariableTSNote);
                                }
                                notes.push_back(logger::MsgData{nullptr, nullptr, text});
                            }
                            p->log.AddIDWithNotes(logger::MsgID::kJS_CommonJSVariableInESM, kind, &p->tracker, RangeOfIdentifier(p->source, dotLoc),
                                logger::FormatMsg(logger::MsgCat::kJS_CommonJSVariableInESM, name),
                                notes);
                        }
                    }
                }
            }
            break;

        case OpCode::kBinOpAddAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpAdd, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpSubAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpSub, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpMulAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpMul, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpDivAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpDiv, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpRemAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpRem, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpPowAssign:
            // Lower the exponentiation operator for browsers that don't support it
            if (compat::Has(p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kExponentOperator)) {
                return p->lowerExponentiationAssignmentOperator(this->loc, ebin);
            }

            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpPow, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpShlAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpShl, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpShrAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpShr, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpUShrAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpUShr, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpBitwiseOrAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpBitwiseOr, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpBitwiseAndAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpBitwiseAnd, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpBitwiseXorAssign:
            if (Expr result = p->maybeLowerSetBinOp(ebin->left, OpCode::kBinOpBitwiseXor, ebin->right); !IsNil(result.data)) {
                return result;
            }
            break;

        case OpCode::kBinOpNullishCoalescingAssign:
            if (std::pair<Expr, bool> value = p->lowerNullishCoalescingAssignmentOperator(this->loc, ebin); value.second) {
                return value.first;
            }
            break;

        case OpCode::kBinOpLogicalAndAssign:
            if (std::pair<Expr, bool> value = p->lowerLogicalAssignmentOperator(this->loc, ebin, OpCode::kBinOpLogicalAnd); value.second) {
                return value.first;
            }
            break;

        case OpCode::kBinOpLogicalOrAssign:
            if (std::pair<Expr, bool> value = p->lowerLogicalAssignmentOperator(this->loc, ebin, OpCode::kBinOpLogicalOr); value.second) {
                return value.first;
            }
            break;
            
        default:
            break;
        }

        // "(a, b) + c" => "a, b + c"
        if (p->options.optionsThatSupportStructuralEquality.minifySyntax && ebin->op != OpCode::kBinOpComma) {
            if (EBinary *comma = Get<EBinary>(ebin->left.data); comma != nullptr && comma->op == OpCode::kBinOpComma) {
                EBinary *b = new EBinary();
                b->op = ebin->op;
                b->left = comma->right;
                b->right = ebin->right;
                return JoinWithComma(comma->left, Expr{/*data=*/std::shared_ptr<EBinary>(b), /*loc=*/comma->right.loc});
            }
        }

        return Expr{/*data=*/this->e, /*loc=*/this->loc};
    }

    void Parser::logAssignToDefine(logger::Range r, const std::string &nameArg, Expr expr) {
        // Logs a warning when user code assigns to a define-substituted constant.
        // Builds a human-readable name by walking dot/index chains (e.g. "foo.bar['baz']").
        // The warning explains that the assignment is suspicious because defines are
        // compile-time constants.
        // If this is a compound expression, pretty-print it for the error message.
        // We don't use a literal slice of the source text in case it contains
        // problematic things (e.g. spans multiple lines, has embedded comments).
        std::string name = nameArg;
        if (!IsNil(expr.data)) {
            std::vector<std::string> parts;
            for (;;) {
                if (auto id = Get<EIdentifier>(expr.data)) {
                    parts.push_back(this->loadNameFromRef(id->ref));
                    break;
                } else if (auto dot = Get<EDot>(expr.data)) {
                    parts.push_back(dot->name);
                    parts.push_back(".");
                    expr = dot->target;
                } else if (auto index = Get<EIndex>(expr.data)) {
                    if (auto str = Get<EString>(index->index.data)) {
                        parts.push_back("]");
                        parts.push_back(std::string(helpers::QuoteSingle(helpers::UTF16ToString(str->value), false)));
                        parts.push_back("[");
                        expr = index->target;
                    } else {
                        return;
                    }
                } else {
                    return;
                }
            }
            for (size_t i = 0, j = parts.size() - 1; i < j; i++, j--) {
                std::swap(parts[i], parts[j]);
            }
            // name = strings.Join(parts, "")
            name.clear();
            for (const std::string &part : parts) {
                name += part;
            }
        }

        logger::MsgKind kind = logger::MsgKind::kWarning;
        if (this->suppressWarningsAboutWeirdCode) {
            kind = logger::MsgKind::kDebug;
        }

        logger::MsgData note = {nullptr, nullptr, logger::FormatMsg(logger::MsgCat::kJS_AssignToDefineNote, name)};
        this->log.AddIDWithNotes(logger::MsgID::kJS_AssignToDefine, kind, &this->tracker, r,
            (logger::FormatMsg(logger::MsgCat::kJS_SuspiciousAssignToDefine, name)),
            std::vector<logger::MsgData>{note});
    }




    // Optimizes for-loop control flow by converting "for(;;) if(x) break;" patterns
    // into equivalent test conditions. Handles three cases:
    //   "for(;;) if(x) break;"           => "for(;!x;);"
    //   "for(;;) if(x) break; else y();" => "for(;!x;) y();"
    //   "for(;;) if(x) y(); else break;" => "for(;x;) y();"
    // Also combines with existing test conditions using logical AND.
    void mangleFor(SFor *s) {
        // Get the first statement in the loop
        Stmt first = s->body;
        if (auto block = Get<SBlock>(first.data); block != nullptr && block->stmts.size() > 0) {
            first = block->stmts[0];
        }

        if (auto ifS = Get<SIf>(first.data); ifS != nullptr) {
            // "for (;;) if (x) break;" => "for (; !x;) ;"
            // "for (; a;) if (x) break;" => "for (; a && !x;) ;"
            // "for (;;) if (x) break; else y();" => "for (; !x;) y();"
            // "for (; a;) if (x) break; else y();" => "for (; a && !x;) y();"
            if (auto breakS = Get<SBreak>(ifS->yes.data); breakS != nullptr && breakS->label == nullptr) {
                Expr notExpr;
                if (auto unary = Get<EUnary>(ifS->test.data); unary != nullptr && unary->op == OpCode::kUnOpNot) {
                    notExpr = unary->value;
                } else {
                    notExpr = Not(ifS->test);
                }
                if (!IsNil(s->test_or_nil.data)) {
                    EBinary *eb = new EBinary();
                    eb->op = OpCode::kBinOpLogicalAnd;
                    eb->left = s->test_or_nil;
                    eb->right = notExpr;
                    s->test_or_nil = Expr{std::shared_ptr<EBinary>(eb), s->test_or_nil.loc};
                } else {
                    s->test_or_nil = notExpr;
                }
                s->body = dropFirstStatement(s->body, ifS->no_or_nil != nullptr ? *ifS->no_or_nil : Stmt{});
                return;
            }

            // "for (;;) if (x) y(); else break;" => "for (; x;) y();"
            // "for (; a;) if (x) y(); else break;" => "for (; a && x;) y();"
            if (ifS->no_or_nil != nullptr && !IsNil(ifS->no_or_nil->data)) {
                if (auto breakS = Get<SBreak>(ifS->no_or_nil->data); breakS != nullptr && breakS->label == nullptr) {
                    if (!IsNil(s->test_or_nil.data)) {
                        EBinary *eb = new EBinary();
                        eb->op = OpCode::kBinOpLogicalAnd;
                        eb->left = s->test_or_nil;
                        eb->right = ifS->test;
                        s->test_or_nil = Expr{std::shared_ptr<EBinary>(eb), s->test_or_nil.loc};
                    } else {
                        s->test_or_nil = ifS->test;
                    }
                    s->body = dropFirstStatement(s->body, ifS->yes);
                    return;
                }
            }
        }
    }



    // Generates a call to the "__name" runtime helper to preserve a function or class
    // symbol's original name after minification. Returns an SExpr statement containing
    // the runtime call. Used when "--keep-names" is enabled.
    // Input: expression referencing the fn/class, and the original name string.
    // Output: statement like "__name(fn, 'originalName')"
    Stmt Parser::keepClassOrFnSymbolName(logger::Loc loc, Expr expr, const std::string &name) {
        std::vector<Expr> args;
        args.push_back(expr);
        EString *estr = new EString();
        estr->value = helpers::StringToUTF16(name);
        args.push_back(Expr{std::shared_ptr<EString>(estr), loc});

        SExpr *sexpr = new SExpr();
        sexpr->value = this->callRuntime(loc, "__name", args);
        sexpr->is_from_class_or_fn_that_can_be_removed_if_unused = true;
        return Stmt{std::shared_ptr<SExpr>(sexpr), loc};
    }



    // Determines whether an expression is safe to use as a prefix in a const local
    // declaration (e.g. "const x = <expr>; const y = <expr>;"). Expressions that
    // could have side effects or change behavior when evaluated multiple times are
    // not safe. Returns true for: literals, empty arrays/objects, functions, arrows,
    // and recursively-checked arrays/objects with safe elements.
    bool isSafeForConstLocalPrefix(Expr expr) {
        if (Get<EMissing>(expr.data) != nullptr || Get<EString>(expr.data) != nullptr ||
            Get<ERegExp>(expr.data) != nullptr || Get<EBigInt>(expr.data) != nullptr ||
            Get<EFunction>(expr.data) != nullptr || Get<EArrow>(expr.data) != nullptr) {
            return true;
        } else if (auto *e = Get<EArray>(expr.data); e != nullptr) {
            for (Expr item : e->items) {
                if (!isSafeForConstLocalPrefix(item)) {
                    return false;
                }
            }
            return true;
        } else if (auto *eObj = Get<EObject>(expr.data); eObj != nullptr) {
            // For now just allow "{}" and forbid everything else
            return eObj->properties.empty();
        }
        return false;
    }

    // Warns when a typeof comparison uses a string that typeof will never return.
    // Common mistake: "typeof x === 'null'" should be "x === null" since typeof
    // returns "object" for null values. Also warns about impossible strings like
    // "typeof x === 'undefined'" when it's actually comparing against a typo.
    void Parser::warnAboutTypeofAndString(Expr a, Expr b, typeofStringOrder order) {
        if (order == checkBothOrders) {
            if (Get<EString>(a.data) != nullptr) {
                std::swap(a, b);
            }
        }

        if (auto *typeof_ = Get<EUnary>(a.data); typeof_ != nullptr && typeof_->op == OpCode::kUnOpTypeof) {
            if (auto *str = Get<EString>(b.data); str != nullptr) {
                std::string value = helpers::UTF16ToString(str->value);
                if (value != "undefined" && value != "object" && value != "boolean" && value != "number" && value != "bigint" &&
                    value != "string" && value != "symbol" && value != "function" && value != "unknown") {
                    // Warn about typeof comparisons with values that will never be
                    // returned. Here's an example of code with this problem:
                    // https://github.com/olifolkerd/tabulator/issues/2962
                    logger::Range r = this->source.RangeOfString(b.loc);
                    std::string text = logger::FormatMsg(logger::MsgCat::kJS_ImpossibleTypeof, value);
                    logger::MsgKind kind = logger::MsgKind::kWarning;
                    if (this->suppressWarningsAboutWeirdCode) {
                        kind = logger::MsgKind::kDebug;
                    }
                    std::vector<logger::MsgData> notes;
                    if (value == "null") {
                        notes.push_back(logger::MsgData{nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_ImpossibleTypeofNullNote))});
                    }
                    this->log.AddIDWithNotes(logger::MsgID::kJS_ImpossibleTypeof, kind, &this->tracker, r, text, notes);
                }
            }
        }
    }

    // Warns about common equality check mistakes:
    //   - Comparing with -0 using ==/=== (always matches 0 too)
    //   - Comparing with NaN using ==/=== (always false/true)
    //   - Comparing reference types with == (always false unless loose equality coercion)
    // Returns true if a warning was emitted (for chaining: try left side first, then right).
    bool Parser::warnAboutEqualityCheck(const std::string &op, Expr value, logger::Loc afterOpLoc) {
        if (auto *e = Get<ENumber>(value.data); e != nullptr) {
            // "0 === -0" is true in JavaScript. Here's an example of code with this
            // problem: https://github.com/mrdoob/three.js/pull/11183
            if (e->value == 0 && std::signbit(e->value)) {
                logger::Range r{value.loc, 0};
                if (static_cast<size_t>(r.loc.start) < this->source.contents.size() && this->source.contents[static_cast<size_t>(r.loc.start)] == '-') {
                    logger::Range zeroRange = this->source.RangeOfNumber(logger::Loc{r.loc.start + 1});
                    r.len = zeroRange.len + 1;
                }
                std::string text = logger::FormatMsg(logger::MsgCat::kJS_EqualsNegativeZeroOperator, op);
                if (op == "case") {
                    text = std::string(logger::MsgTemplate(logger::MsgCat::kJS_EqualsNegativeZeroCase));
                }
                logger::MsgKind kind = logger::MsgKind::kWarning;
                if (this->suppressWarningsAboutWeirdCode) {
                    kind = logger::MsgKind::kDebug;
                }
                this->log.AddIDWithNotes(logger::MsgID::kJS_EqualsNegativeZero, kind, &this->tracker, r, text, std::vector<logger::MsgData>{logger::MsgData{nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_EqualsNegativeZeroNote))}});
                return true;
            }

            // "NaN === NaN" is false in JavaScript
            if (std::isnan(e->value)) {
                std::string text = logger::FormatMsg(logger::MsgCat::kJS_EqualsNaNOperator, op, op[0] == '!' ? std::string("true") : std::string("false"));
                if (op == "case") {
                    text = std::string(logger::MsgTemplate(logger::MsgCat::kJS_EqualsNaNSwitchCase));
                }
                logger::Range r = this->source.RangeOfOperatorBefore(afterOpLoc, op);
                logger::MsgKind kind = logger::MsgKind::kWarning;
                if (this->suppressWarningsAboutWeirdCode) {
                    kind = logger::MsgKind::kDebug;
                }
                this->log.AddIDWithNotes(logger::MsgID::kJS_EqualsNaN, kind, &this->tracker, r, text, std::vector<logger::MsgData>{logger::MsgData{nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_EqualsNaNNote))}});
                return true;
            }
        } else if (Get<EArray>(value.data) != nullptr || Get<EArrow>(value.data) != nullptr ||
            Get<EClass>(value.data) != nullptr || Get<EFunction>(value.data) != nullptr ||
            Get<EObject>(value.data) != nullptr || Get<ERegExp>(value.data) != nullptr) {
            // This warning only applies to strict equality because loose equality can
            // cause string conversions. For example, "x == []" is true if x is the
            // empty string. Here's an example of code with this problem:
            // https://github.com/aws/aws-sdk-js/issues/3325
            if (op.size() > 2) {
                std::string text = logger::FormatMsg(logger::MsgCat::kJS_EqualsNewObjectOperator, op, op[0] == '!' ? std::string("true") : std::string("false"));
                if (op == "case") {
                    text = std::string(logger::MsgTemplate(logger::MsgCat::kJS_EqualsNewObjectSwitchCase));
                }
                logger::Range r = this->source.RangeOfOperatorBefore(afterOpLoc, op);
                logger::MsgKind kind = logger::MsgKind::kWarning;
                if (this->suppressWarningsAboutWeirdCode) {
                    kind = logger::MsgKind::kDebug;
                }
                logger::MsgData note = {nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_EqualsNewObjectNote))};
                this->log.AddIDWithNotes(logger::MsgID::kJS_EqualsNewObject, kind, &this->tracker, r, text, std::vector<logger::MsgData>{note});
                return true;
            }
        }

        return false;
    }

    // Instantiates an injected dot name (from "define" configuration) as an identifier.
    // Records the symbol usage and validates that injected symbols are not assigned to.
    // If the symbol is being assigned, logs an error pointing to where it was injected from.
    Expr Parser::instantiateInjectDotName(logger::Loc loc, injectedDotName name, AssignTarget assignTarget) {
        // Note: We don't need to "ignoreRef" on the underlying identifier
        // because we have only parsed it but not visited it yet
        compiler::Ref ref = this->injectedDefineSymbols[name.injectedDefineIndex];
        this->recordUsage(ref);

        if (assignTarget != AssignTarget::kNone) {
            auto it = this->injectedSymbolSources.find(ref);
            if (it != this->injectedSymbolSources.end()) {
                injectedSymbolSource &where = it->second;
                logger::Range r = RangeOfIdentifier(this->source, loc);
                logger::LineColumnTracker lineTracker(&where.source);
                std::string joined;
                for (size_t k = 0; k < name.parts.size(); k++) {
                    if (k > 0) {
                        joined += ".";
                    }
                    joined += name.parts[k];
                }
                this->log.AddErrorWithNotes(&this->tracker, r,
                    logger::FormatMsg(logger::MsgCat::kJS_AssignToInjectedImport, joined),
                    std::vector<logger::MsgData>{lineTracker.MakeMsgData(RangeOfIdentifier(where.source, where.loc),
                        logger::FormatMsg(logger::MsgCat::kJS_AssignToInjectedImportNote, joined, where.source.pretty_paths.Select(this->options.optionsThatSupportStructuralEquality.logPathStyle)))});
            }
        }

        return Expr{std::shared_ptr<EIdentifier>(new EIdentifier{ref}), loc};
    }

    // Checks whether an identifier name contains non-BMP code points that cannot be
    // represented with Unicode escapes when targeting environments that don't support
    // them. Logs an error if the identifier is unrepresentable in ASCII-only mode.
    void Parser::checkForUnrepresentableIdentifier(logger::Loc loc, const std::string &name) {
        if (this->options.optionsThatSupportStructuralEquality.asciiOnly &&
            compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kUnicodeEscapes) &&
            helpers::ContainsNonBMPCodePoint(name)) {
            if (!this->unrepresentableIdentifiers[name]) {
                this->unrepresentableIdentifiers[name] = true;
                std::string where = config::PrettyPrintTargetEnvironment(this->options.optionsThatSupportStructuralEquality.originalTargetEnv, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatureOverridesMask);
                logger::Range r = RangeOfIdentifier(this->source, loc);
                this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_CannotEscapeName, name, where));
            }
        }
    }

    // Marks known global constructors (WeakSet, WeakMap, Date, Set, Map) as pure
    // when called with side-effect-free arguments. This allows the call to be removed
    // by tree-shaking if the result is unused. For example, "new Date(0)" is pure,
    // but "new Date(x)" is not because converting x to string may have side effects.
    void Parser::maybeMarkKnownGlobalConstructorAsPure(ENew *e) {
        if (EIdentifier *id = Get<EIdentifier>(e->target.data); id != nullptr) {
            if (compiler::Symbol symbol = this->symbols[id->ref.inner_index]; symbol.kind == compiler::SymbolKind::kUnbound) {
                if (symbol.original_name == "WeakSet" || symbol.original_name == "WeakMap") {
                    size_t n = e->args.size();

                    if (n == 0) {
                        // "new WeakSet()" is pure
                        e->can_be_unwrapped_if_unused = true;
                    } else if (n == 1) {
                        if (Get<ENull>(e->args[0].data) != nullptr || Get<EUndefined>(e->args[0].data) != nullptr) {
                            // "new WeakSet(null)" is pure
                            // "new WeakSet(void 0)" is pure
                            e->can_be_unwrapped_if_unused = true;
                        } else if (EArray *array = Get<EArray>(e->args[0].data); array != nullptr) {
                            if (array->items.empty()) {
                                // "new WeakSet([])" is pure
                                e->can_be_unwrapped_if_unused = true;
                            } else {
                                // "new WeakSet([x])" is impure because an exception is thrown if "x" is not an object
                            }
                        } else {
                            // "new WeakSet(x)" is impure because the iterator for "x" could have side effects
                        }
                    }
                } else if (symbol.original_name == "Date") {
                    size_t n = e->args.size();

                    if (n == 0) {
                        // "new Date()" is pure
                        e->can_be_unwrapped_if_unused = true;
                    } else if (n == 1) {
                        switch (KnownPrimitiveType(e->args[0].data)) {
                        case PrimitiveType::kNull:
                        case PrimitiveType::kUndefined:
                        case PrimitiveType::kBoolean:
                        case PrimitiveType::kNumber:
                        case PrimitiveType::kString:
                            // "new Date('')" is pure
                            // "new Date(0)" is pure
                            // "new Date(null)" is pure
                            // "new Date(true)" is pure
                            // "new Date(false)" is pure
                            // "new Date(undefined)" is pure
                            e->can_be_unwrapped_if_unused = true;
                            break;

                        default:
                            // "new Date(x)" is impure because converting "x" to a string could have side effects
                            break;
                        }
                    }
                } else if (symbol.original_name == "Set") {
                    size_t n = e->args.size();

                    if (n == 0) {
                        // "new Set()" is pure
                        e->can_be_unwrapped_if_unused = true;
                    } else if (n == 1) {
                        if (Get<EArray>(e->args[0].data) != nullptr || Get<ENull>(e->args[0].data) != nullptr ||
                            Get<EUndefined>(e->args[0].data) != nullptr) {
                            // "new Set([a, b, c])" is pure
                            // "new Set(null)" is pure
                            // "new Set(void 0)" is pure
                            e->can_be_unwrapped_if_unused = true;
                        } else {
                            // "new Set(x)" is impure because the iterator for "x" could have side effects
                        }
                    }
                } else if (symbol.original_name == "Map") {
                    size_t n = e->args.size();

                    if (n == 0) {
                        // "new Map()" is pure
                        e->can_be_unwrapped_if_unused = true;
                    } else if (n == 1) {
                        if (Get<ENull>(e->args[0].data) != nullptr || Get<EUndefined>(e->args[0].data) != nullptr) {
                            // "new Map(null)" is pure
                            // "new Map(void 0)" is pure
                            e->can_be_unwrapped_if_unused = true;
                        } else if (EArray *array = Get<EArray>(e->args[0].data); array != nullptr) {
                            bool allEntriesAreArrays = true;
                            for (const Expr &item : array->items) {
                                if (Get<EArray>(item.data) == nullptr) {
                                    // "new Map([x])" is impure because "x[0]" could have side effects
                                    allEntriesAreArrays = false;
                                    break;
                                }
                            }

                            // "new Map([[a, b], [c, d]])" is pure
                            if (allEntriesAreArrays) {
                                e->can_be_unwrapped_if_unused = true;
                            }
                        } else {
                            // "new Map(x)" is impure because the iterator for "x" could have side effects
                        }
                    }
                }
            }
        }
    }


    // Remaps source locations in a JSON expression tree using a string-in-JS translation
    // table. This is used for Yarn PnP manifest parsing where a JSON string is embedded
    // inside JavaScript source code; the locations need to be adjusted to point to the
    // correct positions in the original JavaScript source.
    void Parser::remapExprLocsInJSON(Expr *expr, std::vector<logger::StringInJSTableEntry> table) {
        expr->loc = logger::RemapStringInJSLoc(table, expr->loc);

        if (EArray *e2 = Get<EArray>(expr->data); e2 != nullptr) {
            e2->close_bracket_loc = logger::RemapStringInJSLoc(table, e2->close_bracket_loc);
            for (size_t i = 0; i < static_cast<size_t>(e2->items.size()); i++) {
                this->remapExprLocsInJSON(&e2->items[i], table);
            }
        } else if (EObject *eObj = Get<EObject>(expr->data); eObj != nullptr) {
            eObj->close_brace_loc = logger::RemapStringInJSLoc(table, eObj->close_brace_loc);
            for (size_t i = 0; i < eObj->properties.size(); i++) {
                this->remapExprLocsInJSON(&eObj->properties[i].key, table);
                this->remapExprLocsInJSON(&eObj->properties[i].value_or_nil, table);
            }
        }
    }

    // Returns true if a function's parameter list is "simple" (no rest parameters,
    // no destructuring patterns, no default values). Simple parameter lists are required
    // when the function body contains "use strict" (per ES5.1 Section 15.2.1).
    bool isSimpleParameterList(std::vector<Arg> args, bool hasRestArg) {
        if (hasRestArg) {
            return false;
        }
        for (const Arg &arg : args) {
            if (Get<BIdentifier>(arg.binding.data) == nullptr || !IsNil(arg.default_or_nil.data)) {
                return false;
            }
        }
        return true;
    }


    // Checks whether a function body starts with a "use strict" directive.
    // Skips over leading comments and other directives to find it.
    // Returns the location of the directive and whether it was found.
    std::pair<logger::Loc, bool> fnBodyContainsUseStrict(std::vector<Stmt> body) {
        for (const Stmt &stmt : body) {
            if (Get<SComment>(stmt.data) != nullptr) {
                continue;
            } else if (auto directive = Get<SDirective>(stmt.data)) {
                if (helpers::UTF16EqualsString(directive->value, "use strict")) {
                    return {stmt.loc, true};
                }
            } else {
                return {logger::Loc{}, false};
            }
        }
        return {logger::Loc{}, false};
    }


    // Comparator for sorting scope members by their symbol reference index.
    // Used to ensure deterministic iteration order when hoisting symbols.
    bool scopeMemberArrayLess(const ScopeMember &a, const ScopeMember &b) {
        return a.ref.inner_index < b.ref.inner_index ||
            (a.ref.inner_index == b.ref.inner_index && a.ref.source_index < b.ref.source_index);
    }

    // Hoists variable and function declarations from inner scopes to their enclosing
    // function or module scope. Handles Annex B block-level function declarations in
    // sloppy mode, catch binding collisions, and "with" statement scope interactions.
    // This is called during the visit pass after all scopes have been created.
    void Parser::hoistSymbols(Scope *scope) {
        // Duplicate function declarations are forbidden in nested blocks in strict
        // mode. Separately, they are also forbidden at the top-level of modules.
        // This check needs to be delayed until now instead of being done when the
        // functions are declared because we potentially need to scan the whole file
        // to know if the file is considered to be in strict mode (or is considered
        // to be a module). We might only encounter an "export {}" clause at the end
        // of the file.
        if ((scope->strict_mode != StrictModeKind::kSloppyMode && scope->kind == ScopeKind::kBlock) || (scope->parent == nullptr && this->isFileConsideredESM)) {
            for (const ScopeMember &replaced : scope->replaced) {
                compiler::Symbol &symbol = this->symbols[replaced.ref.inner_index];
                if (compiler::SymbolKindIsFunction(symbol.kind)) {
                    if (auto it = scope->members.find(symbol.original_name); it != scope->members.end()) {
                        const ScopeMember &member = it->second;
                        if (compiler::SymbolKindIsFunction(this->symbols[member.ref.inner_index].kind)) {
                            std::vector<logger::MsgData> notes;
                            if (scope->parent == nullptr && this->isFileConsideredESM) {
                                notes = this->whyESModule().second;
                                notes[0].text = std::string(logger::MsgTemplate(logger::MsgCat::kJS_DuplicateFnDeclModule)) + notes[0].text;
                            } else {
                                std::string where;
                                auto why = this->whyStrictMode(scope);
                                where = why.first;
                                notes = why.second;
                                notes[0].text = logger::FormatMsg(logger::MsgCat::kJS_DuplicateFnDeclNested, where) + notes[0].text;
                            }

                            std::vector<logger::MsgData> msgs;
                            msgs.push_back(this->tracker.MakeMsgData(
                                RangeOfIdentifier(this->source, replaced.loc),
                                logger::FormatMsg(logger::MsgCat::kJS_SymbolOriginalDeclaredNote, symbol.original_name)));
                            msgs.insert(msgs.end(), notes.begin(), notes.end());

                            this->log.AddErrorWithNotes(&this->tracker,
                                RangeOfIdentifier(this->source, member.loc),
                                logger::FormatMsg(logger::MsgCat::kJS_SymbolAlreadyDeclared, symbol.original_name),
                                msgs);
                        }
                    }
                }
            }
        }

        if (!StopsHoisting(scope->kind)) {
            // We create new symbols in the loop below, so the iteration order of the
            // loop must be deterministic to avoid generating different minified names
            std::vector<ScopeMember> sortedMembers;
            sortedMembers.reserve(scope->members.size());
            for (const auto &member : scope->members) {
                sortedMembers.push_back(member.second);
            }
            std::sort(sortedMembers.begin(), sortedMembers.end(), scopeMemberArrayLess);

            for (const ScopeMember &member : sortedMembers) {
                // Handle non-hoisted collisions between catch bindings and the catch body.
                // This implements "B.3.4 VariableStatements in Catch Blocks" from Annex B
                // of the ECMAScript standard version 6+ (except for the hoisted case, which
                // is handled later on below):
                //
                // * It is a Syntax Error if any element of the BoundNames of CatchParameter
                //   also occurs in the LexicallyDeclaredNames of Block.
                //
                // * It is a Syntax Error if any element of the BoundNames of CatchParameter
                //   also occurs in the VarDeclaredNames of Block unless CatchParameter is
                //   CatchParameter : BindingIdentifier .
                //
                if (scope->parent->kind == ScopeKind::kCatchBinding) {
                    compiler::Symbol &symbol = this->symbols[member.ref.inner_index];
                    if (symbol.kind != compiler::SymbolKind::kHoisted) {
                        if (auto it = scope->parent->members.find(symbol.original_name); it != scope->parent->members.end()) {
                            this->addSymbolAlreadyDeclaredError(symbol.original_name, member.loc, it->second.loc);
                            continue;
                        }
                    }
                }

                ScopeMember workingMember = member;
                compiler::Symbol &symbolRef = this->symbols[workingMember.ref.inner_index];
                if (!compiler::SymbolKindIsHoisted(symbolRef.kind)) {
                    continue;
                }

                // Implement "Block-Level Function Declarations Web Legacy Compatibility
                // Semantics" from Annex B of the ECMAScript standard version 6+
                bool isSloppyModeBlockLevelFnStmt = false;
                compiler::Ref originalMemberRef = workingMember.ref;
                if (symbolRef.kind == compiler::SymbolKind::kHoistedFunction) {
                    // Block-level function declarations behave like "let" in strict mode
                    if (scope->strict_mode != StrictModeKind::kSloppyMode) {
                        continue;
                    }

                    // In sloppy mode, block level functions behave like "let" except with
                    // an assignment to "var", sort of. This code:
                    //
                    //   if (x) {
                    //     f();
                    //     function f() {}
                    //   }
                    //   f();
                    //
                    // behaves like this code:
                    //
                    //   if (x) {
                    //     let f2 = function() {}
                    //     var f = f2;
                    //     f2();
                    //   }
                    //   f();
                    //
                    compiler::Ref hoistedRef = this->newSymbol(compiler::SymbolKind::kHoisted, symbolRef.original_name);
                    scope->generated.push_back(hoistedRef);
                    this->hoistedRefForSloppyModeBlockFn[workingMember.ref] = hoistedRef;
                    workingMember.ref = hoistedRef;
                    isSloppyModeBlockLevelFnStmt = true;
                }

                compiler::Symbol &symbol = this->symbols[workingMember.ref.inner_index];

                // Check for collisions that would prevent to hoisting "var" symbols up to the enclosing function scope
                Scope *s = scope->parent;
                bool skipMember = false;
                for (;;) {
                    // Variable declarations hoisted past a "with" statement may actually end
                    // up overwriting a property on the target of the "with" statement instead
                    // of initializing the variable. We must not rename them or we risk
                    // causing a behavior change.
                    //
                    //   var obj = { foo: 1 }
                    //   with (obj) { var foo = 2 }
                    //   assert(foo === undefined)
                    //   assert(obj.foo === 2)
                    //
                    if (s->kind == ScopeKind::kWith) {
                        symbol.flags = compiler::Has(symbol.flags, compiler::SymbolFlags::kMustNotBeRenamed) ? symbol.flags : symbol.flags | compiler::SymbolFlags::kMustNotBeRenamed;
                    }

                    if (auto it = s->members.find(symbol.original_name); it != s->members.end()) {
                        const ScopeMember &existingMember = it->second;
                        compiler::Symbol &existingSymbol = this->symbols[existingMember.ref.inner_index];

                        // We can hoist the symbol from the child scope into the symbol in
                        // this scope if:
                        //
                        //   - The symbol is unbound (i.e. a global variable access)
                        //   - The symbol is also another hoisted variable
                        //   - The symbol is a function of any kind and we're in a function or module scope
                        //
                        // Is this unbound (i.e. a global access) or also hoisted?
                        if (existingSymbol.kind == compiler::SymbolKind::kUnbound || existingSymbol.kind == compiler::SymbolKind::kHoisted ||
                            (compiler::SymbolKindIsFunction(existingSymbol.kind) && (s->kind == ScopeKind::kEntry || s->kind == ScopeKind::kFunctionBody))) {
                            // Silently merge this symbol into the existing symbol
                            symbol.link = existingMember.ref;
                            s->members[symbol.original_name] = existingMember;
                            skipMember = true;
                            break;
                        }

                        // Otherwise if this isn't a catch identifier or "arguments", it's a collision
                        if (existingSymbol.kind != compiler::SymbolKind::kCatchIdentifier && existingSymbol.kind != compiler::SymbolKind::kArguments) {
                            // An identifier binding from a catch statement and a function
                            // declaration can both silently shadow another hoisted symbol
                            if (symbol.kind != compiler::SymbolKind::kCatchIdentifier && symbol.kind != compiler::SymbolKind::kHoistedFunction) {
                                if (!isSloppyModeBlockLevelFnStmt) {
                                    this->addSymbolAlreadyDeclaredError(symbol.original_name, workingMember.loc, existingMember.loc);
                                } else if (s == scope->parent) {
                                    // Never mind about this, turns out it's not needed after all
                                    this->hoistedRefForSloppyModeBlockFn.erase(originalMemberRef);
                                }
                            }
                            skipMember = true;
                            break;
                        }

                        // If this is a catch identifier, silently merge the existing symbol
                        // into this symbol but continue hoisting past this catch scope
                        existingSymbol.link = workingMember.ref;
                        s->members[symbol.original_name] = workingMember;
                    }

                    if (StopsHoisting(s->kind)) {
                        // Declare the member in the scope that stopped the hoisting
                        s->members[symbol.original_name] = workingMember;
                        break;
                    }
                    s = s->parent;
                }
                if (skipMember) {
                    continue;
                }
            }
        }

        for (Scope *child : scope->children) {
            this->hoistSymbols(child);
        }
    }

    // Scans a regular expression literal for features unsupported by the target platform.
    // Checks both the pattern (named groups, lookbehinds, unicode property escapes)
    // and the flags (s, y, u, d, v). Returns the pattern string, flags string, and
    // whether any unsupported feature was found. If unsupported, the regex will be
    // converted to a "new RegExp()" call in the output.
    std::tuple<std::string, std::string, bool> Parser::isUnsupportedRegularExpression(logger::Loc loc, const std::string &value) {
        std::string what;
        logger::Range r;

        size_t end = value.find_last_of('/');
        std::string pattern = value.substr(1, end - 1);
        std::string flags = value.substr(end + 1);
        bool isUnicode = flags.find('u') != std::string::npos;
        int parenDepth = 0;
        size_t i = 0;

        // Do a simple scan for unsupported features assuming the regular expression
        // is valid. This doesn't do a full validation of the regular expression
        // because regular expression grammar is complicated. If it contains a syntax
        // error that we don't catch, then we will just generate output code with a
        // syntax error. Garbage in, garbage out.
        bool isUnsupported = false;
        bool breakPattern = false;
        while (i < pattern.size() && !breakPattern) {
            char c = pattern[i];
            i++;

            switch (c) {
            case '[':
            {
                bool breakClass = false;
                while (i < pattern.size() && !breakClass) {
                    c = pattern[i];
                    i++;

                    switch (c) {
                    case ']':
                        breakClass = true;
                        break;

                    case '\\':
                        i++; // Skip the escaped character
                        break;
                    }
                }
                break;
            }

            case '(':
            {
                std::string tail = pattern.substr(i);

                if (tail.rfind("?<=", 0) == 0 || tail.rfind("?<!", 0) == 0) {
                    if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kRegexpLookbehindAssertions)) {
                        what = std::string(logger::MsgTemplate(logger::MsgCat::kJS_UnsupportedRegexpLookbehind));
                        r.loc.start = loc.start + static_cast<int32_t>(i) + 1;
                        r.len = 3;
                        isUnsupported = true;
                        breakPattern = true;
                    }
                } else if (tail.rfind("?<", 0) == 0) {
                    if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kRegexpNamedCaptureGroups)) {
                        size_t nameEnd = tail.find('>');
                        if (nameEnd != std::string::npos) {
                            what = std::string(logger::MsgTemplate(logger::MsgCat::kJS_UnsupportedRegexpNamedCaptureGroup));
                            r.loc.start = loc.start + static_cast<int32_t>(i) + 1;
                            r.len = static_cast<int32_t>(nameEnd) + 1;
                            isUnsupported = true;
                            breakPattern = true;
                        }
                    }
                }

                parenDepth++;
                break;
            }

            case ')':
                if (parenDepth == 0) {
                    logger::Range unexpected;
                    unexpected.loc.start = loc.start + static_cast<int32_t>(i);
                    unexpected.len = 1;
                    this->log.AddError(&this->tracker, unexpected, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedParenInRegexp));
                    return {pattern, flags, isUnsupported};
                }

                parenDepth--;
                break;

            case '\\':
            {
                std::string tail = pattern.substr(i);

                if (isUnicode && (tail.rfind("p{", 0) == 0 || tail.rfind("P{", 0) == 0)) {
                    if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kRegexpUnicodePropertyEscapes)) {
                        size_t escapeEnd = tail.find('}');
                        if (escapeEnd != std::string::npos) {
                            what = std::string(logger::MsgTemplate(logger::MsgCat::kJS_UnsupportedRegexpUnicodePropertyEscape));
                            r.loc.start = loc.start + static_cast<int32_t>(i);
                            r.len = static_cast<int32_t>(escapeEnd) + 2;
                            isUnsupported = true;
                            breakPattern = true;
                        }
                    }
                }

                i++; // Skip the escaped character
                break;
            }
            }
        }

        if (!isUnsupported) {
            for (size_t j = 0; j < flags.size(); j++) {
                char c = flags[j];
                switch (c) {
                case 'g':
                case 'i':
                case 'm':
                    continue; // These are part of ES5 and are always supported

                case 's':
                    if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kRegexpDotAllFlag)) {
                        continue; // This is part of ES2018
                    }
                    break;

                case 'y':
                case 'u':
                    if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kRegexpStickyAndUnicodeFlags)) {
                        continue; // These are part of ES2018
                    }
                    break;

                case 'd':
                    if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kRegexpMatchIndices)) {
                        continue; // This is part of ES2022
                    }
                    break;

                case 'v':
                    if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kRegexpSetNotation)) {
                        continue; // This is from a proposal: https://github.com/tc39/proposal-regexp-v-flag
                    }
                    break;

                default:
                    // Unknown flags are never supported
                    break;
                }

                r.loc.start = loc.start + static_cast<int32_t>(end + 1) + static_cast<int32_t>(j);
                r.len = 1;
                what = logger::FormatMsg(logger::MsgCat::kJS_UnsupportedRegexpFlag, std::string(1, c));
                isUnsupported = true;
                break;
            }
        }

        if (isUnsupported) {
            std::string where = config::PrettyPrintTargetEnvironment(this->options.optionsThatSupportStructuralEquality.originalTargetEnv, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatureOverridesMask);
            logger::MsgData note = logger::MsgData{nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_UnsupportedRegexpNote))};
            this->log.AddIDWithNotes(logger::MsgID::kJS_UnsupportedRegExp, logger::MsgKind::kDebug, &this->tracker, r, what + " in " + where,
                std::vector<logger::MsgData>{note});
        }

        return {pattern, flags, isUnsupported};
    }

    // This function exists to tie all of these checks together in one place
    bool isEvalOrArguments(const std::string &name) {
        return name == "eval" || name == "arguments";
    }

    // Checks whether a property access expression (e.g. "a.b" or "a['b']") matches
    // a user-specified define pattern. Recursively walks dot/index chains to compare
    // against the parts array. Also matches "this" and "import.meta" as root identifiers.
    // Returns true if the expression matches the given define key parts.
    std::pair<Expr, bool> Parser::maybeRewritePropertyAccess(
        logger::Loc loc,
        AssignTarget assignTarget,
        bool isDeleteTarget,
        Expr target,
        const std::string &name,
        logger::Loc nameLoc,
        bool isCallTarget,
        bool isTemplateTag,
        bool preferQuotedKey)
    {
        if (auto id = Get<EIdentifier>(target.data)) {
            // Rewrite property accesses on explicit namespace imports as an identifier.
            // This lets us replace them easily in the printer to rebind them to
            // something else without paying the cost of a whole-tree traversal during
            // module linking just to rewrite these EDot expressions.
            if (this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle) {
                if (auto itemIt = this->importItemsForNamespace.find(id->ref); itemIt != this->importItemsForNamespace.end()) {
                    namespaceImportItems &importItems = itemIt->second;

                    // Cache translation so each property access resolves to the same import
                    auto entryIt = importItems.entries.find(name);
                    compiler::LocRef item;
                    if (entryIt == importItems.entries.end()) {
                        // Replace non-default imports with "undefined" for JSON import assertions
                        compiler::ImportRecord *record = &this->importRecords[importItems.importRecordIndex];
                        if (compiler::Has(record->flags, compiler::ImportRecordFlags::kAssertTypeJSON) && name != "default") {
                            logger::MsgKind kind = logger::MsgKind::kWarning;
                            if (this->suppressWarningsAboutWeirdCode) {
                                kind = logger::MsgKind::kDebug;
                            }
                            this->log.AddIDWithNotes(logger::MsgID::kJS_AssertTypeJSON, kind, &this->tracker, RangeOfIdentifier(this->source, nameLoc),
                                logger::FormatMsg(logger::MsgCat::kJS_NonDefaultJSONImportUndefined, name),
                                this->notesForAssertTypeJSON(record, name));
                            this->ignoreUsage(id->ref);
                            return {Expr{kEUndefinedShared, loc}, true};
                        }

                        // Generate a new import item symbol in the module scope
                        item = compiler::LocRef{/*loc=*/nameLoc, /*ref=*/this->newSymbol(compiler::SymbolKind::kImport, name)};
                        this->moduleScope->generated.push_back(item.ref);

                        // Link the namespace import and the import item together
                        importItems.entries[name] = item;
                        this->isImportItem[item.ref] = true;

                        compiler::Symbol &symbol = this->symbols[item.ref.inner_index];
                        if (this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kPassThrough) {
                            // Make sure the printer prints this as a property access
                            symbol.namespace_alias = new compiler::NamespaceAlias{name, id->ref};
                        } else {
                            // Mark this as generated in case it's missing. We don't want to
                            // generate errors for missing import items that are automatically
                            // generated.
                            symbol.import_item_status = compiler::ImportItemStatus::kGenerated;
                        }
                    } else {
                        item = entryIt->second;
                    }

                    // Undo the usage count for the namespace itself. This is used later
                    // to detect whether the namespace symbol has ever been "captured"
                    // or whether it has just been used to read properties off of.
                    //
                    // The benefit of doing this is that if both this module and the
                    // imported module end up in the same module group and the namespace
                    // symbol has never been captured, then we don't need to generate
                    // any code for the namespace at all.
                    this->ignoreUsage(id->ref);

                    // Track how many times we've referenced this symbol
                    this->recordUsage(item.ref);
                    auto identifier = std::make_shared<EIdentifier>();
                    identifier->ref = item.ref;
                    return {this->handleIdentifier(nameLoc, identifier, identifierOpts{
                        assignTarget,
                        isCallTarget,
                        isDeleteTarget,
                        preferQuotedKey,

                        // If this expression is used as the target of a call expression, make
                        // sure the value of "this" is preserved.
                        false, /* wasOriginallyIdentifier */
                        false, /* matchAgainstDefines */
                    }), true};
                }

                // Rewrite "module.require()" to "require()" for Webpack compatibility.
                // See https://github.com/webpack/webpack/pull/7750 for more info.
                if (isCallTarget && id->ref == this->moduleRef && name == "require") {
                    this->ignoreUsage(this->moduleRef);

                    // This uses "require" instead of a reference to our "__require"
                    // function so that the code coming up that detects calls to
                    // "require" will recognize it.
                    this->recordUsage(this->requireRef);
                    auto identifier = std::make_shared<EIdentifier>();
                    identifier->ref = this->requireRef;
                    return {Expr{identifier, nameLoc}, true};
                }
            }
        }

        // Attempt to simplify statically-determined object literal property accesses
        if (!isCallTarget && !isTemplateTag && this->options.optionsThatSupportStructuralEquality.minifySyntax && assignTarget == AssignTarget::kNone) {
            if (auto object = Get<EObject>(target.data)) {
                Expr replace;
                bool hasProtoNull = false;
                bool isUnsafe = false;

                // Check that doing this is safe
                for (const Property &prop : object->properties) {
                    // "{ ...a }.a" must be preserved
                    // "new ({ a() {} }.a)" must throw
                    // "{ get a() {} }.a" must be preserved
                    // "{ set a(b) {} }.a = 1" must be preserved
                    // "{ a: 1, [String.fromCharCode(97)]: 2 }.a" must be 2
                    if (prop.kind == PropertyKind::kSpread || Has(prop.flags, PropertyFlags::kIsComputed) || IsMethodDefinition(prop.kind)) {
                        isUnsafe = true;
                        break;
                    }

                    // Do not attempt to compare against numeric keys
                    auto str = Get<EString>(prop.key.data);
                    if (str == nullptr) {
                        isUnsafe = true;
                        break;
                    }

                    // The "__proto__" key has special behavior
                    if (helpers::UTF16EqualsString(str->value, "__proto__")) {
                        if (Get<ENull>(prop.value_or_nil.data) != nullptr) {
                            // Replacing "{__proto__: null}.a" with undefined should be safe
                            hasProtoNull = true;
                        }
                    }

                    // This entire object literal must have no side effects
                    if (!this->astHelpers.ExprCanBeRemovedIfUnused(prop.value_or_nil)) {
                        isUnsafe = true;
                        break;
                    }

                    // Note that we need to take the last value if there are duplicate keys
                    if (helpers::UTF16EqualsString(str->value, name)) {
                        replace = prop.value_or_nil;
                    }
                }

                if (!isUnsafe) {
                    // If the key was found, return the value for that key. Note
                    // that "{__proto__: null}.__proto__" is undefined, not null.
                    if (!IsNil(replace.data) && name != "__proto__") {
                        return {replace, true};
                    }

                    // We can only return "undefined" when a key is missing if the prototype is null
                    if (hasProtoNull) {
                        return {Expr{kEUndefinedShared, target.loc}, true};
                    }
                }
            }
        }

        // Handle references to namespaces or namespace members
        if (Get<EIdentifier>(target.data) == this->tsNamespaceTarget && assignTarget == AssignTarget::kNone && !isDeleteTarget) {
            if (auto *ns = std::get_if<std::shared_ptr<TSNamespaceMemberNamespace>>(&this->tsNamespaceMemberData); ns != nullptr) {
                auto memberIt = (*ns)->exported_members->find(name);
                if (memberIt != (*ns)->exported_members->end()) {
                    const TSNamespaceMember &member = memberIt->second;
                    if (auto *m = std::get_if<std::shared_ptr<TSNamespaceMemberEnumNumber>>(&member.data); m != nullptr) {
                        this->ignoreUsageOfIdentifierInDotChain(target);
                        return {this->wrapInlinedEnum(Expr{std::make_shared<ENumber>(ENumber{(*m)->value}), loc}, name), true};
                    } else if (auto *mStr = std::get_if<std::shared_ptr<TSNamespaceMemberEnumString>>(&member.data); mStr != nullptr) {
                        this->ignoreUsageOfIdentifierInDotChain(target);
                        return {this->wrapInlinedEnum(Expr{std::make_shared<EString>(EString{(*mStr)->value, logger::Loc{}}), loc}, name), true};
                    } else if (auto *mNs = std::get_if<std::shared_ptr<TSNamespaceMemberNamespace>>(&member.data); mNs != nullptr) {
                        // If this isn't a constant, return a clone of this property access
                        // but with the namespace member data associated with it so that
                        // more property accesses off of this property access are recognized.
                        if (preferQuotedKey || !IsIdentifier(name)) {
                            auto index = std::make_shared<EIndex>();
                            index->target = target;
                            index->index = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(name), logger::Loc{}}), nameLoc};
                            this->tsNamespaceMemberData = member.data;
                            this->tsNamespaceTarget = Get<EIdentifier>(Expr{index, loc}.data);
                            return {Expr{index, loc}, true};
                        } else {
                            E *result = this->dotOrMangledPropVisit(target, name, nameLoc);
                            this->tsNamespaceMemberData = member.data;
                            this->tsNamespaceTarget = Get<EIdentifier>(*result);
                            return {Expr{*result, loc}, true};
                        }
                    }
                }
            }
        }

        // Symbol uses due to a property access off of an imported symbol are tracked
        // specially. This lets us do tree shaking for cross-file TypeScript enums.
        if (this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle && !this->isControlFlowDead) {
            if (auto id = Get<EImportIdentifier>(target.data)) {
                // Remove the normal symbol use
                SymbolUse use = this->symbolUses[id->ref];
                use.count_estimate--;
                if (use.count_estimate == 0) {
                    this->symbolUses.erase(id->ref);
                } else {
                    this->symbolUses[id->ref] = use;
                }

                // Add a special symbol use instead
                SymbolUse propertyUse = this->importSymbolPropertyUses[id->ref][name];
                propertyUse.count_estimate++;
                this->importSymbolPropertyUses[id->ref][name] = propertyUse;
            }
        }

        // Minify "foo".length
        if (this->options.optionsThatSupportStructuralEquality.minifySyntax && assignTarget == AssignTarget::kNone) {
            if (auto str = Get<EString>(target.data)) {
                if (name == "length") {
                    return {Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(str->value.size())}), loc}, true};
                }
            } else if (auto inlined = Get<EInlinedEnum>(target.data)) {
                if (auto s = Get<EString>(inlined->value.data)) {
                    if (name == "length") {
                        return {Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(s->value.size())}), loc}, true};
                    }
                }
            }
        }

        return {Expr{}, false};
    }


    
    // Records that a private name is used in a brand check (e.g. "#foo in obj").
    // When inside an experimental decorator, marks the name for lowering since
    // decorators may need to be emitted outside the class body.
    void Parser::reportPrivateNameUsage(const std::string &name) {
        if (this->parseExperimentalDecoratorNesting > 0) {
            this->lowerAllOfThesePrivateNames[name] = true;
        }
    }




    // Returns true if a string literal contains a sequence like "</script" that could
    // break out of an inline script tag. Used to decide whether template literals
    // containing such strings must be lowered to avoid generating invalid HTML.
    bool containsClosingScriptTag(std::string text) {
        for (;;) {
            size_t i = text.find("</");
            if (i == std::string::npos) {
                break;
            }
            text = text.substr(i + 2);
            if (text.size() >= 6) {
                // strings.EqualFold(text[:6], "script")
                std::string prefix = text.substr(0, 6);
                std::string lower = prefix;
                for (char &c : lower) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (lower == "script") {
                    return true;
                }
            }
        }
        return false;
    }



    // Entry point for the visit pass on a single expression. Delegates to visitExprInOut
    // with default input flags. This is the most common way to visit an expression when
    // the caller doesn't need to track output flags (e.g. optional chain lowering).
    Expr Parser::visitExpr(Expr expr) {
        auto result = this->visitExprInOut(expr, exprIn{});
        return result.first;
    }

    // Visits a function declaration/expression during the visit pass.
    // Saves and restores function-level visit data (async, generator, arrow status).
    // Pushes function-args and function-body scopes, visits arguments, visits body
    // statements (with temp ref prepending), then calls lowerFunction for any needed
    // syntax lowering (async/await, generators, default params, etc.).
    void Parser::visitFn(Fn *fn, logger::Loc scopeLoc, visitFnOpts opts) {
        Scope *decoratorScope = nullptr;
        auto oldFnOrArrowData = this->fnOrArrowDataVisit;
        auto oldFnOnlyData = this->fnOnlyDataVisit;

        this->fnOrArrowDataVisit = {};
        this->fnOrArrowDataVisit.isAsync = fn->is_async;
        this->fnOrArrowDataVisit.isGenerator = fn->is_generator;
        this->fnOrArrowDataVisit.isDerivedClassCtor = opts.isDerivedClassCtor;
        this->fnOrArrowDataVisit.shouldLowerSuperPropertyAccess = (fn->is_async && compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncAwait)) || opts.isLoweredPrivateMethod;

        this->fnOnlyDataVisit = {};
        this->fnOnlyDataVisit.isThisNested = true;
        this->fnOnlyDataVisit.isNewTargetAllowed = true;
        this->fnOnlyDataVisit.argumentsRef = &fn->arguments_ref;

        if (opts.isMethod) {
            decoratorScope = this->propMethodDecoratorScope;
            this->fnOnlyDataVisit.innerClassNameRef = oldFnOnlyData.innerClassNameRef;
            this->fnOnlyDataVisit.isInStaticClassContext = oldFnOnlyData.isInStaticClassContext;
        }

        if (fn->name != nullptr) {
            this->recordDeclaredSymbol(fn->name->ref);
        }

        this->pushScopeForVisitPass(ScopeKind::kFunctionArgs, scopeLoc);
        this->visitArgs(fn->args, visitArgsOpts{
            fn->body.block.stmts,
            decoratorScope,
            fn->has_rest_arg,
            fn->is_unique_formal_parameters,
        });
        this->pushScopeForVisitPass(ScopeKind::kFunctionBody, fn->body.loc);
        if (fn->name != nullptr) {
            this->validateDeclaredSymbolName(fn->name->loc, this->symbols[fn->name->ref.inner_index].original_name);
        }
        fn->body.block.stmts = this->visitStmtsAndPrependTempRefs(fn->body.block.stmts, prependTempRefsOpts{&fn->body.loc, stmtsFnBody});
        this->popScope();
        this->lowerFunction(&fn->is_async, &fn->is_generator, &fn->args, fn->body.loc, &fn->body.block, nullptr, &fn->has_rest_arg, false /* isArrow */);
        this->popScope();

        this->fnOrArrowDataVisit = oldFnOrArrowData;
        this->fnOnlyDataVisit = oldFnOnlyData;
    }

    // Visits function arguments, checking for:
    //   - "use strict" with non-simple parameter list (error)
    //   - Duplicate parameter names in strict mode (error)
    //   - Default values and decorators on each argument
    // Also handles TypeScript parameter decorators.
    void Parser::visitArgs(std::vector<Arg> &args, visitArgsOpts opts) {
        std::unordered_map<std::string, logger::Range> duplicateArgCheckStorage;
        std::unordered_map<std::string, logger::Range> *duplicateArgCheck = nullptr;
        std::pair<logger::Loc, bool> useStrict = fnBodyContainsUseStrict(opts.body);
        logger::Loc useStrictLoc = useStrict.first;
        bool hasUseStrict = useStrict.second;
        bool hasSimpleArgs = isSimpleParameterList(args, opts.hasRestArg);

        // Section 15.2.1 Static Semantics: Early Errors: "It is a Syntax Error if
        // FunctionBodyContainsUseStrict of FunctionBody is true and
        // IsSimpleParameterList of FormalParameters is false."
        if (hasUseStrict && !hasSimpleArgs) {
            this->log.AddError(&this->tracker, this->source.RangeOfString(useStrictLoc),
                logger::FormatMsg(logger::MsgCat::kJS_UseStrictNonSimpleParamList));
        }

        // Section 15.1.1 Static Semantics: Early Errors: "Multiple occurrences of
        // the same BindingIdentifier in a FormalParameterList is only allowed for
        // functions which have simple parameter lists and which are not defined in
        // strict mode code."
        if (opts.isUniqueFormalParameters || hasUseStrict || !hasSimpleArgs || this->isStrictMode()) {
            duplicateArgCheck = &duplicateArgCheckStorage;
        }

        for (int i = 0; i < static_cast<int>(args.size()); i++) {
            Arg &arg = args[static_cast<size_t>(i)];
            arg.decorators = this->visitDecorators(arg.decorators, opts.decoratorScope);
            bindingOpts bindingOpts;
            bindingOpts.duplicateArgCheck = duplicateArgCheck;
            this->visitBinding(arg.binding, bindingOpts);
            if (!IsNil(arg.default_or_nil.data)) {
                arg.default_or_nil = this->visitExpr(arg.default_or_nil);
            }
        }
    }

    // Visits a class declaration/expression during the visit pass.
    // Handles: decorators, class name binding, extends clause, static block scoping,
    // private identifier lowering, property key/value/initializer visiting, computed
    // key visiting with class name binding restrictions, duplicate property detection,
    // name keeping (--keep-names), and super() call shimming.
    // Returns visitClassResult with lowering info and side-effect analysis.
        visitClassResult Parser::visitClass(logger::Loc nameScopeLoc, Class *cls, compiler::Ref defaultNameRef, std::string newNameToKeep) {
        visitClassResult result;

        cls->decorators = this->visitDecorators(cls->decorators, this->currentScope);

        if (cls->name != nullptr) {
            this->recordDeclaredSymbol(cls->name->ref);
            if (this->options.optionsThatSupportStructuralEquality.keepNames) {
                newNameToKeep = this->symbols[cls->name->ref.inner_index].original_name;
            }
        }

        // Replace "this" with a reference to the cls inside static field
        // initializers if static fields are being lowered, since that relocates the
        // field initializers outside of the cls body and "this" will no longer
        // reference the same thing.
        classLoweringInfo loweringInfo = this->computeClassLoweringInfo(cls);
        bool recomputeClassLoweringInfo = false;

        // Sometimes we need to lower private members even though they are supported.
        // This flags them for lowering so that we lower references to them as we
        // traverse the cls body.
        //
        // We don't need to worry about possible references to the cls shadowing
        // symbol inside the cls body changing our decision to lower private members
        // later on because that shouldn't be possible.
        if (loweringInfo.lowerAllStaticFields) {
            for (Property &prop : cls->properties) {
                // We need to lower all private members if fields of that type are lowered,
                // not just private fields (methods and accessors too):
                //
                //   cls Foo {
                //     get #foo() {}
                //     static bar = new Foo().#foo
                //   }
                //
                // We can't transform that to this:
                //
                //   cls Foo {
                //     get #foo() {}
                //   }
                //   Foo.bar = new Foo().#foo;
                //
                // The private getter must be lowered too.
                if (auto priv = Get<EPrivateIdentifier>(prop.key.data)) {
                    this->symbols[priv->ref.inner_index].flags = this->symbols[priv->ref.inner_index].flags | compiler::SymbolFlags::kPrivateSymbolMustBeLowered;
                    recomputeClassLoweringInfo = true;
                }
            }
        }

        // Conservatively lower all private names that have been used in a private
        // brand check anywhere in the file. See the comment on this map for details.
        if (!this->lowerAllOfThesePrivateNames.empty()) {
            for (Property &prop : cls->properties) {
                if (auto priv = Get<EPrivateIdentifier>(prop.key.data)) {
                    compiler::Symbol &symbol = this->symbols[priv->ref.inner_index];
                    if (this->lowerAllOfThesePrivateNames[symbol.original_name]) {
                        symbol.flags = symbol.flags | compiler::SymbolFlags::kPrivateSymbolMustBeLowered;
                        recomputeClassLoweringInfo = true;
                    }
                }
            }
        }

        // If we changed private symbol lowering decisions, then recompute cls
        // lowering info because that may have changed other decisions too
        if (recomputeClassLoweringInfo) {
            loweringInfo = this->computeClassLoweringInfo(cls);
        }

        this->pushScopeForVisitPass(ScopeKind::kClassName, nameScopeLoc);
        logger::Range oldEnclosingClassKeyword = this->enclosingClassKeyword;
        this->enclosingClassKeyword = cls->class_keyword;
        this->currentScope->RecursiveSetStrictMode(StrictModeKind::kImplicitStrictModeClass);
        if (cls->name != nullptr) {
            this->validateDeclaredSymbolName(cls->name->loc, this->symbols[cls->name->ref.inner_index].original_name);
        }

        // Create the "__super" symbol if necessary. This will cause us to replace
        // all "super()" call expressions with a call to this symbol, which will
        // then be inserted into the "constructor" method.
        result.superCtorRef = compiler::kInvalidRef;
        if (loweringInfo.shimSuperCtorCalls) {
            result.superCtorRef = this->newSymbol(compiler::SymbolKind::kOther, "__super");
            this->currentScope->generated.push_back(result.superCtorRef);
            this->recordDeclaredSymbol(result.superCtorRef);
        }
        compiler::Ref oldSuperCtorRef = this->superCtorRef;
        this->superCtorRef = result.superCtorRef;

        // Insert an immutable inner name that spans the whole cls to match
        // JavaScript's semantics specifically the "CreateImmutableBinding" here:
        // https://262.ecma-international.org/6.0/#sec-runtime-semantics-classdefinitionevaluation
        // The cls body (and extends clause) "captures" the original value of the
        // cls name. This matters for cls statements because the symbol can be
        // re-assigned to something else later. The captured values must be the
        // original value of the name, not the re-assigned value. Use "const" for
        // this symbol to match JavaScript run-time semantics. You are not allowed
        // to assign to this symbol (it throws a TypeError).
        if (cls->name != nullptr) {
            std::string name = this->symbols[cls->name->ref.inner_index].original_name;
            result.innerClassNameRef = this->newSymbol(compiler::SymbolKind::kConst, "_" + name);
            this->currentScope->members[name] = ScopeMember{/*ref=*/result.innerClassNameRef, /*loc=*/cls->name->loc};
        } else {
            std::string name = "_this";
            if (defaultNameRef != compiler::kInvalidRef) {
                name = "_" + this->source.identifier_name + "_default";
            }
            result.innerClassNameRef = this->newSymbol(compiler::SymbolKind::kConst, name);
        }
        this->recordDeclaredSymbol(result.innerClassNameRef);

        if (!IsNil(cls->extends_or_nil.data)) {
            cls->extends_or_nil = this->visitExpr(cls->extends_or_nil);
        }

        // A scope is needed for private identifiers
        this->pushScopeForVisitPass(ScopeKind::kClassBody, cls->body_loc);
        result.bodyScope = this->currentScope;

        for (Property &property : cls->properties) {
            if (property.kind == PropertyKind::kClassStaticBlock) {
                auto oldFnOrArrowData = this->fnOrArrowDataVisit;
                auto oldFnOnlyDataVisit = this->fnOnlyDataVisit;

                this->fnOrArrowDataVisit = {};
                this->fnOnlyDataVisit = {};
                this->fnOnlyDataVisit.isThisNested = true;
                this->fnOnlyDataVisit.isNewTargetAllowed = true;
                this->fnOnlyDataVisit.isInStaticClassContext = true;
                this->fnOnlyDataVisit.innerClassNameRef = &result.innerClassNameRef;

                if (loweringInfo.lowerAllStaticFields) {
                    // Need to lower "this" and "super" since they won't be valid outside the class body
                    this->fnOnlyDataVisit.shouldReplaceThisWithInnerClassNameRef = true;
                    this->fnOrArrowDataVisit.shouldLowerSuperPropertyAccess = true;
                }

                this->pushScopeForVisitPass(ScopeKind::kClassStaticInit, property.class_static_block->loc);

                // Make it an error to use "arguments" in a static class block
                this->currentScope->forbid_arguments = true;

                property.class_static_block->block.stmts = this->visitStmts(property.class_static_block->block.stmts, stmtsFnBody);
                this->popScope();

                this->fnOrArrowDataVisit = oldFnOrArrowData;
                this->fnOnlyDataVisit = oldFnOnlyDataVisit;
                continue;
            }

            property.decorators = this->visitDecorators(property.decorators, result.bodyScope);

            // Visit the property key
            if (auto priv = Get<EPrivateIdentifier>(property.key.data)) {
                // Special-case private identifiers here
                this->recordDeclaredSymbol(priv->ref);
            } else {
                // It's forbidden to reference the cls name in a computed key
                if (Has(property.flags, PropertyFlags::kIsComputed) && cls->name != nullptr) {
                    this->symbols[result.innerClassNameRef.inner_index].kind = compiler::SymbolKind::kClassInComputedPropertyKey;
                }

                exprIn keyIn{};
                keyIn.shouldMangleStringsAsProps = true;
                std::pair<Expr, exprOut> keyResult = this->visitExprInOut(property.key, keyIn);
                property.key = keyResult.first;

                // Re-allow using the cls name after visiting a computed key
                if (Has(property.flags, PropertyFlags::kIsComputed) && cls->name != nullptr) {
                    this->symbols[result.innerClassNameRef.inner_index].kind = compiler::SymbolKind::kConst;
                }

                if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                    if (auto inlined = Get<EInlinedEnum>(property.key.data)) {
                        if (Get<EString>(inlined->value.data) != nullptr || Get<ENumber>(inlined->value.data) != nullptr) {
                            property.key.data = inlined->value.data;
                        }
                    }
                    if (Get<ENumber>(property.key.data) != nullptr || Get<ENameOfSymbol>(property.key.data) != nullptr) {
                        // "cls { [123] }" => "cls { 123 }"
                        property.flags = static_cast<PropertyFlags>(static_cast<uint8_t>(property.flags) & ~static_cast<uint8_t>(PropertyFlags::kIsComputed));
                    } else if (auto str = Get<EString>(property.key.data)) {
                        auto numberValue = StringToEquivalentNumberValue(str->value);
                        bool isStringNumber = numberValue && *numberValue >= 0;
                        if (isStringNumber) {
                            // "cls { '123' }" => "cls { 123 }"
                            auto num = std::make_shared<ENumber>();
                            num->value = *numberValue;
                            property.key.data = num;
                            property.flags = static_cast<PropertyFlags>(static_cast<uint8_t>(property.flags) & ~static_cast<uint8_t>(PropertyFlags::kIsComputed));
                        } else if (Has(property.flags, PropertyFlags::kIsComputed)) {
                            // "cls {['x'] = y}" => "cls {'x' = y}"
                            bool isInvalidConstructor = false;
                            if (helpers::UTF16EqualsString(str->value, "constructor")) {
                                if (!IsMethodDefinition(property.kind)) {
                                    // "constructor" is an invalid name for both instance and static fields
                                    isInvalidConstructor = true;
                                } else if (!Has(property.flags, PropertyFlags::kIsStatic)) {
                                    // Calling an instance method "constructor" is problematic so avoid that too
                                    isInvalidConstructor = true;
                                }
                            }

                            // A static property must not be called "prototype"
                            bool isInvalidPrototype = Has(property.flags, PropertyFlags::kIsStatic) && helpers::UTF16EqualsString(str->value, "prototype");

                            if (!isInvalidConstructor && !isInvalidPrototype) {
                                property.flags = static_cast<PropertyFlags>(static_cast<uint8_t>(property.flags) & ~static_cast<uint8_t>(PropertyFlags::kIsComputed));
                            }
                        }
                    }
                }
            }

            // Make it an error to use "arguments" in a cls body
            this->currentScope->forbid_arguments = true;

            // The value of "this" and "super" is shadowed inside property values
            auto oldFnOnlyDataVisit = this->fnOnlyDataVisit;
            bool oldShouldLowerSuperPropertyAccess = this->fnOrArrowDataVisit.shouldLowerSuperPropertyAccess;
            this->fnOrArrowDataVisit.shouldLowerSuperPropertyAccess = false;
            this->fnOnlyDataVisit.shouldReplaceThisWithInnerClassNameRef = false;
            this->fnOnlyDataVisit.isThisNested = true;
            this->fnOnlyDataVisit.isNewTargetAllowed = true;
            this->fnOnlyDataVisit.isInStaticClassContext = Has(property.flags, PropertyFlags::kIsStatic);
            this->fnOnlyDataVisit.innerClassNameRef = &result.innerClassNameRef;

            // We need to explicitly assign the name to the property initializer if it
            // will be transformed such that it is no longer an inline initializer.
            std::string propNameToKeep = "";
            bool isLoweredPrivateMethod = false;
            if (auto priv = Get<EPrivateIdentifier>(property.key.data)) {
                if (!IsMethodDefinition(property.kind) || this->privateSymbolNeedsToBeLowered(priv)) {
                    propNameToKeep = this->symbols[priv->ref.inner_index].original_name;
                }

                // Lowered private methods (both instance and static) are initialized
                // outside of the cls body, so we must rewrite "super" property
                // accesses inside them. Lowered private instance fields are initialized
                // inside the constructor where "super" is valid, so those don't need to
                // be rewritten.
                if (IsMethodDefinition(property.kind) && this->privateSymbolNeedsToBeLowered(priv)) {
                    isLoweredPrivateMethod = true;
                }
            } else if (!IsMethodDefinition(property.kind) && !Has(property.flags, PropertyFlags::kIsComputed)) {
                if (auto str = Get<EString>(property.key.data)) {
                    propNameToKeep = helpers::UTF16ToString(str->value);
                }
            }

            // Handle methods
            if (!IsNil(property.value_or_nil.data)) {
                this->propMethodDecoratorScope = result.bodyScope;

                // Propagate the name to keep from the method into the initializer
                if (!propNameToKeep.empty()) {
                    this->nameToKeep = propNameToKeep;
                    this->nameToKeepIsFor =property.value_or_nil.data;
                }

                // Propagate whether we're in a derived cls constructor
                if (!IsNil(cls->extends_or_nil.data) && !Has(property.flags, PropertyFlags::kIsComputed)) {
                    if (auto str = Get<EString>(property.key.data)) {
                        if (helpers::UTF16EqualsString(str->value, "constructor")) {
                            this->propDerivedCtorValue = &property.value_or_nil.data;
                        }
                    }
                }

                exprIn valueIn{};
                valueIn.isMethod = true;
                valueIn.isLoweredPrivateMethod = isLoweredPrivateMethod;
                std::pair<Expr, exprOut> valueResult = this->visitExprInOut(property.value_or_nil, valueIn);
                property.value_or_nil = valueResult.first;
            }

            // Handle initialized fields
            if (!IsNil(property.initializer_or_nil.data)) {
                if (Has(property.flags, PropertyFlags::kIsStatic) && loweringInfo.lowerAllStaticFields) {
                    // Need to lower "this" and "super" since they won't be valid outside the cls body
                    this->fnOnlyDataVisit.shouldReplaceThisWithInnerClassNameRef = true;
                    this->fnOrArrowDataVisit.shouldLowerSuperPropertyAccess = true;
                }

                // Propagate the name to keep from the field into the initializer
                if (!propNameToKeep.empty()) {
                    this->nameToKeep = propNameToKeep;
                    this->nameToKeepIsFor =property.initializer_or_nil.data;
                }

                property.initializer_or_nil = this->visitExpr(property.initializer_or_nil);
            }

            // Restore "this" so it will take the inherited value in property keys
            this->fnOnlyDataVisit = oldFnOnlyDataVisit;
            this->fnOrArrowDataVisit.shouldLowerSuperPropertyAccess = oldShouldLowerSuperPropertyAccess;

            // Restore the ability to use "arguments" in decorators and computed properties
            this->currentScope->forbid_arguments = false;
        }

        // Check for and warn about duplicate keys in cls bodies
        if (!this->suppressWarningsAboutWeirdCode) {
            this->warnAboutDuplicateProperties(cls->properties, duplicatePropertiesInClass);
        }

        // Analyze side effects before adding the name keeping call
        result.canBeRemovedIfUnused = this->astHelpers.ClassCanBeRemovedIfUnused(*cls);

        // Implement name keeping using a static block at the start of the cls body
        if (this->options.optionsThatSupportStructuralEquality.keepNames && !newNameToKeep.empty()) {
            bool propertyPreventsKeepNames = false;
            for (const Property &prop : cls->properties) {
                // A static property called "name" shadows the automatically-generated name
                if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                    if (auto str = Get<EString>(prop.key.data)) {
                        if (helpers::UTF16EqualsString(str->value, "name")) {
                            propertyPreventsKeepNames = true;
                            break;
                        }
                    }
                }
            }
            if (!propertyPreventsKeepNames) {
                Expr thisExpr;
                if (loweringInfo.lowerAllStaticFields) {
                    this->recordUsage(result.innerClassNameRef);
                    auto identifier = std::make_shared<EIdentifier>();
                    identifier->ref = result.innerClassNameRef;
                    thisExpr = Expr{identifier, cls->body_loc};
                } else {
                    thisExpr = Expr{kEThisShared, cls->body_loc};
                }
                Property first;
                first.kind = PropertyKind::kClassStaticBlock;
                auto block = std::make_shared<ClassStaticBlock>();
                block->loc = cls->body_loc;
                block->block.stmts.push_back(this->keepClassOrFnSymbolName(cls->body_loc, thisExpr, newNameToKeep));
                first.class_static_block = block;
                std::vector<Property> properties;
                properties.reserve(1 + cls->properties.size());
                properties.push_back(first);
                properties.insert(properties.end(), cls->properties.begin(), cls->properties.end());
                cls->properties = properties;
            }
        }

        this->enclosingClassKeyword = oldEnclosingClassKeyword;
        this->superCtorRef = oldSuperCtorRef;
        this->popScope();

        if (this->symbols[result.innerClassNameRef.inner_index].use_count_estimate == 0) {
            // Don't generate a shadowing name if one isn't needed
            result.innerClassNameRef = compiler::kInvalidRef;
        } else if (cls->name == nullptr) {
            // If there was originally no cls name but something inside needed one
            // (e.g. there was a static property initializer that referenced "this"),
            // populate the cls name. If this is an "export default cls" statement,
            // use the existing default name so that things will work as expected if
            // this is turned into a regular cls statement later on.
            compiler::Ref classNameRef = defaultNameRef;
            if (classNameRef == compiler::kInvalidRef) {
                classNameRef = this->newSymbol(compiler::SymbolKind::kOther, "_this");
                this->currentScope->generated.push_back(classNameRef);
                this->recordDeclaredSymbol(classNameRef);
            }
            cls->name = std::make_shared<compiler::LocRef>(compiler::LocRef{nameScopeLoc, classNameRef});
        }

        this->popScope();

        // Sanity check that the cls lowering info hasn't changed before and after
        // visiting. The cls transform relies on this because lowering assumes that
        // must be able to expect that visiting has done certain things.
        classLoweringInfo newLoweringInfo = this->computeClassLoweringInfo(cls);
        if (loweringInfo.lowerAllInstanceFields != newLoweringInfo.lowerAllInstanceFields ||
            loweringInfo.lowerAllStaticFields != newLoweringInfo.lowerAllStaticFields ||
            loweringInfo.shimSuperCtorCalls != newLoweringInfo.shimSuperCtorCalls) {
            throw std::runtime_error("Internal error");
        }

        return result;
    }

    // Visits a list of statements, handling the full statement visitation pipeline:
    //   1. Preprocesses TypeScript enums for forward-reference inlining
    //   2. Hoists block-level function declarations
    //   3. Visits each statement via visitAndAppendStmt
    //   4. Prepends temporary variable declarations (for captured this/arguments)
    //   5. Transforms block-level function declarations into let/var statements
    //   6. Lowers "using" declarations if present
    //   7. Mangles/minifies statements when minification is enabled
    //   8. Strips dead code in dead control flow branches
    std::vector<Stmt> Parser::visitStmts(std::vector<Stmt> stmts, stmtsKind kind) {
        // Save the current control-flow liveness. This represents if we are
        // currently inside an "if (false) { ... }" block.
        bool oldIsControlFlowDead = this->isControlFlowDead;

        std::vector<compiler::Ref> oldTempLetsToDeclare = this->tempLetsToDeclare;
        this->tempLetsToDeclare = {};

        // Visit all statements first
        std::vector<Stmt> visited;
        visited.reserve(stmts.size());
        std::vector<Stmt> before;
        std::vector<Stmt> after;
        std::unordered_map<int, std::vector<Stmt>> preprocessedEnums;
        if (!this->scopesInOrderForEnum.empty()) {
            // Preprocess TypeScript enums to improve code generation. Otherwise
            // uses of an enum before that enum has been declared won't be inlined:
            //
            //   console.log(Foo.FOO) // We want "FOO" to be inlined here
            //   const enum Foo { FOO = 0 }
            //
            // The TypeScript compiler itself contains code with this pattern, so
            // it's important to implement this optimization.
            for (int i = 0; i < static_cast<int>(stmts.size()); i++) {
                if (Get<SEnum>(stmts[static_cast<size_t>(i)].data) != nullptr) {
                    std::vector<scopeOrder> oldScopesInOrder = this->scopesInOrder;
                    this->scopesInOrder = this->scopesInOrderForEnum[stmts[static_cast<size_t>(i)].loc];
                    preprocessedEnums[i] = this->visitAndAppendStmt(std::vector<Stmt>{}, stmts[static_cast<size_t>(i)]);
                    this->scopesInOrder = oldScopesInOrder;
                }
            }
        }
        for (size_t i = 0; i < stmts.size(); i++) {
            Stmt stmt = stmts[i];

            if (auto s = Get<SExportEquals>(stmt.data); s != nullptr) {
                // TypeScript "export = value;" becomes "module.exports = value;". This
                // must happen at the end after everything is parsed because TypeScript
                // moves this statement to the end when it generates code.
                after = this->visitAndAppendStmt(after, stmt);
                continue;
            }

            if (auto s = Get<SFunction>(stmt.data); s != nullptr) {
                // Manually hoist block-level function declarations to preserve semantics.
                // This is only done for function declarations that are not generators
                // or async functions, since this is a backwards-compatibility hack from
                // Annex B of the JavaScript standard.
                if (!StopsHoisting(this->currentScope->kind) && this->symbols[s->fn.name->ref.inner_index].kind == compiler::SymbolKind::kHoistedFunction) {
                    before = this->visitAndAppendStmt(before, stmt);
                    continue;
                }
            }

            if (Get<SEnum>(stmt.data) != nullptr) {
                auto preprocessed = preprocessedEnums.find(static_cast<int>(i));
                if (preprocessed != preprocessedEnums.end()) {
                    visited.insert(visited.end(), preprocessed->second.begin(), preprocessed->second.end());
                }
                auto scopes = this->scopesInOrderForEnum.find(stmt.loc);
                if (scopes != this->scopesInOrderForEnum.end()) {
                    this->scopesInOrder.erase(this->scopesInOrder.begin(), this->scopesInOrder.begin() + static_cast<long>(scopes->second.size()));
                }
                continue;
            }

            visited = this->visitAndAppendStmt(visited, stmt);
        }

        // This is used for temporary variables that could be captured in a closure,
        // and therefore need to be generated inside the nearest enclosing block in
        // case they are generated inside a loop.
        if (!this->tempLetsToDeclare.empty()) {
            std::vector<Decl> decls;
            decls.reserve(this->tempLetsToDeclare.size());
            for (compiler::Ref &ref : this->tempLetsToDeclare) {
                auto b = std::make_shared<BIdentifier>();
                b->ref = ref;
                decls.push_back(Decl{/*binding=*/Binding{b, logger::Loc{}}, /*valueOrNil=*/Expr{}});
            }
            auto local = std::make_shared<SLocal>();
            local->kind = LocalKind::kLet;
            local->decls = decls;
            before.push_back(Stmt{local, logger::Loc{}});
        }
        this->tempLetsToDeclare = oldTempLetsToDeclare;

        // Transform block-level function declarations into variable declarations
        if (!before.empty()) {
            std::vector<Decl> letDecls;
            std::vector<Decl> varDecls;
            std::vector<Stmt> nonFnStmts;
            std::unordered_map<compiler::Ref, int, RefHash> fnStmts;
            for (Stmt stmt : before) {
                auto s = Get<SFunction>(stmt.data);
                if (s == nullptr) {
                    // We may get non-function statements here in certain scenarios such as when "KeepNames" is enabled
                    nonFnStmts.push_back(stmt);
                    continue;
                }

                // This transformation of function declarations in nested scopes is
                // intended to preserve the hoisting semantics of the original code. In
                // JavaScript, function hoisting works differently in strict mode vs.
                // sloppy mode code. We want the code we generate to use the semantics of
                // the original environment, not the generated environment. However, if
                // direct "eval" is present then it's not possible to preserve the
                // semantics because we need two identifiers to do that and direct "eval"
                // means neither identifier can be renamed to something else. So in that
                // case we give up and do not preserve the semantics of the original code.
                if (this->currentScope->contains_direct_eval) {
                    auto hoistedIt = this->hoistedRefForSloppyModeBlockFn.find(s->fn.name->ref);
                    if (hoistedIt != this->hoistedRefForSloppyModeBlockFn.end()) {
                        // Merge the two identifiers back into a single one
                        this->symbols[hoistedIt->second.inner_index].link = s->fn.name->ref;
                    }
                    nonFnStmts.push_back(stmt);
                    continue;
                }

                auto fnIt = fnStmts.find(s->fn.name->ref);
                int index;
                if (fnIt == fnStmts.end()) {
                    index = static_cast<int>(letDecls.size());
                    fnStmts[s->fn.name->ref] = index;
                    auto b = std::make_shared<BIdentifier>();
                    b->ref = s->fn.name->ref;
                    letDecls.push_back(Decl{/*binding=*/Binding{b, s->fn.name->loc}, /*valueOrNil=*/Expr{}});

                    // Also write the function to the hoisted sibling symbol if applicable
                    auto hoistedIt = this->hoistedRefForSloppyModeBlockFn.find(s->fn.name->ref);
                    if (hoistedIt != this->hoistedRefForSloppyModeBlockFn.end()) {
                        compiler::Ref hoistedRef = hoistedIt->second;
                        this->recordDeclaredSymbol(hoistedRef);
                        this->recordUsage(s->fn.name->ref);
                        auto hb = std::make_shared<BIdentifier>();
                        hb->ref = hoistedRef;
                        auto he = std::make_shared<EIdentifier>();
                        he->ref = s->fn.name->ref;
                        varDecls.push_back(Decl{/*binding=*/Binding{hb, s->fn.name->loc}, /*valueOrNil=*/Expr{he, s->fn.name->loc}});
                    }
                } else {
                    index = fnIt->second;
                }

                // The last function statement for a given symbol wins
                s->fn.name = nullptr;
                auto fnExpr = std::make_shared<EFunction>();
                fnExpr->fn = s->fn;
                letDecls[static_cast<size_t>(index)].value_or_nil = Expr{fnExpr, stmt.loc};
            }

            // Reuse memory from "before"
            before.clear();
            LocalKind kindStmt = LocalKind::kLet;
            if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kConstAndLet)) {
                kindStmt = LocalKind::kVar;
            }
            if (!letDecls.empty()) {
                auto local = std::make_shared<SLocal>();
                local->kind = kindStmt;
                local->decls = letDecls;
                before.push_back(Stmt{local, letDecls[0].value_or_nil.loc});
            }
            if (!varDecls.empty()) {
                // Potentially relocate "var" declarations to the top level
                std::pair<Stmt, bool> assign = this->maybeRelocateVarsToTopLevel(varDecls, relocateVarsMode::relocateVarsNormal);
                if (assign.second) {
                    if (!IsNil(assign.first.data)) {
                        before.push_back(assign.first);
                    }
                } else {
                    auto local = std::make_shared<SLocal>();
                    local->kind = LocalKind::kVar;
                    local->decls = varDecls;
                    before.push_back(Stmt{local, varDecls[0].value_or_nil.loc});
                }
            }
            before.insert(before.end(), nonFnStmts.begin(), nonFnStmts.end());
            visited.insert(visited.begin(), before.begin(), before.end());
        }

        // Move TypeScript "export =" statements to the end
        visited.insert(visited.end(), after.begin(), after.end());

        // Restore the current control-flow liveness if it was changed inside the
        // loop above. This is important because the caller will not restore it.
        this->isControlFlowDead = oldIsControlFlowDead;

        if (this->shouldLowerUsingDeclarations(visited)) {
            struct lowerUsingDeclarationContext ctx = this->lowerUsingDeclarationContext();
            ctx.scanStmts(this, visited);
            visited = ctx.finalize(this, visited, this->currentScope->parent == nullptr);
        }

        // Stop now if we're not mangling
        if (!this->options.optionsThatSupportStructuralEquality.minifySyntax) {
            return visited;
        }
        if (this->isControlFlowDead) {
            int end = 0;
            for (Stmt stmt : visited) {
                if (!shouldKeepStmtInDeadControlFlow(stmt)) {
                    continue;
                }

                // Merge adjacent var statements
                if (auto sLocal = Get<SLocal>(stmt.data); sLocal != nullptr && sLocal->kind == LocalKind::kVar && end > 0) {
                    Stmt &prevStmt = visited[static_cast<size_t>(end) - 1];
                    if (auto prevS = Get<SLocal>(prevStmt.data); prevS != nullptr && prevS->kind == LocalKind::kVar && sLocal->is_export == prevS->is_export) {
                        prevS->decls.insert(prevS->decls.end(), sLocal->decls.begin(), sLocal->decls.end());
                        continue;
                    }
                }

                visited[static_cast<size_t>(end)] = stmt;
                end++;
            }
            visited.resize(static_cast<size_t>(end));
            return visited;
        }

        return this->mangleStmts(visited, kind);
    }


    // Visits decorator expressions, temporarily reverting to the scope enclosing the
    // class declaration so generated code is inserted in the correct location.
    // Parameter decorators need this to avoid being inside the argument list scope.
    std::vector<Decorator> Parser::visitDecorators(std::vector<Decorator> decorators, Scope *decoratorScope) {
        if (!decorators.empty()) {
            // Decorators cause us to temporarily revert to the scope that encloses the
            // class declaration, since that's where the generated code for decorators
            // will be inserted. I believe this currently only matters for parameter
            // decorators, where the scope should not be within the argument list.
            Scope *oldScope = this->currentScope;
            this->currentScope = decoratorScope;

            for (size_t i = 0; i < decorators.size(); i++) {
                decorators[i].value = this->visitExpr(decorators[i].value);
            }

            // Avoid "popScope" because this decorator scope is not hierarchical
            this->currentScope = oldScope;
        }

        return decorators;
    }   

    // Handles identifier resolution during the visit pass. This is the core of symbol
    // resolution and performs many tasks:
    //   - Substitutes inlined constant values for const variables
    //   - Captures "arguments" for arrow functions in unsupported environments
    //   - Errors on assignment to import namespaces
    //   - Resolves namespace aliases (TypeScript namespace imports)
    //   - Converts import items to EImportIdentifier nodes
    //   - Handles TypeScript namespace member references
    //   - Substitutes global "require" with the runtime require stub
    //   - Marks mutated symbols as potentially mutated
    //   - Returns the resolved identifier node
    Expr Parser::handleIdentifier(logger::Loc loc, std::shared_ptr<EIdentifier> node, identifierOpts opts) {
        EIdentifier *e = node.get();
        compiler::Ref ref = e->ref;

        // Substitute inlined constants
        if (this->options.optionsThatSupportStructuralEquality.minifySyntax && !this->currentScope->contains_direct_eval) {
            auto valueIt = this->constValues.find(ref);
            if (valueIt != this->constValues.end()) {
                this->ignoreUsage(ref);
                return ConstValueToExpr(loc, valueIt->second);
            }
        }

        // Capture the "arguments" variable if necessary
        if (this->fnOnlyDataVisit.argumentsRef != nullptr && ref == *this->fnOnlyDataVisit.argumentsRef) {
            bool isInsideUnsupportedArrow = this->fnOrArrowDataVisit.isArrow && compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kArrow);
            bool isInsideUnsupportedAsyncArrow = this->fnOnlyDataVisit.isInsideAsyncArrowFn && compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncAwait);
            if (isInsideUnsupportedArrow || isInsideUnsupportedAsyncArrow) {
                return Expr{std::make_shared<EIdentifier>(EIdentifier{this->captureArguments()}), loc};
            }
        }

        // Create an error for assigning to an import namespace
        if ((opts.assignTarget != AssignTarget::kNone ||
            (opts.isDeleteTarget && this->symbols[ref.inner_index].import_item_status == compiler::ImportItemStatus::kGenerated)) &&
            this->symbols[ref.inner_index].kind == compiler::SymbolKind::kImport) {
            logger::Range r = RangeOfIdentifier(this->source, loc);

            // Try to come up with a setter name to try to make this message more understandable
            std::string setterHint;
            std::string originalName = this->symbols[ref.inner_index].original_name;
            if (IsIdentifier(originalName) && originalName != "_") {
                if (originalName.size() == 1 || (originalName.size() > 1 && static_cast<uint8_t>(originalName[0]) < 0x80)) {
                    std::string upperFirst(1, static_cast<char>(std::toupper(static_cast<unsigned char>(originalName[0]))));
                    setterHint = " (e.g. \"set" + upperFirst + originalName.substr(1) + "\")";
                } else {
                    setterHint = " (e.g. \"set_" + originalName + "\")";
                }
            }

            logger::MsgData note;
            note.text = logger::FormatMsg(logger::MsgCat::kJS_AssignToImportThrowNote, setterHint);

            if (this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle) {
                this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_CannotAssignToImport, originalName), std::vector<logger::MsgData>{note});
            } else {
                logger::MsgKind kind = logger::MsgKind::kWarning;
                if (this->suppressWarningsAboutWeirdCode) {
                    kind = logger::MsgKind::kDebug;
                }
                this->log.AddIDWithNotes(logger::MsgID::kJS_AssignToImport, kind, &this->tracker, r,
                    "This assignment will throw because \"" + originalName + "\" is an import", std::vector<logger::MsgData>{note});
            }
        }

        // Substitute an EImportIdentifier now if this has a namespace alias
        if (opts.assignTarget == AssignTarget::kNone && !opts.isDeleteTarget) {
            compiler::Symbol &symbol = this->symbols[ref.inner_index];
            if (compiler::NamespaceAlias *nsAlias = symbol.namespace_alias; nsAlias != nullptr) {
                E *data = this->dotOrMangledPropVisit(
                    Expr{std::make_shared<EIdentifier>(EIdentifier{nsAlias->namespace_ref}), loc},
                    symbol.original_name, loc);

                // Handle references to namespaces or namespace members
                auto tsIt = this->refToTSNamespaceMemberData.find(nsAlias->namespace_ref);
                if (tsIt != this->refToTSNamespaceMemberData.end()) {
                    if (auto *ns = std::get_if<std::shared_ptr<TSNamespaceMemberNamespace>>(&tsIt->second); ns != nullptr) {
                        auto memberIt = (*ns)->exported_members->find(nsAlias->alias);
                        if (memberIt != (*ns)->exported_members->end()) {
                            if (std::holds_alternative<std::shared_ptr<TSNamespaceMemberEnumNumber>>(memberIt->second.data)) {
                                std::shared_ptr<TSNamespaceMemberEnumNumber> m = std::get<std::shared_ptr<TSNamespaceMemberEnumNumber>>(memberIt->second.data);
                                return this->wrapInlinedEnum(Expr{std::make_shared<ENumber>(ENumber{m->value}), loc}, nsAlias->alias);
                            } else if (std::holds_alternative<std::shared_ptr<TSNamespaceMemberEnumString>>(memberIt->second.data)) {
                                std::shared_ptr<TSNamespaceMemberEnumString> m = std::get<std::shared_ptr<TSNamespaceMemberEnumString>>(memberIt->second.data);
                                return this->wrapInlinedEnum(Expr{std::make_shared<EString>(EString{m->value, logger::Loc{}}), loc}, nsAlias->alias);
                            } else if (std::holds_alternative<std::shared_ptr<TSNamespaceMemberNamespace>>(memberIt->second.data)) {
                                this->tsNamespaceTarget = Get<EIdentifier>(*data);
                                this->tsNamespaceMemberData = memberIt->second.data;
                            }
                        }
                    }
                }

                return Expr{*data, loc};
            }
        }

        // Substitute an EImportIdentifier now if this is an import item
        if (this->isImportItem.count(ref) > 0) {
            std::shared_ptr<EImportIdentifier> importIdent = std::make_shared<EImportIdentifier>();
            importIdent->ref = ref;
            importIdent->prefer_quoted_key = opts.preferQuotedKey;
            importIdent->was_originally_identifier = opts.wasOriginallyIdentifier;
            return Expr{importIdent, loc};
        }

        // Handle references to namespaces or namespace members
        if (auto tsIt = this->refToTSNamespaceMemberData.find(ref); tsIt != this->refToTSNamespaceMemberData.end()) {
            if (std::holds_alternative<std::shared_ptr<TSNamespaceMemberEnumNumber>>(tsIt->second)) {
                std::shared_ptr<TSNamespaceMemberEnumNumber> m = std::get<std::shared_ptr<TSNamespaceMemberEnumNumber>>(tsIt->second);
                return this->wrapInlinedEnum(Expr{std::make_shared<ENumber>(ENumber{m->value}), loc}, this->symbols[ref.inner_index].original_name);
            } else if (std::holds_alternative<std::shared_ptr<TSNamespaceMemberEnumString>>(tsIt->second)) {
                std::shared_ptr<TSNamespaceMemberEnumString> m = std::get<std::shared_ptr<TSNamespaceMemberEnumString>>(tsIt->second);
                return this->wrapInlinedEnum(Expr{std::make_shared<EString>(EString{m->value, logger::Loc{}}), loc}, this->symbols[ref.inner_index].original_name);
            } else if (std::holds_alternative<std::shared_ptr<TSNamespaceMemberNamespace>>(tsIt->second)) {
                this->tsNamespaceTarget = e;
                this->tsNamespaceMemberData = tsIt->second;
            }
        }

        // Substitute a namespace export reference now if appropriate
        if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
            auto nsExportIt = this->isExportedInsideNamespace.find(ref);
            if (nsExportIt != this->isExportedInsideNamespace.end()) {
                compiler::Ref nsRef = nsExportIt->second;
                std::string name = this->symbols[ref.inner_index].original_name;

                // Otherwise, create a property access on the namespace
                this->recordUsage(nsRef);
                E *propertyAccess = this->dotOrMangledPropVisit(Expr{std::make_shared<EIdentifier>(EIdentifier{nsRef}), loc}, name, loc);
                if (this->tsNamespaceTarget == e) {
                    this->tsNamespaceTarget = Get<EIdentifier>(*propertyAccess);
                }
                return Expr{*propertyAccess, loc};
            }
        }

        // Swap references to the global "require" function with our "__require" stub
        if (ref == this->requireRef && !opts.isCallTarget) {
            if (this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle && this->source.index != 0 && (IsNil(this->dotOrIndexTarget) || Get<EIdentifier>(this->dotOrIndexTarget) != e)) {
                this->log.AddID(logger::MsgID::kJS_IndirectRequire, logger::MsgKind::kDebug, &this->tracker, RangeOfIdentifier(this->source, loc),
                    std::string(logger::MsgTemplate(logger::MsgCat::kJS_IndirectRequire)));
            }

            return this->valueToSubstituteForRequire(loc);
        }

        // Mark any mutated symbols as mutable
        if (opts.assignTarget != AssignTarget::kNone) {
            this->symbols[e->ref.inner_index].flags = this->symbols[e->ref.inner_index].flags | compiler::SymbolFlags::kCouldPotentiallyBeMutated;
        }

        return Expr{node, loc};
    }

    // Resolves "this" references based on the current context:
    //   - Inside static class initializers: replaced with inner class name reference
    //   - Top-level in ESM: replaced with "undefined" (with warning)
    //   - Top-level in CommonJS: replaced with "exports"
    //   - User-specified defines for "this": substituted as configured
    // Returns the replacement expression and true if a substitution was made.
    std::pair<Expr, bool> Parser::valueForThis(
        logger::Loc loc,
        bool shouldLog,
        AssignTarget assignTarget,
        bool isCallTarget,
        bool isDeleteTarget)
    {
        // Substitute "this" if we're inside a static class context
        if (this->fnOnlyDataVisit.shouldReplaceThisWithInnerClassNameRef) {
            this->recordUsage(*this->fnOnlyDataVisit.innerClassNameRef);
            return {Expr{std::make_shared<EIdentifier>(EIdentifier{*this->fnOnlyDataVisit.innerClassNameRef}), loc}, true};
        }

        // Is this a top-level use of "this"?
        if (!this->fnOnlyDataVisit.isThisNested) {
            // Substitute user-specified defines
            auto definesIt = this->options.defines->IdentifierDefines.find("this");
            if (definesIt != this->options.defines->IdentifierDefines.end()) {
                if (definesIt->second.DefineExprData != nullptr) {
                    return {this->instantiateDefineExpr(loc, *definesIt->second.DefineExprData, identifierOpts{
                        assignTarget,
                        isCallTarget,
                        isDeleteTarget,
                        false, /* preferQuotedKey */
                        false, /* wasOriginallyIdentifier */
                        false, /* matchAgainstDefines */
                    }), true};
                }
            }

            // Otherwise, replace top-level "this" with either "undefined" or "exports"
            if (this->isFileConsideredToHaveESMExports) {
                // Warn about "this" becoming undefined, but only once per file
                if (shouldLog && !this->messageAboutThisIsUndefined && !this->fnOnlyDataVisit.silenceMessageAboutThisBeingUndefined) {
                    this->messageAboutThisIsUndefined = true;
                    logger::MsgKind kind = logger::MsgKind::kDebug;
                    logger::MsgData data = this->tracker.MakeMsgData(RangeOfIdentifier(this->source, loc),
                        std::string(logger::MsgTemplate(logger::MsgCat::kJS_TopLevelThisUndefined)));
                    data.location->suggestion = "undefined";
                    auto why = this->whyESModule();
                    logger::Msg msg;
                    msg.kind = kind;
                    msg.data = data;
                    msg.notes = why.second;
                    this->log.AddMsgID(logger::MsgID::kJS_ThisIsUndefinedInESM, msg);
                }

                // In an ES6 module, "this" is supposed to be undefined. Instead of
                // doing this at runtime using "fn.call(undefined)", we do it at
                // compile time using expression substitution here.
                return {Expr{kEUndefinedShared, loc}, true};
            } else if (this->options.optionsThatSupportStructuralEquality.mode != config::Mode::kPassThrough) {
                // In a CommonJS module, "this" is supposed to be the same as "exports".
                // Instead of doing this at runtime using "fn.call(module.exports)", we
                // do it at compile time using expression substitution here.
                this->recordUsage(this->exportsRef);
                return {Expr{std::make_shared<EIdentifier>(EIdentifier{this->exportsRef}), loc}, true};
            }
        }

        return {Expr{}, false};
    }

    // Resolves "import.meta" references. If the target doesn't support import.meta
    // or the output format isn't ESM, replaces it with a reference to an injected
    // "__import_meta" variable. Generates the variable lazily on first use.
    std::pair<Expr, bool> Parser::valueForImportMeta(logger::Loc loc) {
        if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kImportMeta) ||
            (this->options.optionsThatSupportStructuralEquality.mode != config::Mode::kPassThrough && !config::FormatKeepESMImportExportSyntax(this->options.optionsThatSupportStructuralEquality.outputFormat))) {
            // Generate the variable if it doesn't exist yet
            if (this->importMetaRef == compiler::kInvalidRef) {
                this->importMetaRef = this->newSymbol(compiler::SymbolKind::kOther, "import_meta");
                this->moduleScope->generated.push_back(this->importMetaRef);
            }

            // Replace "import.meta" with a reference to the symbol
            this->recordUsage(this->importMetaRef);
            return {Expr{std::make_shared<EIdentifier>(EIdentifier{this->importMetaRef}), loc}, true};
        }

        return {Expr{}, false};
    }   
    
    // Pushes a new scope during the visit pass. Validates that the scope being pushed
    // matches the scope recorded during the parse pass (same kind and location).
    // This ensures the two passes produce consistent scope trees.
    void Parser::pushScopeForVisitPass(ScopeKind kind, logger::Loc loc) {
        scopeOrder order = this->scopesInOrder[0];

        // Sanity-check that the scopes generated by the first and second passes match
        if (order.loc != loc || order.scope->kind != kind) {
            throw std::runtime_error("Expected scope (" + std::to_string(static_cast<int>(kind)) + ", " + std::to_string(loc.start) + ") in \"" +
                this->source.pretty_paths.Select(this->options.optionsThatSupportStructuralEquality.logPathStyle) + "\", found scope (" +
                std::to_string(static_cast<int>(order.scope->kind)) + ", " + std::to_string(order.loc.start) + ")");
        }

        this->scopesInOrder.erase(this->scopesInOrder.begin());
        this->currentScope = order.scope;
        this->scopesForCurrentPart.push_back(order.scope);
    }  
    
    // Resolves a named identifier by walking up the scope chain to find its declaration.
    // Returns the symbol reference, declaration location, and whether it was inside a
    // "with" statement body (which prevents renaming). Also handles TypeScript namespace
    // member lookups and allocates "unbound" symbols for globals not found in any scope.
    // Records a usage of the found symbol for tree-shaking analysis.
    findSymbolResult Parser::findSymbol(logger::Loc loc, const std::string &name) {
        compiler::Ref ref;
        logger::Loc declareLoc;
        bool isInsideWithScope = false;
        bool didForbidArguments = false;
        Scope *s = this->currentScope;

        for (;;) {
            // Track if we're inside a "with" statement body
            if (s->kind == ScopeKind::kWith) {
                isInsideWithScope = true;
            }

            // Forbid referencing "arguments" inside class bodies
            if (s->forbid_arguments && name == "arguments" && !didForbidArguments) {
            logger::Range r = RangeOfIdentifier(this->source, loc);
                this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_CannotAccessName, name));
                didForbidArguments = true;
            }

            // Is the symbol a member of this scope?
            auto member = s->members.find(name);
            if (member != s->members.end()) {
                ref = member->second.ref;
                declareLoc = member->second.loc;
                break;
            }

            // Is the symbol a member of this scope's TypeScript namespace?
            if (auto *tsNamespace = s->ts_namespace; tsNamespace != nullptr) {
                auto nsMember = tsNamespace->exported_members->find(name);
                if (nsMember != tsNamespace->exported_members->end() && tsNamespace->is_enum_scope == nsMember->second.is_enum_value) {
                    // If this is an identifier from a sibling TypeScript namespace, then we're
                    // going to have to generate a property access instead of a simple reference.
                    // Lazily-generate an identifier that represents this property access.
                    auto found = tsNamespace->lazily_generated_property_accesses.find(name);
                    if (found == tsNamespace->lazily_generated_property_accesses.end()) {
                        ref = this->newSymbol(compiler::SymbolKind::kOther, name);
                        this->symbols[ref.inner_index].namespace_alias = new compiler::NamespaceAlias{/*alias=*/name, /*namespace_ref=*/tsNamespace->arg_ref};
                        tsNamespace->lazily_generated_property_accesses[name] = ref;
                    } else {
                        ref = found->second;
                    }
                    declareLoc = nsMember->second.loc;
                    break;
                }
            }

            s = s->parent;
            if (s == nullptr) {
                // Allocate an "unbound" symbol
                this->checkForUnrepresentableIdentifier(loc, name);
                ref = this->newSymbol(compiler::SymbolKind::kUnbound, name);
                declareLoc = loc;
                this->moduleScope->members[name] = ScopeMember{/*ref=*/ref, /*loc=*/logger::Loc{/*start=*/-1}};
                break;
            }
        }

        // If we had to pass through a "with" statement body to get to the symbol
        // declaration, then this reference could potentially also refer to a
        // property on the target object of the "with" statement. We must not rename
        // it or we risk changing the behavior of the code.
        if (isInsideWithScope) {
            this->symbols[ref.inner_index].flags = this->symbols[ref.inner_index].flags | compiler::SymbolFlags::kMustNotBeRenamed;
        }

        // Track how many times we've referenced this symbol
        this->recordUsage(ref);
        return findSymbolResult{ref, declareLoc, isInsideWithScope};
    }

    // Resolves a label reference for "break" or "continue" statements.
    // Walks up the scope chain looking for a matching label scope.
    // Returns the label reference, whether the label targets a loop, and success status.
    // Logs an error if no matching label is found.
    std::tuple<compiler::Ref, bool, bool> Parser::findLabelSymbol(logger::Loc loc, const std::string &name) {
        for (Scope *s = this->currentScope; s != nullptr && !StopsHoisting(s->kind); s = s->parent) {
            if (s->kind == ScopeKind::kLabel && name == this->symbols[s->label.ref.inner_index].original_name) {
                // Track how many times we've referenced this symbol
                this->recordUsage(s->label.ref);
                return {s->label.ref, s->label_stmt_is_loop, true};
            }
        }

        logger::Range r = RangeOfIdentifier(this->source, loc);
        this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_NoContainingLabel, name));

        // Allocate an "unbound" symbol
        compiler::Ref ref = this->newSymbol(compiler::SymbolKind::kUnbound, name);

        // Track how many times we've referenced this symbol
        this->recordUsage(ref);
        return {ref, false, false};
    }

    // Initializes the parser for the visit pass (second pass over the AST).
    // Sets up the module scope, determines if the file is ESM, applies strict mode,
    // hoists symbols, declares CommonJS runtime symbols (require, module, exports),
    // and processes JSX pragma comments (@jsxRuntime, @jsx, @jsxFrag, @jsxImportSource).
    void Parser::prepareForVisitPass() {
        this->pushScopeForVisitPass(ScopeKind::kEntry, logger::Loc{/*start=*/-1});
        this->fnOrArrowDataVisit.isOutsideFnOrArrow = true;
        this->moduleScope = this->currentScope;

        // Force-enable strict mode if that's the way TypeScript is configured
        if (config::TSAlwaysStrict *tsAlwaysStrict = this->options.tsAlwaysStrict; tsAlwaysStrict != nullptr && tsAlwaysStrict->Value) {
            this->currentScope->strict_mode = StrictModeKind::kImplicitStrictModeTSAlwaysStrict;
        }

        // Determine whether or not this file is ESM
        this->isFileConsideredToHaveESMExports =
            this->esmExportKeyword.len > 0 ||
                this->esmImportMeta.len > 0 ||
                this->topLevelAwaitKeyword.len > 0 ||
                IsESM(this->options.optionsThatSupportStructuralEquality.moduleTypeData.type);
        this->isFileConsideredESM =
            this->isFileConsideredToHaveESMExports ||
                this->esmImportStatementKeyword.len > 0;

        // Legacy HTML comments are not allowed in ESM files
        if (this->isFileConsideredESM && this->lexer.legacy_html_comment_range.len > 0) {
            std::pair<whyESM, std::vector<logger::MsgData>> why = this->whyESModule();
            this->log.AddErrorWithNotes(&this->tracker, this->lexer.legacy_html_comment_range,
                logger::FormatMsg(logger::MsgCat::kJS_LegacyHTMLCommentInESM), why.second);
        }

        // ECMAScript modules are always interpreted as strict mode. This has to be
        // done before "hoistSymbols" because strict mode can alter hoisting (!).
        if (this->isFileConsideredESM) {
            this->moduleScope->RecursiveSetStrictMode(StrictModeKind::kImplicitStrictModeESM);
        }

        this->hoistSymbols(this->moduleScope);

        if (this->options.optionsThatSupportStructuralEquality.mode != config::Mode::kPassThrough) {
            this->requireRef = this->declareCommonJSSymbol(compiler::SymbolKind::kUnbound, "require");
        } else {
            this->requireRef = this->newSymbol(compiler::SymbolKind::kUnbound, "require");
        }

        // CommonJS-style exports are only enabled if this isn't using ECMAScript-
        // style exports. You can still use "require" in ESM, just not "module" or
        // "exports". You can also still use "import" in CommonJS.
        if (this->options.optionsThatSupportStructuralEquality.mode != config::Mode::kPassThrough && !this->isFileConsideredToHaveESMExports) {
            // CommonJS-style exports
            this->exportsRef = this->declareCommonJSSymbol(compiler::SymbolKind::kHoisted, "exports");
            this->moduleRef = this->declareCommonJSSymbol(compiler::SymbolKind::kHoisted, "module");
        } else {
            // ESM-style exports
            this->exportsRef = this->newSymbol(compiler::SymbolKind::kHoisted, "exports");
            this->moduleRef = this->newSymbol(compiler::SymbolKind::kHoisted, "module");
        }

        // Handle "@jsx" and "@jsxFrag" pragmas now that lexing is done
        if (this->options.jsx.Parse) {
            if (auto jsxRuntime = this->lexer.jsx_runtime_pragma_comment; jsxRuntime.text != "") {
                if (jsxRuntime.text == "automatic") {
                    this->options.jsx.AutomaticRuntime = true;
                } else if (jsxRuntime.text == "classic") {
                    this->options.jsx.AutomaticRuntime = false;
                } else {
                    this->log.AddIDWithNotes(logger::MsgID::kJS_UnsupportedJSXComment, logger::MsgKind::kWarning, &this->tracker, jsxRuntime.range,
                        logger::FormatMsg(logger::MsgCat::kJS_JSXRuntimeInvalid, jsxRuntime.text),
                        std::vector<logger::MsgData>{logger::MsgData{{}, {}, std::string(logger::MsgTemplate(logger::MsgCat::kJS_JSXRuntimeInvalidNote))}});
                }
            }

            if (auto jsxFactory = this->lexer.jsx_factory_pragma_comment; jsxFactory.text != "") {
                if (this->options.jsx.AutomaticRuntime) {
                    this->log.AddID(logger::MsgID::kJS_UnsupportedJSXComment, logger::MsgKind::kWarning, &this->tracker, jsxFactory.range,
                        std::string(logger::MsgTemplate(logger::MsgCat::kJS_JSXFactoryAutomatic)));
                } else {
                    std::pair<config::DefineExpr, std::shared_ptr<E>> expr = ParseDefineExpr(jsxFactory.text);
                    if (expr.first.Parts.size() > 0) {
                        this->options.jsx.Factory = expr.first;
                    } else {
                        this->log.AddID(logger::MsgID::kJS_UnsupportedJSXComment, logger::MsgKind::kWarning, &this->tracker, jsxFactory.range,
                            logger::FormatMsg(logger::MsgCat::kJS_InvalidJSXFactory, jsxFactory.text));
                    }
                }
            }

            if (auto jsxFragment = this->lexer.jsx_fragment_pragma_comment; jsxFragment.text != "") {
                if (this->options.jsx.AutomaticRuntime) {
                    this->log.AddID(logger::MsgID::kJS_UnsupportedJSXComment, logger::MsgKind::kWarning, &this->tracker, jsxFragment.range,
                        std::string(logger::MsgTemplate(logger::MsgCat::kJS_JSXFragmentAutomatic)));
                } else {
                    std::pair<config::DefineExpr, std::shared_ptr<E>> expr = ParseDefineExpr(jsxFragment.text);
                    if (expr.first.Parts.size() > 0 || expr.first.HasConstant()) {
                        this->options.jsx.Fragment = expr.first;
                    } else {
                        this->log.AddID(logger::MsgID::kJS_UnsupportedJSXComment, logger::MsgKind::kWarning, &this->tracker, jsxFragment.range,
                            logger::FormatMsg(logger::MsgCat::kJS_InvalidJSXFragment, jsxFragment.text));
                    }
                }
            }

            if (auto jsxImportSource = this->lexer.jsx_import_source_pragma_comment; jsxImportSource.text != "") {
                if (!this->options.jsx.AutomaticRuntime) {
                    this->log.AddIDWithNotes(logger::MsgID::kJS_UnsupportedJSXComment, logger::MsgKind::kWarning, &this->tracker, jsxImportSource.range,
                        std::string(logger::MsgTemplate(logger::MsgCat::kJS_JSXImportSourceAutomatic)),
                        std::vector<logger::MsgData>{logger::MsgData{{}, {}, std::string(logger::MsgTemplate(logger::MsgCat::kJS_JSXImportSourceAutomaticNote))}});
                } else {
                    this->options.jsx.ImportSource = jsxImportSource.text;
                }
            }
        }

        // Force-enable strict mode if the JSX "automatic" runtime is enabled and
        // there is at least one JSX element. This is because the automatically-
        // generated import statement turns the file into an ES module. This behavior
        // matches TypeScript which also does this. See this PR for more information:
        // https://github.com/microsoft/TypeScript/pull/39199
        if (this->currentScope->strict_mode == StrictModeKind::kSloppyMode && this->options.jsx.AutomaticRuntime && this->firstJSXElementLoc.start != -1) {
            this->currentScope->strict_mode = StrictModeKind::kImplicitStrictModeJSXAutomaticRuntime;
        }
    }

    static std::string textForParenthesesSuggestion(const std::string &text) {
        size_t count = 0;
        for (char c : text) {
            if ((c & 0xC0) != 0x80) {
                count++;
            }
        }
        if (count < 3) {
            count = 1;
        } else {
            count -= 2;
        }
        return "(" + std::string(count, ' ') + ")";
    }

    Expr Parser::parseDecorator() {
        if (this->lexer.token == T::kOpenParen) {
            this->lexer.Next();
            Expr value = this->parseExpr(L::kLowest);
            this->lexer.Expect(T::kCloseParen);
            return value;
        }

        MaybeSubstring name = this->lexer.identifier;
        logger::Range nameRange = this->lexer.Range();
        this->lexer.Expect(T::kIdentifier);

        // Forbid invalid identifiers
        if ((this->fnOrArrowDataParse.await != allowIdent && name.str == "await") ||
            (this->fnOrArrowDataParse.yield != allowIdent && name.str == "yield")) {
            this->log.AddError(&this->tracker, nameRange, logger::FormatMsg(logger::MsgCat::kJS_CannotUseNameIdentifier, name.str));
        }

        Expr memberExpr = Expr{std::make_shared<EIdentifier>(EIdentifier{this->storeNameInRef(name)}), nameRange.loc};

        // Custom error reporting for error recovery
        logger::MsgData syntaxError;
        logger::Range wrapRange = nameRange;

        bool done = false;
        while (!done) {
            switch (this->lexer.token) {
            case T::kExclamation:
                // Skip over TypeScript non-null assertions
                if (this->lexer.has_newline_before) {
                    done = true;
                    break;
                }
                if (!this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                    this->lexer.Unexpected();
                }
                wrapRange.len = this->lexer.Range().End() - wrapRange.loc.start;
                this->lexer.Next();
                break;

            case T::kDot:
            case T::kQuestionDot:
                // The grammar for "DecoratorMemberExpression" currently forbids "?."
                if (this->lexer.token == T::kQuestionDot && syntaxError.location == nullptr) {
                    syntaxError = this->tracker.MakeMsgData(this->lexer.Range(), std::string(logger::MsgTemplate(logger::MsgCat::kJS_DecoratorError)));
                }

                this->lexer.Next();
                wrapRange.len = this->lexer.Range().End() - wrapRange.loc.start;

                if (this->lexer.token == T::kPrivateIdentifier) {
                    MaybeSubstring priv = this->lexer.identifier;
                    memberExpr.data = std::make_shared<EIndex>(EIndex{/*target=*/memberExpr, /*index=*/Expr{std::make_shared<EPrivateIdentifier>(EPrivateIdentifier{this->storeNameInRef(priv)}), this->lexer.Loc()}, /*close_bracket_loc=*/logger::Loc{}});
                    this->reportPrivateNameUsage(priv.str);
                    this->lexer.Next();
                } else {
                    memberExpr.data = std::make_shared<EDot>(EDot{/*target=*/memberExpr, /*name=*/this->lexer.identifier.str, /*name_loc=*/this->lexer.Loc()});
                    this->lexer.Expect(T::kIdentifier);
                }
                break;

            case T::kOpenParen: {
                auto [args, closeParenLoc, isMultiLine] = this->parseCallArgs();
                memberExpr.data = std::make_shared<ECall>(ECall{/*target=*/memberExpr, /*args=*/args, /*close_paren_loc=*/closeParenLoc, /*optional_chain=*/{}, /*kind=*/CallKind::kTargetWasOriginallyPropertyAccess, /*is_multi_line=*/isMultiLine});
                wrapRange.len = closeParenLoc.start + 1 - wrapRange.loc.start;

                // The grammar for "DecoratorCallExpression" currently forbids anything after it
                if (this->lexer.token == T::kDot) {
                    if (syntaxError.location == nullptr) {
                        syntaxError = this->tracker.MakeMsgData(this->lexer.Range(), std::string(logger::MsgTemplate(logger::MsgCat::kJS_DotNotAllowedAfterDecoratorCall)));
                    }
                    break;
                }
                done = true;
                break;
            }

            default:
                // "@x<y>"
                // "@x.y<z>"
                if (this->lexer.token == T::kLessThan) {
                    this->skipTypeScriptTypeArguments(skipTypeScriptTypeArgumentsOpts{});
                } else {
                    done = true;
                }
                break;
            }
        }

        // Suggest that non-decorator expressions be wrapped in parentheses
        if (syntaxError.location != nullptr) {
            std::vector<logger::MsgData> notes;
            std::string text = this->source.TextForRange(wrapRange);
            if (text.find('\n') == std::string::npos) {
                logger::MsgData note = this->tracker.MakeMsgData(wrapRange, std::string(logger::MsgTemplate(logger::MsgCat::kJS_WrapDecoratorInParensNote)));
                note.location->suggestion = textForParenthesesSuggestion(text);
                notes = std::vector<logger::MsgData>{note};
            }
            this->log.add_msg(logger::Msg{/*notes=*/notes, /*plugin_name=*/{}, /*data=*/syntaxError, /*kind=*/logger::MsgKind::kError});
        }

        return memberExpr;
    }

    std::vector<Decorator> Parser::parseDecorators(Scope *decoratorScope, logger::Range classKeyword, decoratorContextFlags context) {
        if (this->lexer.token == T::kAt) {
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                if (this->options.optionsThatSupportStructuralEquality.ts.Config.ExperimentalDecorators == config::MaybeBool::kTrue) {
                    if ((context & decoratorInClassExpr) != 0) {
                        this->lexer.AddRangeErrorWithNotes(this->lexer.Range(), std::string(logger::MsgTemplate(logger::MsgCat::kJS_DecoratorsOnlyClassDeclarations)),
                            std::vector<logger::MsgData>{this->tracker.MakeMsgData(classKeyword, std::string(logger::MsgTemplate(logger::MsgCat::kJS_DecoratorsNotClassExpression)))});
                    } else if ((context & decoratorBeforeClassExpr) != 0) {
                        this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_DecoratorExpressionPosition));
                    }
                } else {
                    if ((context & decoratorInFnArgs) != 0 && this->options.optionsThatSupportStructuralEquality.ts.Config.ExperimentalDecorators != config::MaybeBool::kTrue) {
                        logger::MsgData note;
                        note.text = std::string(logger::MsgTemplate(logger::MsgCat::kJS_EnableExperimentalDecoratorsNote));
                        this->log.AddErrorWithNotes(&this->tracker, this->lexer.Range(),
                            logger::FormatMsg(logger::MsgCat::kJS_ParameterDecoratorsExperimental),
                            std::vector<logger::MsgData>{note});
                    }
                }
            } else {
                if ((context & decoratorInFnArgs) != 0) {
                    this->log.AddError(&this->tracker, this->lexer.Range(), logger::FormatMsg(logger::MsgCat::kJS_ParameterDecoratorsInJS));
                }
            }
        }

        // TypeScript decorators cause us to temporarily revert to the scope that
        // encloses the class declaration, since that's where the generated code
        // for TypeScript decorators will be inserted.
        Scope *oldScope = this->currentScope;
        this->currentScope = decoratorScope;

        std::vector<Decorator> decorators;
        for (; this->lexer.token == T::kAt;) {
            logger::Loc atLoc = this->lexer.Loc();
            this->lexer.Next();

            Expr value;
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && this->options.optionsThatSupportStructuralEquality.ts.Config.ExperimentalDecorators == config::MaybeBool::kTrue) {
                // TypeScript's experimental decorator syntax is more permissive than
                // JavaScript. Parse a new/call expression with "exprFlagDecorator" so
                // we ignore EIndex expressions, since they may be part of a computed
                // property:
                //
                //   class Foo {
                //     @foo ['computed']() {}
                //   }
                //
                // This matches the behavior of the TypeScript compiler.
                this->parseExperimentalDecoratorNesting++;
                value = this->parseExprWithFlags(L::kNew, exprFlagDecorator);
                this->parseExperimentalDecoratorNesting--;
            } else {
                // JavaScript's decorator syntax is more restrictive. Parse it using a
                // special parser that doesn't allow normal expressions (e.g. "?.").
                value = this->parseDecorator();
            }
            Decorator decorator;
            decorator.value = value;
            decorator.at_loc = atLoc;
            decorator.omit_newline_after = !this->lexer.has_newline_before;
            decorators.push_back(decorator);
        }

        // Avoid "popScope" because this decorator scope is not hierarchical
        this->currentScope = oldScope;
        return decorators;
    }
        
    // Visits statements and prepends temporary variable declarations for captured
    // "this" and "arguments" references in arrow functions that will be lowered to
    // regular function expressions. Also handles top-level temporaries and generates
    // "var" declarations for used temp refs, placed after leading directives/comments.
    std::vector<Stmt> Parser::visitStmtsAndPrependTempRefs(std::vector<Stmt> stmts, prependTempRefsOpts opts) {
        std::vector<tempRef> oldTempRefs = this->tempRefsToDeclare;
        int oldTempRefCount = this->tempRefCount;
        this->tempRefsToDeclare = {};
        this->tempRefCount = 0;

        stmts = this->visitStmts(stmts, opts.kind);

        // Prepend values for "this" and "arguments"
        if (opts.fnBodyLoc != nullptr) {
            // Capture "this"
            if (compiler::Ref *ref = this->fnOnlyDataVisit.thisCaptureRef; ref != nullptr) {
                this->tempRefsToDeclare.push_back(tempRef{/*valueOrNil=*/Expr{/*data=*/kEThisShared, /*loc=*/*opts.fnBodyLoc}, /*ref=*/*ref});
                this->currentScope->generated.push_back(*ref);
            }

            // Capture "arguments"
            if (compiler::Ref *ref = this->fnOnlyDataVisit.argumentsCaptureRef; ref != nullptr) {
                EIdentifier *identifier = new EIdentifier();
                identifier->ref = *this->fnOnlyDataVisit.argumentsRef;
                this->tempRefsToDeclare.push_back(tempRef{
                    /*valueOrNil=*/Expr{/*data=*/std::shared_ptr<EIdentifier>(identifier), /*loc=*/*opts.fnBodyLoc},
                    /*ref=*/*ref});
                this->currentScope->generated.push_back(*ref);
            }
        }

        // There may also be special top-level-only temporaries to declare
        if (this->currentScope == this->moduleScope && !this->topLevelTempRefsToDeclare.empty()) {
            this->tempRefsToDeclare.insert(this->tempRefsToDeclare.end(), this->topLevelTempRefsToDeclare.begin(), this->topLevelTempRefsToDeclare.end());
            this->topLevelTempRefsToDeclare.clear();
        }

        // Prepend the generated temporary variables to the beginning of the statement list
        std::vector<Decl> decls;
        for (tempRef &temp : this->tempRefsToDeclare) {
            if (this->symbols[temp.ref.inner_index].use_count_estimate > 0) {
                BIdentifier *b = new BIdentifier();
                b->ref = temp.ref;
                decls.push_back(Decl{/*binding=*/Binding{/*data=*/std::shared_ptr<BIdentifier>(b), /*loc=*/logger::Loc{}}, /*valueOrNil=*/temp.valueOrNil});
                this->recordDeclaredSymbol(temp.ref);
            }
        }
        if (!decls.empty()) {
            // Skip past leading directives and comments
            int split = 0;
            while (split < static_cast<int>(stmts.size())) {
                if (Get<SComment>(stmts[static_cast<size_t>(split)].data) != nullptr || Get<SDirective>(stmts[static_cast<size_t>(split)].data) != nullptr) {
                    split++;
                    continue;
                }
                break;
            }

            SLocal *local = new SLocal();
            local->kind = LocalKind::kVar;
            local->decls = decls;
            std::vector<Stmt> newStmts;
            newStmts.reserve(stmts.size() + 1);
            newStmts.insert(newStmts.end(), stmts.begin(), stmts.begin() + split);
            newStmts.push_back(Stmt{/*data=*/std::shared_ptr<SLocal>(local), /*loc=*/logger::Loc{}});
            newStmts.insert(newStmts.end(), stmts.begin() + split, stmts.end());
            stmts = newStmts;
        }

        this->tempRefsToDeclare = oldTempRefs;
        this->tempRefCount = oldTempRefCount;
        return stmts;
    }

    // Visits a single statement, special-casing blocks to reduce stack depth.
    // Handles function declarations inside if statements (Annex B compat) by
    // introducing a fake block scope. Used by visitAndAppendStmt for non-compound
    // statements.
    Stmt Parser::visitSingleStmt(Stmt stmt, stmtsKind kind) {
        // To reduce stack depth, special-case blocks and process their children directly
        if (auto *block = Get<SBlock>(stmt.data); block != nullptr) {
            this->pushScopeForVisitPass(ScopeKind::kBlock, stmt.loc);
            block->stmts = this->visitStmts(block->stmts, kind);
            this->popScope();
            if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                stmt = stmtsToSingleStmt(stmt.loc, block->stmts, block->close_brace_loc);
            }
            return stmt;
        }

        // Introduce a fake block scope for function declarations inside if statements
        SFunction *fn = Get<SFunction>(stmt.data);
        bool hasIfScope = fn != nullptr && fn->fn.has_if_scope;
        if (hasIfScope) {
            this->pushScopeForVisitPass(ScopeKind::kBlock, stmt.loc);
            if (this->isStrictMode()) {
                this->markStrictModeFeature(ifElseFunctionStmt, RangeOfIdentifier(this->source, stmt.loc), "");
            }
        }

        this->singleStmtDepth++;
        std::vector<Stmt> stmts = this->visitStmts({stmt}, kind);
        this->singleStmtDepth--;

        // Balance the fake block scope introduced above
        if (hasIfScope) {
            this->popScope();
        }

        return stmtsToSingleStmt(stmt.loc, stmts, logger::Loc{});
    }

    // Visits a loop body statement, setting the isInsideLoop flag and storing the
    // loop body pointer for break/continue validation. Delegates to visitSingleStmt.
    Stmt Parser::visitLoopBody(Stmt stmt) {
        bool oldIsInsideLoop = this->fnOrArrowDataVisit.isInsideLoop;
        this->fnOrArrowDataVisit.isInsideLoop = true;
        this->loopBody = &stmt.data;
        stmt = this->visitSingleStmt(stmt, stmtsLoopBody);
        this->fnOrArrowDataVisit.isInsideLoop = oldIsInsideLoop;
        return stmt;
    }

    // Visits the initializer of a for-loop. Handles two cases:
    //   - Expression statement (for(;;)): visits with optional assignment target
    //   - Variable declaration (for(var/let/const ...)): visits bindings and initializers
    // The isInOf flag indicates whether this is inside a "for-in" or "for-of" loop,
    // which affects destructuring handling.
    Stmt Parser::visitForLoopInit(Stmt stmt, bool isInOrOf) {
        if (auto *se = Get<SExpr>(stmt.data); se != nullptr) {
            AssignTarget assignTarget = AssignTarget::kNone;
            if (isInOrOf) {
                assignTarget = AssignTarget::kReplace;
            }
            this->stmtExprValue = se->value.data;

            exprIn in{};
            in.assignTarget = assignTarget;
            std::pair<Expr, exprOut> visited = this->visitExprInOut(se->value, in);
            se->value = visited.first;

        } else if (auto *sl = Get<SLocal>(stmt.data); sl != nullptr) {
            for (size_t i = 0; i < sl->decls.size(); i++) {
                Decl *d = &sl->decls[i];
                this->visitBinding(d->binding, bindingOpts{});
                if (!IsNil(d->value_or_nil.data)) {
                    d->value_or_nil = this->visitExpr(d->value_or_nil);
                }
            }
            sl->decls = this->lowerObjectRestInDecls(sl->decls);
            sl->kind = this->selectLocalKind(sl->kind);

        } else {
            throw std::runtime_error("Internal error");
        }

        return stmt;
    }


    // Recursively visits a binding pattern (identifier, array destructuring, or object
    // destructuring). Records declared symbols, validates names, checks for duplicate
    // parameter names, and visits default values and computed property keys.
    void Parser::visitBinding(Binding binding, bindingOpts opts) {
        if (Get<BMissing>(binding.data) != nullptr) {
        } else if (auto *bi = Get<BIdentifier>(binding.data); bi != nullptr) {            this->recordDeclaredSymbol(bi->ref);
            const std::string name = this->symbols[bi->ref.inner_index].original_name;
            this->validateDeclaredSymbolName(binding.loc, name);
            if (opts.duplicateArgCheck != nullptr) {
                logger::Range r = RangeOfIdentifier(this->source, binding.loc);
                auto it = opts.duplicateArgCheck->find(name);
                if (it != opts.duplicateArgCheck->end() && it->second.len > 0) {
                    logger::Range firstRange = it->second;
                    this->log.AddErrorWithNotes(&this->tracker, r,
                        logger::FormatMsg(logger::MsgCat::kJS_BoundMultipleTimesInParamList, name),
                        std::vector<logger::MsgData>{this->tracker.MakeMsgData(firstRange, logger::FormatMsg(logger::MsgCat::kJS_BoundMultipleTimesOriginalNote, name))});
                } else {
                    (*opts.duplicateArgCheck)[name] = r;
                }
            }

        } else if (auto *ba = Get<BArray>(binding.data); ba != nullptr) {
            for (size_t i = 0; i < ba->items.size(); i++) {
                ArrayBinding &item = ba->items[i];
                this->visitBinding(item.binding, opts);
                if (!IsNil(item.default_value_or_nil.data)) {
                    // Propagate the name to keep from the binding into the initializer
                    if (auto *id = Get<BIdentifier>(item.binding.data); id != nullptr) {
                        this->nameToKeep = this->symbols[id->ref.inner_index].original_name;
                        this->nameToKeepIsFor =item.default_value_or_nil.data;
                    }

                    item.default_value_or_nil = this->visitExpr(item.default_value_or_nil);
                }
            }

        } else if (auto *bo = Get<BObject>(binding.data); bo != nullptr) {
            for (size_t i = 0; i < bo->properties.size(); i++) {
                PropertyBinding &property = bo->properties[i];
                if (!property.is_spread) {
                    exprIn keyIn{};
                    keyIn.shouldMangleStringsAsProps = true;
                    property.key = this->visitExprInOut(property.key, keyIn).first;
                }
                this->visitBinding(property.value, opts);
                if (!IsNil(property.default_value_or_nil.data)) {
                    // Propagate the name to keep from the binding into the initializer
                    if (auto *id = Get<BIdentifier>(property.value.data); id != nullptr) {
                        this->nameToKeep = this->symbols[id->ref.inner_index].original_name;
                        this->nameToKeepIsFor =property.default_value_or_nil.data;
                    }

                    property.default_value_or_nil = this->visitExpr(property.default_value_or_nil);
                }
            }

        } else {
            throw std::runtime_error("Internal error");
        }
    }

    // The main statement visitor. Visits a single statement and appends it to the output
    // statement list. Handles every statement type in the AST, including:
    //   - TypeScript-only statements (stripped from output)
    //   - Import/export statements (symbol resolution, re-export rewriting)
    //   - Variable declarations (binding visiting, initializer visiting, const folding)
    //   - Function/class declarations (delegation to visitFn/visitClass)
    //   - Control flow (if/for/while/switch/try/catch/finally)
    //   - Labels, break, continue (scope validation)
    //   - TypeScript enums and namespaces (closure generation)
    //   - "using" declarations (compatibility lowering)
    // Manages the const-local-prefix optimization for minification.
    std::vector<Stmt> Parser::visitAndAppendStmt(std::vector<Stmt> stmts, Stmt stmt) {
        // By default any statement ends the const local prefix
        bool wasAfterAfterConstLocalPrefix = this->currentScope->is_after_const_local_prefix;
        this->currentScope->is_after_const_local_prefix = true;

        if (Get<SEmpty>(stmt.data) != nullptr || Get<SComment>(stmt.data) != nullptr) {
            // Comments do not end the const local prefix
            this->currentScope->is_after_const_local_prefix = wasAfterAfterConstLocalPrefix;
        } else if (auto *s = Get<SDebugger>(stmt.data); s != nullptr) {
            // Debugger statements do not end the const local prefix
            this->currentScope->is_after_const_local_prefix = wasAfterAfterConstLocalPrefix;

            if (this->options.optionsThatSupportStructuralEquality.dropDebugger) {
                return stmts;
            }
        } else if (auto *sTs = Get<STypeScript>(stmt.data); sTs != nullptr) {
            // Type annotations do not end the const local prefix
            this->currentScope->is_after_const_local_prefix = wasAfterAfterConstLocalPrefix;

            // Erase TypeScript constructs from the output completely
            return stmts;
        } else if (auto *sDir = Get<SDirective>(stmt.data); sDir != nullptr) {
            // Directives do not end the const local prefix
            this->currentScope->is_after_const_local_prefix = wasAfterAfterConstLocalPrefix;

            if (this->isStrictMode() && sDir->legacy_octal_loc.start > 0) {
                this->markStrictModeFeature(legacyOctalEscape, this->source.RangeOfLegacyOctalEscape(sDir->legacy_octal_loc), "");
            }
        } else if (auto *sImp = Get<SImport>(stmt.data); sImp != nullptr) {
            this->recordDeclaredSymbol(sImp->namespace_ref);

            if (sImp->default_name != nullptr) {
                this->recordDeclaredSymbol(sImp->default_name->ref);
            }

            if (sImp->items != nullptr) {
                for (ClauseItem item : *sImp->items) {
                    this->recordDeclaredSymbol(item.name.ref);
                }
            }
        } else if (auto *sExp = Get<SExportClause>(stmt.data); sExp != nullptr) {
            // "export {foo}"
            size_t end = 0;
            for (ClauseItem item : sExp->items) {
                std::string name = this->loadNameFromRef(item.name.ref);
                compiler::Ref ref = this->findSymbol(item.alias_loc, name).ref;

                if (this->symbols[ref.inner_index].kind == compiler::SymbolKind::kUnbound) {
                    // Silently strip exports of non-local symbols in TypeScript, since
                    // those likely correspond to type-only exports. But report exports of
                    // non-local symbols as errors in JavaScript.
                    if (!this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                        logger::Range r = RangeOfIdentifier(this->source, item.name.loc);
                        this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_NotDeclaredInThisFile, name));
                    }
                    continue;
                }

                item.name.ref = ref;
                sExp->items[end] = item;
                end++;
            }

            // Note: do not remove empty export statements since TypeScript uses them as module markers
            sExp->items.resize(end);
        } else if (auto *sExportFrom = Get<SExportFrom>(stmt.data); sExportFrom != nullptr) {
            // "export {foo} from 'path'"
            std::string name = this->loadNameFromRef(sExportFrom->namespace_ref);
            sExportFrom->namespace_ref = this->newSymbol(compiler::SymbolKind::kOther, name);
            this->currentScope->generated.push_back(sExportFrom->namespace_ref);
            this->recordDeclaredSymbol(sExportFrom->namespace_ref);

            // This is a re-export and the symbols created here are used to reference
            // names in another file. This means the symbols are really aliases.
            for (size_t i = 0; i < sExportFrom->items.size(); i++) {
                ClauseItem item = sExportFrom->items[i];
                name = this->loadNameFromRef(item.name.ref);
                compiler::Ref ref = this->newSymbol(compiler::SymbolKind::kOther, name);
                this->currentScope->generated.push_back(ref);
                this->recordDeclaredSymbol(ref);
                sExportFrom->items[i].name.ref = ref;
            }
        } else if (auto *sExportStar = Get<SExportStar>(stmt.data); sExportStar != nullptr) {
            // "export * from 'path'"
            // "export * as ns from 'path'"
            std::string name = this->loadNameFromRef(sExportStar->namespace_ref);
            sExportStar->namespace_ref = this->newSymbol(compiler::SymbolKind::kOther, name);
            this->currentScope->generated.push_back(sExportStar->namespace_ref);
            this->recordDeclaredSymbol(sExportStar->namespace_ref);

            // "export * as ns from 'path'"
            if (sExportStar->alias != nullptr) {
                // "import * as ns from 'path'"
                // "export {ns}"
                if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kExportStarAs)) {
                    this->recordUsage(sExportStar->namespace_ref);

                    SImport *importStmt = new SImport();
                    importStmt->namespace_ref = sExportStar->namespace_ref;
                    importStmt->star_name_loc = std::make_shared<logger::Loc>(sExportStar->alias->loc);
                    importStmt->import_record_index = sExportStar->import_record_index;

                    SExportClause *exportClause = new SExportClause();
                    ClauseItem ci;
                    ci.alias = sExportStar->alias->original_name;
                    ci.original_name = sExportStar->alias->original_name;
                    ci.alias_loc = sExportStar->alias->loc;
                    ci.name = compiler::LocRef{sExportStar->alias->loc, sExportStar->namespace_ref};
                    exportClause->items.push_back(ci);
                    exportClause->is_single_line = true;

                    stmts.push_back(Stmt{std::shared_ptr<SImport>(importStmt), stmt.loc});
                    stmts.push_back(Stmt{std::shared_ptr<SExportClause>(exportClause), stmt.loc});
                    return stmts;
                }
            }
        } else if (auto *sExportDefault = Get<SExportDefault>(stmt.data); sExportDefault != nullptr) {
            this->recordDeclaredSymbol(sExportDefault->default_name.ref);

            if (auto *sExpr = Get<SExpr>(sExportDefault->value.data); sExpr != nullptr) {
                // Propagate the name to keep from the export into the value
                this->nameToKeep = "default";
                this->nameToKeepIsFor =sExpr->value.data;

                sExpr->value = this->visitExpr(sExpr->value);

                // Discard type-only export default statements
                if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
                    if (auto *id = Get<EIdentifier>(sExpr->value.data); id != nullptr) {
                        compiler::Symbol symbol = this->symbols[id->ref.inner_index];
                        if (symbol.kind == compiler::SymbolKind::kUnbound && this->localTypeNames.count(symbol.original_name) > 0) {
                            return stmts;
                        }
                    }
                }

                // If there are lowered "using" declarations, change this into a "var"
                if (this->currentScope->parent == nullptr && this->willWrapModuleInTryCatchForUsing) {
                    SLocal *localStmt = new SLocal();
                    Decl decl;
                    BIdentifier *bid = new BIdentifier();
                    bid->ref = sExportDefault->default_name.ref;
                    decl.binding = Binding{std::shared_ptr<BIdentifier>(bid), sExportDefault->default_name.loc};
                    decl.value_or_nil = sExpr->value;
                    localStmt->decls.push_back(decl);

                    SExportClause *exportClause = new SExportClause();
                    ClauseItem ci;
                    ci.alias = "default";
                    ci.alias_loc = sExportDefault->default_name.loc;
                    ci.name = sExportDefault->default_name;
                    exportClause->items.push_back(ci);

                    stmts.push_back(Stmt{std::shared_ptr<SLocal>(localStmt), stmt.loc});
                    stmts.push_back(Stmt{std::shared_ptr<SExportClause>(exportClause), stmt.loc});
                } else {
                    stmts.push_back(stmt);
                }
            } else if (auto *sFn = Get<SFunction>(sExportDefault->value.data); sFn != nullptr) {
                // If we need to preserve the name but there is no name, generate a name
                std::string name;
                if (this->options.optionsThatSupportStructuralEquality.keepNames) {
                    if (sFn->fn.name == nullptr) {
                        sFn->fn.name = std::make_shared<compiler::LocRef>(sExportDefault->default_name);
                        name = "default";
                    } else {
                        name = this->symbols[sFn->fn.name->ref.inner_index].original_name;
                    }
                }

                this->visitFn(&sFn->fn, sFn->fn.open_paren_loc, visitFnOpts{});
                stmts.push_back(stmt);

                // Optionally preserve the name
                if (this->options.optionsThatSupportStructuralEquality.keepNames) {
                    this->symbols[sFn->fn.name->ref.inner_index].flags = this->symbols[sFn->fn.name->ref.inner_index].flags | compiler::SymbolFlags::kDidKeepName;
                    EIdentifier *fnId = new EIdentifier();
                    fnId->ref = sFn->fn.name->ref;
                    Expr fn{std::shared_ptr<EIdentifier>(fnId), sFn->fn.name->loc};
                    stmts.push_back(this->keepClassOrFnSymbolName(sFn->fn.name->loc, fn, name));
                }
            } else if (auto *sClass = Get<SClass>(sExportDefault->value.data); sClass != nullptr) {
                visitClassResult result = this->visitClass(sExportDefault->value.loc, &sClass->class_, sExportDefault->default_name.ref, "default");

                // Lower class field syntax for browsers that don't support it
                auto lowered = this->lowerClass(stmt, Expr{}, result, "");
                std::vector<Stmt> &classStmts = lowered.first;

                // Remember if the class was side-effect free before lowering
                if (result.canBeRemovedIfUnused) {
                    for (Stmt classStmt : classStmts) {
                        if (auto *classSExpr = Get<SExpr>(classStmt.data); classSExpr != nullptr) {
                            classSExpr->is_from_class_or_fn_that_can_be_removed_if_unused = true;
                        }
                    }
                }

                for (Stmt classStmt : classStmts) {
                    stmts.push_back(classStmt);
                }
            } else {
                throw std::runtime_error("Internal error");
            }

            // Use a more friendly name than "default" now that "--keep-names" has
            // been applied and has made sure to enforce the name "default"
            if (this->symbols[sExportDefault->default_name.ref.inner_index].original_name == "default") {
                this->symbols[sExportDefault->default_name.ref.inner_index].original_name = this->source.identifier_name + "_default";
            }

            return stmts;
        } else if (auto *sExportEquals = Get<SExportEquals>(stmt.data); sExportEquals != nullptr) {
            // "module.exports = value"
            EDot *dot = new EDot();
            EIdentifier *targetId = new EIdentifier();
            targetId->ref = this->moduleRef;
            dot->target = Expr{std::shared_ptr<EIdentifier>(targetId), stmt.loc};
            dot->name = "exports";
            dot->name_loc = stmt.loc;
            stmts.push_back(AssignStmt(Expr{std::shared_ptr<EDot>(dot), stmt.loc}, this->visitExpr(sExportEquals->value)));
            this->recordUsage(this->moduleRef);
            return stmts;
        } else if (auto *sBreak = Get<SBreak>(stmt.data); sBreak != nullptr) {
            if (sBreak->label != nullptr) {
                std::string name = this->loadNameFromRef(sBreak->label->ref);
                sBreak->label->ref = std::get<0>(this->findLabelSymbol(sBreak->label->loc, name));
            } else if (!this->fnOrArrowDataVisit.isInsideLoop && !this->fnOrArrowDataVisit.isInsideSwitch) {
                logger::Range r = RangeOfIdentifier(this->source, stmt.loc);
                this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_CannotUseBreak));
            }
        } else if (auto *sContinue = Get<SContinue>(stmt.data); sContinue != nullptr) {
            if (sContinue->label != nullptr) {
                std::string name = this->loadNameFromRef(sContinue->label->ref);
                std::tuple<compiler::Ref, bool, bool> result = this->findLabelSymbol(sContinue->label->loc, name);
                sContinue->label->ref = std::get<0>(result);
                bool isLoop = std::get<1>(result);
                bool ok = std::get<2>(result);
                if (ok && !isLoop) {
                    logger::Range r = RangeOfIdentifier(this->source, sContinue->label->loc);
                    this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_CannotContinueToLabel, name));
                }
            } else if (!this->fnOrArrowDataVisit.isInsideLoop) {
                logger::Range r = RangeOfIdentifier(this->source, stmt.loc);
                this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_CannotUseContinue));
            }
        } else if (auto *sLabel = Get<SLabel>(stmt.data); sLabel != nullptr) {
            // Forbid functions inside labels in strict mode
            if (this->isStrictMode()) {
                if (Get<SFunction>(sLabel->stmt.data) != nullptr) {
                    this->markStrictModeFeature(labelFunctionStmt, RangeOfIdentifier(this->source, sLabel->stmt.loc), "");
                }
            }

            this->pushScopeForVisitPass(ScopeKind::kLabel, stmt.loc);
            std::string name = this->loadNameFromRef(sLabel->name.ref);
            if (kStrictModeReservedWords.count(name) > 0) {
                this->markStrictModeFeature(reservedWord, RangeOfIdentifier(this->source, sLabel->name.loc), name);
            }
            compiler::Ref ref = this->newSymbol(compiler::SymbolKind::kLabel, name);
            sLabel->name.ref = ref;
            this->recordDeclaredSymbol(ref);

            // Duplicate labels are an error
            for (Scope *scope = this->currentScope->parent; scope != nullptr; scope = scope->parent) {
                if (scope->label.ref != compiler::kInvalidRef && name == this->symbols[scope->label.ref.inner_index].original_name) {
                    std::vector<logger::MsgData> notes{this->tracker.MakeMsgData(RangeOfIdentifier(this->source, scope->label.loc), logger::FormatMsg(logger::MsgCat::kJS_DuplicateLabelOriginalNote, name))};
                    this->log.AddErrorWithNotes(&this->tracker, RangeOfIdentifier(this->source, sLabel->name.loc), logger::FormatMsg(logger::MsgCat::kJS_DuplicateLabel, name), notes);
                    break;
                }
                if (scope->kind == ScopeKind::kFunctionBody) {
                    // Labels are only visible within the function they are defined in.
                    break;
                }
            }

            this->currentScope->label = compiler::LocRef{sLabel->name.loc, ref};
            if (Get<SFor>(sLabel->stmt.data) != nullptr || Get<SForIn>(sLabel->stmt.data) != nullptr ||
                Get<SForOf>(sLabel->stmt.data) != nullptr || Get<SWhile>(sLabel->stmt.data) != nullptr ||
                Get<SDoWhile>(sLabel->stmt.data) != nullptr) {
                this->currentScope->label_stmt_is_loop = true;
            }

            // If we're dropping this statement, consider control flow to be dead
            bool shouldDropLabel = this->dropLabelsMap.count(name) > 0;
            bool old = this->isControlFlowDead;
            if (shouldDropLabel) {
                this->isControlFlowDead = true;
            }

            sLabel->stmt = this->visitSingleStmt(sLabel->stmt, stmtsNormal);
            this->popScope();

            // Drop this entire statement if requested
            if (shouldDropLabel) {
                this->isControlFlowDead = old;
                return stmts;
            }

            if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                // Optimize "x: break x" which some people apparently write by hand
                if (auto *child = Get<SBreak>(sLabel->stmt.data); child != nullptr && child->label != nullptr && child->label->ref == sLabel->name.ref) {
                    return stmts;
                }

                // Remove the label if it's not necessary
                if (this->symbols[ref.inner_index].use_count_estimate == 0) {
                    return appendIfOrLabelBodyPreservingScope(stmts, sLabel->stmt);
                }
            }

            // Handle "for await" that has been lowered by moving this label inside the "try"
            if (auto *tryStmt = Get<STry>(sLabel->stmt.data); tryStmt != nullptr && tryStmt->block.stmts.size() == 1) {
                if (auto *loop = Get<SFor>(tryStmt->block.stmts[0].data); loop != nullptr && loop->is_lowered_for_await) {
                    SLabel *innerLabel = new SLabel();
                    innerLabel->stmt = tryStmt->block.stmts[0];
                    innerLabel->name = sLabel->name;
                    innerLabel->is_single_line_stmt = sLabel->is_single_line_stmt;
                    tryStmt->block.stmts[0] = Stmt{std::shared_ptr<SLabel>(innerLabel), stmt.loc};
                    stmts.push_back(sLabel->stmt);
                    return stmts;
                }
            }
        } else if (auto *sLocal = Get<SLocal>(stmt.data); sLocal != nullptr) {
            // Silently remove unsupported top-level "await" in dead code branches
            if (sLocal->kind == LocalKind::kAwaitUsing && this->fnOrArrowDataVisit.isOutsideFnOrArrow) {
                if (this->isControlFlowDead && (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kTopLevelAwait) || !config::FormatKeepESMImportExportSyntax(this->options.optionsThatSupportStructuralEquality.outputFormat))) {
                    sLocal->kind = LocalKind::kUsing;
                } else {
                    this->liveTopLevelAwaitKeyword = logger::Range{stmt.loc, 5};
                    this->markSyntaxFeature(compat::JSFeature::kTopLevelAwait, logger::Range{stmt.loc, 5});
                }
            }

            // Local statements do not end the const local prefix
            this->currentScope->is_after_const_local_prefix = wasAfterAfterConstLocalPrefix;

            for (Decl &d : sLocal->decls) {
                this->visitBinding(d.binding, bindingOpts{});

                // Visit the initializer
                if (!IsNil(d.value_or_nil.data)) {
                    // Fold numeric constants in the initializer
                    bool oldShouldFoldTypeScriptConstantExpressions = this->shouldFoldTypeScriptConstantExpressions;
                    this->shouldFoldTypeScriptConstantExpressions = this->options.optionsThatSupportStructuralEquality.minifySyntax && !this->currentScope->is_after_const_local_prefix;

                    // Propagate the name to keep from the binding into the initializer
                    if (auto *id = Get<BIdentifier>(d.binding.data); id != nullptr) {
                        this->nameToKeep = this->symbols[id->ref.inner_index].original_name;
                        this->nameToKeepIsFor =d.value_or_nil.data;
                    }

                    d.value_or_nil = this->visitExpr(d.value_or_nil);

                    this->shouldFoldTypeScriptConstantExpressions = oldShouldFoldTypeScriptConstantExpressions;

                    // Initializing to undefined is implicit, but be careful to not
                    // accidentally cause a syntax error or behavior change by removing
                    // the value
                    //
                    // Good:
                    //   "let a = undefined;" => "let a;"
                    //
                    // Bad (a syntax error):
                    //   "let {} = undefined;" => "let {};"
                    //
                    // Bad (a behavior change):
                    //   "a = 123; var a = undefined;" => "a = 123; var a;"
                    //
                    if (this->options.optionsThatSupportStructuralEquality.minifySyntax && sLocal->kind == LocalKind::kLet) {
                        if (Get<BIdentifier>(d.binding.data) != nullptr) {
                            if (Get<EUndefined>(d.value_or_nil.data) != nullptr) {
                                d.value_or_nil = Expr{};
                            }
                        }
                    }

                    // Yarn's PnP data may be stored in a variable: https://github.com/yarnpkg/berry/pull/4320
                    if (this->options.optionsThatSupportStructuralEquality.decodeHydrateRuntimeStateYarnPnP) {
                        if (auto *str = Get<EString>(d.value_or_nil.data); str != nullptr) {
                            if (auto *id = Get<BIdentifier>(d.binding.data); id != nullptr) {
                                stringLocalForYarnPnP local;
                                local.value = std::vector<uint16_t>(str->value.begin(), str->value.end());
                                local.loc = d.value_or_nil.loc;
                                this->stringLocalsForYarnPnP[id->ref] = local;
                            }
                        }
                    }

                    // Attempt to continue the const local prefix
                    if (this->options.optionsThatSupportStructuralEquality.minifySyntax && !this->currentScope->is_after_const_local_prefix) {
                        if (auto *id = Get<BIdentifier>(d.binding.data); id != nullptr) {
                            if (sLocal->kind == LocalKind::kConst && !IsNil(d.value_or_nil.data)) {
                                ConstValue value = ExprToConstValue(d.value_or_nil);
                                if (value.kind != ConstValueKind::kNone) {
                                    this->constValues[id->ref] = value;
                                    continue;
                                }
                            }

                            if (!IsNil(d.value_or_nil.data) && !isSafeForConstLocalPrefix(d.value_or_nil)) {
                                this->currentScope->is_after_const_local_prefix = true;
                            }
                        } else {
                            // A non-identifier binding ends the const local prefix
                            this->currentScope->is_after_const_local_prefix = true;
                        }
                    }
                }
            }

            // Handle being exported inside a namespace
            if (sLocal->is_export && this->enclosingNamespaceArgRef != nullptr) {
                auto wrapIdentifier = [this](logger::Loc loc, compiler::Ref ref) -> Expr {
                    this->recordUsage(*this->enclosingNamespaceArgRef);
                    std::string originalName = this->symbols[ref.inner_index].original_name;
                    EIdentifier *targetId = new EIdentifier();
                    targetId->ref = *this->enclosingNamespaceArgRef;
                    return Expr{*this->dotOrMangledPropVisit(Expr{std::shared_ptr<EIdentifier>(targetId), loc}, originalName, loc), loc};
                };
                for (Decl decl : sLocal->decls) {
                    if (!IsNil(decl.value_or_nil.data)) {
                        Expr target = ConvertBindingToExpr(decl.binding, wrapIdentifier);
                        std::pair<Expr, bool> lowered = this->lowerAssign(target, decl.value_or_nil, objRestReturnValueIsUnused);
                        if (lowered.second) {
                            target = lowered.first;
                        } else {
                            target = Assign(target, decl.value_or_nil);
                        }
                        SExpr *sexpr = new SExpr();
                        sexpr->value = target;
                        stmts.push_back(Stmt{std::shared_ptr<SExpr>(sexpr), stmt.loc});
                    }
                }
                return stmts;
            }

            
            if (this->options.optionsThatSupportStructuralEquality.minifySyntax && sLocal->kind == LocalKind::kUsing) {
                sLocal->kind = LocalKind::kConst;
                for (Decl decl : sLocal->decls) {
                    PrimitiveType t = PrimitiveType::kUnknown;
                    if (!IsNil(decl.value_or_nil.data)) {
                        t = KnownPrimitiveType(decl.value_or_nil.data);
                    }
                    if (t != PrimitiveType::kNull && t != PrimitiveType::kUndefined) {
                        sLocal->kind = LocalKind::kUsing;
                        break;
                    }
                }
            }

            sLocal->decls = this->lowerObjectRestInDecls(sLocal->decls);
            sLocal->kind = this->selectLocalKind(sLocal->kind);

            // Potentially relocate "var" declarations to the top level
            if (sLocal->kind == LocalKind::kVar) {
                std::pair<Stmt, bool> relocated = this->maybeRelocateVarsToTopLevel(sLocal->decls, relocateVarsNormal);
                if (relocated.second) {
                    if (!IsNil(relocated.first.data)) {
                        stmts.push_back(relocated.first);
                    }
                    return stmts;
                }
            }
        } else if (auto *sExpr = Get<SExpr>(stmt.data); sExpr != nullptr) {
            bool shouldTrimUnsightlyPrimitives = !this->options.optionsThatSupportStructuralEquality.minifySyntax && !isUnsightlyPrimitive(sExpr->value.data);
            this->stmtExprValue = sExpr->value.data;
            sExpr->value = this->visitExpr(sExpr->value);

            // Expressions that have been simplified down to a single primitive don't
            // have any effect, and are automatically removed during minification.
            // However, some people are really bothered by seeing them. Remove them
            // so we don't bother these people.
            if (shouldTrimUnsightlyPrimitives && isUnsightlyPrimitive(sExpr->value.data)) {
                return stmts;
            }
        } else if (auto *sThrow = Get<SThrow>(stmt.data); sThrow != nullptr) {
            sThrow->value = this->visitExpr(sThrow->value);
        } else if (auto *sReturn = Get<SReturn>(stmt.data); sReturn != nullptr) {
            // Forbid top-level return inside modules with ECMAScript syntax
            if (this->fnOrArrowDataVisit.isOutsideFnOrArrow) {
                if (this->isFileConsideredESM) {
                    std::vector<logger::MsgData> notes = this->whyESModule().second;
                    this->log.AddErrorWithNotes(&this->tracker, RangeOfIdentifier(this->source, stmt.loc),
                        logger::FormatMsg(logger::MsgCat::kJS_TopLevelReturnInESM), notes);
                } else {
                    this->hasTopLevelReturn = true;
                }
            }

            if (!IsNil(sReturn->value_or_nil.data)) {
                sReturn->value_or_nil = this->visitExpr(sReturn->value_or_nil);

                // Returning undefined is implicit except when inside an async generator
                // function, where "return undefined" behaves like "return await undefined"
                // but just "return" has no "await".
                if (this->options.optionsThatSupportStructuralEquality.minifySyntax && (!this->fnOrArrowDataVisit.isAsync || !this->fnOrArrowDataVisit.isGenerator)) {
                    if (Get<EUndefined>(sReturn->value_or_nil.data) != nullptr) {
                        sReturn->value_or_nil = Expr{};
                    }
                }
            }
        } else if (auto *sBlock = Get<SBlock>(stmt.data); sBlock != nullptr) {
            this->pushScopeForVisitPass(ScopeKind::kBlock, stmt.loc);

            // Pass the "is loop body" status on to the direct children of a block used
            // as a loop body. This is used to enable optimizations specific to the
            // topmost scope in a loop body block.
            if (this->loopBody == &stmt.data) {
                sBlock->stmts = this->visitStmts(sBlock->stmts, stmtsLoopBody);
            } else {
                sBlock->stmts = this->visitStmts(sBlock->stmts, stmtsNormal);
            }

            this->popScope();

            if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                if (sBlock->stmts.size() == 1 && !stmtCaresAboutScope(sBlock->stmts[0])) {
                    // Unwrap blocks containing a single statement
                    stmt = sBlock->stmts[0];
                } else if (sBlock->stmts.empty()) {
                    // Trim empty blocks
                    stmt = Stmt{kSEmptyShared, stmt.loc};
                }
            }
        } else if (auto *sWith = Get<SWith>(stmt.data); sWith != nullptr) {
            this->markStrictModeFeature(withStatement, RangeOfIdentifier(this->source, stmt.loc), "");
            sWith->value = this->visitExpr(sWith->value);
            this->pushScopeForVisitPass(ScopeKind::kWith, sWith->body_loc);
            sWith->body = this->visitSingleStmt(sWith->body, stmtsNormal);
            this->popScope();
        } else if (auto *sWhile = Get<SWhile>(stmt.data); sWhile != nullptr) {
            sWhile->test = this->visitExpr(sWhile->test);
            sWhile->body = this->visitLoopBody(sWhile->body);

            if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                sWhile->test = this->astHelpers.SimplifyBooleanExpr(sWhile->test);

                // A true value is implied
                Expr testOrNil = sWhile->test;
                if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(sWhile->test.data); boolean && boolean->first && boolean->second == SideEffects::kNoSideEffects) {
                    testOrNil = Expr{};
                }

                // "while (a) {}" => "for (;a;) {}"
                SFor *forS = new SFor();
                forS->test_or_nil = testOrNil;
                forS->body = sWhile->body;
                forS->is_single_line_body = sWhile->is_single_line_body;
                mangleFor(forS);
                stmt = Stmt{std::shared_ptr<SFor>(forS), stmt.loc};
            }
        } else if (auto *sDoWhile = Get<SDoWhile>(stmt.data); sDoWhile != nullptr) {
            sDoWhile->body = this->visitLoopBody(sDoWhile->body);
            sDoWhile->test = this->visitExpr(sDoWhile->test);

            if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                sDoWhile->test = this->astHelpers.SimplifyBooleanExpr(sDoWhile->test);
            }
        } else if (auto *SIfPtr = std::get_if<std::shared_ptr<SIf>>(&stmt.data); SIfPtr != nullptr) {
            const std::shared_ptr<SIf> sIf = *SIfPtr;
            sIf->test = this->visitExpr(sIf->test);

            if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                sIf->test = this->astHelpers.SimplifyBooleanExpr(sIf->test);
            }

            // Fold constants
            std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(sIf->test.data);

            // Mark the control flow as dead if the branch is never taken
            if (boolean && !boolean->first) {
                bool old = this->isControlFlowDead;
                this->isControlFlowDead = true;
                sIf->yes = this->visitSingleStmt(sIf->yes, stmtsNormal);
                this->isControlFlowDead = old;
            } else {
                sIf->yes = this->visitSingleStmt(sIf->yes, stmtsNormal);
            }

            // The "else" clause is optional
            if (sIf->no_or_nil != nullptr) {
                // Mark the control flow as dead if the branch is never taken
                if (boolean && boolean->first) {
                    bool old = this->isControlFlowDead;
                    this->isControlFlowDead = true;
                    sIf->no_or_nil = std::make_shared<Stmt>(this->visitSingleStmt(*sIf->no_or_nil, stmtsNormal));
                    this->isControlFlowDead = old;
                } else {
                    sIf->no_or_nil = std::make_shared<Stmt>(this->visitSingleStmt(*sIf->no_or_nil, stmtsNormal));
                }

                // Trim unnecessary "else" clauses
                if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                    if (Get<SEmpty>(sIf->no_or_nil->data) != nullptr) {
                        sIf->no_or_nil = nullptr;
                    }
                }
            }

            if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                return this->mangleIf(stmts, stmt.loc, sIf);
            }
        } else if (auto *sFor = Get<SFor>(stmt.data); sFor != nullptr) {
            this->pushScopeForVisitPass(ScopeKind::kBlock, stmt.loc);
            if (sFor->init_or_nil != nullptr) {
                sFor->init_or_nil = std::make_shared<Stmt>(this->visitForLoopInit(*sFor->init_or_nil, false));
            }

            if (!IsNil(sFor->test_or_nil.data)) {
                sFor->test_or_nil = this->visitExpr(sFor->test_or_nil);

                if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                    sFor->test_or_nil = this->astHelpers.SimplifyBooleanExpr(sFor->test_or_nil);

                    // A true value is implied
                    if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(sFor->test_or_nil.data); boolean && boolean->first && boolean->second == SideEffects::kNoSideEffects) {
                        sFor->test_or_nil = Expr{};
                    }
                }
            }

            if (!IsNil(sFor->update_or_nil.data)) {
                sFor->update_or_nil = this->visitExpr(sFor->update_or_nil);
            }
            sFor->body = this->visitLoopBody(sFor->body);

            // Potentially relocate "var" declarations to the top level. Note that this
            // must be done inside the scope of the for loop or they won't be relocated.
            if (sFor->init_or_nil != nullptr) {
                if (auto *init = Get<SLocal>(sFor->init_or_nil->data); init != nullptr && init->kind == LocalKind::kVar) {
                    std::pair<Stmt, bool> relocated = this->maybeRelocateVarsToTopLevel(init->decls, relocateVarsNormal);
                    if (relocated.second) {
                        if (!IsNil(relocated.first.data)) {
                            sFor->init_or_nil = std::make_shared<Stmt>(relocated.first);
                        } else {
                            sFor->init_or_nil = nullptr;
                        }
                    }
                }
            }

            this->popScope();

            if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                mangleFor(sFor);
            }
        } else if (auto *sForIn = Get<SForIn>(stmt.data); sForIn != nullptr) {
            this->pushScopeForVisitPass(ScopeKind::kBlock, stmt.loc);
            sForIn->init = std::make_shared<Stmt>(this->visitForLoopInit(*sForIn->init, true));
            sForIn->value = this->visitExpr(sForIn->value);
            sForIn->body = this->visitLoopBody(sForIn->body);

            // Check for a variable initializer
            if (auto *local = Get<SLocal>(sForIn->init->data); local != nullptr && local->kind == LocalKind::kVar && local->decls.size() == 1) {
                Decl *decl = &local->decls[0];
                if (auto *id = Get<BIdentifier>(decl->binding.data); id != nullptr && !IsNil(decl->value_or_nil.data)) {
                    this->markStrictModeFeature(forInVarInit, this->source.RangeOfOperatorBefore(decl->value_or_nil.loc, "="), "");

                    // Lower for-in variable initializers in case the output is used in strict mode
                    SExpr *sexpr = new SExpr();
                    EIdentifier *id2 = new EIdentifier();
                    id2->ref = id->ref;
                    sexpr->value = Assign(Expr{std::shared_ptr<EIdentifier>(id2), decl->binding.loc}, decl->value_or_nil);
                    stmts.push_back(Stmt{std::shared_ptr<SExpr>(sexpr), stmt.loc});
                    decl->value_or_nil = Expr{};
                }
            }

            // Potentially relocate "var" declarations to the top level. Note that this
            // must be done inside the scope of the for loop or they won't be relocated.
            if (auto *init = Get<SLocal>(sForIn->init->data); init != nullptr && init->kind == LocalKind::kVar) {
                std::pair<Stmt, bool> replacement = this->maybeRelocateVarsToTopLevel(init->decls, relocateVarsForInOrForOf);
                if (replacement.second) {
                    sForIn->init = std::make_shared<Stmt>(replacement.first);
                }
            }

            this->popScope();

            this->lowerObjectRestInForLoopInit(*sForIn->init, &sForIn->body);
        } else if (auto *sForOf = Get<SForOf>(stmt.data); sForOf != nullptr) {
            // Silently remove unsupported top-level "await" in dead code branches
            if (sForOf->await.len > 0 && this->fnOrArrowDataVisit.isOutsideFnOrArrow) {
                if (this->isControlFlowDead && (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kTopLevelAwait) || !config::FormatKeepESMImportExportSyntax(this->options.optionsThatSupportStructuralEquality.outputFormat))) {
                    sForOf->await = logger::Range{};
                } else {
                    this->liveTopLevelAwaitKeyword = sForOf->await;
                    this->markSyntaxFeature(compat::JSFeature::kTopLevelAwait, sForOf->await);
                }
            }

            this->pushScopeForVisitPass(ScopeKind::kBlock, stmt.loc);
            sForOf->init = std::make_shared<Stmt>(this->visitForLoopInit(*sForOf->init, true));
            sForOf->value = this->visitExpr(sForOf->value);
            sForOf->body = this->visitLoopBody(sForOf->body);

            // Potentially relocate "var" declarations to the top level. Note that this
            // must be done inside the scope of the for loop or they won't be relocated.
            if (auto *init = Get<SLocal>(sForOf->init->data); init != nullptr && init->kind == LocalKind::kVar) {
                std::pair<Stmt, bool> replacement = this->maybeRelocateVarsToTopLevel(init->decls, relocateVarsForInOrForOf);
                if (replacement.second) {
                    sForOf->init = std::make_shared<Stmt>(replacement.first);
                }
            }

            // Handle "for (using x of y)" and "for (await using x of y)"
            if (auto *local = Get<SLocal>(sForOf->init->data); local != nullptr) {
                if (local->kind == LocalKind::kUsing && compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kUsing)) {
                    this->lowerUsingDeclarationInForOf(sForOf->init->loc, local, &sForOf->body);
                } else if (local->kind == LocalKind::kAwaitUsing) {
                    if (this->fnOrArrowDataVisit.isOutsideFnOrArrow) {
                        if (this->isControlFlowDead && (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kTopLevelAwait) || !config::FormatKeepESMImportExportSyntax(this->options.optionsThatSupportStructuralEquality.outputFormat))) {
                            // Silently remove unsupported top-level "await" in dead code branches
                            local->kind = LocalKind::kUsing;
                        } else {
                            this->liveTopLevelAwaitKeyword = logger::Range{sForOf->init->loc, 5};
                            this->markSyntaxFeature(compat::JSFeature::kTopLevelAwait, this->liveTopLevelAwaitKeyword);
                        }
                        if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kUsing)) {
                            this->lowerUsingDeclarationInForOf(sForOf->init->loc, local, &sForOf->body);
                        }
                    } else if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kUsing) || compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncAwait) ||
                        (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncGenerator) && this->fnOrArrowDataVisit.isGenerator)) {
                        this->lowerUsingDeclarationInForOf(sForOf->init->loc, local, &sForOf->body);
                    }
                }
            }

            this->popScope();

            this->lowerObjectRestInForLoopInit(*sForOf->init, &sForOf->body);

            // Lower "for await" if it's unsupported if it's in a lowered async generator
            if (sForOf->await.len > 0 && (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kForAwait) ||
                (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncGenerator) && this->fnOrArrowDataVisit.isGenerator))) {
                return this->lowerForAwaitLoop(stmt.loc, sForOf, stmts);
            }
        } else if (auto *sTry = Get<STry>(stmt.data); sTry != nullptr) {
            this->pushScopeForVisitPass(ScopeKind::kBlock, stmt.loc);
            if (this->fnOrArrowDataVisit.tryBodyCount == 0) {
                if (sTry->catch_block != nullptr) {
                    this->fnOrArrowDataVisit.tryCatchLoc = sTry->catch_block->loc;
                } else {
                    this->fnOrArrowDataVisit.tryCatchLoc = stmt.loc;
                }
            }
            this->fnOrArrowDataVisit.tryBodyCount++;
            sTry->block.stmts = this->visitStmts(sTry->block.stmts, stmtsNormal);
            this->fnOrArrowDataVisit.tryBodyCount--;
            this->popScope();

            if (sTry->catch_block != nullptr) {
                bool old = this->isControlFlowDead;

                // If the try body is empty, then the catch body is dead
                if (sTry->block.stmts.empty()) {
                    this->isControlFlowDead = true;
                }

                this->pushScopeForVisitPass(ScopeKind::kCatchBinding, sTry->catch_block->loc);
                if (!IsNil(sTry->catch_block->binding_or_nil.data)) {
                    this->visitBinding(sTry->catch_block->binding_or_nil, bindingOpts{});
                }

                this->pushScopeForVisitPass(ScopeKind::kBlock, sTry->catch_block->block_loc);
                sTry->catch_block->block.stmts = this->visitStmts(sTry->catch_block->block.stmts, stmtsNormal);
                this->popScope();

                this->lowerObjectRestInCatchBinding(sTry->catch_block.get());
                this->popScope();

                this->isControlFlowDead = old;
            }

            if (sTry->finally_block != nullptr) {
                this->pushScopeForVisitPass(ScopeKind::kBlock, sTry->finally_block->loc);
                sTry->finally_block->block.stmts = this->visitStmts(sTry->finally_block->block.stmts, stmtsNormal);
                this->popScope();
            }

            if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                if (sTry->block.stmts.empty()) {
                    // Try to drop the whole thing if the try body is empty
                    bool keepCatch = false;

                    // Certain "catch" blocks need to be preserved:
                    //
                    //   try {} catch { let foo } // Can be removed
                    //   try {} catch { var foo } // Must be kept
                    //
                    if (sTry->catch_block != nullptr) {
                        for (Stmt stmt2 : sTry->catch_block->block.stmts) {
                            if (shouldKeepStmtInDeadControlFlow(stmt2)) {
                                keepCatch = true;
                                break;
                            }
                        }
                    }

                    // Make sure to preserve the "finally" block if present
                    if (!keepCatch) {
                        if (sTry->finally_block == nullptr) {
                            return stmts;
                        }
                        if (!stmtsCareAboutScope(sTry->finally_block->block.stmts)) {
                            for (Stmt finallyStmt : sTry->finally_block->block.stmts) {
                                stmts.push_back(finallyStmt);
                            }
                            return stmts;
                        }
                        SBlock *block = new SBlock(sTry->finally_block->block);
                        stmt = Stmt{std::shared_ptr<SBlock>(block), sTry->finally_block->loc};
                    }
                } else if (sTry->finally_block != nullptr && sTry->finally_block->block.stmts.empty()) {
                    if (sTry->catch_block != nullptr) {
                        // Just remove the "finally" block if there's a "catch"
                        sTry->finally_block = nullptr;
                    } else {
                        // Otherwise, try to unwrap the whole "try" statement
                        if (!stmtsCareAboutScope(sTry->block.stmts)) {
                            for (Stmt blockStmt : sTry->block.stmts) {
                                stmts.push_back(blockStmt);
                            }
                            return stmts;
                        }
                        SBlock *block = new SBlock(sTry->block);
                        stmt = Stmt{std::shared_ptr<SBlock>(block), sTry->finally_block->loc};
                    }
                }
            }
        } else if (auto *sSwitch = Get<SSwitch>(stmt.data); sSwitch != nullptr) {
            sSwitch->test = this->visitExpr(sSwitch->test);
            this->pushScopeForVisitPass(ScopeKind::kBlock, sSwitch->body_loc);
            bool oldIsInsideSwitch = this->fnOrArrowDataVisit.isInsideSwitch;
            this->fnOrArrowDataVisit.isInsideSwitch = true;

            // Disable const inlining in switch cases. They all share the same scope
            // and can be evaluated in an unusual order. For example:
            //
            //   // This should not become "return 0"
            //   switch (1) {
            //     case 0:
            //       const x = 0
            //     case 1:
            //       return x
            //   }
            //
            //   // This should not become "return 0"
            //   switch (0) {
            //     case 0:
            //       return x
            //     case 1:
            //       const x = 0
            //   }
            //
            this->currentScope->is_after_const_local_prefix = true;

            // Visit case values first
            for (size_t i = 0; i < sSwitch->cases.size(); i++) {
                Case &c = sSwitch->cases[i];
                if (!IsNil(c.value_or_nil.data)) {
                    c.value_or_nil = this->visitExpr(c.value_or_nil);
                    (void)this->warnAboutEqualityCheck("case", c.value_or_nil, c.value_or_nil.loc);
                    this->warnAboutTypeofAndString(sSwitch->test, c.value_or_nil, onlyCheckOriginalOrder);
                }
            }

            // Check for duplicate case values
            this->duplicateCaseChecker.reset();
                for (Case c : sSwitch->cases) {
                if (!IsNil(c.value_or_nil.data)) {
                    this->duplicateCaseChecker.check(this, c.value_or_nil);
                }
            }

            // Then analyze the cases to determine which ones are live and/or dead
            std::vector<switchCaseLiveness> cases = analyzeSwitchCasesForLiveness(sSwitch);

            // Then visit case bodies, and potentially filter out dead cases
            size_t end = 0;
            for (size_t i = 0; i < sSwitch->cases.size(); i++) {
                Case c = sSwitch->cases[i];
                bool isAlwaysDead = cases[i].status == alwaysDead;

                // Potentially treat the case body as dead code
                bool old = this->isControlFlowDead;
                if (isAlwaysDead) {
                    this->isControlFlowDead = true;
                }
                c.body = this->visitStmts(c.body, stmtsNormal);
                this->isControlFlowDead = old;

                // Filter out this case when minifying if it's known to be dead. Visiting
                // the body above should already have removed any statements that can be
                // removed safely, so if the body isn't empty then that means it contains
                // some statements that can't be removed safely (e.g. a hoisted "var").
                // So don't remove this case if the body isn't empty.
                if (this->options.optionsThatSupportStructuralEquality.minifySyntax && isAlwaysDead && c.body.empty()) {
                    continue;
                }

                // Make sure the assignment to the body above is preserved
                sSwitch->cases[end] = c;
                end++;
            }
            sSwitch->cases.resize(end);

            this->fnOrArrowDataVisit.isInsideSwitch = oldIsInsideSwitch;
            this->popScope();

            // Unwrap switch statements in dead code
            if (this->options.optionsThatSupportStructuralEquality.minifySyntax && this->isControlFlowDead) {
            for (Case c : sSwitch->cases) {
                    for (Stmt caseStmt : c.body) {
                        stmts.push_back(caseStmt);
                    }
                }
                return stmts;
            }

            if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
                return this->minifySwitchStmt(stmt.loc, *std::get_if<std::shared_ptr<SSwitch>>(&stmt.data), stmts);
            }
        } else if (auto *sFn = Get<SFunction>(stmt.data); sFn != nullptr) {
            this->visitFn(&sFn->fn, sFn->fn.open_paren_loc, visitFnOpts{});

            // Strip this function declaration if it was overwritten
            if (compiler::Has(this->symbols[sFn->fn.name->ref.inner_index].flags, compiler::SymbolFlags::kRemoveOverwrittenFunctionDeclaration) && !sFn->is_export) {
                return stmts;
            }

            if (this->options.optionsThatSupportStructuralEquality.minifySyntax && !sFn->fn.is_generator && !sFn->fn.is_async && !sFn->fn.has_rest_arg && sFn->fn.name != nullptr) {
                if (sFn->fn.body.block.stmts.empty()) {
                    // Mark if this function is an empty function
                    bool hasSideEffectFreeArguments = true;
                    for (Arg arg : sFn->fn.args) {
                        if (Get<BIdentifier>(arg.binding.data) == nullptr) {
                            hasSideEffectFreeArguments = false;
                            break;
                        }
                    }
                    if (hasSideEffectFreeArguments) {
                        this->symbols[sFn->fn.name->ref.inner_index].flags = this->symbols[sFn->fn.name->ref.inner_index].flags | compiler::SymbolFlags::kIsEmptyFunction;
                    }
                } else if (sFn->fn.args.size() == 1 && sFn->fn.body.block.stmts.size() == 1) {
                    // Mark if this function is an identity function
                    Arg arg = sFn->fn.args[0];
                    if (IsNil(arg.default_or_nil.data)) {
                        if (auto *id = Get<BIdentifier>(arg.binding.data); id != nullptr) {
                            if (auto *ret = Get<SReturn>(sFn->fn.body.block.stmts[0].data); ret != nullptr) {
                                if (auto *retID = Get<EIdentifier>(ret->value_or_nil.data); retID != nullptr && id->ref == retID->ref) {
                                    this->symbols[sFn->fn.name->ref.inner_index].flags = this->symbols[sFn->fn.name->ref.inner_index].flags | compiler::SymbolFlags::kIsIdentityFunction;
                                }
                            }
                        }
                    }
                }
            }

            // Handle exporting this function from a namespace
            if (sFn->is_export && this->enclosingNamespaceArgRef != nullptr) {
                sFn->is_export = false;
                EIdentifier *targetId = new EIdentifier();
                targetId->ref = *this->enclosingNamespaceArgRef;
                EIdentifier *fnId = new EIdentifier();
                fnId->ref = sFn->fn.name->ref;
                std::string originalName = this->symbols[sFn->fn.name->ref.inner_index].original_name;
                Expr target{*this->dotOrMangledPropVisit(Expr{std::shared_ptr<EIdentifier>(targetId), stmt.loc}, originalName, sFn->fn.name->loc), stmt.loc};
                stmts.push_back(stmt);
                stmts.push_back(AssignStmt(target, Expr{std::shared_ptr<EIdentifier>(fnId), sFn->fn.name->loc}));
            } else {
                stmts.push_back(stmt);
            }

            // Optionally preserve the name
            if (this->options.optionsThatSupportStructuralEquality.keepNames) {
                compiler::Symbol &symbol = this->symbols[sFn->fn.name->ref.inner_index];
                symbol.flags = symbol.flags | compiler::SymbolFlags::kDidKeepName;
                EIdentifier *fnId = new EIdentifier();
                fnId->ref = sFn->fn.name->ref;
                Expr fn{std::shared_ptr<EIdentifier>(fnId), sFn->fn.name->loc};
                stmts.push_back(this->keepClassOrFnSymbolName(sFn->fn.name->loc, fn, symbol.original_name));
            }
            return stmts;
        } else if (auto *sClass = Get<SClass>(stmt.data); sClass != nullptr) {
            visitClassResult result = this->visitClass(stmt.loc, &sClass->class_, compiler::kInvalidRef, "");

            // Remove the export flag inside a namespace
            std::string nameToExport;
            bool wasExportInsideNamespace = sClass->is_export && this->enclosingNamespaceArgRef != nullptr;
            if (wasExportInsideNamespace) {
                nameToExport = this->symbols[sClass->class_.name->ref.inner_index].original_name;
                sClass->is_export = false;
            }

            // Lower class field syntax for browsers that don't support it
            auto lowered = this->lowerClass(stmt, Expr{}, result, "");
            std::vector<Stmt> &classStmts = lowered.first;

            // Remember if the class was side-effect free before lowering
            if (result.canBeRemovedIfUnused) {
                for (Stmt classStmt : classStmts) {
                    if (auto *classSExpr = Get<SExpr>(classStmt.data); classSExpr != nullptr) {
                        classSExpr->is_from_class_or_fn_that_can_be_removed_if_unused = true;
                    }
                }
            }

            for (Stmt classStmt : classStmts) {
                stmts.push_back(classStmt);
            }

            // Handle exporting this class from a namespace
            if (wasExportInsideNamespace) {
                EIdentifier *targetId = new EIdentifier();
                targetId->ref = *this->enclosingNamespaceArgRef;
                EIdentifier *classId = new EIdentifier();
                classId->ref = sClass->class_.name->ref;
                Expr target{*this->dotOrMangledPropVisit(Expr{std::shared_ptr<EIdentifier>(targetId), stmt.loc}, nameToExport, sClass->class_.name->loc), stmt.loc};
                stmts.push_back(AssignStmt(target, Expr{std::shared_ptr<EIdentifier>(classId), sClass->class_.name->loc}));
            }

            return stmts;
        } else if (auto *sEnum = Get<SEnum>(stmt.data); sEnum != nullptr) {
            // Do not end the const local prefix after TypeScript enums. We process
            // them first within their scope so that they are inlined into all code in
            // that scope. We don't want that to cause the const local prefix to end.
            this->currentScope->is_after_const_local_prefix = wasAfterAfterConstLocalPrefix;

            // Track cross-module enum constants during bundling
            bool hasTsTopLevelEnumValues = this->currentScope == this->moduleScope && this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle;
            std::unordered_map<std::string, TSEnumValue> tsTopLevelEnumValues;

            this->recordDeclaredSymbol(sEnum->name.ref);
            this->pushScopeForVisitPass(ScopeKind::kEntry, stmt.loc);
            this->recordDeclaredSymbol(sEnum->arg);

            // Scan ahead for any variables inside this namespace. This must be done
            // ahead of time before visiting any statements inside the namespace
            // because we may end up visiting the uses before the declarations.
            // We need to convert the uses into property accesses on the namespace.
            for (EnumValue value : sEnum->values) {
                if (value.ref != compiler::kInvalidRef) {
                    this->isExportedInsideNamespace[value.ref] = sEnum->arg;
                }
            }

            // Values without initializers are initialized to one more than the
            // previous value if the previous value is numeric. Otherwise values
            // without initializers are initialized to undefined.
            double nextNumericValue = 0;
            bool hasNumericValue = true;
            std::vector<Expr> valueExprs;
            bool allValuesArePure = true;

            // Update the exported members of this enum as we constant fold each one
            std::unordered_map<std::string, TSNamespaceMember> &exportedMembers = *this->currentScope->ts_namespace->exported_members;

            // We normally don't fold numeric constants because they might increase code
            // size, but it's important to fold numeric constants inside enums since
            // that's what the TypeScript compiler does.
            bool oldShouldFoldTypeScriptConstantExpressions = this->shouldFoldTypeScriptConstantExpressions;
            this->shouldFoldTypeScriptConstantExpressions = true;

            // Create an assignment for each enum value
            for (EnumValue value : sEnum->values) {
                std::string name = helpers::UTF16ToString(value.name);
                Expr assignTarget;
                bool hasStringValue = false;

                if (!IsNil(value.value_or_nil.data)) {
                    value.value_or_nil = this->visitExpr(value.value_or_nil);
                    hasNumericValue = false;

                    // "See through" any wrapped comments
                    Expr underlyingValue = value.value_or_nil;
                    if (auto *inlined = Get<EInlinedEnum>(underlyingValue.data); inlined != nullptr) {
                        underlyingValue = inlined->value;
                    }

                    if (auto *e = Get<ENumber>(underlyingValue.data); e != nullptr) {
                        if (hasTsTopLevelEnumValues) {
                            tsTopLevelEnumValues[name] = TSEnumValue{u"", e->value, false};
                        }
                        TSNamespaceMember &member = exportedMembers[name];
                        member.data = std::make_shared<TSNamespaceMemberEnumNumber>(TSNamespaceMemberEnumNumber{e->value});
                        this->refToTSNamespaceMemberData[value.ref] = member.data;
                        hasNumericValue = true;
                        nextNumericValue = e->value + 1;
                    } else if (auto *eStr = Get<EString>(underlyingValue.data); eStr != nullptr) {
                        if (hasTsTopLevelEnumValues) {
                            tsTopLevelEnumValues[name] = TSEnumValue{eStr->value, 0.0, true};
                        }
                        TSNamespaceMember &member = exportedMembers[name];
                        member.data = std::make_shared<TSNamespaceMemberEnumString>(TSNamespaceMemberEnumString{eStr->value});
                        this->refToTSNamespaceMemberData[value.ref] = member.data;
                        hasStringValue = true;
                    } else {
                        if (KnownPrimitiveType(underlyingValue.data) == PrimitiveType::kString) {
                            hasStringValue = true;
                        }
                        if (!this->astHelpers.ExprCanBeRemovedIfUnused(underlyingValue)) {
                            allValuesArePure = false;
                        }
                    }
                } else if (hasNumericValue) {
                    if (hasTsTopLevelEnumValues) {
                        tsTopLevelEnumValues[name] = TSEnumValue{u"", nextNumericValue, false};
                    }
                    TSNamespaceMember &member = exportedMembers[name];
                    member.data = std::make_shared<TSNamespaceMemberEnumNumber>(TSNamespaceMemberEnumNumber{nextNumericValue});
                    this->refToTSNamespaceMemberData[value.ref] = member.data;
                    ENumber *num = new ENumber();
                    num->value = nextNumericValue;
                    value.value_or_nil = Expr{std::shared_ptr<ENumber>(num), value.loc};
                    nextNumericValue++;
                } else {
                    value.value_or_nil = Expr{kEUndefinedShared, value.loc};
                }

                if (this->options.optionsThatSupportStructuralEquality.minifySyntax && IsIdentifier(helpers::UTF16ToString(value.name))) {
                    // "Enum.Name = value"
                    EIdentifier *targetId = new EIdentifier();
                    targetId->ref = sEnum->arg;
                    EDot *dot = new EDot();
                    dot->target = Expr{std::shared_ptr<EIdentifier>(targetId), value.loc};
                    dot->name = name;
                    dot->name_loc = value.loc;
                    assignTarget = Assign(Expr{std::shared_ptr<EDot>(dot), value.loc}, value.value_or_nil);
                } else {
                    // "Enum['Name'] = value"
                    EIdentifier *targetId = new EIdentifier();
                    targetId->ref = sEnum->arg;
                    EIndex *index = new EIndex();
                    index->target = Expr{std::shared_ptr<EIdentifier>(targetId), value.loc};
                    EString *estr = new EString();
                    estr->value = value.name;
                    index->index = Expr{std::shared_ptr<EString>(estr), value.loc};
                    assignTarget = Assign(Expr{std::shared_ptr<EIndex>(index), value.loc}, value.value_or_nil);
                }
                this->recordUsage(sEnum->arg);

                // String-valued enums do not form a two-way map
                if (hasStringValue) {
                    valueExprs.push_back(assignTarget);
                } else {
                    // "Enum[assignTarget] = 'Name'"
                    EIdentifier *targetId = new EIdentifier();
                    targetId->ref = sEnum->arg;
                    EIndex *index = new EIndex();
                    index->target = Expr{std::shared_ptr<EIdentifier>(targetId), value.loc};
                    index->index = assignTarget;
                    EString *estr = new EString();
                    estr->value = value.name;
                    valueExprs.push_back(Assign(Expr{std::shared_ptr<EIndex>(index), value.loc}, Expr{std::shared_ptr<EString>(estr), value.loc}));
                    this->recordUsage(sEnum->arg);
                }
            }

            this->popScope();
            this->shouldFoldTypeScriptConstantExpressions = oldShouldFoldTypeScriptConstantExpressions;

            // Track all exported top-level enums for cross-module inlining
            if (hasTsTopLevelEnumValues) {
                this->tsEnums[sEnum->name.ref] = tsTopLevelEnumValues;
            }

            // Wrap this enum definition in a closure
            stmts = this->generateClosureForTypeScriptEnum(stmts, stmt.loc, sEnum->is_export, sEnum->name.loc, sEnum->name.ref, sEnum->arg, valueExprs, allValuesArePure);
            return stmts;
        } else if (auto *sNamespace = Get<SNamespace>(stmt.data); sNamespace != nullptr) {
            this->recordDeclaredSymbol(sNamespace->name.ref);

            // Scan ahead for any variables inside this namespace. This must be done
            // ahead of time before visiting any statements inside the namespace
            // because we may end up visiting the uses before the declarations.
            // We need to convert the uses into property accesses on the namespace.
            for (Stmt childStmt : sNamespace->stmts) {
                if (auto *local = Get<SLocal>(childStmt.data); local != nullptr) {
                    if (local->is_export) {
                        ForEachIdentifierBindingInDecls(local->decls, [this, &sNamespace](logger::Loc /*loc*/, BIdentifier &b) {
                            this->isExportedInsideNamespace[b.ref] = sNamespace->arg;
                        });
                    }
                }
            }

            compiler::Ref *oldEnclosingNamespaceArgRef = this->enclosingNamespaceArgRef;
            this->enclosingNamespaceArgRef = &sNamespace->arg;
            this->pushScopeForVisitPass(ScopeKind::kEntry, stmt.loc);
            this->recordDeclaredSymbol(sNamespace->arg);
            std::vector<Stmt> stmtsInsideNamespace = this->visitStmtsAndPrependTempRefs(sNamespace->stmts, prependTempRefsOpts{nullptr, stmtsFnBody});
            this->popScope();
            this->enclosingNamespaceArgRef = oldEnclosingNamespaceArgRef;

            // Generate a closure for this namespace
            stmts = this->generateClosureForTypeScriptNamespaceOrEnum(stmts, stmt.loc, sNamespace->is_export, sNamespace->name.loc, sNamespace->name.ref, sNamespace->arg, stmtsInsideNamespace);
            return stmts;
        } else {
            // TODO: panic("Internal error")
        }

        stmts.push_back(stmt);
        return stmts;
    }

    // The main expression visitor. Takes an expression and input flags (exprIn), visits
    // the expression recursively, performs type-checking, constant folding, error reporting,
    // and syntax lowering, and returns the (possibly transformed) expression along with
    // output flags (exprOut) for optional chaining and side-effect tracking.
    //
    // This is the heart of the visit pass. The giant if/else-if chain handles every
    // expression type. Binary expressions use an iterative approach via binaryExprVisitor
    // to avoid stack overflow on deeply-nested left-associative chains.
    //
    // Key behaviors per expression type:
    //   - Literals (null, boolean, number, string, bigint): minimal processing, optional
    //     constant folding and mangled property mangling
    //   - Identifiers: symbol resolution, define substitution, import item conversion
    //   - Dot/Index: define matching, optional chain lowering, property access rewriting
    //   - Binary: iterative left-associative handling, operator-specific folding/warnings
    //   - Call: require/import rewriting, direct eval detection, IIFE inlining
    //   - Arrow/Function/Class: full sub-tree visiting with scope management
    //   - JSX: transformation to createElement() or jsx() calls
    //   - Template literals: inline primitives, lower for unsupported environments
    std::pair<Expr, exprOut> Parser::visitExprInOut(Expr expr, exprIn in) {
        auto &structural = this->options.optionsThatSupportStructuralEquality;

        if (in.assignTarget != AssignTarget::kNone && !this->isValidAssignmentTarget(expr)) {
            this->log.AddError(&this->tracker, logger::Range{/*loc=*/expr.loc}, logger::FormatMsg(logger::MsgCat::kJS_InvalidAssignmentTarget));
        }

        // Note: Anything added before or after this switch statement will be bypassed
        // when visiting nested "EBinary" nodes due to stack overflow mitigations for
        // deeply-nested ASTs. If anything like that is added, care must be taken that
        // it doesn't affect these mitigations by ensuring that the mitigations are not
        // applied in those cases (e.g. by adding an additional conditional check).
        if (Get<ENull>(expr.data) != nullptr || Get<ESuper>(expr.data) != nullptr ||
            Get<EBoolean>(expr.data) != nullptr || Get<EUndefined>(expr.data) != nullptr ||
            Get<EJSXText>(expr.data) != nullptr) {
            // No-op
        } else if (auto *eBigInt = Get<EBigInt>(expr.data); eBigInt != nullptr) {
            if (compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kBigint)) {
                // For ease of implementation, the actual reference of the "BigInt"
                // symbol is deferred to print time. That means we don't have to
                // special-case the "BigInt" constructor in side-effect computations
                // and future big integer constant folding (of which there isn't any
                // at the moment).
                this->markSyntaxFeature(compat::JSFeature::kBigint, this->source.RangeOfNumber(expr.loc));
                this->recordUsage(this->makeBigIntRef());
            }
        } else if (auto *eNameOfSymbol = Get<ENameOfSymbol>(expr.data); eNameOfSymbol != nullptr) {
            eNameOfSymbol->ref = this->symbolForMangledProp(this->loadNameFromRef(eNameOfSymbol->ref));
        } else if (auto *eRegExp = Get<ERegExp>(expr.data); eRegExp != nullptr) {
            // "/pattern/flags" => "new RegExp('pattern', 'flags')"
            auto [rePattern, reFlags, reOk] = this->isUnsupportedRegularExpression(expr.loc, eRegExp->value);
            if (reOk) {
                std::vector<Expr> args;
                std::shared_ptr<EString> patternString = std::make_shared<EString>();
                patternString->value = helpers::StringToUTF16(rePattern);
                args.push_back(Expr{patternString, logger::Loc{/*start=*/expr.loc.start + 1}});
                if (!reFlags.empty()) {
                    std::shared_ptr<EString> flagsString = std::make_shared<EString>();
                    flagsString->value = helpers::StringToUTF16(reFlags);
                    args.push_back(Expr{flagsString, logger::Loc{/*start=*/expr.loc.start + static_cast<int32_t>(rePattern.size()) + 2}});
                }
                compiler::Ref regExpRefLocal = this->makeRegExpRef();
                this->recordUsage(regExpRefLocal);
                std::shared_ptr<EIdentifier> target = std::make_shared<EIdentifier>();
                target->ref = regExpRefLocal;
                std::shared_ptr<ENew> created = std::make_shared<ENew>();
                created->target = Expr{target, expr.loc};
                created->args = args;
                created->close_paren_loc = logger::Loc{/*start=*/expr.loc.start + static_cast<int32_t>(eRegExp->value.size())};
                return {Expr{created, expr.loc}, exprOut{}};
            }
        } else if (auto *e = Get<ENewTarget>(expr.data); e != nullptr) {
            if (!this->fnOnlyDataVisit.isNewTargetAllowed) {
                this->log.AddError(&this->tracker, e->range, logger::FormatMsg(logger::MsgCat::kJS_CannotUseNewTarget));
            }
        } else if (auto *eStr = Get<EString>(expr.data); eStr != nullptr) {
            if (eStr->legacy_octal_loc.start > 0) {
                if (eStr->prefer_template) {
                    this->log.AddError(&this->tracker, this->source.RangeOfLegacyOctalEscape(eStr->legacy_octal_loc),
                        logger::FormatMsg(logger::MsgCat::kJS_LegacyOctalInTemplate));
                } else if (this->isStrictMode()) {
                    this->markStrictModeFeature(legacyOctalEscape, this->source.RangeOfLegacyOctalEscape(eStr->legacy_octal_loc), "");
                }
            }

            if (in.shouldMangleStringsAsProps && structural.mangleQuoted && !eStr->prefer_template) {
                std::string name = helpers::UTF16ToString(eStr->value);
                if (this->isMangledProp(name)) {
                    std::shared_ptr<ENameOfSymbol> mangled = std::make_shared<ENameOfSymbol>();
                    mangled->ref = this->symbolForMangledProp(name);
                    mangled->has_property_key_comment = eStr->has_property_key_comment;
                    return {Expr{mangled, expr.loc}, exprOut{}};
                }
            }
        } else if (auto *eNum = Get<ENumber>(expr.data); eNum != nullptr) {
            (void)eNum;
            if (!this->legacyOctalLiterals.empty() && this->isStrictMode()) {
                auto it = this->legacyOctalLiterals.find(eNum);
                if (it != this->legacyOctalLiterals.end()) {
                    this->markStrictModeFeature(legacyOctalLiteral, it->second, "");
                }
            }
        } else if (auto *eThis = Get<EThis>(expr.data); eThis != nullptr) {
            (void)eThis;
            bool isDeleteTarget = this->deleteTarget != E{} && IsSameNodeAs(this->deleteTarget, expr.data);
            bool isCallTarget = this->callTarget != E{} && IsSameNodeAs(this->callTarget, expr.data);

            this->fnOnlyDataVisit.hasThisUsage = true;

            if (std::pair<Expr, bool> vf = this->valueForThis(expr.loc, true /* shouldLog */, in.assignTarget, isDeleteTarget, isCallTarget); vf.second) {
                return {vf.first, exprOut{}};
            }

            // Capture "this" inside arrow functions that will be lowered into normal
            // function expressions for older language environments
            if (this->fnOrArrowDataVisit.isArrow && compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kArrow) && this->fnOnlyDataVisit.isThisNested) {
                std::shared_ptr<EIdentifier> thisCapture = std::make_shared<EIdentifier>();
                thisCapture->ref = this->captureThis();
                return {Expr{thisCapture, expr.loc}, exprOut{}};
            }
        } else if (auto *eImportMeta = Get<EImportMeta>(expr.data); eImportMeta != nullptr) {
            bool isDeleteTarget = this->deleteTarget != E{} && IsSameNodeAs(this->deleteTarget, expr.data);
            bool isCallTarget = this->callTarget != E{} && IsSameNodeAs(this->callTarget, expr.data);

            // Check both user-specified defines and known globals
            auto metaDefinesIt = this->options.defines->DotDefines.find("meta");
            if (metaDefinesIt != this->options.defines->DotDefines.end()) {
                for (config::DefineData &define : metaDefinesIt->second) {
                    if (this->isDotOrIndexDefineMatch(expr, define.KeyParts)) {
                        // Substitute user-specified defines
                        if (define.DefineExprData != nullptr) {
                            return {this->instantiateDefineExpr(expr.loc, *define.DefineExprData, identifierOpts{/*assignTarget=*/in.assignTarget, /*isCallTarget=*/isCallTarget, /*isDeleteTarget=*/isDeleteTarget, /*preferQuotedKey=*/false, /*wasOriginallyIdentifier=*/false, /*matchAgainstDefines=*/false}), exprOut{}};
                        }
                    }
                }
            }

            // Check injected dot names
            auto metaInjectedIt = this->injectedDotNames.find("meta");
            if (metaInjectedIt != this->injectedDotNames.end()) {
                for (injectedDotName &name : metaInjectedIt->second) {
                    if (this->isDotOrIndexDefineMatch(expr, name.parts)) {
                        // Note: We don't need to "ignoreRef" on the underlying identifier
                        // because we have only parsed it but not visited it yet
                        return {this->instantiateInjectDotName(expr.loc, name, in.assignTarget), exprOut{}};
                    }
                }
            }

            // Warn about "import.meta" if it's not replaced by a define
            if (compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kImportMeta)) {
                logger::Range r = logger::Range{/*loc=*/expr.loc, /*len=*/eImportMeta->range_len};
                this->markSyntaxFeature(compat::JSFeature::kImportMeta, r);
            } else if (structural.mode != config::Mode::kPassThrough && !config::FormatKeepESMImportExportSyntax(structural.outputFormat)) {
                logger::Range r = logger::Range{/*loc=*/expr.loc, /*len=*/eImportMeta->range_len};
                logger::MsgKind kind = logger::MsgKind::kWarning;
                if (this->suppressWarningsAboutWeirdCode || this->fnOrArrowDataVisit.tryBodyCount > 0) {
                    kind = logger::MsgKind::kDebug;
                }
                std::string fmt = "\"import.meta\" is not available with the \"" + std::string(config::FormatToString(structural.outputFormat)) + "\" output format and will be empty";
                std::vector<logger::MsgData> notes = {logger::MsgData{nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_ImportMetaNotAvailableNote))}};
                this->log.AddIDWithNotes(logger::MsgID::kJS_EmptyImportMeta, kind, &this->tracker, r, fmt, notes);
            }

            // Convert "import.meta" to a variable if it's not supported in the output format
            if (std::pair<Expr, bool> importMeta = this->valueForImportMeta(expr.loc); importMeta.second) {
                return {importMeta.first, exprOut{}};
            }
        } else if (auto *eSpread = Get<ESpread>(expr.data); eSpread != nullptr) {
            eSpread->value = this->visitExpr(eSpread->value);
        } else if (auto *eId = Get<EIdentifier>(expr.data); eId != nullptr) {
            bool isCallTarget = this->callTarget != E{} && IsSameNodeAs(this->callTarget, expr.data);
            bool isDeleteTarget = this->deleteTarget != E{} && IsSameNodeAs(this->deleteTarget, expr.data);
            std::string name = this->loadNameFromRef(eId->ref);
            if (this->isStrictMode() && kStrictModeReservedWords.count(name) != 0) {
                this->markStrictModeFeature(reservedWord, RangeOfIdentifier(this->source, expr.loc), name);
            }
            findSymbolResult result = this->findSymbol(expr.loc, name);
            eId->must_keep_due_to_with_stmt = result.isInsideWithScope;
            eId->ref = result.ref;

            // Handle referencing a class name within that class's computed property
            // key. This is not allowed, and must fail at run-time:
            //
            //   class Foo {
            //     static foo = 'bar'
            //     static [Foo.foo] = 'foo'
            //   }
            //
            if (this->symbols[result.ref.inner_index].kind == compiler::SymbolKind::kClassInComputedPropertyKey) {
                this->log.AddID(logger::MsgID::kJS_ClassNameWillThrow, logger::MsgKind::kWarning, &this->tracker, RangeOfIdentifier(this->source, expr.loc),
                    "Accessing class \"" + name + "\" before initialization will throw");
                std::vector<Expr> earlyAccessArgs = {Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16(name), logger::Loc{}}), expr.loc}};
                return {this->callRuntime(expr.loc, "__earlyAccess", earlyAccessArgs), exprOut{}};
            }

            // Handle assigning to a constant
            if (in.assignTarget != AssignTarget::kNone) {
                switch (this->symbols[result.ref.inner_index].kind) {
                case compiler::SymbolKind::kConst: {
                    logger::Range r = RangeOfIdentifier(this->source, expr.loc);
                    std::vector<logger::MsgData> notes = {this->tracker.MakeMsgData(RangeOfIdentifier(this->source, result.declareLoc),
                        logger::FormatMsg(logger::MsgCat::kJS_AssignToConstantThrowNote, name))};

                    // Make this an error when bundling because we may need to convert this
                    // "const" into a "var" during bundling. Also make this an error when
                    // the constant is inlined because we will otherwise generate code with
                    // a syntax error.
                    if (this->constValues.find(result.ref) != this->constValues.end() || structural.mode == config::Mode::kBundle ||
                        (this->currentScope->parent == nullptr && this->willWrapModuleInTryCatchForUsing)) {
                        this->log.AddErrorWithNotes(&this->tracker, r,
                            logger::FormatMsg(logger::MsgCat::kJS_AssignToConstant, name), notes);
                    } else {
                        this->log.AddIDWithNotes(logger::MsgID::kJS_AssignToConstant, logger::MsgKind::kWarning, &this->tracker, r,
                            "This assignment will throw because \"" + name + "\" is a constant", notes);
                    }
                    break;
                }
                case compiler::SymbolKind::kInjected: {
                    auto injectedIt = this->injectedSymbolSources.find(result.ref);
                    if (injectedIt != this->injectedSymbolSources.end()) {
                        injectedSymbolSource &where = injectedIt->second;
                        logger::Range r = RangeOfIdentifier(this->source, expr.loc);
                        logger::LineColumnTracker trackerForNote(&where.source);
                        std::vector<logger::MsgData> injectedNotes = {trackerForNote.MakeMsgData(RangeOfIdentifier(where.source, where.loc),
                            logger::FormatMsg(logger::MsgCat::kJS_AssignToInjectedImportNote2, name, where.source.pretty_paths.Select(structural.logPathStyle)))};
                        this->log.AddErrorWithNotes(&this->tracker, r,
                            logger::FormatMsg(logger::MsgCat::kJS_CannotAssignToInjectedImport2, name), injectedNotes);
                    }
                    break;
                }
                default:
                    break;
                }
            }

            // Substitute user-specified defines for unbound or injected symbols
            bool methodCallMustBeReplacedWithUndefined = false;
            if (compiler::SymbolKindIsUnboundOrInjected(this->symbols[eId->ref.inner_index].kind) && !result.isInsideWithScope && !isDeleteTarget) {
                auto defineIt = this->options.defines->IdentifierDefines.find(name);
                if (defineIt != this->options.defines->IdentifierDefines.end()) {
                    config::DefineData &data = defineIt->second;
                    if (data.DefineExprData != nullptr) {
                        Expr newDefined = this->instantiateDefineExpr(expr.loc, *data.DefineExprData, identifierOpts{/*assignTarget=*/in.assignTarget, /*isCallTarget=*/isCallTarget, /*isDeleteTarget=*/isDeleteTarget, /*preferQuotedKey=*/false, /*wasOriginallyIdentifier=*/false, /*matchAgainstDefines=*/false});
                        if (in.assignTarget == AssignTarget::kNone || defineValueCanBeUsedInAssignTarget(newDefined.data)) {
                            this->ignoreUsage(eId->ref);
                            return {newDefined, exprOut{}};
                        } else {
                            this->logAssignToDefine(RangeOfIdentifier(this->source, expr.loc), name, Expr{});
                        }
                    }

                    // Copy the side effect flags over in case this expression is unused
                    if (config::Has(data.Flags, config::DefineFlags::kCanBeRemovedIfUnused)) {
                        eId->can_be_removed_if_unused = true;
                    }
                    if (config::Has(data.Flags, config::DefineFlags::kCallCanBeUnwrappedIfUnused) && !structural.ignoreDCEAnnotations) {
                        eId->call_can_be_unwrapped_if_unused = true;
                    }
                    if (config::Has(data.Flags, config::DefineFlags::kMethodCallsMustBeReplacedWithUndefined)) {
                        methodCallMustBeReplacedWithUndefined = true;
                    }
                }
            }

            Expr handledIdentifier = this->handleIdentifier(expr.loc, *std::get_if<std::shared_ptr<EIdentifier>>(&expr.data), identifierOpts{/*assignTarget=*/in.assignTarget, /*isCallTarget=*/isCallTarget, /*isDeleteTarget=*/isDeleteTarget, /*preferQuotedKey=*/false, /*wasOriginallyIdentifier=*/true, /*matchAgainstDefines=*/false});
            exprOut identifierOut;
            identifierOut.methodCallMustBeReplacedWithUndefined = methodCallMustBeReplacedWithUndefined;
            return {handledIdentifier, identifierOut};
        } else if (auto *eJSX = Get<EJSXElement>(expr.data); eJSX != nullptr) {
            logger::Loc propsLoc = expr.loc;

            // Resolving the location index to a specific line and column in
            // development mode is not too expensive because we seek from the
            // previous JSX element. It amounts to at most a single additional
            // scan over the source code. Note that this has to happen before
            // we visit anything about this JSX element to make sure that we
            // only ever need to scan forward, not backward.
            int jsxSrcLine = 0;
            int jsxSrcCol = 0;
            if (this->options.jsx.Development && this->options.jsx.AutomaticRuntime) {
                while (this->jsxSourceLoc < propsLoc.start) {
                    std::pair<char32_t, int> decoded = helpers::DecodeRuneInString(std::string_view(this->source.contents).substr(static_cast<size_t>(this->jsxSourceLoc)));
                    char32_t r = decoded.first;
                    this->jsxSourceLoc += decoded.second;
                    if (r == '\n' || r == '\r' || r == 0x2028 || r == 0x2029) {
                        if (r == '\r' && static_cast<size_t>(this->jsxSourceLoc) < this->source.contents.size() && this->source.contents[static_cast<size_t>(this->jsxSourceLoc)] == '\n') {
                            this->jsxSourceLoc++; // Handle Windows-style CRLF newlines
                        }
                        this->jsxSourceLine++;
                        this->jsxSourceColumn = 0;
                    } else {
                        // Babel and TypeScript count columns in UTF-16 code units
                        if (r < 0xFFFF) {
                            this->jsxSourceColumn++;
                        } else {
                            this->jsxSourceColumn += 2;
                        }
                    }
                }
                jsxSrcLine = this->jsxSourceLine;
                jsxSrcCol = this->jsxSourceColumn;
            }

            if (!IsNil(eJSX->tag_or_nil.data)) {
                propsLoc = eJSX->tag_or_nil.loc;
                eJSX->tag_or_nil = this->visitExpr(eJSX->tag_or_nil);
                this->warnAboutImportNamespaceCall(eJSX->tag_or_nil, importNamespaceCallKind::exprKindJSXTag);
            }

            // Visit properties
            bool hasSpread = false;
            for (size_t i = 0; i < eJSX->properties.size(); i++) {
                Property &property = eJSX->properties[i];
                if (property.kind == PropertyKind::kSpread) {
                    hasSpread = true;
                } else {
                    if (ENameOfSymbol *mangled = Get<ENameOfSymbol>(property.key.data); mangled != nullptr) {
                        mangled->ref = this->symbolForMangledProp(this->loadNameFromRef(mangled->ref));
                    } else {
                        property.key = this->visitExpr(property.key);
                    }
                }
                if (!IsNil(property.value_or_nil.data)) {
                    property.value_or_nil = this->visitExpr(property.value_or_nil);
                }
                if (!IsNil(property.initializer_or_nil.data)) {
                    property.initializer_or_nil = this->visitExpr(property.initializer_or_nil);
                }
            }

            // "{a, ...{b, c}, d}" => "{a, b, c, d}"
            if (structural.minifySyntax && hasSpread) {
                eJSX->properties = MangleObjectSpread(eJSX->properties);
            }

            // Visit children
            if (!eJSX->nullable_children.empty()) {
                for (size_t i = 0; i < eJSX->nullable_children.size(); i++) {
                    if (!IsNil(eJSX->nullable_children[i].data)) {
                        eJSX->nullable_children[i] = this->visitExpr(eJSX->nullable_children[i]);
                    }
                }
            }

            if (this->options.jsx.Preserve) {
                // If the tag is an identifier, mark it as needing to be upper-case
                if (EIdentifier *tag = Get<EIdentifier>(eJSX->tag_or_nil.data); tag != nullptr) {
                    this->symbols[tag->ref.inner_index].flags = this->symbols[tag->ref.inner_index].flags | compiler::SymbolFlags::kMustStartWithCapitalLetterForJSX;
                } else if (EImportIdentifier *tagImp = Get<EImportIdentifier>(eJSX->tag_or_nil.data); tagImp != nullptr) {
                    this->symbols[tagImp->ref.inner_index].flags = this->symbols[tagImp->ref.inner_index].flags | compiler::SymbolFlags::kMustStartWithCapitalLetterForJSX;
                }
            } else {
                // Remove any nil children in the array (in place) before iterating over it
                std::vector<Expr> children = eJSX->nullable_children;
                {
                    size_t end = 0;
                    for (Expr &childOrNil : children) {
                        if (!IsNil(childOrNil.data)) {
                            children[end] = childOrNil;
                            end++;
                        }
                    }
                    children.resize(end);
                }

                // A missing tag is a fragment
                if (IsNil(eJSX->tag_or_nil.data)) {
                    if (this->options.jsx.AutomaticRuntime) {
                        eJSX->tag_or_nil = this->importJSXSymbol(expr.loc, JSXImportFragment);
                    } else {
                        eJSX->tag_or_nil = this->instantiateDefineExpr(expr.loc, this->options.jsx.Fragment, identifierOpts{/*assignTarget=*/AssignTarget::kNone, /*isCallTarget=*/false, /*isDeleteTarget=*/false, /*preferQuotedKey=*/false, /*wasOriginallyIdentifier=*/true, /*matchAgainstDefines=*/true});
                    }
                }

                bool shouldUseCreateElement = !this->options.jsx.AutomaticRuntime;
                if (!shouldUseCreateElement) {
                    // Even for runtime="automatic", <div {...props} key={key} /> is special cased to createElement
                    // See https://github.com/babel/babel/blob/e482c763466ba3f44cb9e3467583b78b7f030b4a/packages/babel-plugin-transform-react-jsx/src/create-plugin.ts#L352
                    bool seenPropsSpread = false;
                    for (Property &property : eJSX->properties) {
                        if (seenPropsSpread && property.kind == PropertyKind::kField) {
                            if (EString *str = Get<EString>(property.key.data); str != nullptr && helpers::UTF16EqualsString(str->value, "key")) {
                                shouldUseCreateElement = true;
                                break;
                            }
                        } else if (property.kind == PropertyKind::kSpread) {
                            seenPropsSpread = true;
                        }
                    }
                }

                if (shouldUseCreateElement) {
                    // Arguments to createElement()
                    std::vector<Expr> args = {eJSX->tag_or_nil};
                    if (!eJSX->properties.empty()) {
                        std::shared_ptr<EObject> propsObject = std::make_shared<EObject>();
                        propsObject->properties = eJSX->properties;
                        propsObject->is_single_line = eJSX->is_tag_single_line;
                        args.push_back(this->lowerObjectSpread(propsLoc, propsObject.get()));
                    } else {
                        args.push_back(Expr{kENullShared, propsLoc});
                    }
                    if (!children.empty()) {
                        for (Expr &child : children) {
                            args.push_back(child);
                        }
                    }

                    // Call createElement()
                    Expr target;
                    CallKind kind = CallKind::kNormal;
                    if (this->options.jsx.AutomaticRuntime) {
                        target = this->importJSXSymbol(expr.loc, JSXImportCreateElement);
                    } else {
                        target = this->instantiateDefineExpr(expr.loc, this->options.jsx.Factory, identifierOpts{/*assignTarget=*/AssignTarget::kNone, /*isCallTarget=*/false, /*isDeleteTarget=*/false, /*preferQuotedKey=*/false, /*wasOriginallyIdentifier=*/true, /*matchAgainstDefines=*/true});
                        if (IsPropertyAccess(target)) {
                            kind = CallKind::kTargetWasOriginallyPropertyAccess;
                        }
                        this->warnAboutImportNamespaceCall(target, importNamespaceCallKind::exprKindCall);
                    }
                    std::shared_ptr<ECall> call = std::make_shared<ECall>();
                    call->target = target;
                    call->args = args;
                    call->close_paren_loc = eJSX->close_loc;
                    call->is_multi_line = !eJSX->is_tag_single_line;
                    call->kind = kind;

                    // Enable tree shaking
                    call->can_be_unwrapped_if_unused = !structural.ignoreDCEAnnotations && !this->options.jsx.SideEffects;
                    return {Expr{call, expr.loc}, exprOut{}};
                } else {
                    // Arguments to jsx()
                    std::vector<Expr> args = {eJSX->tag_or_nil};

                    // Props argument
                    std::vector<Property> properties;
                    properties.reserve(eJSX->properties.size() + 1);

                    // For jsx(), "key" is passed in as a separate argument, so filter it out
                    // from the props here. Also, check for __source and __self, which might have
                    // been added by some upstream plugin. Their presence here would represent a
                    // configuration error.
                    bool hasKey = false;
                    Expr keyProperty = Expr{kEUndefinedShared, expr.loc};
                    for (Property &property : eJSX->properties) {
                        if (EString *str = Get<EString>(property.key.data); str != nullptr) {
                            std::string propName = helpers::UTF16ToString(str->value);
                            if (propName == "key") {
                                if (EBoolean *boolean = Get<EBoolean>(property.value_or_nil.data); boolean != nullptr && boolean->value && Has(property.flags, PropertyFlags::kWasShorthand)) {
                                    logger::Range r = RangeOfIdentifier(this->source, property.loc);
                                    logger::Msg msg;
                                    msg.kind = logger::MsgKind::kError;
                                    msg.data = this->tracker.MakeMsgData(r, "Please provide an explicit value for \"key\":");
                                    msg.notes = {logger::MsgData{nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_KeyShorthandNotAllowedNote))}};
                                    if (msg.data.location != nullptr) {
                                        msg.data.location->suggestion = "key={true}";
                                    }
                                    this->log.AddMsgID(logger::MsgID::kNone, msg);
                                } else {
                                    keyProperty = property.value_or_nil;
                                    hasKey = true;
                                }
                                continue;

                            } else if (propName == "__source" || propName == "__self") {
                                logger::Range r = RangeOfIdentifier(this->source, property.loc);
                                std::vector<logger::MsgData> notes = {logger::MsgData{nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_DuplicatePropFoundNote))}};
                                this->log.AddErrorWithNotes(&this->tracker, r,
                                    logger::FormatMsg(logger::MsgCat::kJS_DuplicatePropFound, propName), notes);
                                continue;
                            }
                        }
                        properties.push_back(property);
                    }

                    bool isStaticChildren = children.size() > 1;

                    // Children are passed in as an explicit prop
                    if (!children.empty()) {
                        Expr childrenValue = children[0];

                        if (children.size() > 1) {
                            std::shared_ptr<EArray> arr = std::make_shared<EArray>();
                            arr->items = children;
                            childrenValue.data = arr;
                        } else if (Get<ESpread>(childrenValue.data) != nullptr) {
                            // TypeScript considers spread children to be static, but Babel considers
                            // it to be an error ("Spread children are not supported in React.").
                            // We'll follow TypeScript's behavior here because spread children may be
                            // valid with non-React source runtimes.
                            std::shared_ptr<EArray> arr = std::make_shared<EArray>();
                            arr->items = {childrenValue};
                            childrenValue.data = arr;
                            isStaticChildren = true;
                        }

                        Property childrenProperty;
                        childrenProperty.key = Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16("children"), logger::Loc{}}), childrenValue.loc};
                        childrenProperty.value_or_nil = childrenValue;
                        childrenProperty.kind = PropertyKind::kField;
                        childrenProperty.loc = childrenValue.loc;
                        properties.push_back(childrenProperty);
                    }

                    std::shared_ptr<EObject> propsObject = std::make_shared<EObject>();
                    propsObject->properties = properties;
                    propsObject->is_single_line = eJSX->is_tag_single_line;
                    args.push_back(this->lowerObjectSpread(propsLoc, propsObject.get()));

                    // "key"
                    if (hasKey || this->options.jsx.Development) {
                        args.push_back(keyProperty);
                    }

                    if (this->options.jsx.Development) {
                        // "isStaticChildren"
                        args.push_back(Expr{std::make_shared<EBoolean>(EBoolean{/*value=*/isStaticChildren}), expr.loc});

                        // "__source"
                        std::shared_ptr<EObject> sourceObject = std::make_shared<EObject>();
                        Property fileNameProperty;
                        fileNameProperty.kind = PropertyKind::kField;
                        fileNameProperty.key = Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16("fileName"), logger::Loc{}}), expr.loc};
                        fileNameProperty.value_or_nil = Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16(this->source.pretty_paths.Select(structural.codePathStyle)), logger::Loc{}}), expr.loc};
                        Property lineNumberProperty;
                        lineNumberProperty.kind = PropertyKind::kField;
                        lineNumberProperty.key = Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16("lineNumber"), logger::Loc{}}), expr.loc};
                        lineNumberProperty.value_or_nil = Expr{std::make_shared<ENumber>(ENumber{/*value=*/static_cast<double>(jsxSrcLine + 1)}), expr.loc}; // 1-based lines
                        Property columnNumberProperty;
                        columnNumberProperty.kind = PropertyKind::kField;
                        columnNumberProperty.key = Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16("columnNumber"), logger::Loc{}}), expr.loc};
                        columnNumberProperty.value_or_nil = Expr{std::make_shared<ENumber>(ENumber{/*value=*/static_cast<double>(jsxSrcCol + 1)}), expr.loc}; // 1-based columns
                        sourceObject->properties = {fileNameProperty, lineNumberProperty, columnNumberProperty};
                        args.push_back(Expr{sourceObject, expr.loc});

                        // "__self"
                        Expr self = Expr{kEThisShared, expr.loc};
                        {
                            if (this->fnOnlyDataVisit.shouldReplaceThisWithInnerClassNameRef) {
                                // Substitute "this" if we're inside a static class context
                                this->recordUsage(*this->fnOnlyDataVisit.innerClassNameRef);
                                std::shared_ptr<EIdentifier> selfId = std::make_shared<EIdentifier>();
                                selfId->ref = *this->fnOnlyDataVisit.innerClassNameRef;
                                self.data = selfId;
                            } else if (!this->fnOnlyDataVisit.isThisNested && structural.mode != config::Mode::kPassThrough) {
                                // Replace top-level "this" with "undefined" if there's an output format
                                self.data = kEUndefinedShared;
                            } else if (this->fnOrArrowDataVisit.isDerivedClassCtor) {
                                // We can't use "this" here in case it comes before "super()"
                                self.data = kEUndefinedShared;
                            }
                        }
                        if (Get<EUndefined>(self.data) == nullptr) {
                            // Omit "__self" entirely if it's undefined
                            args.push_back(self);
                        }
                    }

                    JSXImport jsx = JSXImportJSX;
                    if (isStaticChildren) {
                        jsx = JSXImportJSXS;
                    }

                    std::shared_ptr<ECall> call = std::make_shared<ECall>();
                    call->target = this->importJSXSymbol(expr.loc, jsx);
                    call->args = args;
                    call->close_paren_loc = eJSX->close_loc;
                    call->is_multi_line = !eJSX->is_tag_single_line;

                    // Enable tree shaking
                    call->can_be_unwrapped_if_unused = !structural.ignoreDCEAnnotations && !this->options.jsx.SideEffects;
                    return {Expr{call, expr.loc}, exprOut{}};
                }
            }
        } else if (auto *eTemplate = Get<ETemplate>(expr.data); eTemplate != nullptr) {
            if (eTemplate->legacy_octal_loc.start > 0) {
                this->log.AddError(&this->tracker, this->source.RangeOfLegacyOctalEscape(eTemplate->legacy_octal_loc),
                    logger::FormatMsg(logger::MsgCat::kJS_LegacyOctalInTemplate));
            }

            std::function<Expr()> tagThisFunc;
            std::function<Expr(Expr)> tagWrapFunc;

            if (!IsNil(eTemplate->tag_or_nil.data)) {
                // Capture the value for "this" if the tag is a lowered optional chain.
                // We'll need to manually apply this value later to preserve semantics.
                bool tagIsLoweredOptionalChain = false;
                if (compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kOptionalChain)) {
                    if (EDot *tDot = Get<EDot>(eTemplate->tag_or_nil.data); tDot != nullptr) {
                        tagIsLoweredOptionalChain = tDot->optional_chain != OptionalChain::kNone;
                    } else if (EIndex *tIdx = Get<EIndex>(eTemplate->tag_or_nil.data); tIdx != nullptr) {
                        tagIsLoweredOptionalChain = tIdx->optional_chain != OptionalChain::kNone;
                    }
                }

                this->templateTag = eTemplate->tag_or_nil.data;
                std::pair<Expr, exprOut> tagResult = this->visitExprInOut(eTemplate->tag_or_nil, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/tagIsLoweredOptionalChain, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/AssignTarget::kNone});
                eTemplate->tag_or_nil = tagResult.first;
                tagThisFunc = tagResult.second.thisArgFunc;
                tagWrapFunc = tagResult.second.thisArgWrapFunc;

                // Copy the call side effect flag over if this is a known target
                if (EIdentifier *id = Get<EIdentifier>(tagResult.first.data); id != nullptr && compiler::Has(this->symbols[id->ref.inner_index].flags, compiler::SymbolFlags::kCallCanBeUnwrappedIfUnused)) {
                    eTemplate->can_be_unwrapped_if_unused = true;
                }

                // The value of "this" must be manually preserved for private member
                // accesses inside template tag expressions such as "this.#foo``".
                // The private member "this.#foo" must see the value of "this".
                auto [privateTarget, privateLoc, private_] = this->extractPrivateIndex(eTemplate->tag_or_nil);
                if (private_ != nullptr) {
                    // "foo.#bar`123`" => "__privateGet(_a = foo, #bar).bind(_a)`123`"
                    std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured = this->captureValueWithPossibleSideEffects(privateTarget.loc, 2, privateTarget, valueCouldBeMutated);
                    std::function<Expr()> targetFunc = captured.first;
                    std::function<Expr(Expr)> targetWrapFunc = captured.second;
                    std::shared_ptr<EDot> bind = std::make_shared<EDot>();
                    bind->target = this->lowerPrivateGet(targetFunc(), privateLoc, private_);
                    bind->name = "bind";
                    bind->name_loc = privateTarget.loc;
                    std::shared_ptr<ECall> bound = std::make_shared<ECall>();
                    bound->target = Expr{bind, privateTarget.loc};
                    bound->args = {targetFunc()};
                    bound->kind = CallKind::kTargetWasOriginallyPropertyAccess;
                    eTemplate->tag_or_nil = targetWrapFunc(Expr{bound, privateTarget.loc});
                }
            }

            for (size_t i = 0; i < eTemplate->parts.size(); i++) {
                eTemplate->parts[i].value = this->visitExpr(eTemplate->parts[i].value);
            }

            // When mangling, inline string values into the template literal. Note that
            // it may no longer be a template literal after this point (it may turn into
            // a plain string literal instead).
            if (this->shouldFoldTypeScriptConstantExpressions || structural.minifySyntax) {
                expr = InlinePrimitivesIntoTemplate(expr.loc, *eTemplate);
            }

            bool shouldLowerTemplateLiteral = compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kTemplateLiteral);

            // If the tag was originally an optional chaining property access, then
            // we'll need to lower this template literal as well to preserve the value
            // for "this".
            if (tagThisFunc) {
                shouldLowerTemplateLiteral = true;
            }

            // Lower tagged template literals that include "</script"
            // since we won't be able to escape it without lowering it
            if (!shouldLowerTemplateLiteral && !compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kInlineScript) && !IsNil(eTemplate->tag_or_nil.data)) {
                if (containsClosingScriptTag(eTemplate->head_raw)) {
                    shouldLowerTemplateLiteral = true;
                } else {
                    for (TemplatePart &part : eTemplate->parts) {
                        if (containsClosingScriptTag(part.tail_raw)) {
                            shouldLowerTemplateLiteral = true;
                            break;
                        }
                    }
                }
            }

            // Convert template literals to older syntax if this is still a template literal
            if (shouldLowerTemplateLiteral) {
                if (ETemplate *current = Get<ETemplate>(expr.data); current != nullptr) {
                    return {this->lowerTemplateLiteral(expr.loc, current, tagThisFunc, tagWrapFunc), exprOut{}};
                }
            }
        } else if (auto *eBin = Get<EBinary>(expr.data); eBin != nullptr) {
            // The handling of binary expressions is convoluted because we're using
            // iteration on the heap instead of recursion on the call stack to avoid
            // stack overflow for deeply-nested ASTs. See the comment before the
            // definition of "binaryExprVisitor" for details.
            binaryExprVisitor v = binaryExprVisitor{/*e=*/std::get<std::shared_ptr<EBinary>>(expr.data), /*loc=*/expr.loc, /*in=*/in, /*leftIn=*/exprIn{}, /*isStmtExpr=*/false, /*oldSilenceWarningAboutThisBeingUndefined=*/false};

            // Everything uses a single stack to reduce allocation overhead. This stack
            // should almost always be very small, and almost all visits should reuse
            // existing memory without allocating anything.
            size_t stackBottom = this->binaryExprStack.size();

            // Iterate down into the AST along the left node of the binary operation.
            // Continue iterating until we encounter something that's not a binary node.
            for (;;) {
                // Check whether this node is a special case. If it is, a result will be
                // provided which ends our iteration. Otherwise, the visitor object will
                // be prepared for visiting.
                Expr result = v.checkAndPrepare(this);
                if (!IsNil(result.data)) {
                    expr = result;
                    break;
                }

                // Grab the arguments to our nested "visitExprInOut" call for the left
                // node. We only care about deeply-nested left nodes because most binary
                // operators in JavaScript are left-associative and the problematic edge
                // cases we're trying to avoid crashing on have lots of left-associative
                // binary operators chained together without parentheses (e.g. "1+2+...").
                Expr left = v.e->left;
                exprIn leftIn = v.leftIn;
                EBinary *leftBinary = Get<EBinary>(left.data);

                // Stop iterating if iteration doesn't apply to the left node. This checks
                // the assignment target because "visitExprInOut" has additional behavior
                // in that case that we don't want to miss (before the top-level "switch"
                // statement).
                if (leftBinary == nullptr || leftIn.assignTarget != AssignTarget::kNone) {
                    v.e->left = this->visitExprInOut(left, leftIn).first;
                    expr = v.visitRightAndFinish(this);
                    break;
                }

                // Note that we only append to the stack (and therefore allocate memory
                // on the heap) when there are nested binary expressions. A single binary
                // expression doesn't add anything to the stack.
                this->binaryExprStack.push_back(v);
                v = binaryExprVisitor{/*e=*/std::get<std::shared_ptr<EBinary>>(left.data), /*loc=*/left.loc, /*in=*/leftIn, /*leftIn=*/exprIn{}, /*isStmtExpr=*/false, /*oldSilenceWarningAboutThisBeingUndefined=*/false};
            }

            // Process all binary operations from the deepest-visited node back toward
            // our original top-level binary operation.
            for (;;) {
                size_t n = this->binaryExprStack.size();
                if (n <= stackBottom) {
                    break;
                }
                binaryExprVisitor v2 = this->binaryExprStack[n - 1];
                this->binaryExprStack.resize(n - 1);
                v2.e->left = expr;
                expr = v2.visitRightAndFinish(this);
            }

            return {expr, exprOut{}};
        } else if (auto *eDot = Get<EDot>(expr.data); eDot != nullptr) {
            bool isDeleteTarget = this->deleteTarget != E{} && IsSameNodeAs(this->deleteTarget, expr.data);
            bool isCallTarget = this->callTarget != E{} && IsSameNodeAs(this->callTarget, expr.data);
            bool isTemplateTag = this->templateTag != E{} && IsSameNodeAs(this->templateTag, expr.data);

            // Check both user-specified defines and known globals
            auto dotDefinesIt = this->options.defines->DotDefines.find(eDot->name);
            if (dotDefinesIt != this->options.defines->DotDefines.end()) {
                for (config::DefineData &define : dotDefinesIt->second) {
                    if (this->isDotOrIndexDefineMatch(expr, define.KeyParts)) {
                        // Substitute user-specified defines
                        if (define.DefineExprData != nullptr) {
                            Expr newDefined = this->instantiateDefineExpr(expr.loc, *define.DefineExprData, identifierOpts{/*assignTarget=*/in.assignTarget, /*isCallTarget=*/isCallTarget, /*isDeleteTarget=*/isDeleteTarget, /*preferQuotedKey=*/false, /*wasOriginallyIdentifier=*/false, /*matchAgainstDefines=*/false});
                            if (in.assignTarget == AssignTarget::kNone || defineValueCanBeUsedInAssignTarget(newDefined.data)) {
                                // Note: We don't need to "ignoreRef" on the underlying identifier
                                // because we have only parsed it but not visited it yet
                                return {newDefined, exprOut{}};
                            } else {
                                logger::Range range = logger::Range{/*loc=*/expr.loc, /*len=*/RangeOfIdentifier(this->source, eDot->name_loc).End() - expr.loc.start};
                                this->logAssignToDefine(range, "", expr);
                            }
                        }

                        // Copy the side effect flags over in case this expression is unused
                        if (config::Has(define.Flags, config::DefineFlags::kCanBeRemovedIfUnused)) {
                            eDot->can_be_removed_if_unused = true;
                        }
                        if (config::Has(define.Flags, config::DefineFlags::kCallCanBeUnwrappedIfUnused) && !structural.ignoreDCEAnnotations) {
                            eDot->call_can_be_unwrapped_if_unused = true;
                        }
                        if (config::Has(define.Flags, config::DefineFlags::kIsSymbolInstance)) {
                            eDot->is_symbol_instance = true;
                        }
                        break;
                    }
                }
            }

            // Check injected dot names
            auto dotInjectedIt = this->injectedDotNames.find(eDot->name);
            if (dotInjectedIt != this->injectedDotNames.end()) {
                for (injectedDotName &name : dotInjectedIt->second) {
                    if (this->isDotOrIndexDefineMatch(expr, name.parts)) {
                        // Note: We don't need to "ignoreRef" on the underlying identifier
                        // because we have only parsed it but not visited it yet
                        return {this->instantiateInjectDotName(expr.loc, name, in.assignTarget), exprOut{}};
                    }
                }
            }

            // Track ".then().catch()" chains
            if (isCallTarget && this->thenCatchChain.nextTarget != E{} && IsSameNodeAs(this->thenCatchChain.nextTarget, expr.data)) {
                if (eDot->name == "catch") {
                    struct thenCatchChain chain;
                    chain.nextTarget = eDot->target.data;
                    chain.hasCatch = true;
                    chain.catchLoc = eDot->name_loc;
                    this->thenCatchChain = chain;
                } else if (eDot->name == "then") {
                    struct thenCatchChain chain;
                    chain.nextTarget = eDot->target.data;
                    chain.hasCatch = this->thenCatchChain.hasCatch || this->thenCatchChain.hasMultipleArgs;
                    chain.catchLoc = this->thenCatchChain.catchLoc;
                    this->thenCatchChain = chain;
                }
            }

            this->dotOrIndexTarget = eDot->target.data;
            std::pair<Expr, exprOut> visitedDot = this->visitExprInOut(eDot->target, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/eDot->optional_chain == OptionalChain::kContinue, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/AssignTarget::kNone});
            eDot->target = visitedDot.first;
            exprOut out = visitedDot.second;

            // Lower "super.prop" if necessary
            if (eDot->optional_chain == OptionalChain::kNone && in.assignTarget == AssignTarget::kNone &&
                !isCallTarget && this->shouldLowerSuperPropertyAccess(eDot->target)) {
                // "super.foo" => "__superGet('foo')"
                Expr key = Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16(eDot->name), logger::Loc{}}), eDot->name_loc};
                Expr value = this->lowerSuperPropertyGet(expr.loc, key);
                if (isTemplateTag) {
                    std::shared_ptr<EDot> bind = std::make_shared<EDot>();
                    bind->target = value;
                    bind->name = "bind";
                    bind->name_loc = value.loc;
                    std::shared_ptr<ECall> bound = std::make_shared<ECall>();
                    bound->target = Expr{bind, value.loc};
                    bound->args = {Expr{kEThisShared, value.loc}};
                    bound->kind = CallKind::kTargetWasOriginallyPropertyAccess;
                    value = Expr{bound, value.loc};
                }
                return {value, exprOut{}};
            }

            // Lower optional chaining if we're the top of the chain
            bool containsOptionalChain = eDot->optional_chain == OptionalChain::kStart ||
                (eDot->optional_chain == OptionalChain::kContinue && out.childContainsOptionalChain);
            if (containsOptionalChain && !in.hasChainParent) {
                return this->lowerOptionalChain(expr, in, out);
            }

            // Also erase "console.log.call(console, 123)" and "console.log.bind(console)"
            if (out.callMustBeReplacedWithUndefined) {
                if (eDot->name == "call" || eDot->name == "apply") {
                    out.methodCallMustBeReplacedWithUndefined = true;
                } else if (compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kArrow)) {
                    eDot->target.data = std::make_shared<EFunction>();
                } else {
                    eDot->target.data = std::make_shared<EArrow>();
                }
            }

            // Potentially rewrite this property access
            exprOut newOut;
            newOut.childContainsOptionalChain = containsOptionalChain;
            newOut.callMustBeReplacedWithUndefined = out.methodCallMustBeReplacedWithUndefined;
            newOut.thisArgFunc = out.thisArgFunc;
            newOut.thisArgWrapFunc = out.thisArgWrapFunc;
            out = newOut;
            if (!in.hasChainParent) {
                out.thisArgFunc = nullptr;
                out.thisArgWrapFunc = nullptr;
            }
            if (eDot->optional_chain == OptionalChain::kNone) {
                if (std::pair<Expr, bool> rewritten = this->maybeRewritePropertyAccess(expr.loc, in.assignTarget,
                    isDeleteTarget, eDot->target, eDot->name, eDot->name_loc, isCallTarget, isTemplateTag, false); rewritten.second) {
                    return {rewritten.first, out};
                }
            }
            return {Expr{*std::get_if<std::shared_ptr<EDot>>(&expr.data), expr.loc}, out};
        } else if (auto *eIdx = Get<EIndex>(expr.data); eIdx != nullptr) {
            bool isCallTarget = this->callTarget != E{} && IsSameNodeAs(this->callTarget, expr.data);
            bool isTemplateTag = this->templateTag != E{} && IsSameNodeAs(this->templateTag, expr.data);
            bool isDeleteTarget = this->deleteTarget != E{} && IsSameNodeAs(this->deleteTarget, expr.data);

            // Check both user-specified defines and known globals
            if (const std::shared_ptr<EString> *str = std::get_if<std::shared_ptr<EString>>(&eIdx->index.data)) {
                if (auto it = this->options.defines->DotDefines.find(helpers::UTF16ToString((*str)->value)); it != this->options.defines->DotDefines.end()) {
                    for (const config::DefineData &define : it->second) {
                        if (this->isDotOrIndexDefineMatch(expr, define.KeyParts)) {
                            // Substitute user-specified defines
                            if (define.DefineExprData != nullptr) {
                                Expr newDefined = this->instantiateDefineExpr(expr.loc, *define.DefineExprData, identifierOpts{/*assignTarget=*/in.assignTarget, /*isCallTarget=*/isCallTarget, /*isDeleteTarget=*/isDeleteTarget, /*preferQuotedKey=*/false, /*wasOriginallyIdentifier=*/false, /*matchAgainstDefines=*/false});
                                if (in.assignTarget == AssignTarget::kNone || defineValueCanBeUsedInAssignTarget(newDefined.data)) {
                                    // Note: We don't need to "ignoreRef" on the underlying identifier
                                    // because we have only parsed it but not visited it yet
                                    return {newDefined, exprOut{}};
                                } else {
                                    logger::Range r{/*loc=*/expr.loc};
                                    logger::Loc afterIndex{/*start=*/this->source.RangeOfString(eIdx->index.loc).End()};
                                    logger::Range closeBracket = this->source.RangeOfOperatorAfter(afterIndex, "]");
                                    if (closeBracket.len > 0) {
                                        r.len = closeBracket.End() - r.loc.start;
                                    }
                                    this->logAssignToDefine(r, "", expr);
                                }
                            }

                            // Copy the side effect flags over in case this expression is unused
                            if (config::Has(define.Flags, config::DefineFlags::kCanBeRemovedIfUnused)) {
                                eIdx->can_be_removed_if_unused = true;
                            }
                            if (config::Has(define.Flags, config::DefineFlags::kCallCanBeUnwrappedIfUnused) && !structural.ignoreDCEAnnotations) {
                                eIdx->call_can_be_unwrapped_if_unused = true;
                            }
                            if (config::Has(define.Flags, config::DefineFlags::kIsSymbolInstance)) {
                                eIdx->is_symbol_instance = true;
                            }
                            break;
                        }
                    }
                }
            }

            // "a['b']" => "a.b"
            if (structural.minifySyntax) {
                if (const std::shared_ptr<EString> *str = std::get_if<std::shared_ptr<EString>>(&eIdx->index.data)) {
                    if ((*str)->value.size() > 0x10000000ULL) {
                        fprintf(stderr, "DBG bind a['b']=>a.b HUGE SIZE %zu\n", (*str)->value.size());
                    }
                    if (IsIdentifierUTF16(std::span<const uint16_t>(reinterpret_cast<const uint16_t *>((*str)->value.data()), (*str)->value.size()))) {
                        E *dot = this->dotOrMangledPropParse(eIdx->target, MaybeSubstring{/*str=*/helpers::UTF16ToString((*str)->value)}, eIdx->index.loc, eIdx->optional_chain, wasOriginallyIndex);
                        if (isCallTarget) {
                            this->callTarget = *dot;
                        }
                        if (isTemplateTag) {
                            this->templateTag = *dot;
                        }
                        if (isDeleteTarget) {
                            this->deleteTarget = *dot;
                        }
                        return this->visitExprInOut(Expr{*dot, expr.loc}, in);
                    }
                }
            }

            this->dotOrIndexTarget = eIdx->target.data;
            std::pair<Expr, exprOut> indexTargetResult = this->visitExprInOut(eIdx->target, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/eIdx->optional_chain == OptionalChain::kContinue, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/AssignTarget::kNone});
            eIdx->target = indexTargetResult.first;
            exprOut out = indexTargetResult.second;

            // Special-case private identifiers
            if (EPrivateIdentifier *private_ = Get<EPrivateIdentifier>(eIdx->index.data); private_ != nullptr) {
                std::string privateName = this->loadNameFromRef(private_->ref);
                findSymbolResult privateResult = this->findSymbol(eIdx->index.loc, privateName);
                private_->ref = privateResult.ref;

                // Unlike regular identifiers, there are no unbound private identifiers
                compiler::SymbolKind privateKind = this->symbols[privateResult.ref.inner_index].kind;
                if (!compiler::SymbolKindIsPrivate(privateKind)) {
                    logger::Range r{/*loc=*/eIdx->index.loc, /*len=*/static_cast<int32_t>(privateName.size())};
                    this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_PrivateNameNotInEnclosing, privateName));
                } else {
                    logger::Range r;
                    std::string text;
                    if (in.assignTarget != AssignTarget::kNone && (privateKind == compiler::SymbolKind::kPrivateMethod || privateKind == compiler::SymbolKind::kPrivateStaticMethod)) {
                        r = logger::Range{/*loc=*/eIdx->index.loc, /*len=*/static_cast<int32_t>(privateName.size())};
                        text = "Writing to read-only method \"" + privateName + "\" will throw";
                    } else if (in.assignTarget != AssignTarget::kNone && (privateKind == compiler::SymbolKind::kPrivateGet || privateKind == compiler::SymbolKind::kPrivateStaticGet)) {
                        r = logger::Range{/*loc=*/eIdx->index.loc, /*len=*/static_cast<int32_t>(privateName.size())};
                        text = "Writing to getter-only property \"" + privateName + "\" will throw";
                    } else if (in.assignTarget != AssignTarget::kReplace && (privateKind == compiler::SymbolKind::kPrivateSet || privateKind == compiler::SymbolKind::kPrivateStaticSet)) {
                        r = logger::Range{/*loc=*/eIdx->index.loc, /*len=*/static_cast<int32_t>(privateName.size())};
                        text = "Reading from setter-only property \"" + privateName + "\" will throw";
                    }
                    if (!text.empty()) {
                        logger::MsgKind msgKind = logger::MsgKind::kWarning;
                        if (this->suppressWarningsAboutWeirdCode) {
                            msgKind = logger::MsgKind::kDebug;
                        }
                        this->log.AddID(logger::MsgID::kJS_PrivateNameWillThrow, msgKind, &this->tracker, r, text);
                    }
                }

                // Lower private member access only if we're sure the target isn't needed
                // for the value of "this" for a call expression. All other cases will be
                // taken care of by the enclosing call expression.
                if (this->privateSymbolNeedsToBeLowered(private_) && eIdx->optional_chain == OptionalChain::kNone &&
                    in.assignTarget == AssignTarget::kNone && !isCallTarget && !isTemplateTag) {
                    // "foo.#bar" => "__privateGet(foo, #bar)"
                    return {this->lowerPrivateGet(eIdx->target, eIdx->index.loc, private_), exprOut{}};
                }
            } else {
                eIdx->index = this->visitExprInOut(eIdx->index, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/true, /*assignTarget=*/AssignTarget::kNone}).first;
            }

            // Lower "super[prop]" if necessary
            if (eIdx->optional_chain == OptionalChain::kNone && in.assignTarget == AssignTarget::kNone &&
                !isCallTarget && this->shouldLowerSuperPropertyAccess(eIdx->target)) {
                // "super[foo]" => "__superGet(foo)"
                Expr value = this->lowerSuperPropertyGet(expr.loc, eIdx->index);
                if (isTemplateTag) {
                    std::shared_ptr<EDot> bindTarget = std::make_shared<EDot>();
                    bindTarget->target = value;
                    bindTarget->name = "bind";
                    bindTarget->name_loc = value.loc;
                    std::shared_ptr<ECall> call = std::make_shared<ECall>();
                    call->target = Expr{bindTarget, value.loc};
                    call->args.push_back(Expr{kEThisShared, value.loc});
                    call->kind = CallKind::kTargetWasOriginallyPropertyAccess;
                    value.data = call;
                }
                return {value, exprOut{}};
            }

            // Lower optional chaining if we're the top of the chain
            bool containsOptionalChain = eIdx->optional_chain == OptionalChain::kStart ||
                (eIdx->optional_chain == OptionalChain::kContinue && out.childContainsOptionalChain);
            if (containsOptionalChain && !in.hasChainParent) {
                return this->lowerOptionalChain(expr, in, out);
            }

            // Potentially rewrite this property access
            exprOut rewrittenOut;
            rewrittenOut.childContainsOptionalChain = containsOptionalChain;
            rewrittenOut.callMustBeReplacedWithUndefined = out.methodCallMustBeReplacedWithUndefined;
            rewrittenOut.thisArgFunc = out.thisArgFunc;
            rewrittenOut.thisArgWrapFunc = out.thisArgWrapFunc;
            out = rewrittenOut;
            if (!in.hasChainParent) {
                out.thisArgFunc = nullptr;
                out.thisArgWrapFunc = nullptr;
            }
            if (const std::shared_ptr<EString> *str = std::get_if<std::shared_ptr<EString>>(&eIdx->index.data); str != nullptr && eIdx->optional_chain == OptionalChain::kNone) {
                bool preferQuotedKey = !structural.minifySyntax;
                std::pair<Expr, bool> rewritten = this->maybeRewritePropertyAccess(expr.loc, in.assignTarget, isDeleteTarget,
                    eIdx->target, helpers::UTF16ToString((*str)->value), eIdx->index.loc, isCallTarget, isTemplateTag, preferQuotedKey);
                if (rewritten.second) {
                    return {rewritten.first, out};
                }
            }

            // Create an error for assigning to an import namespace when bundling. Even
            // though this is a run-time error, we make it a compile-time error when
            // bundling because scope hoisting means these will no longer be run-time
            // errors.
            if (structural.mode == config::Mode::kBundle && (in.assignTarget != AssignTarget::kNone || isDeleteTarget)) {
                if (EIdentifier *id = Get<EIdentifier>(eIdx->target.data); id != nullptr && this->symbols[id->ref.inner_index].kind == compiler::SymbolKind::kImport) {
                    logger::Range r = RangeOfIdentifier(this->source, eIdx->target.loc);
                    std::vector<logger::MsgData> notes;
                    notes.push_back(logger::MsgData{nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_CannotAssignToPropertyOnImportNote))});
                    this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_CannotAssignToPropertyOnImport, this->symbols[id->ref.inner_index].original_name), notes);
                }
            }

            if (structural.minifySyntax) {
                if (const std::shared_ptr<EString> *indexStr = std::get_if<std::shared_ptr<EString>>(&eIdx->index.data)) {
                    if ((*indexStr)->value.size() > 0x10000000ULL) {
                        fprintf(stderr, "DBG bind a['x'+'y'] HUGE SIZE %zu\n", (*indexStr)->value.size());
                    }
                    // "a['x' + 'y']" => "a.xy" (this is done late to allow for constant folding)
                    if (IsIdentifierUTF16(std::span<const uint16_t>(reinterpret_cast<const uint16_t *>((*indexStr)->value.data()), (*indexStr)->value.size()))) {
                        std::shared_ptr<EDot> dot = std::make_shared<EDot>();
                        dot->target = eIdx->target;
                        dot->name = helpers::UTF16ToString((*indexStr)->value);
                        dot->name_loc = eIdx->index.loc;
                        dot->optional_chain = eIdx->optional_chain;
                        dot->can_be_removed_if_unused = eIdx->can_be_removed_if_unused;
                        dot->call_can_be_unwrapped_if_unused = eIdx->call_can_be_unwrapped_if_unused;
                        return {Expr{dot, expr.loc}, out};
                    }

                    // "a['123']" => "a[123]" (this is done late to allow "'123'" to be mangled)
                    if (std::optional<double> numberValue = StringToEquivalentNumberValue((*indexStr)->value)) {
                        std::shared_ptr<ENumber> num = std::make_shared<ENumber>();
                        num->value = *numberValue;
                        eIdx->index.data = num;
                    }
                } else if (const std::shared_ptr<ENumber> *indexNum = std::get_if<std::shared_ptr<ENumber>>(&eIdx->index.data)) {
                    // "'abc'[1]" => "'b'"
                    if (const std::shared_ptr<EString> *targetStr = std::get_if<std::shared_ptr<EString>>(&eIdx->target.data)) {
                        double indexValue = (*indexNum)->value;
                        if (indexValue >= 0 && indexValue < static_cast<double>((*targetStr)->value.size()) && indexValue == static_cast<double>(static_cast<size_t>(indexValue))) {
                            std::shared_ptr<EString> substring = std::make_shared<EString>();
                            substring->value = std::u16string(1, (*targetStr)->value[static_cast<size_t>(indexValue)]);
                            return {Expr{substring, expr.loc}, out};
                        }
                    }
                }
            }

            return {Expr{*std::get_if<std::shared_ptr<EIndex>>(&expr.data), expr.loc}, out};
        } else if (auto *eUn = Get<EUnary>(expr.data); eUn != nullptr) {
            switch (eUn->op) {
            case OpCode::kUnOpTypeof:
                eUn->value = this->visitExprInOut(eUn->value, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/UnaryAssignTarget(eUn->op)}).first;

                // Compile-time "typeof" evaluation
                if (std::optional<std::string> typeof_ = TypeofWithoutSideEffects(eUn->value.data)) {
                    return {Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16(*typeof_), logger::Loc{}}), expr.loc}, exprOut{}};
                }
                break;

            case OpCode::kUnOpDelete: {
                // Warn about code that tries to do "delete super.foo"
                logger::Loc superPropLoc;
                if (EDot *e2 = Get<EDot>(eUn->value.data); e2 != nullptr) {
                    if (Get<ESuper>(e2->target.data) != nullptr) {
                        superPropLoc = e2->target.loc;
                    }
                } else if (EIndex *eDelIdx = Get<EIndex>(eUn->value.data); eDelIdx != nullptr) {
                    if (Get<ESuper>(eDelIdx->target.data) != nullptr) {
                        superPropLoc = eDelIdx->target.loc;
                    }
                } else if (EIdentifier *eIdDel = Get<EIdentifier>(eUn->value.data); eIdDel != nullptr) {
                    this->markStrictModeFeature(deleteBareName, RangeOfIdentifier(this->source, eUn->value.loc), "");
                }
                if (superPropLoc.start != 0) {
                    logger::Range r = RangeOfIdentifier(this->source, superPropLoc);
                    std::string text = std::string(logger::MsgTemplate(logger::MsgCat::kJS_DeletePropertyOfSuper));
                    logger::MsgKind kind = logger::MsgKind::kWarning;
                    if (this->suppressWarningsAboutWeirdCode) {
                        kind = logger::MsgKind::kDebug;
                    }
                    this->log.AddID(logger::MsgID::kJS_DeleteSuperProperty, kind, &this->tracker, r, text);
                }

                this->deleteTarget = eUn->value.data;
                std::pair<Expr, exprOut> deleteResult = this->visitExprInOut(eUn->value, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/true, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/AssignTarget::kNone});
                eUn->value = deleteResult.first;

                // Lower optional chaining if present since we're guaranteed to be the
                // end of the chain
                if (deleteResult.second.childContainsOptionalChain) {
                    return this->lowerOptionalChain(expr, in, deleteResult.second);
                }
                break;
            }

            default: {
                eUn->value = this->visitExprInOut(eUn->value, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/UnaryAssignTarget(eUn->op)}).first;

                // Post-process the unary expression
                switch (eUn->op) {
                case OpCode::kUnOpNot:
                    if (structural.minifySyntax) {
                        eUn->value = this->astHelpers.SimplifyBooleanExpr(eUn->value);
                    }

                    if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(eUn->value.data); boolean && boolean->second == SideEffects::kNoSideEffects) {
                        return {Expr{std::make_shared<EBoolean>(EBoolean{/*value=*/!boolean->first}), expr.loc}, exprOut{}};
                    }

                    if (structural.minifySyntax) {
                        if (std::optional<Expr> result = MaybeSimplifyNot(eUn->value); result) {
                            return {*result, exprOut{}};
                        }
                    }
                    break;

                case OpCode::kUnOpVoid: {
                    bool shouldRemove;
                    if (structural.minifySyntax) {
                        shouldRemove = this->astHelpers.ExprCanBeRemovedIfUnused(eUn->value);
                    } else {
                        shouldRemove = isUnsightlyPrimitive(eUn->value.data);
                    }
                    if (shouldRemove) {
                        return {Expr{kEUndefinedShared, expr.loc}, exprOut{}};
                    }
                    break;
                }

                case OpCode::kUnOpPos:
                    if (std::optional<double> number = ToNumberWithoutSideEffects(eUn->value.data)) {
                        return {Expr{std::make_shared<ENumber>(ENumber{/*value=*/*number}), expr.loc}, exprOut{}};
                    }
                    break;

                case OpCode::kUnOpNeg:
                    if (std::optional<double> number = ToNumberWithoutSideEffects(eUn->value.data)) {
                        return {Expr{std::make_shared<ENumber>(ENumber{/*value=*/-*number}), expr.loc}, exprOut{}};
                    }
                    break;

                case OpCode::kUnOpCpl:
                    if (this->shouldFoldTypeScriptConstantExpressions || structural.minifySyntax) {
                        // Minification folds complement operations since they are unlikely to result in larger output
                        if (std::optional<double> number = ToNumberWithoutSideEffects(eUn->value.data)) {
                            return {Expr{std::make_shared<ENumber>(ENumber{/*value=*/static_cast<double>(~static_cast<int32_t>(ToInt32(*number)))}), expr.loc}, exprOut{}};
                        }
                    }
                    break;

                    ////////////////////////////////////////////////////////////////////////////////
                    // All assignment operators below here

                case OpCode::kUnOpPreDec:
                case OpCode::kUnOpPreInc:
                case OpCode::kUnOpPostDec:
                case OpCode::kUnOpPostInc: {
                    auto [target, loc, private_] = this->extractPrivateIndex(eUn->value);
                    if (private_ != nullptr) {
                        return {this->lowerPrivateSetUnOp(target, loc, private_, eUn->op), exprOut{}};
                    }
                    Expr property = this->extractSuperProperty(eUn->value);
                    if (!IsNil(property.data)) {
                        eUn->value = this->callSuperPropertyWrapper(expr.loc, property);
                    }
                    break;
                }
                default:
                    break;
                }
                break;

            }
            }

            // "-(a, b)" => "a, -b"
            if (structural.minifySyntax && eUn->op != OpCode::kUnOpDelete && eUn->op != OpCode::kUnOpTypeof) {
                if (EBinary *comma = Get<EBinary>(eUn->value.data); comma != nullptr && comma->op == OpCode::kBinOpComma) {
                    std::shared_ptr<EUnary> unary = std::make_shared<EUnary>();
                    unary->op = eUn->op;
                    unary->value = comma->right;
                    return {JoinWithComma(comma->left, Expr{unary, comma->right.loc}), exprOut{}};
                }
            }
        } else if (auto *eIf = Get<EIf>(expr.data); eIf != nullptr) {
            eIf->test = this->visitExpr(eIf->test);

            if (structural.minifySyntax) {
                eIf->test = this->astHelpers.SimplifyBooleanExpr(eIf->test);
            }

            // Propagate these flags into the branches
            exprIn childIn = exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/in.shouldMangleStringsAsProps, /*assignTarget=*/AssignTarget::kNone};

            // Fold constants
            if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(eIf->test.data); !boolean) {
                eIf->yes = this->visitExprInOut(eIf->yes, childIn).first;
                eIf->no = this->visitExprInOut(eIf->no, childIn).first;
            } else {
                // Mark the control flow as dead if the branch is never taken
                if (boolean->first) {
                    // "true ? live : dead"
                    eIf->yes = this->visitExprInOut(eIf->yes, childIn).first;
                    bool oldControlFlowDead = this->isControlFlowDead;
                    this->isControlFlowDead = true;
                    eIf->no = this->visitExprInOut(eIf->no, childIn).first;
                    this->isControlFlowDead = oldControlFlowDead;

                    if (structural.minifySyntax) {
                        // "(a, true) ? b : c" => "a, b"
                        if (boolean->second == SideEffects::kCouldHaveSideEffects) {
                            return {JoinWithComma(this->astHelpers.SimplifyUnusedExpr(eIf->test, structural.unsupportedJSFeatures), eIf->yes), exprOut{}};
                        }

                        return {eIf->yes, exprOut{}};
                    }
                } else {
                    // "false ? dead : live"
                    bool oldControlFlowDead = this->isControlFlowDead;
                    this->isControlFlowDead = true;
                    eIf->yes = this->visitExprInOut(eIf->yes, childIn).first;
                    this->isControlFlowDead = oldControlFlowDead;
                    eIf->no = this->visitExprInOut(eIf->no, childIn).first;

                    if (structural.minifySyntax) {
                        // "(a, false) ? b : c" => "a, c"
                        if (boolean->second == SideEffects::kCouldHaveSideEffects) {
                            return {JoinWithComma(this->astHelpers.SimplifyUnusedExpr(eIf->test, structural.unsupportedJSFeatures), eIf->no), exprOut{}};
                        }

                        return {eIf->no, exprOut{}};
                    }
                }
            }

            if (structural.minifySyntax) {
                return {this->astHelpers.MangleIfExpr(expr.loc, *eIf, structural.unsupportedJSFeatures), exprOut{}};
            }
        } else if (auto *eAwait = Get<EAwait>(expr.data); eAwait != nullptr) {
            // Silently remove unsupported top-level "await" in dead code branches
            if (this->fnOrArrowDataVisit.isOutsideFnOrArrow) {
                if (this->isControlFlowDead && (compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kTopLevelAwait) || !FormatKeepESMImportExportSyntax(structural.outputFormat))) {
                    return this->visitExprInOut(eAwait->value, in);
                } else {
                    this->liveTopLevelAwaitKeyword = logger::Range{/*loc=*/expr.loc, /*len=*/5};
                    this->markSyntaxFeature(compat::JSFeature::kTopLevelAwait, logger::Range{/*loc=*/expr.loc, /*len=*/5});
                }
            }

            this->awaitTarget = eAwait->value.data;
            eAwait->value = this->visitExpr(eAwait->value);

            // "await" expressions turn into "yield" expressions when lowering
            return this->maybeLowerAwait(expr.loc, eAwait);
        } else if (auto *eYield = Get<EYield>(expr.data); eYield != nullptr) {
            if (!IsNil(eYield->value_or_nil.data)) {
                eYield->value_or_nil = this->visitExpr(eYield->value_or_nil);
            }

            // "yield* x" turns into "yield* __yieldStar(x)" when lowering async generator functions
            if (eYield->is_star && compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kAsyncGenerator) && this->fnOrArrowDataVisit.isGenerator) {
                std::vector<Expr> args;
                args.push_back(eYield->value_or_nil);
                eYield->value_or_nil = this->callRuntime(expr.loc, "__yieldStar", args);
            }
        } else if (auto *eArr = Get<EArray>(expr.data); eArr != nullptr) {
            if (in.assignTarget != AssignTarget::kNone) {
                if (eArr->comma_after_spread.start != 0) {
                    this->log.AddError(&this->tracker, logger::Range{/*loc=*/eArr->comma_after_spread, /*len=*/1}, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedCommaAfterRest));
                }
                this->markSyntaxFeature(compat::JSFeature::kDestructuring, logger::Range{/*loc=*/expr.loc, /*len=*/1});
            }
            bool hasSpread = false;
            for (size_t i = 0; i < eArr->items.size(); i++) {
                Expr item = eArr->items[i];
                if (Get<EMissing>(item.data) != nullptr) {
                    // No-op
                } else if (ESpread *e2 = Get<ESpread>(item.data); e2 != nullptr) {
                    e2->value = this->visitExprInOut(e2->value, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/in.assignTarget}).first;
                    hasSpread = true;
                } else if (EBinary *eArrBin = Get<EBinary>(item.data); eArrBin != nullptr) {
                    if (in.assignTarget != AssignTarget::kNone && eArrBin->op == OpCode::kBinOpAssign) {
                        eArrBin->left = this->visitExprInOut(eArrBin->left, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/AssignTarget::kReplace}).first;

                        // Propagate the name to keep from the binding into the initializer
                        if (EIdentifier *id = Get<EIdentifier>(eArrBin->left.data); id != nullptr) {
                            this->nameToKeep = this->symbols[id->ref.inner_index].original_name;
                            this->nameToKeepIsFor =eArrBin->right.data;
                        }

                        eArrBin->right = this->visitExpr(eArrBin->right);
                    } else {
                        item = this->visitExprInOut(item, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/in.assignTarget}).first;
                    }
                } else {
                    item = this->visitExprInOut(item, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/in.assignTarget}).first;
                }
                eArr->items[i] = item;
            }

            // "[1, ...[2, 3], 4]" => "[1, 2, 3, 4]"
            if (structural.minifySyntax && hasSpread && in.assignTarget == AssignTarget::kNone) {
                eArr->items = InlineSpreadsOfArrayLiterals(eArr->items);
            }
        } else if (auto *eObj = Get<EObject>(expr.data); eObj != nullptr) {
            if (in.assignTarget != AssignTarget::kNone) {
                if (eObj->comma_after_spread.start != 0) {
                    this->log.AddError(&this->tracker, logger::Range{/*loc=*/eObj->comma_after_spread, /*len=*/1}, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedCommaAfterRest));
                }
                this->markSyntaxFeature(compat::JSFeature::kDestructuring, logger::Range{/*loc=*/expr.loc, /*len=*/1});
            }

            bool hasSpread = false;
            logger::Range protoRange;
            compiler::Ref innerClassNameRef = compiler::kInvalidRef;

            for (size_t i = 0; i < eObj->properties.size(); i++) {
                Property &property = eObj->properties[i];

                if (property.kind != PropertyKind::kSpread) {
                    Expr key = property.key;
                    if (ENameOfSymbol *mangled = Get<ENameOfSymbol>(key.data); mangled != nullptr) {
                        mangled->ref = this->symbolForMangledProp(this->loadNameFromRef(mangled->ref));
                    } else {
                        key = this->visitExprInOut(property.key, exprIn{/*isMethod=*/false, /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/true, /*assignTarget=*/AssignTarget::kNone}).first;
                        property.key = key;
                    }

                    // Forbid duplicate "__proto__" properties according to the specification
                    if (!Has(property.flags, PropertyFlags::kIsComputed) && !Has(property.flags, PropertyFlags::kWasShorthand) &&
                        property.kind == PropertyKind::kField && in.assignTarget == AssignTarget::kNone) {
                        if (EString *str = Get<EString>(key.data); str != nullptr && helpers::UTF16EqualsString(str->value, "__proto__")) {
                            logger::Range r = RangeOfIdentifier(this->source, key.loc);
                            if (protoRange.len > 0) {
                                std::vector<logger::MsgData> notes;
                                notes.push_back(this->tracker.MakeMsgData(protoRange, "The earlier \"__proto__\" property is here:"));
                                this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_ProtoPropertyMoreThanOnce), notes);
                            } else {
                                protoRange = r;
                            }
                        }
                    }

                    // "{['x']: y}" => "{x: y}"
                    if (structural.minifySyntax && Has(property.flags, PropertyFlags::kIsComputed)) {
                        if (EInlinedEnum *inlined = Get<EInlinedEnum>(key.data); inlined != nullptr) {
                            if (Get<EString>(inlined->value.data) != nullptr || Get<ENumber>(inlined->value.data) != nullptr) {
                                key.data = inlined->value.data;
                                property.key.data = key.data;
                            }
                        }
                        if (Get<ENumber>(key.data) != nullptr || Get<ENameOfSymbol>(key.data) != nullptr) {
                            property.flags = static_cast<PropertyFlags>(static_cast<uint8_t>(property.flags) & ~static_cast<uint8_t>(PropertyFlags::kIsComputed));
                        } else if (EString *keyStr = Get<EString>(key.data); keyStr != nullptr) {
                            if (!helpers::UTF16EqualsString(keyStr->value, "__proto__")) {
                                property.flags = static_cast<PropertyFlags>(static_cast<uint8_t>(property.flags) & ~static_cast<uint8_t>(PropertyFlags::kIsComputed));
                            }
                        }
                    }
                } else {
                    hasSpread = true;
                }

                // Extract the initializer for expressions like "({ a: b = c } = d)"
                if (in.assignTarget != AssignTarget::kNone && IsNil(property.initializer_or_nil.data) && !IsNil(property.value_or_nil.data)) {
                    if (EBinary *binary = Get<EBinary>(property.value_or_nil.data); binary != nullptr && binary->op == OpCode::kBinOpAssign) {
                        // "binary" points into "property.value_or_nil", so snapshot the
                        // operands before reassigning it.
                        Expr left = binary->left;
                        Expr right = binary->right;
                        property.initializer_or_nil = right;
                        property.value_or_nil = left;
                    }
                }

                if (!IsNil(property.value_or_nil.data)) {
                    bool oldIsInStaticClassContext = this->fnOnlyDataVisit.isInStaticClassContext;
                    compiler::Ref *oldInnerClassNameRef = this->fnOnlyDataVisit.innerClassNameRef;

                    // If this is an async method and async methods are unsupported,
                    // generate a temporary variable in case this async method contains a
                    // "super" property reference. If that happens, the "super" expression
                    // must be lowered which will need a reference to this object literal.
                    if (property.kind == PropertyKind::kMethod && compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kAsyncAwait)) {
                        if (EFunction *fn = Get<EFunction>(property.value_or_nil.data); fn != nullptr && fn->fn.is_async) {
                            if (innerClassNameRef == compiler::kInvalidRef) {
                                innerClassNameRef = this->generateTempRef(tempRefNeedsDeclareMayBeCapturedInsideLoop, "");
                            }
                            this->fnOnlyDataVisit.isInStaticClassContext = true;
                            this->fnOnlyDataVisit.innerClassNameRef = &innerClassNameRef;
                        }
                    }

                    // Propagate the name to keep from the property into the value
                    if (EString *str = Get<EString>(property.key.data); str != nullptr) {
                        this->nameToKeep = helpers::UTF16ToString(str->value);
                        this->nameToKeepIsFor =property.value_or_nil.data;
                    }

                    property.value_or_nil = this->visitExprInOut(property.value_or_nil, exprIn{/*isMethod=*/IsMethodDefinition(property.kind), /*isLoweredPrivateMethod=*/false, /*hasChainParent=*/false, /*storeThisArgForParentOptionalChain=*/false, /*shouldMangleStringsAsProps=*/false, /*assignTarget=*/in.assignTarget}).first;

                    this->fnOnlyDataVisit.innerClassNameRef = oldInnerClassNameRef;
                    this->fnOnlyDataVisit.isInStaticClassContext = oldIsInStaticClassContext;
                }

                if (!IsNil(property.initializer_or_nil.data)) {
                    // Propagate the name to keep from the binding into the initializer
                    if (EIdentifier *id = Get<EIdentifier>(property.value_or_nil.data); id != nullptr) {
                        this->nameToKeep = this->symbols[id->ref.inner_index].original_name;
                        this->nameToKeepIsFor =property.initializer_or_nil.data;
                    }

                    property.initializer_or_nil = this->visitExpr(property.initializer_or_nil);
                }

                // "{ '123': 4 }" => "{ 123: 4 }" (this is done late to allow "'123'" to be mangled)
                if (structural.minifySyntax) {
                    if (EString *str = Get<EString>(property.key.data); str != nullptr) {
                        if (std::optional<double> numberValue = StringToEquivalentNumberValue(str->value); numberValue && *numberValue >= 0) {
                            std::shared_ptr<ENumber> num = std::make_shared<ENumber>();
                            num->value = *numberValue;
                            property.key.data = num;
                        }
                    }
                }
            }

            // Check for and warn about duplicate keys in object literals
            if (!this->suppressWarningsAboutWeirdCode) {
                this->warnAboutDuplicateProperties(eObj->properties, duplicatePropertiesInObject);
            }

            if (in.assignTarget == AssignTarget::kNone) {
                // "{a, ...{b, c}, d}" => "{a, b, c, d}"
                if (structural.minifySyntax && hasSpread) {
                    eObj->properties = MangleObjectSpread(eObj->properties);
                }

                // Object expressions represent both object literals and binding patterns.
                // Only lower object spread if we're an object literal, not a binding pattern.
                Expr value = this->lowerObjectSpread(expr.loc, eObj);

                // If we generated and used the temporary variable for a lowered "super"
                // property reference inside a lowered "async" method, then initialize
                // the temporary with this object literal.
                if (innerClassNameRef != compiler::kInvalidRef && this->symbols[innerClassNameRef.inner_index].use_count_estimate > 0) {
                    this->recordUsage(innerClassNameRef);
                    value = Assign(Expr{std::make_shared<EIdentifier>(EIdentifier{/*ref=*/innerClassNameRef}), expr.loc}, value);
                }

                return {value, exprOut{}};
            }
        } else if (auto *eImport = Get<EImportCall>(expr.data); eImport != nullptr) {
            bool isAwaitTarget = this->awaitTarget != E{} && IsSameNodeAs(this->awaitTarget, expr.data);
            bool isThenCatchTarget = this->thenCatchChain.nextTarget != E{} && IsSameNodeAs(this->thenCatchChain.nextTarget, expr.data) && this->thenCatchChain.hasCatch;
            eImport->expr = this->visitExpr(eImport->expr);

            std::shared_ptr<compiler::ImportAssertOrWith> assertOrWith;
            compiler::ImportRecordFlags flags = compiler::ImportRecordFlags::kNone;
            bool breakOut = false;
            if (!IsNil(eImport->options_or_nil.data)) {
                eImport->options_or_nil = this->visitExpr(eImport->options_or_nil);

                // If there's an additional argument, this can't be split because the
                // additional argument requires evaluation and our AST nodes can't be
                // reused in different places in the AST (e.g. function scopes must be
                // unique). Also the additional argument may have side effects and we
                // don't currently account for that.
                std::string why = "the second argument was not an object literal";
                logger::Loc whyLoc = eImport->options_or_nil.loc;

                // However, make a special case for an additional argument that contains
                // only an "assert" or a "with" clause. In that case we can split this
                // AST node.
                if (EObject *object = Get<EObject>(eImport->options_or_nil.data); object != nullptr) {
                    if (object->properties.size() == 1) {
                        Property &prop = object->properties[0];
                        if (prop.kind == PropertyKind::kField && !Has(prop.flags, PropertyFlags::kIsComputed)) {
                            if (EString *str = Get<EString>(prop.key.data); str != nullptr && (helpers::UTF16EqualsString(str->value, "assert") || helpers::UTF16EqualsString(str->value, "with"))) {
                                compiler::AssertOrWithKeyword keyword = compiler::AssertOrWithKeyword::kWith;
                                if (helpers::UTF16EqualsString(str->value, "assert")) {
                                    keyword = compiler::AssertOrWithKeyword::kAssert;
                                }
                                if (EObject *assertValue = Get<EObject>(prop.value_or_nil.data); assertValue != nullptr) {
                                    std::optional<std::vector<compiler::AssertOrWithEntry>> entries = std::vector<compiler::AssertOrWithEntry>{};
                                    for (const Property &entryProp : assertValue->properties) {
                                        if (entryProp.kind == PropertyKind::kField && !Has(entryProp.flags, PropertyFlags::kIsComputed)) {
                                            if (const EString *key = Get<EString>(entryProp.key.data); key != nullptr) {
                                                if (const EString *value = Get<EString>(entryProp.value_or_nil.data); value != nullptr) {
                                                    entries->push_back(compiler::AssertOrWithEntry{/*key=*/key->value, /*value=*/value->value, /*key_loc=*/entryProp.key.loc, /*value_loc=*/entryProp.value_or_nil.loc, /*prefer_quoted_key=*/Has(entryProp.flags, PropertyFlags::kPreferQuotedKey)});
                                                    if (keyword == compiler::AssertOrWithKeyword::kAssert && helpers::UTF16EqualsString(key->value, "type") && helpers::UTF16EqualsString(value->value, "json")) {
                                                        flags = flags | compiler::ImportRecordFlags::kAssertTypeJSON;
                                                    }
                                                    continue;
                                                } else {
                                                    why = "the value for the property \"" + helpers::UTF16ToString(key->value) + "\" was not a string literal";
                                                    whyLoc = entryProp.value_or_nil.loc;
                                                }
                                            } else {
                                                why = "this property was not a string literal";
                                                whyLoc = entryProp.key.loc;
                                            }
                                        } else {
                                            why = "this property was invalid";
                                            whyLoc = entryProp.key.loc;
                                        }
                                        entries.reset();
                                        break;
                                    }
                                    if (entries) {
                                        if (keyword == compiler::AssertOrWithKeyword::kAssert) {
                                            this->maybeWarnAboutAssertKeyword(prop.key.loc);
                                        }
                                        assertOrWith = std::make_shared<compiler::ImportAssertOrWith>(compiler::ImportAssertOrWith{/*entries=*/*entries, /*keyword_loc=*/prop.key.loc, /*inner_open_brace_loc=*/prop.value_or_nil.loc, /*inner_close_brace_loc=*/assertValue->close_brace_loc, /*outer_open_brace_loc=*/eImport->options_or_nil.loc, /*outer_close_brace_loc=*/object->close_brace_loc, /*keyword=*/keyword});
                                        why = "";
                                    }
                                } else {
                                    why = "the value for \"assert\" was not an object literal";
                                    whyLoc = prop.value_or_nil.loc;
                                }
                            } else {
                                why = "this property was not called \"assert\" or \"with\"";
                                whyLoc = prop.key.loc;
                            }
                        } else {
                            why = "this property was invalid";
                            whyLoc = prop.key.loc;
                        }
                    } else {
                        why = "the second argument was not an object literal with a single property called \"assert\" or \"with\"";
                        whyLoc = eImport->options_or_nil.loc;
                    }
                }

                // Handle the case that isn't just an import assertion or attribute clause
                if (!why.empty()) {
                    // Only warn when bundling
                    if (structural.mode == config::Mode::kBundle) {
                        std::string text = "This \"import()\" was not recognized because " + why;
                        logger::MsgKind kind = logger::MsgKind::kWarning;
                        if (this->suppressWarningsAboutWeirdCode) {
                            kind = logger::MsgKind::kDebug;
                        }
                        this->log.AddID(logger::MsgID::kJS_UnsupportedDynamicImport, kind, &this->tracker, logger::Range{/*loc=*/whyLoc}, text);
                    }

                    // If import assertions and/attributes are both not supported in the
                    // target platform, then "import()" cannot accept a second argument
                    // and keeping them would be a syntax error, so we need to get rid of
                    // them. We can't just not print them because they may have important
                    // side effects. Attempt to discard them without changing side effects
                    // and generate an error if that isn't possible.
                    if (compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kImportAssertions) && compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kImportAttributes)) {
                        if (this->astHelpers.ExprCanBeRemovedIfUnused(eImport->options_or_nil)) {
                            eImport->options_or_nil = Expr{};
                        } else {
                            this->markSyntaxFeature(compat::JSFeature::kImportAttributes, logger::Range{/*loc=*/eImport->options_or_nil.loc});
                        }
                    }

                    // Stop now so we don't try to split "?:" expressions below and
                    // potentially end up with an AST node reused multiple times
                    breakOut = true;
                }
            }

            if (!breakOut) {
                Expr transposed = this->maybeTransposeIfExprChain(eImport->expr, [&](Expr arg) -> Expr {
                    // The argument must be a string
                    if (EString *str = Get<EString>(arg.data); str != nullptr) {
                        // Ignore calls to import() if the control flow is provably dead here.
                        // We don't want to spend time scanning the required files if they will
                        // never be used.
                        if (this->isControlFlowDead) {
                            return Expr{kENullShared, arg.loc};
                        }

                        uint32_t importRecordIndex = this->addImportRecord(compiler::ImportKind::kDynamic, eImport->phase, this->source.RangeOfString(arg.loc), helpers::UTF16ToString(str->value), assertOrWith, flags);
                        if (isAwaitTarget && this->fnOrArrowDataVisit.tryBodyCount != 0) {
                            compiler::ImportRecord &record = this->importRecords[importRecordIndex];
                            record.flags = record.flags | compiler::ImportRecordFlags::kHandlesImportErrors;
                            record.error_handler_loc = this->fnOrArrowDataVisit.tryCatchLoc;
                        } else if (isThenCatchTarget) {
                            compiler::ImportRecord &record = this->importRecords[importRecordIndex];
                            record.flags = record.flags | compiler::ImportRecordFlags::kHandlesImportErrors;
                            record.error_handler_loc = this->thenCatchChain.catchLoc;
                        }
                        this->importRecordsForCurrentPart.push_back(importRecordIndex);
                        std::shared_ptr<EImportString> importString = std::make_shared<EImportString>();
                        importString->import_record_index = importRecordIndex;
                        importString->close_paren_loc = eImport->close_paren_loc;
                        return Expr{importString, expr.loc};
                    }

                    // Handle glob patterns
                    if (structural.mode == config::Mode::kBundle) {
                        Expr value = this->handleGlobPattern(arg, compiler::ImportKind::kDynamic, eImport->phase, "globImport", assertOrWith);
                        if (!IsNil(value.data)) {
                            return value;
                        }
                    }

                    // Use a debug log so people can see this if they want to
                    logger::Range r = RangeOfIdentifier(this->source, expr.loc);
                    this->log.AddID(logger::MsgID::kJS_UnsupportedDynamicImport, logger::MsgKind::kDebug, &this->tracker, r,
                        "This \"import\" expression will not be bundled because the argument is not a string literal");

                    // We need to convert this into a call to "require()" if ES6 syntax is
                    // not supported in the current output format. The full conversion:
                    //
                    //   Before:
                    //     import(foo)
                    //
                    //   After:
                    //     Promise.resolve().then(() => __toESM(require(foo)))
                    //
                    // This is normally done by the printer since we don't know during the
                    // parsing stage whether this module is external or not. However, it's
                    // guaranteed to be external if the argument isn't a string. We handle
                    // this case here instead of in the printer because both the printer
                    // and the linker currently need an import record to handle this case
                    // correctly, and you need a string literal to get an import record.
                    if (compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kDynamicImport)) {
                        std::shared_ptr<ECall> requireCall = std::make_shared<ECall>();
                        requireCall->target = this->valueToSubstituteForRequire(expr.loc);
                        requireCall->args.push_back(arg);
                        requireCall->close_paren_loc = eImport->close_paren_loc;
                        std::vector<Expr> toESMArgs;
                        toESMArgs.push_back(Expr{requireCall, expr.loc});
                        Expr value = this->callRuntime(arg.loc, "__toESM", toESMArgs);
                        FnBody body;
                        body.loc = expr.loc;
                        std::shared_ptr<SReturn> returnStmt = std::make_shared<SReturn>();
                        returnStmt->value_or_nil = value;
                        body.block.stmts.push_back(Stmt{returnStmt, expr.loc});
                        Expr then;
                        if (compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kArrow)) {
                            std::shared_ptr<EFunction> function = std::make_shared<EFunction>();
                            function->fn.body = body;
                            then = Expr{function, expr.loc};
                        } else {
                            std::shared_ptr<EArrow> arrow = std::make_shared<EArrow>();
                            arrow->body = body;
                            arrow->prefer_expr = true;
                            then = Expr{arrow, expr.loc};
                        }

                        std::shared_ptr<EIdentifier> promiseId = std::make_shared<EIdentifier>();
                        promiseId->ref = this->makePromiseRef();
                        std::shared_ptr<EDot> resolveDot = std::make_shared<EDot>();
                        resolveDot->target = Expr{promiseId, expr.loc};
                        resolveDot->name = "resolve";
                        resolveDot->name_loc = expr.loc;
                        std::shared_ptr<ECall> resolveCall = std::make_shared<ECall>();
                        resolveCall->target = Expr{resolveDot, expr.loc};
                        resolveCall->kind = CallKind::kTargetWasOriginallyPropertyAccess;
                        std::shared_ptr<EDot> thenDot = std::make_shared<EDot>();
                        thenDot->target = Expr{resolveCall, expr.loc};
                        thenDot->name = "then";
                        thenDot->name_loc = expr.loc;
                        std::shared_ptr<ECall> thenCall = std::make_shared<ECall>();
                        thenCall->target = Expr{thenDot, expr.loc};
                        thenCall->args.push_back(then);
                        thenCall->kind = CallKind::kTargetWasOriginallyPropertyAccess;
                        return Expr{thenCall, expr.loc};
                    }

                    std::shared_ptr<EImportCall> importCall = std::make_shared<EImportCall>();
                    importCall->expr = arg;
                    importCall->options_or_nil = eImport->options_or_nil;
                    importCall->close_paren_loc = eImport->close_paren_loc;
                    return Expr{importCall, expr.loc};
                });
                return {transposed, exprOut{}};
            }
        } else if (auto *eCall = Get<ECall>(expr.data); eCall != nullptr) {
            this->callTarget = eCall->target.data;

            // Track ".then().catch()" chains
            bool hasCatch = this->thenCatchChain.nextTarget != E{} && IsSameNodeAs(this->thenCatchChain.nextTarget, expr.data) && this->thenCatchChain.hasCatch;
            struct thenCatchChain chain;
            chain.nextTarget = eCall->target.data;
            chain.catchLoc = this->thenCatchChain.catchLoc;
            chain.hasMultipleArgs = eCall->args.size() >= 2;
            chain.hasCatch = hasCatch;
            this->thenCatchChain = chain;
            if (this->thenCatchChain.hasMultipleArgs) {
                this->thenCatchChain.catchLoc = eCall->args[1].loc;
            }

            bool wasIdentifierBeforeVisit = false;
            bool isParenthesizedOptionalChain = false;
            if (EIdentifier *e2Id = Get<EIdentifier>(eCall->target.data); e2Id != nullptr) {
                wasIdentifierBeforeVisit = true;
            } else if (EDot *e2Dot = Get<EDot>(eCall->target.data); e2Dot != nullptr) {
                isParenthesizedOptionalChain = eCall->optional_chain == OptionalChain::kNone && e2Dot->optional_chain != OptionalChain::kNone;
            } else if (EIndex *e2Idx = Get<EIndex>(eCall->target.data); e2Idx != nullptr) {
                isParenthesizedOptionalChain = eCall->optional_chain == OptionalChain::kNone && e2Idx->optional_chain != OptionalChain::kNone;
            }
            std::pair<Expr, exprOut> targetResult = this->visitExprInOut(eCall->target, exprIn{
                /*isMethod=*/false,
                /*isLoweredPrivateMethod=*/false,
                /*hasChainParent=*/eCall->optional_chain == OptionalChain::kContinue,

                // Signal to our child if this is an ECall at the start of an optional
                // chain. If so, the child will need to stash the "this" context for us
                // that we need for the ".call(this, ...args)".
                /*storeThisArgForParentOptionalChain=*/eCall->optional_chain == OptionalChain::kStart || isParenthesizedOptionalChain,
                /*shouldMangleStringsAsProps=*/false,
                /*assignTarget=*/AssignTarget::kNone,
            });
            eCall->target = targetResult.first;
            exprOut out = targetResult.second;
            this->warnAboutImportNamespaceCall(eCall->target, importNamespaceCallKind::exprKindCall);

            // Automatically mark immediately-invoked function expressions for eager compilation
            if (EFunction *fn = Get<EFunction>(eCall->target.data); fn != nullptr) {
                fn->is_parenthesized = true;
            }

            bool hasSpread = false;
            bool oldIsControlFlowDead = this->isControlFlowDead;

            // If we're removing this call, don't count any arguments as symbol uses
            if (out.callMustBeReplacedWithUndefined) {
                if (IsPropertyAccess(eCall->target)) {
                    this->isControlFlowDead = true;
                } else {
                    out.callMustBeReplacedWithUndefined = false;
                }
            }

            // Visit the arguments
            for (Expr &arg : eCall->args) {
                arg = this->visitExpr(arg);
                if (Get<ESpread>(arg.data) != nullptr) {
                    hasSpread = true;
                }
            }

            // Mark side-effect free IIFEs with "/* @__PURE__ */"
            if (!eCall->can_be_unwrapped_if_unused) {
                if (EArrow *arrow = Get<EArrow>(eCall->target.data); arrow != nullptr) {
                    if (!arrow->is_async && this->iifeCanBeRemovedIfUnused(arrow->args, arrow->body)) {
                        eCall->can_be_unwrapped_if_unused = true;
                    }
                } else if (EFunction *fn = Get<EFunction>(eCall->target.data); fn != nullptr) {
                    if (!fn->fn.is_async && !fn->fn.is_generator && this->iifeCanBeRemovedIfUnused(fn->fn.args, fn->fn.body)) {
                        eCall->can_be_unwrapped_if_unused = true;
                    }
                }
            }

            // Our hack for reading Yarn PnP files is implemented here:
            if (structural.decodeHydrateRuntimeStateYarnPnP) {
                if (EIdentifier *id = Get<EIdentifier>(eCall->target.data); id != nullptr && this->symbols[id->ref.inner_index].original_name == "hydrateRuntimeState" && eCall->args.size() >= 1) {
                    if (EObject *object = Get<EObject>(eCall->args[0].data); object != nullptr) {
                        // "hydrateRuntimeState(<object literal>)"
                        Expr arg = eCall->args[0];
                        if (this->IsValidJSON(arg)) {
                            this->manifestForYarnPnP = arg;
                        }
                    } else if (ECall *call = Get<ECall>(eCall->args[0].data); call != nullptr) {
                        // "hydrateRuntimeState(JSON.parse(<something>))"
                        if (call->args.size() == 1) {
                            if (EDot *dot = Get<EDot>(call->target.data); dot != nullptr && dot->name == "parse") {
                                if (EIdentifier *jsonId = Get<EIdentifier>(dot->target.data); jsonId != nullptr) {
                                    compiler::Symbol &symbol = this->symbols[jsonId->ref.inner_index];
                                    if (symbol.kind == compiler::SymbolKind::kUnbound && symbol.original_name == "JSON") {
                                        Expr arg = call->args[0];
                                        if (EString *str = Get<EString>(arg.data); str != nullptr) {
                                            // "hydrateRuntimeState(JSON.parse(<string literal>))"
                                            logger::Source yarnSource;
                                            yarnSource.key_path = this->source.key_path;
                                            yarnSource.contents = helpers::UTF16ToString(str->value);
                                            std::vector<logger::StringInJSTableEntry> stringInJSTable = logger::GenerateStringInJSTable(this->source.contents, arg.loc, yarnSource.contents);
                                            logger::Log yarnLog = logger::NewStringInJSLog(this->log, this->tracker, stringInJSTable);
                                            this->manifestForYarnPnP = this->ParseJSON(yarnLog, yarnSource, JSONOptions{}).first;
                                            this->remapExprLocsInJSON(&this->manifestForYarnPnP, stringInJSTable);
                                        } else if (EIdentifier *id2 = Get<EIdentifier>(arg.data); id2 != nullptr) {
                                            // "hydrateRuntimeState(JSON.parse(<identifier>))"
                                            auto it = this->stringLocalsForYarnPnP.find(id2->ref);
                                            if (it != this->stringLocalsForYarnPnP.end()) {
                                                const stringLocalForYarnPnP &data = it->second;
                                                logger::Source yarnSource;
                                                yarnSource.key_path = this->source.key_path;
                                                yarnSource.contents = helpers::UTF16ToString(std::span<const char16_t>(reinterpret_cast<const char16_t *>(data.value.data()), static_cast<size_t>(data.value.size())));
                                                std::vector<logger::StringInJSTableEntry> stringInJSTable = logger::GenerateStringInJSTable(this->source.contents, data.loc, yarnSource.contents);
                                                logger::Log yarnLog = logger::NewStringInJSLog(this->log, this->tracker, stringInJSTable);
                                                this->manifestForYarnPnP = this->ParseJSON(yarnLog, yarnSource, JSONOptions{}).first;
                                                this->remapExprLocsInJSON(&this->manifestForYarnPnP, stringInJSTable);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Stop now if this call must be removed
            if (out.callMustBeReplacedWithUndefined) {
                this->isControlFlowDead = oldIsControlFlowDead;
                return {Expr{kEUndefinedShared, expr.loc}, exprOut{}};
            }

            if (structural.minifySyntax) {
                // "foo(1, ...[2, 3], 4)" => "foo(1, 2, 3, 4)"
                if (hasSpread) {
                    eCall->args = InlineSpreadsOfArrayLiterals(eCall->args);
                }

                // "(() => x)()" => "x"
                std::pair<Expr, bool> inlineResult = this->maybeInlineIIFE(expr.loc, eCall);
                if (inlineResult.second) {
                    return {inlineResult.first, exprOut{}};
                }
            }

            if (EImportIdentifier *t = Get<EImportIdentifier>(eCall->target.data); t != nullptr) {
                // If this function is inlined, allow it to be tree-shaken
                if (structural.minifySyntax && !this->isControlFlowDead) {
                    this->convertSymbolUseToCall(t->ref, eCall->args.size() == 1 && !hasSpread);
                }
            } else if (EIdentifier *tId = Get<EIdentifier>(eCall->target.data); tId != nullptr) {
                // Detect if this is a direct eval. Note that "(1 ? eval : 0)(x)" will
                // become "eval(x)" after we visit the target due to dead code elimination,
                // but that doesn't mean it should become a direct eval.
                //
                // Note that "eval?.(x)" is considered an indirect eval. There was debate
                // about this after everyone implemented it as a direct eval, but the
                // language committee said it was indirect and everyone had to change it:
                // https://github.com/tc39/ecma262/issues/2062.
                if (eCall->optional_chain == OptionalChain::kNone) {
                    compiler::Symbol symbol = this->symbols[tId->ref.inner_index];
                    if (wasIdentifierBeforeVisit && symbol.original_name == "eval") {
                        eCall->kind = CallKind::kDirectEval;

                        // Pessimistically assume that if this looks like a CommonJS module
                        // (e.g. no "export" keywords), a direct call to "eval" means that
                        // code could potentially access "module" or "exports".
                        if (structural.mode == config::Mode::kBundle && !this->isFileConsideredToHaveESMExports) {
                            this->recordUsage(this->moduleRef);
                            this->recordUsage(this->exportsRef);
                        }

                        // Mark this scope and all parent scopes as containing a direct eval.
                        // This will prevent us from renaming any symbols.
                        for (Scope *s = this->currentScope; s != nullptr; s = s->parent) {
                            s->contains_direct_eval = true;
                        }

                        // Warn when direct eval is used in an ESM file. There is no way we
                        // can guarantee that this will work correctly for top-level imported
                        // and exported symbols due to scope hoisting. Except don't warn when
                        // this code is in a 3rd-party library because there's nothing people
                        // will be able to do about the warning.
                        std::string text = "Using direct eval with a bundler is not recommended and may cause problems";
                        logger::MsgKind kind = logger::MsgKind::kDebug;
                        if (structural.mode == config::Mode::kBundle && this->isFileConsideredESM && !this->suppressWarningsAboutWeirdCode) {
                            kind = logger::MsgKind::kWarning;
                        }
                        this->log.AddIDWithNotes(logger::MsgID::kJS_DirectEval, kind, &this->tracker, RangeOfIdentifier(this->source, eCall->target.loc), text,
                            std::vector<logger::MsgData>{logger::MsgData{nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_DirectEvalNote))}});
                    } else if (Has(symbol.flags, compiler::SymbolFlags::kCallCanBeUnwrappedIfUnused)) {
                        // Automatically add a "/* @__PURE__ */" comment to file-local calls
                        // of functions declared with a "/* @__NO_SIDE_EFFECTS__ */" comment
                        tId->call_can_be_unwrapped_if_unused = true;
                    }
                }

                // Handle certain special cases
                if (eCall->args.size() <= 1 && !hasSpread) {
                    compiler::Symbol &symbol = this->symbols[tId->ref.inner_index];
                    if (symbol.kind == compiler::SymbolKind::kUnbound) {
                        if (symbol.original_name == "Symbol") {
                            // Calling the "Symbol()" constructor with a primitive will never throw
                            if (eCall->args.empty() || KnownPrimitiveType(eCall->args[0].data) != PrimitiveType::kUnknown) {
                                eCall->can_be_unwrapped_if_unused = true;
                            }
                        }

                        // Optimize references to global constructors
                        if (structural.minifySyntax && tId->can_be_removed_if_unused) {
                            // Note: We construct expressions by assigning to "expr.data" so
                            // that the source map position for the constructor is preserved
                            if (symbol.original_name == "Boolean") {
                                if (eCall->args.empty()) {
                                    return {Expr{std::make_shared<EBoolean>(EBoolean{/*value=*/false}), expr.loc}, exprOut{}};
                                } else {
                                    expr.data = std::make_shared<EUnary>(EUnary{/*value=*/this->astHelpers.SimplifyBooleanExpr(eCall->args[0]), /*op=*/OpCode::kUnOpNot});
                                    return {Not(expr), exprOut{}};
                                }
                            } else if (symbol.original_name == "Number") {
                                if (eCall->args.empty()) {
                                    return {Expr{std::make_shared<ENumber>(ENumber{/*value=*/0}), expr.loc}, exprOut{}};
                                } else {
                                    Expr arg = eCall->args[0];
                                    switch (KnownPrimitiveType(arg.data)) {
                                    case PrimitiveType::kNumber:
                                        return {arg, exprOut{}};

                                    case PrimitiveType::kUndefined: // NaN
                                    case PrimitiveType::kNull:      // 0
                                    case PrimitiveType::kBoolean:   // 0 or 1
                                    case PrimitiveType::kString:    // StringToNumber
                                        if (std::optional<double> number = ToNumberWithoutSideEffects(arg.data)) {
                                            expr.data = std::make_shared<ENumber>(ENumber{/*value=*/*number});
                                        } else {
                                            expr.data = std::make_shared<EUnary>(EUnary{/*value=*/arg, /*op=*/OpCode::kUnOpPos});
                                        }
                                        return {expr, exprOut{}};
                                    default:
                                        break;
                                    }
                                }
                            } else if (symbol.original_name == "String") {
                                if (eCall->args.empty()) {
                                    return {Expr{std::make_shared<EString>(EString{/*value=*/std::u16string{}, /*legacy_octal_loc=*/logger::Loc{}}), expr.loc}, exprOut{}};
                                } else {
                                    Expr arg = eCall->args[0];
                                    switch (KnownPrimitiveType(arg.data)) {
                                    case PrimitiveType::kString:
                                        return {arg, exprOut{}};
                                    default:
                                        break;
                                    }
                                }
                            } else if (symbol.original_name == "BigInt") {
                                if (eCall->args.size() == 1) {
                                    Expr arg = eCall->args[0];
                                    switch (KnownPrimitiveType(arg.data)) {
                                    case PrimitiveType::kBigInt:
                                        return {arg, exprOut{}};
                                    default:
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }

                // Copy the call side effect flag over if this is a known target
                if (tId->call_can_be_unwrapped_if_unused) {
                    eCall->can_be_unwrapped_if_unused = true;
                }

                // If this function is inlined, allow it to be tree-shaken
                if (structural.minifySyntax && !this->isControlFlowDead) {
                    this->convertSymbolUseToCall(tId->ref, eCall->args.size() == 1 && !hasSpread);
                }
            } else if (EDot *tDot = Get<EDot>(eCall->target.data); tDot != nullptr) {
                if (eCall->args.size() == 1) {
                    if (tDot->name == "resolve") {
                        // Recognize "require.resolve()" calls
                        if (tDot->optional_chain == OptionalChain::kNone && structural.mode != config::Mode::kPassThrough) {
                            if (EIdentifier *id = Get<EIdentifier>(tDot->target.data); id != nullptr && id->ref == this->requireRef) {
                                this->ignoreUsage(this->requireRef);
                                Expr transposed = this->maybeTransposeIfExprChain(eCall->args[0], [this, eCall, expr, tDot](Expr arg) -> Expr {
                                    if (EString *str = Get<EString>(eCall->args[0].data); str != nullptr) {
                                        // Ignore calls to require.resolve() if the control flow is provably
                                        // dead here. We don't want to spend time scanning the required files
                                        // if they will never be used.
                                        if (this->isControlFlowDead) {
                                            return Expr{kENullShared, expr.loc};
                                        }

                                        uint32_t importRecordIndex = this->addImportRecord(compiler::ImportKind::kRequireResolve, compiler::ImportPhase::kEvaluation, this->source.RangeOfString(eCall->args[0].loc), helpers::UTF16ToString(str->value), nullptr, compiler::ImportRecordFlags{});
                                        if (this->fnOrArrowDataVisit.tryBodyCount != 0) {
                                            compiler::ImportRecord &record = this->importRecords[importRecordIndex];
                                            record.flags = record.flags | compiler::ImportRecordFlags::kHandlesImportErrors;
                                            record.error_handler_loc = this->fnOrArrowDataVisit.tryCatchLoc;
                                        }
                                        this->importRecordsForCurrentPart.push_back(importRecordIndex);

                                        // Create a new expression to represent the operation
                                        std::shared_ptr<ERequireResolveString> repl = std::make_shared<ERequireResolveString>();
                                        repl->import_record_index = importRecordIndex;
                                        repl->close_paren_loc = eCall->close_paren_loc;
                                        return Expr{repl, expr.loc};
                                    }

                                    // Otherwise just return a clone of the "require.resolve()" call
                                    return Expr{std::make_shared<ECall>(ECall{/*target=*/Expr{std::make_shared<EDot>(EDot{/*target=*/this->valueToSubstituteForRequire(tDot->target.loc), /*name=*/tDot->name, /*name_loc=*/tDot->name_loc}), eCall->target.loc}, /*args=*/std::vector<Expr>{arg}, /*close_paren_loc=*/eCall->close_paren_loc, /*optional_chain=*/{}, /*kind=*/eCall->kind}), expr.loc};
                                });
                                return {transposed, exprOut{}};
                            }
                        }
                    } else if (tDot->name == "for") {
                        // Calling "Symbol.for()" with a primitive will never throw
                        if (EIdentifier *id = Get<EIdentifier>(tDot->target.data); id != nullptr) {
                            compiler::Symbol &symbol = this->symbols[id->ref.inner_index];
                            if (symbol.kind == compiler::SymbolKind::kUnbound && symbol.original_name == "Symbol") {
                                if (KnownPrimitiveType(eCall->args[0].data) != PrimitiveType::kUnknown) {
                                    eCall->can_be_unwrapped_if_unused = true;
                                }
                            }
                        }
                    } else if (tDot->name == "create") {
                        // Recognize "Object.create()" calls
                        if (EIdentifier *id = Get<EIdentifier>(tDot->target.data); id != nullptr) {
                            compiler::Symbol &symbol = this->symbols[id->ref.inner_index];
                            if (symbol.kind == compiler::SymbolKind::kUnbound && symbol.original_name == "Object") {
                                // Mark "Object.create(null)" and "Object.create({})" as pure
                                if (Get<ENull>(eCall->args[0].data) != nullptr || Get<EObject>(eCall->args[0].data) != nullptr) {
                                    eCall->can_be_unwrapped_if_unused = true;
                                }
                            }
                        }
                    } else if (tDot->name == "escape") {
                        // Recognize "RegExp.escape()" calls
                        if (EIdentifier *id = Get<EIdentifier>(tDot->target.data); id != nullptr) {
                            compiler::Symbol &symbol = this->symbols[id->ref.inner_index];
                            if (symbol.kind == compiler::SymbolKind::kUnbound && symbol.original_name == "RegExp") {
                                if (KnownPrimitiveType(eCall->args[0].data) == PrimitiveType::kString) {
                                    // Mark "RegExp.escape" with a string literal as pure
                                    eCall->can_be_unwrapped_if_unused = true;
                                }
                            }
                        }
                    }
                }

                if (structural.minifySyntax) {
                    if (tDot->name == "charCodeAt") {
                        // Recognize "'string'.charCodeAt()" calls
                        if (EString *str = Get<EString>(tDot->target.data); str != nullptr && eCall->args.size() <= 1) {
                            int index = 0;
                            bool hasIndex = false;
                            if (eCall->args.empty()) {
                                hasIndex = true;
                            } else if (ENumber *num = Get<ENumber>(eCall->args[0].data); num != nullptr && num->value == std::trunc(num->value) && std::abs(num->value) <= 0x7FFFFFFF) {
                                index = static_cast<int>(num->value);
                                hasIndex = true;
                            }
                            if (hasIndex) {
                                if (index >= 0 && static_cast<size_t>(index) < str->value.size()) {
                                    return {Expr{std::make_shared<ENumber>(ENumber{/*value=*/static_cast<double>(static_cast<uint16_t>(str->value[static_cast<size_t>(index)]))}), expr.loc}, exprOut{}};
                                } else {
                                    return {Expr{std::make_shared<ENumber>(ENumber{/*value=*/std::numeric_limits<double>::quiet_NaN()}), expr.loc}, exprOut{}};
                                }
                            }
                        }
                    } else if (tDot->name == "fromCharCode") {
                        // Recognize "String.fromCharCode()" calls
                        if (EIdentifier *id = Get<EIdentifier>(tDot->target.data); id != nullptr) {
                            compiler::Symbol &symbol = this->symbols[id->ref.inner_index];
                            if (symbol.kind == compiler::SymbolKind::kUnbound && symbol.original_name == "String") {
                                std::u16string charCodes;
                                for (Expr arg : eCall->args) {
                                    std::optional<double> num = ToNumberWithoutSideEffects(arg.data);
                                    if (!num) {
                                        break;
                                    }
                                    charCodes.push_back(static_cast<char16_t>(ToInt32(*num)));
                                }
                                if (charCodes.size() == eCall->args.size()) {
                                    return {Expr{std::make_shared<EString>(EString{/*value=*/charCodes, logger::Loc{}}), expr.loc}, exprOut{}};
                                }
                            }
                        }
                    } else if (tDot->name == "toString") {
                        if (ENumber *targetNum = Get<ENumber>(tDot->target.data); targetNum != nullptr) {
                            int radix = 0;
                            if (eCall->args.empty()) {
                                radix = 10;
                            } else if (eCall->args.size() == 1) {
                                if (ENumber *num = Get<ENumber>(eCall->args[0].data); num != nullptr && num->value == std::trunc(num->value) && num->value >= 2 && num->value <= 36) {
                                    radix = static_cast<int>(num->value);
                                }
                            }
                            if (radix != 0) {
                                if (std::optional<std::string> str = TryToStringOnNumberSafely(targetNum->value, radix)) {
                                    return {Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16(*str), logger::Loc{}}), expr.loc}, exprOut{}};
                                }
                            }
                        } else if (ERegExp *targetReg = Get<ERegExp>(tDot->target.data); targetReg != nullptr) {
                            if (eCall->args.empty()) {
                                return {Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16(targetReg->value), logger::Loc{}}), expr.loc}, exprOut{}};
                            }
                        } else if (EBoolean *targetBool = Get<EBoolean>(tDot->target.data); targetBool != nullptr) {
                            if (eCall->args.empty()) {
                                if (targetBool->value) {
                                    return {Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16("true"), logger::Loc{}}), expr.loc}, exprOut{}};
                                } else {
                                    return {Expr{std::make_shared<EString>(EString{/*value=*/helpers::StringToUTF16("false"), logger::Loc{}}), expr.loc}, exprOut{}};
                                }
                            }
                        } else if (EString *targetStr = Get<EString>(tDot->target.data); targetStr != nullptr) {
                            if (eCall->args.empty()) {
                                return {tDot->target, exprOut{}};
                            }
                        }
                    }
                }

                // Copy the call side effect flag over if this is a known target
                if (tDot->call_can_be_unwrapped_if_unused) {
                    eCall->can_be_unwrapped_if_unused = true;
                }
            } else if (EIndex *tIdx = Get<EIndex>(eCall->target.data); tIdx != nullptr) {
                // Copy the call side effect flag over if this is a known target
                if (tIdx->call_can_be_unwrapped_if_unused) {
                    eCall->can_be_unwrapped_if_unused = true;
                }
            } else if (Get<ESuper>(eCall->target.data) != nullptr) {
                // If we're shimming "super()" calls, replace this call with "__super()"
                if (this->superCtorRef != compiler::kInvalidRef) {
                    this->recordUsage(this->superCtorRef);
                    eCall->target.data = std::make_shared<EIdentifier>(EIdentifier{/*ref=*/this->superCtorRef});
                }
            }

            // Handle parenthesized optional chains
            if (isParenthesizedOptionalChain && out.thisArgFunc && out.thisArgWrapFunc) {
                return {this->lowerParenthesizedOptionalChain(expr.loc, eCall, out), exprOut{}};
            }

            // Lower optional chaining if we're the top of the chain
            bool containsOptionalChain = eCall->optional_chain == OptionalChain::kStart ||
                (eCall->optional_chain == OptionalChain::kContinue && out.childContainsOptionalChain);
            if (containsOptionalChain && !in.hasChainParent) {
                return this->lowerOptionalChain(expr, in, out);
            }

            // If this is a plain call expression (instead of an optional chain), lower
            // private member access in the call target now if there is one
            if (!containsOptionalChain) {
                auto [target, loc, private_] = this->extractPrivateIndex(eCall->target);
                if (private_ != nullptr) {
                    // "foo.#bar(123)" => "__privateGet(_a = foo, #bar).call(_a, 123)"
                    auto [targetFunc, targetWrapFunc] = this->captureValueWithPossibleSideEffects(target.loc, 2, target, captureValueMode::valueCouldBeMutated);
                    ECall call;
                    call.target = Expr{std::make_shared<EDot>(EDot{/*target=*/this->lowerPrivateGet(targetFunc(), loc, private_), /*name=*/"call", /*name_loc=*/target.loc}), target.loc};
                    call.args = std::vector<Expr>{targetFunc()};
                    call.args.insert(call.args.end(), eCall->args.begin(), eCall->args.end());
                    call.can_be_unwrapped_if_unused = eCall->can_be_unwrapped_if_unused;
                    call.kind = CallKind::kTargetWasOriginallyPropertyAccess;
                    return {targetWrapFunc(Expr{std::make_shared<ECall>(call), target.loc}), exprOut{}};
                }
                this->maybeLowerSuperPropertyGetInsideCall(eCall);
            }

            // Track calls to require() so we can use them while bundling
            if (structural.mode != config::Mode::kPassThrough && eCall->optional_chain == OptionalChain::kNone) {
                if (EIdentifier *id = Get<EIdentifier>(eCall->target.data); id != nullptr && id->ref == this->requireRef) {
                    // Heuristic: omit warnings inside try/catch blocks because presumably
                    // the try/catch statement is there to handle the potential run-time
                    // error from the unbundled require() call failing.
                    bool omitWarnings = this->fnOrArrowDataVisit.tryBodyCount != 0;

                    if (structural.mode != config::Mode::kPassThrough) {
                        // There must be one argument
                        if (eCall->args.size() == 1) {
                            this->ignoreUsage(this->requireRef);
                            Expr transposed = this->maybeTransposeIfExprChain(eCall->args[0], [this, eCall, expr, omitWarnings, &structural](Expr arg) -> Expr {
                                // The argument must be a string
                                if (EString *str = Get<EString>(arg.data); str != nullptr) {
                                    // Ignore calls to require() if the control flow is provably dead here.
                                    // We don't want to spend time scanning the required files if they will
                                    // never be used.
                                    if (this->isControlFlowDead) {
                                        return Expr{kENullShared, expr.loc};
                                    }

                                    uint32_t importRecordIndex = this->addImportRecord(compiler::ImportKind::kRequire, compiler::ImportPhase::kEvaluation, this->source.RangeOfString(arg.loc), helpers::UTF16ToString(str->value), nullptr, compiler::ImportRecordFlags{});
                                    if (this->fnOrArrowDataVisit.tryBodyCount != 0) {
                                        compiler::ImportRecord &record = this->importRecords[importRecordIndex];
                                        record.flags = record.flags | compiler::ImportRecordFlags::kHandlesImportErrors;
                                        record.error_handler_loc = this->fnOrArrowDataVisit.tryCatchLoc;
                                    }
                                    this->importRecordsForCurrentPart.push_back(importRecordIndex);

                                    // Currently "require" is not converted into "import" for ESM
                                    if (structural.mode != config::Mode::kBundle && structural.outputFormat == config::Format::kESModule && !omitWarnings) {
                                        logger::Range r = RangeOfIdentifier(this->source, eCall->target.loc);
                                        this->log.AddID(logger::MsgID::kJS_UnsupportedRequireCall, logger::MsgKind::kWarning, &this->tracker, r, "Converting \"require\" to \"esm\" is currently not supported");
                                    }

                                    // Create a new expression to represent the operation
                                    std::shared_ptr<ERequireString> repl = std::make_shared<ERequireString>();
                                    repl->import_record_index = importRecordIndex;
                                    repl->close_paren_loc = eCall->close_paren_loc;
                                    return Expr{repl, expr.loc};
                                }

                                // Handle glob patterns
                                if (structural.mode == config::Mode::kBundle) {
                                    Expr value = this->handleGlobPattern(arg, compiler::ImportKind::kRequire, compiler::ImportPhase::kEvaluation, "globRequire", nullptr);
                                    if (!IsNil(value.data)) {
                                        return value;
                                    }
                                }

                                // Use a debug log so people can see this if they want to
                                logger::Range r = RangeOfIdentifier(this->source, eCall->target.loc);
                                this->log.AddID(logger::MsgID::kJS_UnsupportedRequireCall, logger::MsgKind::kDebug, &this->tracker, r,
                                    std::string(logger::MsgTemplate(logger::MsgCat::kJS_RequireCallNotBundledNotStringLiteral)));

                                // Otherwise just return a clone of the "require()" call
                                return Expr{std::make_shared<ECall>(ECall{/*target=*/this->valueToSubstituteForRequire(eCall->target.loc), /*args=*/std::vector<Expr>{arg}, /*close_paren_loc=*/eCall->close_paren_loc}), expr.loc};
                            });
                            return {transposed, exprOut{}};
                        } else {
                            // Use a debug log so people can see this if they want to
                            logger::Range r = RangeOfIdentifier(this->source, eCall->target.loc);
                            this->log.AddIDWithNotes(logger::MsgID::kJS_UnsupportedRequireCall, logger::MsgKind::kDebug, &this->tracker, r,
                                "This call to \"require\" will not be bundled because it has " + std::to_string(eCall->args.size()) + " arguments",
                                std::vector<logger::MsgData>{logger::MsgData{nullptr, nullptr, std::string(logger::MsgTemplate(logger::MsgCat::kJS_RequireArgCountNote))}});
                        }

                        return {Expr{std::make_shared<ECall>(ECall{/*target=*/this->valueToSubstituteForRequire(eCall->target.loc), /*args=*/eCall->args, /*close_paren_loc=*/eCall->close_paren_loc}), expr.loc}, exprOut{}};
                    }
                }
            }

            // ---- Require/import call handling ----
            // When bundling, "require('foo')" calls with string literal arguments are
            // converted to internal import records so the bundler can resolve and bundle
            // the dependency. Non-string arguments result in a debug warning since they
            // can't be statically analyzed. When dynamic import is unsupported, "import(x)"
            // is lowered to "Promise.resolve().then(() => __toESM(require(x)))".
            out.childContainsOptionalChain = containsOptionalChain;
            out.callMustBeReplacedWithUndefined = false;
            out.methodCallMustBeReplacedWithUndefined = false;
            if (!in.hasChainParent) {
                out.thisArgFunc = nullptr;
                out.thisArgWrapFunc = nullptr;
            }
            return {expr, out};
        } else if (auto *eNew = Get<ENew>(expr.data); eNew != nullptr) {
            bool hasSpread = false;

            eNew->target = this->visitExpr(eNew->target);
            this->warnAboutImportNamespaceCall(eNew->target, importNamespaceCallKind::exprKindNew);

            for (Expr &arg : eNew->args) {
                arg = this->visitExpr(arg);
                if (Get<ESpread>(arg.data) != nullptr) {
                    hasSpread = true;
                }
            }

            // "new foo(1, ...[2, 3], 4)" => "new foo(1, 2, 3, 4)"
            if (structural.minifySyntax && hasSpread) {
                eNew->args = InlineSpreadsOfArrayLiterals(eNew->args);
            }

            this->maybeMarkKnownGlobalConstructorAsPure(eNew);
        } else if (auto *eArrow = Get<EArrow>(expr.data); eArrow != nullptr) {
            // Check for a propagated name to keep from the parent context
            std::string nameToKeepLocal;
            if (this->nameToKeepIsFor != E{} && IsSameNodeAs(this->nameToKeepIsFor, expr.data)) {
                nameToKeepLocal = this->nameToKeep;
            }

            // Prepare for suspicious logical operator checking
            if (eArrow->prefer_expr && eArrow->args.size() == 1 && IsNil(eArrow->args[0].default_or_nil.data) && eArrow->body.block.stmts.size() == 1) {
                if (Get<BIdentifier>(eArrow->args[0].binding.data) != nullptr) {
                    if (SReturn *stmt = Get<SReturn>(eArrow->body.block.stmts[0].data); stmt != nullptr) {
                        if (EBinary *binary = Get<EBinary>(stmt->value_or_nil.data); binary != nullptr && (binary->op == OpCode::kBinOpLogicalAnd || binary->op == OpCode::kBinOpLogicalOr)) {
                            this->suspiciousLogicalOperatorInsideArrow = stmt->value_or_nil.data;
                        }
                    }
                }
            }

            bool asyncArrowNeedsToBeLowered = eArrow->is_async && compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kAsyncAwait);
            struct fnOrArrowDataVisit oldFnOrArrowData = this->fnOrArrowDataVisit;
            struct fnOrArrowDataVisit visitData;
            visitData.tryBodyCount = 0;
            visitData.tryCatchLoc = logger::Loc{};
            visitData.isArrow = true;
            visitData.isAsync = eArrow->is_async;
            visitData.isGenerator = false;
            visitData.isInsideLoop = false;
            visitData.isInsideSwitch = false;
            visitData.isDerivedClassCtor = false;
            visitData.isOutsideFnOrArrow = false;
            visitData.shouldLowerSuperPropertyAccess = oldFnOrArrowData.shouldLowerSuperPropertyAccess || asyncArrowNeedsToBeLowered;
            this->fnOrArrowDataVisit = visitData;

            // Mark if we're inside an async arrow function. This value should be true
            // even if we're inside multiple arrow functions and the closest inclosing
            // arrow function isn't async, as long as at least one enclosing arrow
            // function within the current enclosing function is async.
            bool oldInsideAsyncArrowFn = this->fnOnlyDataVisit.isInsideAsyncArrowFn;
            if (eArrow->is_async) {
                this->fnOnlyDataVisit.isInsideAsyncArrowFn = true;
            }

            this->pushScopeForVisitPass(ScopeKind::kFunctionArgs, expr.loc);
            this->visitArgs(eArrow->args, visitArgsOpts{/*body=*/eArrow->body.block.stmts, /*decoratorScope=*/nullptr, /*hasRestArg=*/eArrow->has_rest_arg, /*isUniqueFormalParameters=*/true});
            this->pushScopeForVisitPass(ScopeKind::kFunctionBody, eArrow->body.loc);
            eArrow->body.block.stmts = this->visitStmtsAndPrependTempRefs(eArrow->body.block.stmts, prependTempRefsOpts{/*fnBodyLoc=*/nullptr, /*kind=*/stmtsFnBody});
            this->popScope();
            this->lowerFunction(&eArrow->is_async, nullptr, &eArrow->args, eArrow->body.loc, &eArrow->body.block, &eArrow->prefer_expr, &eArrow->has_rest_arg, /*isArrow=*/true);
            this->popScope();

            if (structural.minifySyntax && eArrow->body.block.stmts.size() == 1) {
                if (SReturn *s = Get<SReturn>(eArrow->body.block.stmts[0].data); s != nullptr && !IsNil(s->value_or_nil.data)) {
                    // "() => { return x }" => "() => x"
                    eArrow->prefer_expr = true;
                }
            }

            this->fnOnlyDataVisit.isInsideAsyncArrowFn = oldInsideAsyncArrowFn;
            this->fnOrArrowDataVisit = oldFnOrArrowData;

            // Convert arrow functions to function expressions when lowering
            if (compat::Has(structural.unsupportedJSFeatures, compat::JSFeature::kArrow)) {
                Fn fn;
                fn.args = eArrow->args;
                fn.body = eArrow->body;
                fn.arguments_ref = compiler::kInvalidRef;
                fn.is_async = eArrow->is_async;
                fn.has_rest_arg = eArrow->has_rest_arg;
                expr.data = std::make_shared<EFunction>(EFunction{/*fn=*/fn});
            }

            // Optionally preserve the name
            if (structural.keepNames && !nameToKeepLocal.empty()) {
                expr = this->keepExprSymbolName(expr, nameToKeepLocal);
            }
        } else if (auto *eFn = Get<EFunction>(expr.data); eFn != nullptr) {
            // Check for a propagated name to keep from the parent context
            std::string nameToKeepLocal;
            if (this->nameToKeepIsFor != E{} && IsSameNodeAs(this->nameToKeepIsFor, expr.data)) {
                nameToKeepLocal = this->nameToKeep;
            }

            this->visitFn(&eFn->fn, expr.loc, visitFnOpts{/*isMethod=*/in.isMethod, /*isDerivedClassCtor=*/this->propDerivedCtorValue != nullptr && IsSameNodeAs(*this->propDerivedCtorValue, expr.data), /*isLoweredPrivateMethod=*/in.isLoweredPrivateMethod});
            std::shared_ptr<compiler::LocRef> &name = eFn->fn.name;

            // Remove unused function names when minifying
            if (structural.minifySyntax && !this->currentScope->contains_direct_eval &&
                name != nullptr && this->symbols[name->ref.inner_index].use_count_estimate == 0) {
                eFn->fn.name = nullptr;
            }

            // Optionally preserve the name for functions, but not for methods
            if (structural.keepNames && (!in.isMethod || in.isLoweredPrivateMethod)) {
                if (name != nullptr) {
                    expr = this->keepExprSymbolName(expr, this->symbols[name->ref.inner_index].original_name);
                } else if (!nameToKeepLocal.empty()) {
                    expr = this->keepExprSymbolName(expr, nameToKeepLocal);
                }
            }
        } else if (auto *eClass = Get<EClass>(expr.data); eClass != nullptr) {
            // Check for a propagated name to keep from the parent context
            std::string nameToKeepLocal;
            if (this->nameToKeepIsFor != E{} && IsSameNodeAs(this->nameToKeepIsFor, expr.data)) {
                nameToKeepLocal = this->nameToKeep;
            }

            visitClassResult result = this->visitClass(expr.loc, &eClass->class_, compiler::kInvalidRef, nameToKeepLocal);

            // Lower class field syntax for browsers that don't support it
            std::pair<std::vector<Stmt>, Expr> lowered = this->lowerClass(Stmt{}, expr, result, nameToKeepLocal);
            expr = lowered.second;

            // We may be able to determine that a class is side-effect before lowering
            // but not after lowering (e.g. due to "--keep-names" mutating the object).
            // If that's the case, add a special annotation so this doesn't prevent
            // tree-shaking from happening.
            if (result.canBeRemovedIfUnused) {
                expr.data = std::make_shared<EAnnotation>(EAnnotation{/*value=*/expr, /*flags=*/AnnotationFlags::kCanBeRemovedIfUnused});
            }
        } else {
            // EPrivateIdentifier should have already been handled
            std::abort();
        }

        return {expr, exprOut{}};
    }

} // namespace guchho::javascript
