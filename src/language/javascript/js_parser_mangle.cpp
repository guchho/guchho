#include <string>
#include <utility>
#include <vector>

#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/config.hpp"
#include "guchho/compiler.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/javascript/js_helpers.hpp"


namespace guchho::javascript {

    compiler::Ref Parser::symbolForMangledProp(const std::string &name) {
        compiler::Ref ref;
        auto found = this->mangledProps.find(name);
        if (found == this->mangledProps.end()) {
            ref = this->newSymbol(compiler::SymbolKind::kMangledProp, name);
            this->mangledProps[name] = ref;
        } else {
            ref = found->second;
        }
        if (!this->isControlFlowDead) {
            this->symbols[ref.inner_index].use_count_estimate++;
        }
        return ref;
    }

    std::pair<std::vector<Stmt>, bool> tryToInlineCaseBody(logger::Loc openBraceLoc, std::vector<Stmt> stmts, logger::Loc closeBraceLoc) {
        if (stmts.size() == 1) {
            if (auto *block = Get<SBlock>(stmts[0].data); block != nullptr) {
                return tryToInlineCaseBody(stmts[0].loc, block->stmts, block->close_brace_loc);
            }
        }

        bool caresAboutScope = false;

        size_t i;
        for (i = 0; i < stmts.size(); i++) {
            Stmt &stmt = stmts[i];
            if (Get<SEmpty>(stmt.data) != nullptr || Get<SDirective>(stmt.data) != nullptr ||
                Get<SComment>(stmt.data) != nullptr || Get<SExpr>(stmt.data) != nullptr ||
                Get<SDebugger>(stmt.data) != nullptr || Get<SContinue>(stmt.data) != nullptr ||
                Get<SReturn>(stmt.data) != nullptr || Get<SThrow>(stmt.data) != nullptr) {
                // These can all be inlined outside of the switch without problems
                continue;
            } else if (auto *s = Get<SLocal>(stmt.data); s != nullptr) {
                if (s->kind != LocalKind::kVar) {
                    caresAboutScope = true;
                }
            } else if (auto *sb = Get<SBreak>(stmt.data); sb != nullptr) {
                if (sb->label != nullptr) {
                    // The break label could target this switch, but we don't know whether that's the case or not here
                    return {std::vector<Stmt>{}, false};
                }

                // An unlabeled "break" inside a switch breaks out of the case
                stmts.resize(i);
                break;
            } else {
                // Assume anything else can't be inlined
                return {std::vector<Stmt>{}, false};
            }
        }

        // If we still need a scope, wrap the result in a block
        if (caresAboutScope) {
            auto block = std::make_shared<SBlock>();
            block->stmts = stmts;
            block->close_brace_loc = closeBraceLoc;
            std::vector<Stmt> out;
            out.push_back(Stmt{block, openBraceLoc});
            return {out, true};
        }
        return {stmts, true};
    }



    std::vector<Stmt> Parser::mangleStmts(std::vector<Stmt> stmts, stmtsKind kind) {
        // Remove inlined constants now that we know whether any of these statements
        // contained a direct eval() or not. This can't be done earlier when we
        // encounter the constant because we haven't encountered the eval() yet.
        // Inlined constants are not removed if they are in a top-level scope or
        // if they are exported (which could be in a nested TypeScript namespace).
        if (this->currentScope->parent != nullptr && !this->currentScope->contains_direct_eval) {
            for (int i = 0; i < static_cast<int>(stmts.size()); i++) {
                Stmt stmt = stmts[static_cast<size_t>(i)];
                if (Get<SEmpty>(stmt.data) != nullptr || Get<SComment>(stmt.data) != nullptr ||
                    Get<SDirective>(stmt.data) != nullptr || Get<SDebugger>(stmt.data) != nullptr ||
                    Get<STypeScript>(stmt.data) != nullptr) {
                    continue;
                }

                if (auto s = Get<SLocal>(stmt.data); s != nullptr) {
                    if (!s->is_export) {
                        int end = 0;
                        for (int k = 0; k < static_cast<int>(s->decls.size()); k++) {
                            Decl d = s->decls[static_cast<size_t>(k)];
                            if (auto id = Get<BIdentifier>(d.binding.data); id != nullptr) {
                                if (this->constValues.count(id->ref) > 0 && this->symbols[id->ref.inner_index].use_count_estimate == 0) {
                                    continue;
                                }
                            }
                            s->decls[static_cast<size_t>(end)] = d;
                            end++;
                        }
                        if (end == 0) {
                            stmts[static_cast<size_t>(i)].data = kSEmptyShared;
                        } else {
                            s->decls.resize(static_cast<size_t>(end));
                        }
                    }
                    continue;
                }
                break;
            }
        }

        // Merge adjacent statements during mangling
        std::vector<Stmt> result;
        result.reserve(stmts.size());
        bool dead = false;
        for (int i = 0; i < static_cast<int>(stmts.size()); i++) {
            Stmt stmt = stmts[static_cast<size_t>(i)];
            if (dead && !shouldKeepStmtInDeadControlFlow(stmt)) {
                // Strip unnecessary statements if the control flow is dead here
                continue;
            }

            // Inline single-use variable declarations where possible:
            //
            //   // Before
            //   let x = fn();
            //   return x.y();
            //
            //   // After
            //   return fn().y();
            //
            // The declaration must not be exported. We can't just check for the
            // "export" keyword because something might do "export {id};" later on.
            // Instead we just ignore all top-level declarations for now. That means
            // this optimization currently only applies in nested scopes.
            //
            // Ignore declarations if the scope is shadowed by a direct "eval" call.
            // The eval'd code may indirectly reference this symbol and the actual
            // use count may be greater than 1.
            if (this->currentScope != this->moduleScope && !this->currentScope->contains_direct_eval) {
                // Keep inlining variables until a failure or until there are none left.
                // That handles cases like this:
                //
                //   // Before
                //   let x = fn();
                //   let y = x.prop;
                //   return y;
                //
                //   // After
                //   return fn().prop;
                //
                while (!result.empty()) {
                    // Ignore "var" declarations since those have function-level scope and
                    // we may not have visited all of their uses yet by this point. We
                    // should have visited all the uses of "let" and "const" declarations
                    // by now since they are scoped to this block which we just finished
                    // visiting.
                    auto prevS = Get<SLocal>(result[result.size() - 1].data);
                    if (prevS != nullptr && (prevS->kind == LocalKind::kLet || prevS->kind == LocalKind::kConst)) {

                        Decl& last = prevS->decls[prevS->decls.size() - 1];

                        // The binding must be an identifier that is only used once.
                        // Ignore destructuring bindings since that's not the simple case.
                        // Destructuring bindings could potentially execute side-effecting
                        // code which would invalidate reordering.
                        if (auto id = Get<BIdentifier>(last.binding.data); id != nullptr) {
                            // Don't do this if "__name" was called on this symbol. In that
                            // case there is actually more than one use even though it says
                            // there is only one. The "__name" use isn't counted so that
                            // tree shaking still works when names are kept.
                            compiler::Symbol& symbol = this->symbols[id->ref.inner_index];
                            if (symbol.use_count_estimate == 1 && !compiler::Has(symbol.flags, compiler::SymbolFlags::kDidKeepName)) {
                                Expr replacement = last.value_or_nil;

                                // The variable must be initialized, since we will be substituting
                                // the value into the usage.
                                if (IsNil(replacement.data)) {
                                    replacement = Expr{kEUndefinedShared, last.binding.loc};
                                }

                                // Try to substitute the identifier with the initializer. This will
                                // fail if something with side effects is in between the declaration
                                // and the usage.
                                if (this->substituteSingleUseSymbolInStmt(stmt, id->ref, replacement)) {
                                    // Remove the previous declaration, since the substitution was
                                    // successful.
                                    if (prevS->decls.size() == 1) {
                                        result.resize(result.size() - 1);
                                    } else {
                                        prevS->decls.resize(prevS->decls.size() - 1);
                                    }

                                    // Loop back to try again
                                    continue;
                                }
                            }
                        }
                    }

                    // Substitution failed so stop trying
                    break;
                }
            }

            if (Get<SEmpty>(stmt.data) != nullptr) {
                // Strip empty statements
                continue;
            }

            if (auto s = Get<SLocal>(stmt.data); s != nullptr) {
                // Merge adjacent local statements
                if (!result.empty()) {
                    Stmt prevStmt = result[result.size() - 1];
                    if (auto prevS = Get<SLocal>(prevStmt.data); prevS != nullptr && s->kind == prevS->kind && s->is_export == prevS->is_export) {
                        prevS->decls.insert(prevS->decls.end(), s->decls.begin(), s->decls.end());
                        continue;
                    }
                }
            } else if (auto sExpr = Get<SExpr>(stmt.data); sExpr != nullptr) {
                // Trim expressions without side effects
                sExpr->value = this->astHelpers.SimplifyUnusedExpr(sExpr->value, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures);
                if (IsNil(sExpr->value.data)) {
                    continue;
                }

                // Merge adjacent expression statements
                if (!result.empty()) {
                    Stmt prevStmt = result[result.size() - 1];
                    if (auto prevS = Get<SExpr>(prevStmt.data); prevS != nullptr) {
                        if (!sExpr->is_from_class_or_fn_that_can_be_removed_if_unused) {
                            prevS->is_from_class_or_fn_that_can_be_removed_if_unused = false;
                        }
                        prevS->value = JoinWithComma(prevS->value, sExpr->value);
                        continue;
                    }
                }
            } else if (auto sSwitch = Get<SSwitch>(stmt.data); sSwitch != nullptr) {
                // Absorb a previous expression statement
                if (!result.empty()) {
                    Stmt prevStmt = result[result.size() - 1];
                    if (auto prevS = Get<SExpr>(prevStmt.data); prevS != nullptr) {
                        sSwitch->test = JoinWithComma(prevS->value, sSwitch->test);
                        result.resize(result.size() - 1);
                    }
                }
            } else if (auto sIf = Get<SIf>(stmt.data); sIf != nullptr) {
                // Absorb a previous expression statement
                if (!result.empty()) {
                    Stmt prevStmt = result[result.size() - 1];
                    if (auto prevS = Get<SExpr>(prevStmt.data); prevS != nullptr) {
                        sIf->test = JoinWithComma(prevS->value, sIf->test);
                        result.resize(result.size() - 1);
                    }
                }

                if (isJumpStatement(&sIf->yes.data)) {
                    bool optimizeImplicitJump = false;

                    // Absorb a previous if statement
                    if (!result.empty()) {
                        Stmt prevStmt = result[result.size() - 1];
                        if (auto prevS = Get<SIf>(prevStmt.data); prevS != nullptr && prevS->no_or_nil == nullptr && jumpStmtsLookTheSame(&prevS->yes.data, &sIf->yes.data)) {
                            // "if (a) break c; if (b) break c;" => "if (a || b) break c;"
                            // "if (a) continue c; if (b) continue c;" => "if (a || b) continue c;"
                            // "if (a) return c; if (b) return c;" => "if (a || b) return c;"
                            // "if (a) throw c; if (b) throw c;" => "if (a || b) throw c;"
                            sIf->test = JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, prevS->test, sIf->test);
                            result.resize(result.size() - 1);
                        }
                    }

                    // "while (x) { if (y) continue; z(); }" => "while (x) { if (!y) z(); }"
                    // "while (x) { if (y) continue; else z(); w(); }" => "while (x) { if (!y) { z(); w(); } }" => "for (; x;) !y && (z(), w());"
                    if (kind == stmtsLoopBody) {
                        if (auto continueS = Get<SContinue>(sIf->yes.data); continueS != nullptr && continueS->label == nullptr) {
                            optimizeImplicitJump = true;
                        }
                    }

                    // "let x = () => { if (y) return; z(); };" => "let x = () => { if (!y) z(); };"
                    // "let x = () => { if (y) return; else z(); w(); };" => "let x = () => { if (!y) { z(); w(); } };" => "let x = () => { !y && (z(), w()); };"
                    if (kind == stmtsFnBody) {
                        if (auto returnS = Get<SReturn>(sIf->yes.data); returnS != nullptr && IsNil(returnS->value_or_nil.data)) {
                            optimizeImplicitJump = true;
                        }
                    }

                    if (optimizeImplicitJump) {
                        std::vector<Stmt> body;
                        if (sIf->no_or_nil != nullptr) {
                            body.push_back(*sIf->no_or_nil);
                        }
                        body.insert(body.end(), stmts.begin() + i + 1, stmts.end());

                        // Don't do this transformation if the branch condition could
                        // potentially access symbols declared later on this scope below.
                        // If so, inverting the branch condition and nesting statements after
                        // this in a block would break that access which is a behavior change.
                        //
                        //   // This transformation is incorrect
                        //   if (a()) return; function a() {}
                        //   if (!a()) { function a() {} }
                        //
                        //   // This transformation is incorrect
                        //   if (a(() => b)) return; let b;
                        //   if (a(() => b)) { let b; }
                        //
                        if (!stmtsCareAboutScope(body)) {
                            body = this->mangleStmts(body, kind);
                            logger::Loc bodyLoc = sIf->yes.loc;
                            if (!body.empty()) {
                                bodyLoc = body[0].loc;
                            }
                            SIf* ifS = new SIf();
                            ifS->test = this->astHelpers.SimplifyBooleanExpr(Not(sIf->test));
                            ifS->yes = stmtsToSingleStmt(bodyLoc, body, logger::Loc{});
                            return this->mangleIf(result, stmt.loc, std::shared_ptr<SIf>(ifS));
                        }
                    }

                    if (sIf->no_or_nil != nullptr) {
                        // "if (a) return b; else if (c) return d; else return e;" => "if (a) return b; if (c) return d; return e;"
                        for (;;) {
                            result.push_back(stmt);
                            stmt = *sIf->no_or_nil;
                            sIf->no_or_nil = nullptr;
                            sIf = Get<SIf>(stmt.data);
                            if (sIf == nullptr || !isJumpStatement(&sIf->yes.data) || sIf->no_or_nil == nullptr) {
                                break;
                            }
                        }
                        result = appendIfOrLabelBodyPreservingScope(result, stmt);
                        if (isJumpStatement(&stmt.data)) {
                            dead = true;
                        }
                        continue;
                    }
                }
            } else if (auto sReturn = Get<SReturn>(stmt.data); sReturn != nullptr) {
                // Merge return statements with the previous expression statement
                if (!result.empty() && !IsNil(sReturn->value_or_nil.data)) {
                    Stmt prevStmt = result[result.size() - 1];
                    if (auto prevS = Get<SExpr>(prevStmt.data); prevS != nullptr) {
                        result[result.size() - 1] = Stmt{std::make_shared<SReturn>(SReturn{JoinWithComma(prevS->value, sReturn->value_or_nil)}), prevStmt.loc};
                        continue;
                    }
                }

                dead = true;
            } else if (auto sThrow = Get<SThrow>(stmt.data); sThrow != nullptr) {
                // Merge throw statements with the previous expression statement
                if (!result.empty()) {
                    Stmt prevStmt = result[result.size() - 1];
                    if (auto prevS = Get<SExpr>(prevStmt.data); prevS != nullptr) {
                        result[result.size() - 1] = Stmt{std::make_shared<SThrow>(SThrow{JoinWithComma(prevS->value, sThrow->value)}), prevStmt.loc};
                        continue;
                    }
                }

                dead = true;
            } else if (Get<SBreak>(stmt.data) != nullptr || Get<SContinue>(stmt.data) != nullptr) {
                dead = true;
            } else if (auto sFor = Get<SFor>(stmt.data); sFor != nullptr) {
                if (!result.empty()) {
                    Stmt prevStmt = result[result.size() - 1];
                    if (auto prevS = Get<SExpr>(prevStmt.data); prevS != nullptr) {
                        // Insert the previous expression into the for loop initializer
                        if (sFor->init_or_nil == nullptr) {
                            result[result.size() - 1] = stmt;
                            sFor->init_or_nil = std::make_shared<Stmt>(Stmt{std::make_shared<SExpr>(SExpr{prevS->value}), prevStmt.loc});
                            continue;
                        } else if (auto s2 = Get<SExpr>(sFor->init_or_nil->data); s2 != nullptr) {
                            result[result.size() - 1] = stmt;
                            sFor->init_or_nil = std::make_shared<Stmt>(Stmt{std::make_shared<SExpr>(SExpr{JoinWithComma(prevS->value, s2->value)}), prevStmt.loc});
                            continue;
                        }
                    } else {
                        // Insert the previous variable declaration into the for loop
                        // initializer if it's a "var" declaration, since the scope
                        // doesn't matter due to scope hoisting
                        if (sFor->init_or_nil == nullptr) {
                            if (auto s2 = Get<SLocal>(prevStmt.data); s2 != nullptr && s2->kind == LocalKind::kVar && !s2->is_export) {
                                result[result.size() - 1] = stmt;
                                sFor->init_or_nil = std::make_shared<Stmt>(prevStmt);
                                continue;
                            }
                        } else {
                            if (auto s2 = Get<SLocal>(prevStmt.data); s2 != nullptr && s2->kind == LocalKind::kVar && !s2->is_export) {
                                if (auto s3 = Get<SLocal>(sFor->init_or_nil->data); s3 != nullptr && s3->kind == LocalKind::kVar) {
                                    result[result.size() - 1] = stmt;
                                    std::shared_ptr<SLocal> local = std::make_shared<SLocal>();
                                    local->kind = LocalKind::kVar;
                                    local->decls = s2->decls;
                                    local->decls.insert(local->decls.end(), s3->decls.begin(), s3->decls.end());
                                    sFor->init_or_nil->data = local;
                                    continue;
                                }
                            }
                        }
                    }
                }
            } else if (auto sTry = Get<STry>(stmt.data); sTry != nullptr) {
                // Drop an unused identifier binding if the optional catch binding feature is supported
                if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kOptionalCatchBinding) && sTry->catch_block != nullptr) {
                    if (auto id = Get<BIdentifier>(sTry->catch_block->binding_or_nil.data); id != nullptr) {
                        compiler::Symbol& symbol = this->symbols[id->ref.inner_index];
                        if (symbol.use_count_estimate == 0) {
                            if (symbol.link != compiler::kInvalidRef) {
                                // We cannot transform "try { x() } catch (y) { var y = 1 }" into
                                // "try { x() } catch { var y = 1 }" even though "y" is never used
                                // because the hoisted variable "y" would have different values
                                // after the statement ends due to a strange JavaScript quirk:
                                //
                                //   try { x() } catch (y) { var y = 1 }
                                //   console.log(y) // undefined
                                //
                                //   try { x() } catch { var y = 1 }
                                //   console.log(y) // 1
                                //
                            } else if (this->currentScope->contains_direct_eval) {
                                // We cannot transform "try { x() } catch (y) { eval('z = y') }"
                                // into "try { x() } catch { eval('z = y') }" because the variable
                                // "y" is actually still used.
                            } else {
                                // "try { x() } catch (y) {}" => "try { x() } catch {}"
                                sTry->catch_block->binding_or_nil.data = std::shared_ptr<BIdentifier>();
                            }
                        }
                    }
                }
            }

            result.push_back(stmt);
        }

        // Drop a trailing unconditional jump statement if applicable
        if (!result.empty()) {
            switch (kind) {
            case stmtsLoopBody:
                // "while (x) { y(); continue; }" => "while (x) { y(); }"
                if (auto continueS = Get<SContinue>(result[result.size() - 1].data); continueS != nullptr && continueS->label == nullptr) {
                    result.resize(result.size() - 1);
                }
                break;

            case stmtsFnBody:
                if (auto returnS = Get<SReturn>(result[result.size() - 1].data); returnS != nullptr) {
                    if (IsNil(returnS->value_or_nil.data)) {
                        // "function f() { x(); return; }" => "function f() { x(); }"
                        result.resize(result.size() - 1);
                    } else if (auto unary = Get<EUnary>(returnS->value_or_nil.data); unary != nullptr && unary->op == OpCode::kUnOpVoid) {
                        // "function f() { return void x(); }" => "function f() { x(); }"
                        result[result.size() - 1].data = std::make_shared<SExpr>(SExpr{unary->value});
                    }
                }
                break;

            default:
                break;
            }
        }

        // Merge certain statements in reverse order
        if (result.size() >= 2) {
            Stmt lastStmt = result[result.size() - 1];

            if (auto lastReturn = Get<SReturn>(lastStmt.data); lastReturn != nullptr) {
                // "if (a) return b; if (c) return d; return e;" => "return a ? b : c ? d : e;"
                while (result.size() >= 2) {
                    int prevIndex = static_cast<int>(result.size()) - 2;
                    Stmt prevStmt = result[static_cast<size_t>(prevIndex)];

                    if (auto prevS = Get<SExpr>(prevStmt.data); prevS != nullptr) {
                        // This return statement must have a value
                        if (IsNil(lastReturn->value_or_nil.data)) {
                            break;
                        }

                        // "a(); return b;" => "return a(), b;"
                        SReturn* newReturn = new SReturn();
                        newReturn->value_or_nil = JoinWithComma(prevS->value, lastReturn->value_or_nil);
                        lastReturn = newReturn;

                        // Merge the last two statements
                        lastStmt = Stmt{std::shared_ptr<SReturn>(lastReturn), prevStmt.loc};
                        result[static_cast<size_t>(prevIndex)] = lastStmt;
                        result.resize(result.size() - 1);
                        continue;
                    }

                    SIf* prevS = Get<SIf>(prevStmt.data);
                    if (prevS == nullptr) {
                        break;
                    }

                    // The previous statement must be an if statement with no else clause
                    if (prevS->no_or_nil != nullptr) {
                        break;
                    }

                    // The then clause must be a return
                    auto prevReturn = Get<SReturn>(prevS->yes.data);
                    if (prevReturn == nullptr) {
                        break;
                    }

                    // Handle some or all of the values being undefined
                    Expr left = prevReturn->value_or_nil;
                    Expr right = lastReturn->value_or_nil;
                    if (IsNil(left.data)) {
                        // "if (a) return; return b;" => "return a ? void 0 : b;"
                        left = Expr{kEUndefinedShared, prevS->yes.loc};
                    }
                    if (IsNil(right.data)) {
                        // "if (a) return a; return;" => "return a ? b : void 0;"
                        right = Expr{kEUndefinedShared, lastStmt.loc};
                    }

                    // "if (!a) return b; return c;" => "return a ? c : b;"
                    if (auto not_ = Get<EUnary>(prevS->test.data); not_ != nullptr && not_->op == OpCode::kUnOpNot) {
                        Expr value = not_->value;
                        prevS->test = value;
                        Expr tmp = left;
                        left = right;
                        right = tmp;
                    }

                    SReturn* newReturn = new SReturn();
                    if (auto comma = Get<EBinary>(prevS->test.data); comma != nullptr && comma->op == OpCode::kBinOpComma) {
                        // "if (a, b) return c; return d;" => "return a, b ? c : d;"
                        newReturn->value_or_nil = JoinWithComma(comma->left,
                            this->astHelpers.MangleIfExpr(comma->right.loc, EIf{comma->right, left, right}, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures));
                    } else {
                        // "if (a) return b; return c;" => "return a ? b : c;"
                        newReturn->value_or_nil = this->astHelpers.MangleIfExpr(
                            prevS->test.loc, EIf{prevS->test, left, right}, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures);
                    }
                    lastReturn = newReturn;

                    // Merge the last two statements
                    lastStmt = Stmt{std::shared_ptr<SReturn>(lastReturn), prevStmt.loc};
                    result[static_cast<size_t>(prevIndex)] = lastStmt;
                    result.resize(result.size() - 1);
                }
            } else if (auto lastThrow = Get<SThrow>(lastStmt.data); lastThrow != nullptr) {
                // "if (a) throw b; if (c) throw d; throw e;" => "throw a ? b : c ? d : e;"
                while (result.size() >= 2) {
                    int prevIndex = static_cast<int>(result.size()) - 2;
                    Stmt prevStmt = result[static_cast<size_t>(prevIndex)];

                    if (auto prevS = Get<SExpr>(prevStmt.data); prevS != nullptr) {
                        // "a(); throw b;" => "throw a(), b;"
                        SThrow* newThrow = new SThrow();
                        newThrow->value = JoinWithComma(prevS->value, lastThrow->value);
                        lastThrow = newThrow;

                        // Merge the last two statements
                        lastStmt = Stmt{std::shared_ptr<SThrow>(lastThrow), prevStmt.loc};
                        result[static_cast<size_t>(prevIndex)] = lastStmt;
                        result.resize(result.size() - 1);
                        continue;
                    }

                    SIf* prevS = Get<SIf>(prevStmt.data);
                    if (prevS == nullptr) {
                        break;
                    }

                    // The previous statement must be an if statement with no else clause
                    if (prevS->no_or_nil != nullptr) {
                        break;
                    }

                    // The then clause must be a throw
                    auto prevThrow = Get<SThrow>(prevS->yes.data);
                    if (prevThrow == nullptr) {
                        break;
                    }

                    Expr left = prevThrow->value;
                    Expr right = lastThrow->value;

                    // "if (!a) throw b; throw c;" => "throw a ? c : b;"
                    if (auto not_ = Get<EUnary>(prevS->test.data); not_ != nullptr && not_->op == OpCode::kUnOpNot) {
                        Expr value = not_->value;
                        prevS->test = value;
                        Expr tmp = left;
                        left = right;
                        right = tmp;
                    }

                    SThrow* newThrow = new SThrow();
                    if (auto comma = Get<EBinary>(prevS->test.data); comma != nullptr && comma->op == OpCode::kBinOpComma) {
                        // "if (a, b) throw c; throw d;" => "throw a, b ? c : d;"
                        newThrow->value = JoinWithComma(comma->left,
                            this->astHelpers.MangleIfExpr(comma->right.loc, EIf{comma->right, left, right}, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures));
                    } else {
                        // "if (a) throw b; throw c;" => "throw a ? b : c;"
                        newThrow->value = this->astHelpers.MangleIfExpr(
                            prevS->test.loc, EIf{prevS->test, left, right}, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures);
                    }
                    lastThrow = newThrow;

                    // Merge the last two statements
                    lastStmt = Stmt{std::shared_ptr<SThrow>(lastThrow), prevStmt.loc};
                    result[static_cast<size_t>(prevIndex)] = lastStmt;
                    result.resize(result.size() - 1);
                }
            }
        }

        return result;
    }


    std::vector<Stmt> Parser::mangleIf(std::vector<Stmt> stmts, logger::Loc loc, std::shared_ptr<SIf> s) {
        // Constant folding using the test expression
        if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(s->test.data)) {
            if (boolean->first) {
                // The test is truthy
                if (s->no_or_nil == nullptr || !shouldKeepStmtInDeadControlFlow(*s->no_or_nil)) {
                    // We can drop the "no" branch
                    if (boolean->second == SideEffects::kCouldHaveSideEffects) {
                        // Keep the condition if it could have side effects (but is still known to be truthy)
                        Expr test = this->astHelpers.SimplifyUnusedExpr(s->test, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures);
                        if (!IsNil(test.data)) {
                            stmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{test}), s->test.loc});
                        }
                    }
                    return appendIfOrLabelBodyPreservingScope(stmts, s->yes);
                } else {
                    // We have to keep the "no" branch
                }
            } else {
                // The test is falsy
                if (!shouldKeepStmtInDeadControlFlow(s->yes)) {
                    // We can drop the "yes" branch
                    if (boolean->second == SideEffects::kCouldHaveSideEffects) {
                        // Keep the condition if it could have side effects (but is still known to be falsy)
                        Expr test = this->astHelpers.SimplifyUnusedExpr(s->test, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures);
                        if (!IsNil(test.data)) {
                            stmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{test}), s->test.loc});
                        }
                    }
                    if (s->no_or_nil == nullptr) {
                        return stmts;
                    }
                    return appendIfOrLabelBodyPreservingScope(stmts, *s->no_or_nil);
                } else {
                    // We have to keep the "yes" branch
                }
            }

            // Use "1" and "0" instead of "true" and "false" to be shorter
            if (boolean->second == SideEffects::kNoSideEffects) {
                s->test.data = std::make_shared<ENumber>(ENumber{boolean->first ? 1.0 : 0.0});
            }
        }

        Expr expr;

        if (auto yes = Get<SExpr>(s->yes.data); yes != nullptr) {
            // "yes" is an expression
            if (s->no_or_nil == nullptr) {
                if (auto not_ = Get<EUnary>(s->test.data); not_ != nullptr && not_->op == OpCode::kUnOpNot) {
                    // "if (!a) b();" => "a || b();"
                    expr = JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, not_->value, yes->value);
                } else {
                    // "if (a) b();" => "a && b();"
                    expr = JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, s->test, yes->value);
                }
            } else if (auto no = Get<SExpr>(s->no_or_nil->data); no != nullptr) {
                // "if (a) b(); else c();" => "a ? b() : c();"
                expr = this->astHelpers.MangleIfExpr(loc, EIf{s->test, yes->value, no->value}, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures);
            }
        } else if (Get<SEmpty>(s->yes.data) != nullptr) {
            // "yes" is missing
            if (s->no_or_nil == nullptr) {
                // "yes" and "no" are both missing
                if (this->astHelpers.ExprCanBeRemovedIfUnused(s->test)) {
                    // "if (1) {}" => ""
                    return stmts;
                } else {
                    // "if (a) {}" => "a;"
                    expr = s->test;
                }
            } else if (auto no = Get<SExpr>(s->no_or_nil->data); no != nullptr) {
                if (auto not_ = Get<EUnary>(s->test.data); not_ != nullptr && not_->op == OpCode::kUnOpNot) {
                    // "if (!a) {} else b();" => "a && b();"
                    expr = JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, not_->value, no->value);
                } else {
                    // "if (a) {} else b();" => "a || b();"
                    expr = JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, s->test, no->value);
                }
            } else {
                // "yes" is missing and "no" is not missing (and is not an expression)
                if (auto not_ = Get<EUnary>(s->test.data); not_ != nullptr && not_->op == OpCode::kUnOpNot) {
                    // "if (!a) {} else throw b;" => "if (a) throw b;"
                    Expr newTest = not_->value;
                    s->test = newTest;
                    s->yes = *s->no_or_nil;
                    s->no_or_nil = nullptr;
                } else {
                    // "if (a) {} else throw b;" => "if (!a) throw b;"
                    s->test = Not(s->test);
                    s->yes = *s->no_or_nil;
                    s->no_or_nil = nullptr;
                }
            }
        } else {
            // "yes" is not missing (and is not an expression)
            if (s->no_or_nil != nullptr) {
                // "yes" is not missing (and is not an expression) and "no" is not missing
                if (auto not_ = Get<EUnary>(s->test.data); not_ != nullptr && not_->op == OpCode::kUnOpNot) {
                    // "if (!a) return b; else return c;" => "if (a) return c; else return b;"
                    Expr value = not_->value;
                    s->test = value;
                    Stmt temp = s->yes;
                    s->yes = *s->no_or_nil;
                    s->no_or_nil = std::make_shared<Stmt>(temp);
                }
            } else {
                // "no" is missing
                if (auto s2 = Get<SIf>(s->yes.data); s2 != nullptr && s2->no_or_nil == nullptr) {
                    // "if (a) if (b) return c;" => "if (a && b) return c;"
                    s->test = JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, s->test, s2->test);
                    Stmt newYes = s2->yes;
                    s->yes = newYes;
                }
            }
        }

        // Return an expression if we replaced the if statement with an expression above
        if (!IsNil(expr.data)) {
            expr = this->astHelpers.SimplifyUnusedExpr(expr, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures);
            stmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{expr}), loc});
            return stmts;
        }

        stmts.push_back(Stmt{std::move(s), loc});
        return stmts;
    }

    std::vector<Stmt> Parser::minifySwitchStmt(logger::Loc loc, std::shared_ptr<SSwitch> s, std::vector<Stmt> stmts) {
        // Trim empty cases before a trailing default clause
        if (s->cases.size() > 0) {
            if (size_t i = s->cases.size() - 1; IsNil(s->cases[i].value_or_nil.data)) {
                // "switch (x) { case 0: default: y() }" => "switch (x) { default: y() }"
                while (i > 0 && s->cases[i - 1].body.empty() && IsPrimitiveLiteral(s->cases[i - 1].value_or_nil.data)) {
                    i--;
                }
                s->cases.erase(s->cases.begin() + static_cast<std::ptrdiff_t>(i), s->cases.end() - 1);
            }
        }

        // Attempt to partially-evaluate statically-determined switch statements
        if (IsPrimitiveLiteral(s->test.data)) {
            bool allCasesArePrimitives = true;
            ptrdiff_t defaultIndex = -1;

            // Pass 1: Check for primitives and find the "default" case
            for (size_t i = 0; i < s->cases.size(); i++) {
                Case c = s->cases[i];
                if (IsNil(c.value_or_nil.data)) {
                    defaultIndex = static_cast<ptrdiff_t>(i);
                } else if (!IsPrimitiveLiteral(c.value_or_nil.data)) {
                    allCasesArePrimitives = false;
                }
            }

            // To simplify analysis, only continue when all cases are primitives
            if (allCasesArePrimitives) {
                ptrdiff_t takenIndex = -1;

                // Find the case that compares equal and will be taken
                for (size_t i = 0; i < s->cases.size(); i++) {
                    Case c = s->cases[i];
                    if (std::optional<bool> isEqualToTest = CheckEqualityIfNoSideEffects(s->test.data, c.value_or_nil.data, EqualityKind::kStrictEquality); isEqualToTest && *isEqualToTest) {
                        takenIndex = static_cast<ptrdiff_t>(i);
                        break;
                    }
                }
                if (takenIndex == -1) {
                    takenIndex = defaultIndex;
                }

                // Partially evaluate the cases
                if (takenIndex != -1) {
                    bool isFallThrough = false;
                    ptrdiff_t liveIndex = -1;
                    size_t end = 0;
                    for (size_t i = 0; i < s->cases.size(); i++) {
                        Case c = s->cases[i];
                        bool isTaken = static_cast<ptrdiff_t>(i) == takenIndex;
                        if (isTaken) {
                            liveIndex = static_cast<ptrdiff_t>(end);
                        }
                        if (isFallThrough) {
                            Case& live = s->cases[static_cast<size_t>(liveIndex)];
                            for (Stmt cs : c.body) {
                                live.body.push_back(cs);
                            }
                        } else if (isTaken || !c.body.empty()) {
                            s->cases[end] = c;
                            end++;
                        }
                        if (isTaken || isFallThrough) {
                            isFallThrough = caseBodyCouldHaveFallThrough(c.body);
                        }
                    }
                    s->cases.resize(end);
                }
            }
        }

        // Handle empty switch statements
        if (s->cases.empty()) {
            if (this->astHelpers.ExprCanBeRemovedIfUnused(s->test)) {
                // Remove everything
                return stmts;
            } else {
                // Just keep the test expression
                stmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{s->test}), s->test.loc});
                return stmts;
            }
        }

        // Handle a switch statement containing only a "default" clause
        if (s->cases.size() == 1) {
            Case c = s->cases[0];
            if (IsNil(c.value_or_nil.data) && this->astHelpers.ExprCanBeRemovedIfUnused(s->test)) {
                std::pair<std::vector<Stmt>, bool> inlined = tryToInlineCaseBody(s->body_loc, c.body, s->close_brace_loc);
                if (inlined.second) {
                    for (Stmt bodyStmt : inlined.first) {
                        stmts.push_back(bodyStmt);
                    }
                    return stmts;
                }
            }
        }

        // Try to turn this into an if-else statement
        Case yesCase;
        Case noCase;
        if (s->cases.size() == 1) {
            Case yes = s->cases[0];
            if (!IsNil(yes.value_or_nil.data)) {
                yesCase = yes;
            }
        } else if (s->cases.size() == 2) {
            // "switch (x) { case y: a(); break; default: b() }"
            Case yes = s->cases[0];
            if (!IsNil(yes.value_or_nil.data) && !caseBodyCouldHaveFallThrough(yes.body)) {
                Case no = s->cases[1];
                if (IsNil(no.value_or_nil.data)) {
                    yesCase = yes;
                    noCase = no;
                }
            }

            // "switch (x) { default: a(); break; case y: b() }"
            Case no = s->cases[0];
            if (IsNil(no.value_or_nil.data) && !caseBodyCouldHaveFallThrough(no.body)) {
                Case yes2 = s->cases[1];
                if (!IsNil(yes2.value_or_nil.data)) {
                    yesCase = yes2;
                    noCase = no;
                }
            }
        }
        if (!IsNil(yesCase.value_or_nil.data)) {
            std::pair<std::vector<Stmt>, bool> yesInline = tryToInlineCaseBody(s->body_loc, yesCase.body, s->close_brace_loc);
            if (yesInline.second) {
                std::pair<std::vector<Stmt>, bool> noInline = tryToInlineCaseBody(s->body_loc, noCase.body, s->close_brace_loc);
                if (noInline.second) {
                    SIf* ifElse = new SIf();
                    ifElse->test = Expr{E{}, s->test.loc};
                    ifElse->yes = stmtsToSingleStmt(yesCase.loc, yesInline.first, logger::Loc{});
                    if (std::optional<bool> isEqualToTest = CheckEqualityIfNoSideEffects(s->test.data, yesCase.value_or_nil.data, EqualityKind::kStrictEquality); isEqualToTest) {
                        ifElse->test.data = std::make_shared<EBoolean>(EBoolean{*isEqualToTest});
                    } else {
                        ifElse->test.data = std::make_shared<EBinary>(EBinary{s->test, yesCase.value_or_nil, OpCode::kBinOpStrictEq});
                    }
                    if (!noInline.first.empty()) {
                        ifElse->no_or_nil = std::make_shared<Stmt>(stmtsToSingleStmt(noCase.loc, noInline.first, logger::Loc{}));
                    }
                    return this->mangleIf(stmts, loc, std::shared_ptr<SIf>(ifElse));
                }
            }
        }

        stmts.push_back(Stmt{std::move(s), loc});
        return stmts;
    }

    bool Parser::substituteSingleUseSymbolInStmt(Stmt stmt, compiler::Ref ref, Expr replacement) {
        Expr* expr = nullptr;

        S& s = stmt.data;
        if (auto se = Get<SExpr>(s); se != nullptr) {
            expr = &se->value;
        } else if (auto st = Get<SThrow>(s); st != nullptr) {
            expr = &st->value;
        } else if (auto sr = Get<SReturn>(s); sr != nullptr) {
            expr = &sr->value_or_nil;
        } else if (auto si = Get<SIf>(s); si != nullptr) {
            expr = &si->test;
        } else if (auto ss = Get<SSwitch>(s); ss != nullptr) {
            expr = &ss->test;
        } else if (auto sl = Get<SLocal>(s); sl != nullptr) {
            // Only try substituting into the initializer for the first declaration
            Decl& first = sl->decls[0];
            if (!IsNil(first.value_or_nil.data)) {
                // Make sure there isn't destructuring, which could evaluate code
                if (Get<BIdentifier>(first.binding.data) != nullptr) {
                    expr = &first.value_or_nil;
                }
            }
        }

        if (expr != nullptr) {
            // Only continue trying to insert this replacement into sub-expressions
            // after the first one if the replacement has no side effects:
            //
            //   // Substitution is ok
            //   let replacement = 123;
            //   return x + replacement;
            //
            //   // Substitution is not ok because "fn()" may change "x"
            //   let replacement = fn();
            //   return x + replacement;
            //
            //   // Substitution is not ok because "x == x" may change "x" due to "valueOf()" evaluation
            //   let replacement = [x];
            //   return (x == x) + replacement;
            //
            bool replacementCanBeRemoved = this->astHelpers.ExprCanBeRemovedIfUnused(replacement);

            std::pair<Expr, substituteStatus> newExpr = this->substituteSingleUseSymbolInExpr(*expr, ref, replacement, replacementCanBeRemoved);
            if (newExpr.second == substituteSuccess) {
                *expr = newExpr.first;
                return true;
            }
        }

        return false;
    }

    std::pair<Expr, substituteStatus> Parser::substituteSingleUseSymbolInExpr(
        Expr expr,
        compiler::Ref ref,
        Expr replacement,
        bool replacementCanBeRemoved) {

        if (auto id = Get<EIdentifier>(expr.data); id != nullptr) {
            if (id->ref == ref) {
                this->ignoreUsage(ref);
                return {replacement, substituteSuccess};
            }

        } else if (auto spread = Get<ESpread>(expr.data); spread != nullptr) {
            std::pair<Expr, substituteStatus> result = this->substituteSingleUseSymbolInExpr(spread->value, ref, replacement, replacementCanBeRemoved);
            if (result.second != substituteContinue) {
                spread->value = result.first;
                return {expr, result.second};
            }

        } else if (auto awaitExpr = Get<EAwait>(expr.data); awaitExpr != nullptr) {
            std::pair<Expr, substituteStatus> result = this->substituteSingleUseSymbolInExpr(awaitExpr->value, ref, replacement, replacementCanBeRemoved);
            if (result.second != substituteContinue) {
                awaitExpr->value = result.first;
                return {expr, result.second};
            }

        } else if (auto yieldExpr = Get<EYield>(expr.data); yieldExpr != nullptr) {
            if (!IsNil(yieldExpr->value_or_nil.data)) {
                std::pair<Expr, substituteStatus> result = this->substituteSingleUseSymbolInExpr(yieldExpr->value_or_nil, ref, replacement, replacementCanBeRemoved);
                if (result.second != substituteContinue) {
                    yieldExpr->value_or_nil = result.first;
                    return {expr, result.second};
                }
            }

        } else if (auto importCall = Get<EImportCall>(expr.data); importCall != nullptr) {
            std::pair<Expr, substituteStatus> result = this->substituteSingleUseSymbolInExpr(importCall->expr, ref, replacement, replacementCanBeRemoved);
            if (result.second != substituteContinue) {
                importCall->expr = result.first;
                return {expr, result.second};
            }

            // The "import()" expression has side effects but the side effects are
            // always asynchronous so there is no way for the side effects to modify
            // the replacement value. So it's ok to reorder the replacement value
            // past the "import()" expression assuming everything else checks out.
            if (replacementCanBeRemoved && this->astHelpers.ExprCanBeRemovedIfUnused(importCall->expr)) {
                return {expr, substituteContinue};
            }

        } else if (auto unary = Get<EUnary>(expr.data); unary != nullptr) {
            switch (unary->op) {
            case OpCode::kUnOpPreInc:
            case OpCode::kUnOpPostInc:
            case OpCode::kUnOpPreDec:
            case OpCode::kUnOpPostDec:
            case OpCode::kUnOpDelete:
                // Do not substitute into an assignment position
                break;

            default:
                std::pair<Expr, substituteStatus> result = this->substituteSingleUseSymbolInExpr(unary->value, ref, replacement, replacementCanBeRemoved);
                if (result.second != substituteContinue) {
                    unary->value = result.first;
                    return {expr, result.second};
                }
            }

        } else if (auto dot = Get<EDot>(expr.data); dot != nullptr) {
            std::pair<Expr, substituteStatus> result = this->substituteSingleUseSymbolInExpr(dot->target, ref, replacement, replacementCanBeRemoved);
            if (result.second != substituteContinue) {
                dot->target = result.first;
                return {expr, result.second};
            }

        } else if (auto binary = Get<EBinary>(expr.data); binary != nullptr) {
            // Do not substitute into an assignment position
            if (BinaryAssignTarget(binary->op) == AssignTarget::kNone) {
                std::pair<Expr, substituteStatus> result = this->substituteSingleUseSymbolInExpr(binary->left, ref, replacement, replacementCanBeRemoved);
                if (result.second != substituteContinue) {
                    binary->left = result.first;
                    return {expr, result.second};
                }
            } else if (!this->astHelpers.ExprCanBeRemovedIfUnused(binary->left)) {
                // Do not reorder past a side effect in an assignment target, as that may
                // change the replacement value. For example, "fn()" may change "a" here:
                //
                //   let a = 1;
                //   foo[fn()] = a;
                //
                return {expr, substituteFailure};
            } else if (BinaryAssignTarget(binary->op) == AssignTarget::kUpdate && !replacementCanBeRemoved) {
                // If this is a read-modify-write assignment and the replacement has side
                // effects, don't reorder it past the assignment target. The assignment
                // target is being read so it may be changed by the side effect. For
                // example, "fn()" may change "foo" here:
                //
                //   let a = fn();
                //   foo += a;
                //
                return {expr, substituteFailure};
            }

            // If we get here then it should be safe to attempt to substitute the
            // replacement past the left operand into the right operand.
            std::pair<Expr, substituteStatus> result = this->substituteSingleUseSymbolInExpr(binary->right, ref, replacement, replacementCanBeRemoved);
            if (result.second != substituteContinue) {
                binary->right = result.first;
                return {expr, result.second};
            }

        } else if (auto ifExpr = Get<EIf>(expr.data); ifExpr != nullptr) {
            std::pair<Expr, substituteStatus> result = this->substituteSingleUseSymbolInExpr(ifExpr->test, ref, replacement, replacementCanBeRemoved);
            if (result.second != substituteContinue) {
                ifExpr->test = result.first;
                return {expr, result.second};
            }

            // Do not substitute our unconditionally-executed value into a branch
            // unless the value itself has no side effects
            if (replacementCanBeRemoved) {
                // Unlike other branches in this function such as "a && b" or "a?.[b]",
                // the "a ? b : c" form has potential code evaluation along both control
                // flow paths. Handle this by allowing substitution into either branch.
                // Side effects in one branch should not prevent the substitution into
                // the other branch.

                std::pair<Expr, substituteStatus> yesResult = this->substituteSingleUseSymbolInExpr(ifExpr->yes, ref, replacement, replacementCanBeRemoved);
                if (yesResult.second == substituteSuccess) {
                    ifExpr->yes = yesResult.first;
                    return {expr, yesResult.second};
                }

                std::pair<Expr, substituteStatus> noResult = this->substituteSingleUseSymbolInExpr(ifExpr->no, ref, replacement, replacementCanBeRemoved);
                if (noResult.second == substituteSuccess) {
                    ifExpr->no = noResult.first;
                    return {expr, noResult.second};
                }

                // Side effects in either branch should stop us from continuing to try to
                // substitute the replacement after the control flow branches merge again.
                if (yesResult.second != substituteContinue || noResult.second != substituteContinue) {
                    return {expr, substituteFailure};
                }
            }

        } else if (auto index = Get<EIndex>(expr.data); index != nullptr) {
            std::pair<Expr, substituteStatus> result = this->substituteSingleUseSymbolInExpr(index->target, ref, replacement, replacementCanBeRemoved);
            if (result.second != substituteContinue) {
                index->target = result.first;
                return {expr, result.second};
            }

            // Do not substitute our unconditionally-executed value into a branch
            // unless the value itself has no side effects
            if (replacementCanBeRemoved || index->optional_chain == OptionalChain::kNone) {
                std::pair<Expr, substituteStatus> indexResult = this->substituteSingleUseSymbolInExpr(index->index, ref, replacement, replacementCanBeRemoved);
                if (indexResult.second != substituteContinue) {
                    index->index = indexResult.first;
                    return {expr, indexResult.second};
                }
            }

        } else if (auto call = Get<ECall>(expr.data); call != nullptr) {
            // Don't substitute something into a call target that could change "this"
            bool isDot = Get<EDot>(replacement.data) != nullptr;
            bool isIndex = Get<EIndex>(replacement.data) != nullptr;
            bool skipCall = false;
            if (isDot || isIndex) {
                if (auto id2 = Get<EIdentifier>(call->target.data); id2 != nullptr && id2->ref == ref) {
                    skipCall = true;
                }
            }

            if (!skipCall) {
                std::pair<Expr, substituteStatus> targetResult = this->substituteSingleUseSymbolInExpr(call->target, ref, replacement, replacementCanBeRemoved);
                if (targetResult.second != substituteContinue) {
                    call->target = targetResult.first;
                    if (targetResult.second == substituteSuccess) {
                        // "const y = () => x; y()" => "(() => x)()" => "x"
                        std::pair<Expr, bool> inlineResult = this->maybeInlineIIFE(expr.loc, call);
                        if (inlineResult.second) {
                            return {inlineResult.first, substituteSuccess};
                        }
                    }
                    return {expr, targetResult.second};
                }

                // Do not substitute our unconditionally-executed value into a branch
                // unless the value itself has no side effects
                if (replacementCanBeRemoved || call->optional_chain == OptionalChain::kNone) {
                    for (int i = 0; i < static_cast<int>(call->args.size()); i++) {
                        std::pair<Expr, substituteStatus> argResult = this->substituteSingleUseSymbolInExpr(call->args[static_cast<size_t>(i)], ref, replacement, replacementCanBeRemoved);
                        if (argResult.second != substituteContinue) {
                            call->args[static_cast<size_t>(i)] = argResult.first;
                            return {expr, argResult.second};
                        }
                    }
                }
            }

        } else if (auto array = Get<EArray>(expr.data); array != nullptr) {
            for (int i = 0; i < static_cast<int>(array->items.size()); i++) {
                std::pair<Expr, substituteStatus> itemResult = this->substituteSingleUseSymbolInExpr(array->items[static_cast<size_t>(i)], ref, replacement, replacementCanBeRemoved);
                if (itemResult.second != substituteContinue) {
                    array->items[static_cast<size_t>(i)] = itemResult.first;
                    return {expr, itemResult.second};
                }
            }

        } else if (auto object = Get<EObject>(expr.data); object != nullptr) {
            for (int i = 0; i < static_cast<int>(object->properties.size()); i++) {
                Property& property = object->properties[static_cast<size_t>(i)];

                // Check the key
                if (Has(property.flags, PropertyFlags::kIsComputed)) {
                    std::pair<Expr, substituteStatus> keyResult = this->substituteSingleUseSymbolInExpr(property.key, ref, replacement, replacementCanBeRemoved);
                    if (keyResult.second != substituteContinue) {
                        object->properties[static_cast<size_t>(i)].key = keyResult.first;
                        return {expr, keyResult.second};
                    }

                    // Stop now because both computed keys and property spread have side effects
                    return {expr, substituteFailure};
                }

                // Check the value
                if (!IsNil(property.value_or_nil.data)) {
                    std::pair<Expr, substituteStatus> valueResult = this->substituteSingleUseSymbolInExpr(property.value_or_nil, ref, replacement, replacementCanBeRemoved);
                    if (valueResult.second != substituteContinue) {
                        object->properties[static_cast<size_t>(i)].value_or_nil = valueResult.first;
                        return {expr, valueResult.second};
                    }
                }
            }

        } else if (auto templateExpr = Get<ETemplate>(expr.data); templateExpr != nullptr) {
            if (!IsNil(templateExpr->tag_or_nil.data)) {
                std::pair<Expr, substituteStatus> tagResult = this->substituteSingleUseSymbolInExpr(templateExpr->tag_or_nil, ref, replacement, replacementCanBeRemoved);
                if (tagResult.second != substituteContinue) {
                    templateExpr->tag_or_nil = tagResult.first;
                    return {expr, tagResult.second};
                }
            }

            for (int i = 0; i < static_cast<int>(templateExpr->parts.size()); i++) {
                TemplatePart& part = templateExpr->parts[static_cast<size_t>(i)];
                std::pair<Expr, substituteStatus> partResult = this->substituteSingleUseSymbolInExpr(part.value, ref, replacement, replacementCanBeRemoved);
                if (partResult.second != substituteContinue) {
                    templateExpr->parts[static_cast<size_t>(i)].value = partResult.first;

                    // If we substituted a primitive, merge it into the template
                    if (IsPrimitiveLiteral(partResult.first.data)) {
                        expr = InlinePrimitivesIntoTemplate(expr.loc, *templateExpr);
                    }
                    return {expr, partResult.second};
                }
            }
        }

        // If both the replacement and this expression have no observable side
        // effects, then we can reorder the replacement past this expression
        if (replacementCanBeRemoved && this->astHelpers.ExprCanBeRemovedIfUnused(expr)) {
            return {expr, substituteContinue};
        }

        // We can always reorder past primitive values
        if (IsPrimitiveLiteral(expr.data) || IsPrimitiveLiteral(replacement.data)) {
            return {expr, substituteContinue};
        }

        // Otherwise we should stop trying to substitute past this point
        return {expr, substituteFailure};
    }


    std::pair<Expr, bool> Parser::maybeInlineIIFE(logger::Loc loc, ECall* call) {
        if (call->args.size() != 0) {
            return {Expr{}, false};
        }

        // Note: Do not inline async arrow functions as they are not IIFEs. In
        // particular, they are not necessarily invoked immediately, and any
        // exceptions involved in their evaluation will be swallowed without
        // bubbling up to the surrounding context.
        if (EArrow* arrow = Get<EArrow>(call->target.data); arrow != nullptr && arrow->args.size() == 0 && !arrow->is_async) {
            std::vector<Stmt> stmts = arrow->body.block.stmts;

            // "(() => {})()" => "void 0"
            if (stmts.size() == 0) {
                return {Expr{kEUndefinedShared, loc}, true};
            }

            if (stmts.size() == 1) {
                Expr value;

                if (auto s = Get<SReturn>(stmts[0].data); s != nullptr) {
                    // "(() => { return })()" => "void 0"
                    // "(() => { return 123 })()" => "123"
                    value = s->value_or_nil;
                    if (IsNil(value.data)) {
                        value.data = kEUndefinedShared;
                    }
                } else if (auto sExpr = Get<SExpr>(stmts[0].data); sExpr != nullptr) {
                    // "(() => { x })()" => "void x"
                    value = sExpr->value;
                    value.data = std::make_shared<EUnary>(EUnary{value, OpCode::kUnOpVoid});
                }

                if (!IsNil(value.data)) {
                    // Be careful about "/* @__PURE__ */" comments:
                    //
                    //   OK:  "(() => x)()"                 => "x"
                    //   BAD: "/* @__PURE__ */ (() => x)()" => "x"
                    //
                    // The comment indicates that the function body is eligible for
                    // dead code elimination. Since we don't have a direct AST node
                    // for that, we can't currently unwrap that and preserve the
                    // intent. So if it does have a pure comment, only remove it if
                    // the value itself is already pure.
                    if (!call->can_be_unwrapped_if_unused || this->astHelpers.ExprCanBeRemovedIfUnused(value)) {
                        return {value, true};
                    }
                }
            }
        }

        return {Expr{}, false};
    }

    bool Parser::iifeCanBeRemovedIfUnused(std::vector<Arg> args, FnBody body) {
        for (const Arg& arg : args) {
            if (!IsNil(arg.default_or_nil.data) && !this->astHelpers.ExprCanBeRemovedIfUnused(arg.default_or_nil)) {
                // The default value has a side effect
                return false;
            }

            if (Get<BIdentifier>(arg.binding.data) == nullptr) {
                // Destructuring is a side effect (due to property access)
                return false;
            }
        }

        // Check whether any statements have side effects or not. Consider return
        // statements as not having side effects because if the IIFE can be removed
        // then we know the return value is unused, so we know that returning the
        // value has no side effects.
        return this->astHelpers.StmtsCanBeRemovedIfUnused(body.block.stmts, StmtsCanBeRemovedIfUnusedFlags::kReturnCanBeRemovedIfUnused);
    }


    Expr Parser::maybeTransposeIfExprChain(Expr expr, const std::function<Expr(Expr)>& visit) {
        if (EIf* e = Get<EIf>(expr.data); e != nullptr) {
            e->yes = this->maybeTransposeIfExprChain(e->yes, visit);
            e->no = this->maybeTransposeIfExprChain(e->no, visit);
            return expr;
        }
        return visit(expr);
    }

    Expr Parser::keepExprSymbolName(Expr value, const std::string& name) {
        std::vector<Expr> args;
        args.push_back(value);
        EString* estr = new EString();
        estr->value = helpers::StringToUTF16(name);
        args.push_back(Expr{std::shared_ptr<EString>(estr), value.loc});

        value = this->callRuntime(value.loc, "__name", args);

        // Make sure tree shaking removes this if the function is never used
        ECall* call = Get<ECall>(value.data);
        call->can_be_unwrapped_if_unused = true;
        return value;
    }

    std::pair<Stmt, bool> Parser::maybeRelocateVarsToTopLevel(std::vector<Decl> decls, relocateVarsMode mode) {
        // Only do this when bundling, and not when the scope is already top-level
        if (this->options.optionsThatSupportStructuralEquality.mode != config::Mode::kBundle || (this->currentScope == this->moduleScope && this->singleStmtDepth == 0)) {
            return {Stmt{}, false};
        }

        // Only do this if we're not inside a function
        Scope* scope = this->currentScope;
        while (!StopsHoisting(scope->kind)) {
            scope = scope->parent;
        }
        if (scope != this->moduleScope) {
            return {Stmt{}, false};
        }

        // Convert the declarations to assignments
        auto wrapIdentifier = [this](logger::Loc loc, compiler::Ref ref) -> Expr {
            this->relocatedTopLevelVars.push_back(compiler::LocRef{loc, ref});
            this->recordUsage(ref);
            EIdentifier* id = new EIdentifier();
            id->ref = ref;
            return Expr{std::shared_ptr<EIdentifier>(id), loc};
        };
        Expr value;
        for (Decl decl : decls) {
            Expr binding = ConvertBindingToExpr(decl.binding, wrapIdentifier);
            if (!IsNil(decl.value_or_nil.data)) {
                value = JoinWithComma(value, Assign(binding, decl.value_or_nil));
            } else if (mode == relocateVarsForInOrForOf) {
                value = JoinWithComma(value, binding);
            }
        }
        if (IsNil(value.data)) {
            // If none of the variables had any initializers, just remove the declarations
            return {Stmt{}, true};
        }
        return {Stmt{std::make_shared<SExpr>(SExpr{value}), value.loc}, true};
    }


    std::vector<switchCaseLiveness> analyzeSwitchCasesForLiveness(SSwitch* s) {
        std::vector<switchCaseLiveness> cases;
        cases.reserve(s->cases.size());
        ptrdiff_t defaultIndex = -1;

        // Determine the status of the individual cases independently
        livenessStatus maxStatus = alwaysDead;
        for (size_t i = 0; i < s->cases.size(); i++) {
            const Case& c = s->cases[i];
            if (IsNil(c.value_or_nil.data)) {
                defaultIndex = static_cast<ptrdiff_t>(i);
            }

            // Check the value for strict equality
            livenessStatus status;
            if (maxStatus == alwaysLive) {
                status = alwaysDead; // Everything after an always-live case is always dead
            } else if (IsNil(c.value_or_nil.data)) {
                status = alwaysDead; // This is the default case, and will be filled in later
            } else if (std::optional<bool> check = CheckEqualityIfNoSideEffects(s->test.data, c.value_or_nil.data, EqualityKind::kStrictEquality); check) {
                if (*check) {
                    status = alwaysLive; // This branch will always be matched, and will be taken unless an earlier branch was taken
                } else {
                    status = alwaysDead; // This branch will never be matched, and will not be taken unless there was fall-through
                }
            } else {
                status = livenessUnknown; // This branch depends on run-time values and may or may not be matched
            }
            if (maxStatus < status) {
                maxStatus = status;
            }

            cases.push_back({status, caseBodyCouldHaveFallThrough(c.body)});
        }

        // Set the liveness for the default case last based on the other cases
        if (defaultIndex != -1) {
            // The negation here transposes "always live" with "always dead"
            livenessStatus status = static_cast<livenessStatus>(-static_cast<int>(maxStatus));
            if (maxStatus < status) {
                maxStatus = status;
            }
            cases[static_cast<size_t>(defaultIndex)].status = status;
        }

        // Then propagate fall-through information in linear fall-through order
        for (size_t i = 0; i < cases.size(); i++) {
            switchCaseLiveness& c = cases[i];
            // Propagate state forward if this isn't dead. Note that the "can fall
            // through" flag does not imply "must fall through". The body may have
            // an embedded "break" inside an if statement, for example.
            if (c.status != alwaysDead) {
                for (size_t j = i + 1; j < cases.size() && cases[j - 1].canFallThrough; j++) {
                    cases[j].status = livenessUnknown;
                }
            } else if (maxStatus > alwaysDead && stmtsCareAboutScope(s->cases[i].body)) {
                // Since adjacent cases share a scope, dead cases can potentially still
                // affect other cases that are live. Consider the following:
                //
                //   globalThis.foo = true
                //   switch (1) {
                //     case 0:
                //       let foo
                //     case 1:
                //       return foo
                //   }
                //
                // This code is supposed to throw a ReferenceError. But if we treat the
                // first case as dead code, then "let foo" will end up being removed and
                // the code will incorrectly return true instead.
                cases[i].status = livenessUnknown;
            }
        }
        return cases;
    }


    // Check for potential fall-through by checking for a jump at the end of the body
    bool caseBodyCouldHaveFallThrough(std::vector<Stmt> stmts) {
        while (stmts.size() > 0) {
            S& s = stmts[stmts.size() - 1].data;
            if (auto block = Get<SBlock>(s); block != nullptr) {
                // If this ends with a block, check the block's body next
                stmts = block->stmts;
                continue;
            } else if (Get<SBreak>(s) != nullptr || Get<SContinue>(s) != nullptr ||
                Get<SReturn>(s) != nullptr || Get<SThrow>(s) != nullptr) {
                return false;
            }
            break;
        }
        return true;
    }

    bool isJumpStatement(S* data) {
        if (Get<SBreak>(*data) != nullptr || Get<SContinue>(*data) != nullptr ||
            Get<SReturn>(*data) != nullptr || Get<SThrow>(*data) != nullptr) {
            return true;
        }

        return false;
    }

    bool jumpStmtsLookTheSame(S* left, S* right) {
        if (auto a = Get<SBreak>(*left); a != nullptr) {
            auto b = Get<SBreak>(*right);
            return b != nullptr && (a->label == nullptr) == (b->label == nullptr) && (a->label == nullptr || a->label->ref == b->label->ref);
        }

        if (auto a = Get<SContinue>(*left); a != nullptr) {
            auto b = Get<SContinue>(*right);
            return b != nullptr && (a->label == nullptr) == (b->label == nullptr) && (a->label == nullptr || a->label->ref == b->label->ref);
        }

        if (auto a = Get<SReturn>(*left); a != nullptr) {
            auto b = Get<SReturn>(*right);
            return b != nullptr && (IsNil(a->value_or_nil.data)) == (IsNil(b->value_or_nil.data)) &&
                (IsNil(a->value_or_nil.data) || ValuesLookTheSame(a->value_or_nil.data, b->value_or_nil.data));
        }

        if (auto a = Get<SThrow>(*left); a != nullptr) {
            auto b = Get<SThrow>(*right);
            return b != nullptr && ValuesLookTheSame(a->value.data, b->value.data);
        }

        return false;
    }

    bool stmtCaresAboutScope(Stmt stmt) {
        S& s = stmt.data;
        if (Get<SBlock>(s) != nullptr || Get<SEmpty>(s) != nullptr || Get<SDebugger>(s) != nullptr || Get<SExpr>(s) != nullptr || Get<SIf>(s) != nullptr ||
            Get<SFor>(s) != nullptr || Get<SForIn>(s) != nullptr || Get<SForOf>(s) != nullptr || Get<SDoWhile>(s) != nullptr || Get<SWhile>(s) != nullptr ||
            Get<SWith>(s) != nullptr || Get<STry>(s) != nullptr || Get<SSwitch>(s) != nullptr || Get<SReturn>(s) != nullptr || Get<SThrow>(s) != nullptr ||
            Get<SBreak>(s) != nullptr || Get<SContinue>(s) != nullptr || Get<SDirective>(s) != nullptr || Get<SLabel>(s) != nullptr) {
            return false;
        }

        if (auto sl = Get<SLocal>(s); sl != nullptr) {
            return sl->kind != LocalKind::kVar;
        }

        return true;
    }

    bool stmtsCareAboutScope(std::vector<Stmt> stmts) {
        for (Stmt& stmt : stmts) {
            if (stmtCaresAboutScope(stmt)) {
                return true;
            }
        }
        return false;
    }

    Stmt stmtsToSingleStmt(logger::Loc loc, std::vector<Stmt> stmts, logger::Loc closeBraceLoc) {
        if (stmts.empty()) {
            return Stmt{kSEmptyShared, loc};
        }
        if (stmts.size() == 1 && !stmtCaresAboutScope(stmts[0])) {
            return stmts[0];
        }
        return Stmt{std::make_shared<SBlock>(SBlock{stmts, closeBraceLoc}), loc};
    }

    std::vector<Stmt> appendIfOrLabelBodyPreservingScope(std::vector<Stmt> stmts, Stmt body) {
        if (auto block = Get<SBlock>(body.data); block != nullptr && !stmtsCareAboutScope(block->stmts)) {
            stmts.insert(stmts.end(), block->stmts.begin(), block->stmts.end());
            return stmts;
        }
        if (stmtCaresAboutScope(body)) {
            stmts.push_back(Stmt{std::make_shared<SBlock>(SBlock{std::vector<Stmt>{body}, logger::Loc{}}), body.loc});
            return stmts;
        }
        stmts.push_back(body);
        return stmts;
    }


}
