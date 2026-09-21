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

enum classKind : uint8_t {
    classKindExpr,
    classKindStmt,
    classKindExportStmt,
    classKindExportDefaultStmt,
};


std::pair<int, bool> fieldOrAccessorOrder(PropertyKind kind, PropertyFlags flags) {
    if (kind == PropertyKind::kAutoAccessor) {
        if (Has(flags, PropertyFlags::kIsStatic)) {
            return {0, true};
        } else {
            return {1, true};
        }
    } else if (kind == PropertyKind::kField) {
        if (Has(flags, PropertyFlags::kIsStatic)) {
            return {2, true};
        } else {
            return {3, true};
        }
    }
    return {0, false};
}


struct propertyAnalysis {
    EPrivateIdentifier *private_member;
    std::vector<Decorator> prop_experimental_decorators;
    std::vector<Decorator> prop_decorators;
    bool must_lower_field;
    bool needs_value_of_key;
    bool rewrite_auto_accessor_to_get_set;
    bool should_omit_field_initializer;
    bool static_field_to_block_assign;
    bool is_computed_property_copied_or_moved;
};

// Returns a copy of the "std::shared_ptr<T>" stored inside a variant, or nullptr
template <typename T, typename V>
std::shared_ptr<T> GetShared(const V &data) {
    const auto *sp = std::get_if<std::shared_ptr<T>>(&data);
    return sp ? *sp : nullptr;
}

Expr cloneKeyForLowerClass(Expr key) {
    if (auto kNum = Get<ENumber>(key.data)) {
        key.data = std::make_shared<ENumber>(ENumber{*kNum});
    } else if (auto kStr = Get<EString>(key.data)) {
        key.data = std::make_shared<EString>(EString{*kStr});
    } else if (auto kId = Get<EIdentifier>(key.data)) {
        key.data = std::make_shared<EIdentifier>(EIdentifier{*kId});
    } else if (auto kSym = Get<ENameOfSymbol>(key.data)) {
        key.data = std::make_shared<ENameOfSymbol>(ENameOfSymbol{*kSym});
    } else if (auto kPriv = Get<EPrivateIdentifier>(key.data)) {
        key.data = std::make_shared<EPrivateIdentifier>(EPrivateIdentifier{*kPriv});
    } else {
        // panic("Internal error")
    }
    return key;
}


std::tuple<Expr, logger::Loc, std::shared_ptr<ECall>, Expr> findFirstTopLevelSuperCall(
    Expr expr,
    compiler::Ref superCtorRef) {
    if (std::shared_ptr<ECall> call = GetShared<ECall>(expr.data)) {
        if (auto target = Get<EIdentifier>(call->target.data); target != nullptr && target->ref == superCtorRef) {
            call->target.data = kESuperShared;
            return {Expr{}, expr.loc, call, Expr{}};
        }
    }

    // Also search down comma operator chains for a super call
    if (auto comma = Get<EBinary>(expr.data); comma != nullptr && comma->op == OpCode::kBinOpComma) {
        std::tuple<Expr, logger::Loc, std::shared_ptr<ECall>, Expr> found =
            findFirstTopLevelSuperCall(comma->left, superCtorRef);
        if (std::get<2>(found) != nullptr) {
            return {std::get<0>(found), std::get<1>(found), std::get<2>(found),
                    JoinWithComma(std::get<3>(found), comma->right)};
        }

        found = findFirstTopLevelSuperCall(comma->right, superCtorRef);
        if (std::get<2>(found) != nullptr) {
            return {JoinWithComma(comma->left, std::get<0>(found)), std::get<1>(found), std::get<2>(found),
                    std::get<3>(found)};
        }
    }

    return {Expr{}, logger::Loc{}, nullptr, Expr{}};
}

struct lowerClassContext {
    std::string name_to_keep;
    classKind kind;
    Class *class_;
    logger::Loc class_loc;
    Expr class_expr; // Only for "kind == classKindExpr", may be replaced by "nameFunc()"
    compiler::LocRef default_name;

    EFunction *ctor;
    compiler::Ref extends_ref;
    std::vector<Property> parameter_field_props;
    std::vector<Stmt> parameter_fields;
    std::vector<Stmt> instance_members;
    std::vector<Stmt> instance_private_methods;
    int auto_accessor_count;

    // These expressions are generated after the class body, in this order
    Expr computed_property_chain;
    std::vector<Expr> private_members;
    std::vector<Expr> static_members;
    std::vector<Expr> static_private_methods;

    // These contain calls to "__decorateClass" for TypeScript experimental decorators
    std::vector<Expr> instance_experimental_decorators;
    std::vector<Expr> static_experimental_decorators;

    // These are used for implementing JavaScript decorators
    compiler::Ref decorator_context_ref;
    Expr decorator_class_decorators;
    std::unordered_map<int, int> decorator_property_to_initializer_map;
    bool decorator_call_instance_method_extra_initializers;
    bool decorator_call_static_method_extra_initializers;
    std::vector<Expr> decorator_static_non_field_elements;
    std::vector<Expr> decorator_instance_non_field_elements;
    std::vector<Expr> decorator_static_field_elements;
    std::vector<Expr> decorator_instance_field_elements;

    // These are used by "lowerMethod"
    compiler::Ref private_instance_method_ref;
    compiler::Ref private_static_method_ref;

    // These are only for class expressions that need to be captured
    std::function<Expr()> name_func;
    std::function<Expr()> name_func_captured;
    std::function<Expr(Expr)> wrap_func;
    bool did_capture_class_expr;

    void enableNameCapture(Parser *p, const visitClassResult &result);
    std::tuple<Property, compiler::Ref, bool> lowerField(Parser *p, Property prop,
        EPrivateIdentifier *private_member, bool shouldOmitFieldInitializer,
        bool staticFieldToBlockAssign, int initializerIndex);
    void lowerPrivateMethod(Parser *p, Property prop, EPrivateIdentifier *private_member);
    bool lowerMethod(Parser *p, Property prop, EPrivateIdentifier *private_member);
    propertyAnalysis analyzeProperty(Parser *p, Property &prop, classLoweringInfo classLoweringInfo);
    std::pair<std::unordered_map<int, compiler::Ref>, std::unordered_map<int, compiler::Ref>>
        hoistComputedProperties(Parser *p, classLoweringInfo classLoweringInfo);
    void processProperties(Parser *p, classLoweringInfo classLoweringInfo, const visitClassResult &result);
    void lowerStaticBlock(Parser *p, logger::Loc loc, const ClassStaticBlock &block);
    std::vector<Property> rewriteAutoAccessorToGetSet(Parser *p, Property prop,
        std::vector<Property> properties, Expr keyExprNoSideEffects, bool mustLowerField,
        EPrivateIdentifier *private_member, const visitClassResult &result);
    void insertInitializersIntoConstructor(Parser *p, classLoweringInfo classLoweringInfo,
        const visitClassResult &result);
    std::pair<std::vector<Stmt>, Expr> finishAndGenerateCode(Parser *p, const visitClassResult &result);
};

void lowerClassContext::enableNameCapture(Parser *p, const visitClassResult &result) {
    // Class statements can be missing a name if they are in an
    // "export default" statement:
    //
    //   export default class {
    //     static foo = 123
    //   }
    //
    if (this->kind == classKindExpr) {
        // If this is a class expression, capture and store it lazily. We have
        // to do this even if it has a name since the name isn't exposed
        // outside the class body. However, the capture only needs to happen if
        // the class name is actually referenced by one of the generated
        // initializers, so defer it until "name_func" is first called.
        this->name_func = [p, this]() -> Expr {
            if (!this->did_capture_class_expr) {
                std::shared_ptr<EClass> classExpr = std::make_shared<EClass>();
                classExpr->class_ = *this->class_;
                this->class_ = &classExpr->class_;

                std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
                    p->captureValueWithPossibleSideEffects(this->class_loc, 2,
                        Expr{classExpr, this->class_loc}, captureValueMode::valueDefinitelyNotMutated);
                this->name_func_captured = std::move(captured.first);
                this->wrap_func = std::move(captured.second);
                this->class_expr = this->name_func_captured();
                this->did_capture_class_expr = true;
                Expr name = this->name_func_captured();

                // If we're storing the class expression in a variable, remove the class
                // name and rewrite all references to the class name with references to
                // the temporary variable holding the class expression. This ensures that
                // references to the class expression by name in any expressions that end
                // up being pulled outside of the class body still work. For example:
                //
                //   let Bar = class Foo {
                //     static foo = 123
                //     static bar = Foo.foo
                //   }
                //
                // This might be converted into the following:
                //
                //   var _a;
                //   let Bar = (_a = class {
                //   }, _a.foo = 123, _a.bar = _a.foo, _a);
                //
                if (this->class_->name != nullptr) {
                    p->mergeSymbols(this->class_->name->ref, Get<EIdentifier>(name.data)->ref);
                    this->class_->name.reset();
                }
            }
            return this->name_func_captured();
        };
    } else {
        // If anything referenced the inner class name, then we should use that
        // name for any automatically-generated initialization code, since it
        // will come before the outer class name is initialized.
        this->name_func = [p, this, &result]() -> Expr {
            if (result.innerClassNameRef != compiler::kInvalidRef) {
                p->recordUsage(result.innerClassNameRef);
                return Expr{std::make_shared<EIdentifier>(EIdentifier{result.innerClassNameRef}),
                    this->class_->name->loc};
            }

            // Otherwise we should just use the outer class name
            if (this->class_->name == nullptr) {
                if (this->kind == classKindExportDefaultStmt) {
                    this->class_->name = std::make_shared<compiler::LocRef>(this->default_name);
                } else {
                    this->class_->name = std::make_shared<compiler::LocRef>(
                        compiler::LocRef{this->class_loc, p->generateTempRef(tempRefNoDeclare, "")});
                }
            }
            p->recordUsage(this->class_->name->ref);
            return Expr{std::make_shared<EIdentifier>(EIdentifier{this->class_->name->ref}),
                this->class_->name->loc};
        };
    }
}

// Handle lowering of instance and static fields. Move their initializers
// from the class body to either the constructor (instance fields) or after
// the class (static fields).
//
// If the returned "bool" is true, the returned property should be added to the
// class body. Otherwise the property should be omitted from the class body.
std::tuple<Property, compiler::Ref, bool> lowerClassContext::lowerField(Parser *p, Property prop,
    EPrivateIdentifier *private_member, bool shouldOmitFieldInitializer,
    bool staticFieldToBlockAssign, int initializerIndex) {
    bool mustLowerPrivate = private_member != nullptr && p->privateSymbolNeedsToBeLowered(private_member);
    compiler::Ref ref = compiler::kInvalidRef;

    // The TypeScript compiler doesn't follow the JavaScript spec for
    // uninitialized fields. They are supposed to be set to undefined but the
    // TypeScript compiler just omits them entirely.
    if (!shouldOmitFieldInitializer) {
        logger::Loc loc = prop.loc;

        // Determine where to store the field
        Expr target;
        if (Has(prop.flags, PropertyFlags::kIsStatic) && !staticFieldToBlockAssign) {
            target = this->name_func();
        } else {
            target = Expr{kEThisShared, loc};
        }

        // Generate the assignment initializer
        Expr init;
        if (!IsNil(prop.initializer_or_nil.data)) {
            init = prop.initializer_or_nil;
        } else {
            init = Expr{kEUndefinedShared, loc};
        }

        // Optionally call registered decorator initializers
        if (initializerIndex != -1) {
            Expr value;
            if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                value = this->name_func();
            } else {
                value = Expr{kEThisShared, loc};
            }
            std::vector<Expr> args;
            args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{this->decorator_context_ref}), loc});
            args.push_back(Expr{std::make_shared<ENumber>(ENumber{static_cast<double>((4 + 2 * initializerIndex) << 1)}), loc});
            args.push_back(value);
            if (Get<EUndefined>(init.data) == nullptr) {
                args.push_back(init);
            }
            init = p->callRuntime(init.loc, "__runInitializers", args);
            p->recordUsage(this->decorator_context_ref);
        }

        // Generate the assignment target
        Expr memberExpr;
        if (mustLowerPrivate) {
            // Generate a new symbol for this private field
            ref = p->generateTempRef(tempRefNeedsDeclare,
                "_" + p->symbols[private_member->ref.inner_index].original_name.substr(1));
            p->symbols[private_member->ref.inner_index].link = ref;

            // Initialize the private field to a new WeakMap
            if (p->weakMapRef == compiler::kInvalidRef) {
                p->weakMapRef = p->newSymbol(compiler::SymbolKind::kUnbound, "WeakMap");
                p->moduleScope->generated.push_back(p->weakMapRef);
            }
            std::shared_ptr<ENew> newExpr = std::make_shared<ENew>();
            newExpr->target = Expr{std::make_shared<EIdentifier>(EIdentifier{p->weakMapRef}), prop.key.loc};
            this->private_members.push_back(Assign(
                Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), prop.key.loc},
                Expr{newExpr, prop.key.loc}));
            p->recordUsage(ref);

            // Add every newly-constructed instance into this map
            Expr key{std::make_shared<EIdentifier>(EIdentifier{ref}), prop.key.loc};
            std::vector<Expr> args;
            args.push_back(target);
            args.push_back(key);
            if (Get<EUndefined>(init.data) == nullptr) {
                args.push_back(init);
            }
            memberExpr = p->callRuntime(loc, "__privateAdd", args);
            p->recordUsage(ref);
        } else if (private_member == nullptr && this->class_->use_define_for_class_fields) {
            if (p->shouldAddKeyComment) {
                if (auto str = Get<EString>(prop.key.data); str != nullptr) {
                    str->has_property_key_comment = true;
                }
            }
            std::vector<Expr> args;
            args.push_back(target);
            args.push_back(prop.key);
            if (Get<EUndefined>(init.data) == nullptr) {
                args.push_back(init);
            }
            auto call = std::make_shared<ECall>();
            call->target = p->importFromRuntime(loc, "__publicField");
            call->args = args;
            memberExpr = Expr{call, loc};
        } else {
            if (auto key = Get<EString>(prop.key.data); key != nullptr &&
                !Has(prop.flags, PropertyFlags::kIsComputed) &&
                !Has(prop.flags, PropertyFlags::kPreferQuotedKey)) {
                auto dot = std::make_shared<EDot>();
                dot->target = target;
                dot->name = helpers::UTF16ToString(key->value);
                dot->name_loc = prop.key.loc;
                target = Expr{dot, loc};
            } else {
                auto index = std::make_shared<EIndex>();
                index->target = target;
                index->index = prop.key;
                target = Expr{index, loc};
            }

            memberExpr = Assign(target, init);
        }

        // Run extra initializers
        if (initializerIndex != -1) {
            Expr value;
            if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                value = this->name_func();
            } else {
                value = Expr{kEThisShared, loc};
            }
            std::vector<Expr> args;
            args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{this->decorator_context_ref}), loc});
            args.push_back(Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(((5 + 2 * initializerIndex) << 1) | 1)}), loc});
            args.push_back(value);
            memberExpr = JoinWithComma(memberExpr, p->callRuntime(loc, "__runInitializers", args));
            p->recordUsage(this->decorator_context_ref);
        }

        if (Has(prop.flags, PropertyFlags::kIsStatic)) {
            // Move this property to an assignment after the class ends
            if (staticFieldToBlockAssign) {
                // Use inline assignment in a static block instead of lowering
                Property resultProp;
                resultProp.loc = loc;
                resultProp.kind = PropertyKind::kClassStaticBlock;
                std::shared_ptr<ClassStaticBlock> staticBlock = std::make_shared<ClassStaticBlock>();
                staticBlock->loc = loc;
                staticBlock->block.stmts.push_back(
                    Stmt{std::make_shared<SExpr>(SExpr{memberExpr}), loc});
                resultProp.class_static_block = staticBlock;
                return {resultProp, ref, true};
            } else {
                // Move this property to an assignment after the class ends
                this->static_members.push_back(memberExpr);
            }
        } else {
            // Move this property to an assignment inside the class constructor
            this->instance_members.push_back(
                Stmt{std::make_shared<SExpr>(SExpr{memberExpr}), loc});
        }
    }

    if (private_member == nullptr || mustLowerPrivate) {
        // Remove the field from the class body
        return {Property{}, ref, false};
    }

    // Keep the private field but remove the initializer
    prop.initializer_or_nil = Expr{};
    return {prop, ref, true};
}

void lowerClassContext::lowerPrivateMethod(Parser *p, Property prop, EPrivateIdentifier *private_member) {
    // All private methods can share the same WeakSet
    compiler::Ref *ref = nullptr;
    if (Has(prop.flags, PropertyFlags::kIsStatic)) {
        ref = &this->private_static_method_ref;
    } else {
        ref = &this->private_instance_method_ref;
    }
    if (*ref == compiler::kInvalidRef) {
        // Generate a new symbol to store the WeakSet
        std::string name;
        if (Has(prop.flags, PropertyFlags::kIsStatic)) {
            name = "_static";
        } else {
            name = "_instances";
        }
        if (!this->name_to_keep.empty()) {
            name = "_" + this->name_to_keep + name;
        }
        *ref = p->generateTempRef(tempRefNeedsDeclare, name);

        // Generate the initializer
        if (p->weakSetRef == compiler::kInvalidRef) {
            p->weakSetRef = p->newSymbol(compiler::SymbolKind::kUnbound, "WeakSet");
            p->moduleScope->generated.push_back(p->weakSetRef);
        }
        std::shared_ptr<ENew> newExpr = std::make_shared<ENew>();
        newExpr->target = Expr{std::make_shared<EIdentifier>(EIdentifier{p->weakSetRef}), this->class_loc};
        this->private_members.push_back(Assign(
            Expr{std::make_shared<EIdentifier>(EIdentifier{*ref}), this->class_loc},
            Expr{newExpr, this->class_loc}));
        p->recordUsage(*ref);
        p->recordUsage(p->weakSetRef);

        // Determine what to store in the WeakSet
        Expr target;
        if (Has(prop.flags, PropertyFlags::kIsStatic)) {
            target = this->name_func();
        } else {
            target = Expr{kEThisShared, this->class_loc};
        }

        // Add every newly-constructed instance into this set
        std::vector<Expr> args;
        args.push_back(target);
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{*ref}), this->class_loc});
        Expr methodExpr = p->callRuntime(this->class_loc, "__privateAdd", args);
        p->recordUsage(*ref);

        // Make sure that adding to the map happens before any field
        // initializers to handle cases like this:
        //
        //   class A {
        //     pub = this.#priv;
        //     #priv() {}
        //   }
        //
        if (Has(prop.flags, PropertyFlags::kIsStatic)) {
            // Move this property to an assignment after the class ends
            this->static_private_methods.push_back(methodExpr);
        } else {
            // Move this property to an assignment inside the class constructor
            this->instance_private_methods.push_back(
                Stmt{std::make_shared<SExpr>(SExpr{methodExpr}), this->class_loc});
        }
    }
    p->symbols[private_member->ref.inner_index].link = *ref;
}

// If this returns true, the method property should be dropped as it has
// already been accounted for elsewhere (e.g. a lowered private method).
bool lowerClassContext::lowerMethod(Parser *p, Property prop, EPrivateIdentifier *private_member) {
    if (private_member != nullptr && p->privateSymbolNeedsToBeLowered(private_member)) {
        this->lowerPrivateMethod(p, prop, private_member);

        // Move the method definition outside the class body
        compiler::Ref methodRef = p->generateTempRef(tempRefNeedsDeclare, "_");
        if (prop.kind == PropertyKind::kSetter) {
            p->symbols[methodRef.inner_index].link = p->privateSetters[private_member->ref];
        } else {
            p->symbols[methodRef.inner_index].link = p->privateGetters[private_member->ref];
        }
        p->recordUsage(methodRef);
        this->private_members.push_back(Assign(
            Expr{std::make_shared<EIdentifier>(EIdentifier{methodRef}), prop.key.loc},
            prop.value_or_nil));
        return true;
    }

    if (auto key = Get<EString>(prop.key.data); key != nullptr && helpers::UTF16EqualsString(key->value, "constructor")) {
        if (auto fn = Get<EFunction>(prop.value_or_nil.data); fn != nullptr) {
            // Remember where the constructor is for later
            this->ctor = fn;

            // Initialize TypeScript constructor parameter fields
            if (p->options.optionsThatSupportStructuralEquality.ts.Parse) {
                for (Arg &arg : this->ctor->fn.args) {
                    if (arg.is_typescript_ctor_field) {
                        if (auto id = Get<BIdentifier>(arg.binding.data); id != nullptr) {
                            logger::Loc loc = arg.binding.loc;
                            std::string name = p->symbols[id->ref.inner_index].original_name;
                            Expr target{kEThisShared, loc};
                            Expr init{std::make_shared<EIdentifier>(EIdentifier{id->ref}), loc};

                            if (!this->class_->use_define_for_class_fields ||
                                !compat::Has(p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
                                    compat::JSFeature::kClassField)) {
                                this->parameter_fields.push_back(AssignStmt(
                                    Expr{*p->dotOrMangledPropVisit(target, name, loc), loc},
                                    init));
                            }
                            if (this->class_->use_define_for_class_fields) {
                                Expr defKey{std::make_shared<EString>(EString{helpers::StringToUTF16(name), logger::Loc{}}), loc};
                                if (compat::Has(p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
                                        compat::JSFeature::kClassField)) {
                                    std::vector<Expr> args;
                                    args.push_back(target);
                                    args.push_back(defKey);
                                    args.push_back(init);
                                    auto call = std::make_shared<ECall>();
                                    call->target = p->importFromRuntime(loc, "__publicField");
                                    call->args = args;
                                    this->parameter_fields.push_back(
                                        Stmt{std::make_shared<SExpr>(SExpr{Expr{call, loc}}), loc});
                                } else {
                                    Property fieldProp;
                                    fieldProp.kind = PropertyKind::kField;
                                    fieldProp.loc = loc;
                                    fieldProp.key = defKey;
                                    this->parameter_field_props.push_back(fieldProp);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return false;
}

propertyAnalysis lowerClassContext::analyzeProperty(Parser *p, Property &prop,
    classLoweringInfo classLoweringInfo) {
    propertyAnalysis analysis{};
    analysis.private_member = Get<EPrivateIdentifier>(prop.key.data);
    bool mustLowerPrivate = analysis.private_member != nullptr && p->privateSymbolNeedsToBeLowered(analysis.private_member);
    analysis.should_omit_field_initializer = p->options.optionsThatSupportStructuralEquality.ts.Parse &&
        !IsMethodDefinition(prop.kind) && IsNil(prop.initializer_or_nil.data) &&
        !this->class_->use_define_for_class_fields && !mustLowerPrivate && !this->class_->should_lower_standard_decorators;

    // Class fields must be lowered if the environment doesn't support them
    if (!IsMethodDefinition(prop.kind)) {
        if (Has(prop.flags, PropertyFlags::kIsStatic)) {
            analysis.must_lower_field = classLoweringInfo.lowerAllStaticFields;
        } else if (prop.kind == PropertyKind::kField && p->options.optionsThatSupportStructuralEquality.ts.Parse &&
            !this->class_->use_define_for_class_fields && analysis.private_member == nullptr) {
            // Lower non-private instance fields (not accessors) if TypeScript's
            // "useDefineForClassFields" setting is disabled. When all such fields
            // have no initializers, we avoid setting the "lowerAllInstanceFields"
            // flag as an optimization because we can just remove all class field
            // declarations in that case without messing with the constructor. But
            // we must set the "mustLowerField" flag here to cause this class field
            // declaration to still be removed.
            analysis.must_lower_field = true;
        } else {
            analysis.must_lower_field = classLoweringInfo.lowerAllInstanceFields;
        }
    }

    // If the field uses the TypeScript "declare" or "abstract" keyword, just
    // omit it entirely. However, we must still keep any side-effects in the
    // computed value and/or in the decorators.
    if (prop.kind == PropertyKind::kDeclareOrAbstract && IsNil(prop.value_or_nil.data)) {
        analysis.must_lower_field = true;
        analysis.should_omit_field_initializer = true;
    }

    // For convenience, split decorators off into separate fields based on how
    // they will end up being lowered (if they are even being lowered at all)
    if (p->options.optionsThatSupportStructuralEquality.ts.Parse &&
        p->options.optionsThatSupportStructuralEquality.ts.Config.ExperimentalDecorators == config::MaybeBool::kTrue) {
        analysis.prop_experimental_decorators = prop.decorators;
    } else if (this->class_->should_lower_standard_decorators) {
        analysis.prop_decorators = prop.decorators;
    }

    // Note: Auto-accessors use a different transform when they are decorated.
    // This transform trades off worse run-time performance for better code size.
    analysis.rewrite_auto_accessor_to_get_set = analysis.prop_decorators.empty() && prop.kind == PropertyKind::kAutoAccessor &&
        (compat::Has(p->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
            compat::JSFeature::kDecorators) || analysis.must_lower_field);

    // Transform non-lowered static fields that use assign semantics into an
    // assignment in an inline static block instead of lowering them. This lets
    // us avoid having to unnecessarily lower static private fields when
    // "useDefineForClassFields" is disabled.
    analysis.static_field_to_block_assign = prop.kind == PropertyKind::kField && !analysis.must_lower_field &&
        !this->class_->use_define_for_class_fields &&
        Has(prop.flags, PropertyFlags::kIsStatic) && analysis.private_member == nullptr;

    // Computed properties can't be copied or moved because they have side effects
    // and we don't want to evaluate their side effects twice or change their
    // evaluation order. We'll need to store them in temporary variables to keep
    // their side effects in place when we reference them elsewhere.
    analysis.needs_value_of_key = true;
    if (Has(prop.flags, PropertyFlags::kIsComputed) &&
        (analysis.prop_experimental_decorators.size() > 0 ||
            analysis.prop_decorators.size() > 0 ||
            analysis.must_lower_field ||
            analysis.static_field_to_block_assign ||
            analysis.rewrite_auto_accessor_to_get_set)) {
        analysis.is_computed_property_copied_or_moved = true;

        // Determine if we don't actually need the value of the key (only the side
        // effects). In that case we don't need a temporary variable.
        if (analysis.prop_experimental_decorators.empty() &&
            analysis.prop_decorators.empty() &&
            !analysis.rewrite_auto_accessor_to_get_set &&
            analysis.should_omit_field_initializer) {
            analysis.needs_value_of_key = false;
        }
    }
    return analysis;
}

std::pair<std::unordered_map<int, compiler::Ref>, std::unordered_map<int, compiler::Ref>>
lowerClassContext::hoistComputedProperties(Parser *p, classLoweringInfo classLoweringInfo) {
    std::unordered_map<int, compiler::Ref> propertyKeyTempRefs;
    std::unordered_map<int, compiler::Ref> decoratorTempRefs;
    Expr *nextComputedPropertyKey = nullptr;

    // Computed property keys must be evaluated in a specific order for their
    // side effects. This order must be preserved even when we have to move a
    // class element around. For example, this can happen when using class fields
    // with computed property keys and targeting environments without class field
    // support. For example:
    //
    //   class Foo {
    //     [a()]() {}
    //     static [b()] = null;
    //     [c()]() {}
    //   }
    //
    // If we need to lower the static field because static fields aren't supported,
    // we still need to ensure that "b()" is called before "a()" and after "c()".
    // That looks something like this:
    //
    //   var _a;
    //   class Foo {
    //     [a()]() {}
    //     [(_a = b(), c())]() {}
    //   }
    //   __publicField(Foo, _a, null);
    //
    // Iterate in reverse so that any initializers are "pushed up" before the
    // class body if there's nowhere else to put them. They can't be "pushed
    // down" into a static block in the class body (the logical place to put
    // them that's next in the evaluation order) because these expressions
    // may contain "await" and static blocks do not allow "await".
    for (int propIndex = static_cast<int>(this->class_->properties.size()) - 1; propIndex >= 0; propIndex--) {
        Property &prop = this->class_->properties[static_cast<size_t>(propIndex)];
        propertyAnalysis analysis = this->analyzeProperty(p, prop, classLoweringInfo);

        // Evaluate the decorator expressions inline before computed property keys
        Expr decorators;
        if (!analysis.prop_decorators.empty()) {
            std::string name = p->propertyNameHint(prop.key);
            if (!name.empty()) {
                name = "_" + name;
            }
            name += "_dec";
            compiler::Ref ref = p->generateTempRef(tempRefNeedsDeclare, name);
            std::vector<Expr> values;
            for (Decorator &decorator : analysis.prop_decorators) {
                values.push_back(decorator.value);
            }
            logger::Loc atLoc = analysis.prop_decorators[0].at_loc;
            auto array = std::make_shared<EArray>();
            array->items = values;
            array->is_single_line = true;
            decorators = Assign(
                Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), atLoc},
                Expr{array, atLoc});
            p->recordUsage(ref);
            decoratorTempRefs[propIndex] = ref;
        }

        // Skip property keys that we know are side-effect free
        if (Get<EString>(prop.key.data) != nullptr || Get<ENameOfSymbol>(prop.key.data) != nullptr ||
            Get<ENumber>(prop.key.data) != nullptr || Get<EPrivateIdentifier>(prop.key.data) != nullptr) {
            // Figure out where to stick the decorator side effects to preserve their order
            if (nextComputedPropertyKey != nullptr) {
                // Insert it before everything that comes after it
                *nextComputedPropertyKey = JoinWithComma(decorators, *nextComputedPropertyKey);
            } else {
                // Insert it after the first thing that comes before it
                this->computed_property_chain = JoinWithComma(decorators, this->computed_property_chain);
            }
            continue;
        }

        // Otherwise, evaluate the decorators right before the property key
        if (!IsNil(decorators.data)) {
            prop.key = JoinWithComma(decorators, prop.key);
            prop.flags |= PropertyFlags::kIsComputed;
        }

        // If this key is referenced elsewhere, make sure to still preserve
        // its side effects in the property's original location
        if (analysis.is_computed_property_copied_or_moved) {
            // If this property is being duplicated instead of moved or removed, then
            // we still need the assignment to the temporary so that we can reference
            // it in multiple places, but we don't have to hoist the assignment to an
            // earlier property (since this property is still there). In that case
            // we can reduce generated code size by avoiding the hoist. One example
            // of this case is a decorator on a class element with a computed
            // property key:
            //
            //   class Foo {
            //     @dec [a()]() {}
            //   }
            //
            // We want to do this:
            //
            //   var _a;
            //   class Foo {
            //     [_a = a()]() {}
            //   }
            //   __decorateClass([dec], Foo.prototype, _a, 1);
            //
            // instead of this:
            //
            //   var _a;
            //   _a = a();
            //   class Foo {
            //     [_a]() {}
            //   }
            //   __decorateClass([dec], Foo.prototype, _a, 1);
            //
            // So only do the hoist if this property is being moved or removed.
            if (!analysis.rewrite_auto_accessor_to_get_set &&
                (analysis.must_lower_field || analysis.static_field_to_block_assign)) {
                Expr inlineKey = prop.key;

                if (analysis.needs_value_of_key) {
                    // Store the key in a temporary so we can refer to it later
                    compiler::Ref ref = p->generateTempRef(tempRefNeedsDeclare, "");
                    inlineKey = Assign(
                        Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), prop.key.loc}, prop.key);
                    p->recordUsage(ref);

                    // Replace this property key with a reference to the temporary. We
                    // don't need to store the temporary in the "propertyKeyTempRefs"
                    // map because all references will refer to the temporary, not just
                    // some of them.
                    prop.key = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), prop.key.loc};
                    p->recordUsage(ref);
                }

                // Figure out where to stick this property's side effect to preserve its order
                if (nextComputedPropertyKey != nullptr) {
                    // Insert it before everything that comes after it
                    *nextComputedPropertyKey = JoinWithComma(inlineKey, *nextComputedPropertyKey);
                } else {
                    // Insert it after the first thing that comes before it
                    this->computed_property_chain = JoinWithComma(inlineKey, this->computed_property_chain);
                }
                continue;
            }

            // Otherwise, we keep the side effects in place (as described above) but
            // just store the key in a temporary so we can refer to it later.
            compiler::Ref ref = p->generateTempRef(tempRefNeedsDeclare, "");
            prop.key = Assign(
                Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), prop.key.loc}, prop.key);
            p->recordUsage(ref);

            // Use this temporary when creating duplicate references to this key
            propertyKeyTempRefs[propIndex] = ref;

            // Deliberately continue to fall through to the "computed" case below:
        }

        // Otherwise, this computed property could be a good location to evaluate
        // something that comes before it. Remember this location for later.
        if (Has(prop.flags, PropertyFlags::kIsComputed)) {
            // If any side effects after this were hoisted here, then inline them now.
            // We don't want to reorder any side effects.
            if (!IsNil(this->computed_property_chain.data)) {
                auto it = propertyKeyTempRefs.find(propIndex);
                compiler::Ref ref;
                if (it != propertyKeyTempRefs.end()) {
                    ref = it->second;
                } else {
                    ref = p->generateTempRef(tempRefNeedsDeclare, "");
                    prop.key = Assign(
                        Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), prop.key.loc}, prop.key);
                    p->recordUsage(ref);
                }
                prop.key = JoinWithComma(
                    JoinWithComma(prop.key, this->computed_property_chain),
                    Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), prop.key.loc});
                p->recordUsage(ref);
                this->computed_property_chain = Expr{};
            }

            // Remember this location for later
            nextComputedPropertyKey = &prop.key;
        }
    }

    // If any side effects in the class body were hoisted up to the "extends"
    // clause, then inline them before the "extends" clause is evaluated. We
    // don't want to reorder any side effects. For example:
    //
    //   class Foo extends a() {
    //     static [b()]
    //   }
    //
    // We want to do this:
    //
    //   var _a, _b;
    //   class Foo extends (_b = a(), _a = b(), _b) {
    //   }
    //   __publicField(Foo, _a);
    //
    // instead of this:
    //
    //   var _a;
    //   _a = b();
    //   class Foo extends a() {
    //   }
    //   __publicField(Foo, _a);
    //
    if (!IsNil(this->computed_property_chain.data) && !IsNil(this->class_->extends_or_nil.data)) {
        this->extends_ref = p->generateTempRef(tempRefNeedsDeclare, "");
        this->class_->extends_or_nil = JoinWithComma(JoinWithComma(
            Assign(Expr{std::make_shared<EIdentifier>(EIdentifier{this->extends_ref}),
                this->class_->extends_or_nil.loc}, this->class_->extends_or_nil),
            this->computed_property_chain),
            Expr{std::make_shared<EIdentifier>(EIdentifier{this->extends_ref}),
                this->class_->extends_or_nil.loc});
        p->recordUsage(this->extends_ref);
        p->recordUsage(this->extends_ref);
        this->computed_property_chain = Expr{};
    }

    return {std::move(propertyKeyTempRefs), std::move(decoratorTempRefs)};
}

void lowerClassContext::processProperties(Parser *p, classLoweringInfo classLoweringInfo,
    const visitClassResult &result) {
    std::vector<Property> properties;
    properties.reserve(this->class_->properties.size());

    std::pair<std::unordered_map<int, compiler::Ref>, std::unordered_map<int, compiler::Ref>> hoisted =
        this->hoistComputedProperties(p, classLoweringInfo);
    std::unordered_map<int, compiler::Ref> &propertyKeyTempRefs = hoisted.first;
    std::unordered_map<int, compiler::Ref> &decoratorTempRefs = hoisted.second;

    // Save the initializer index for each field and accessor element
    if (this->class_->should_lower_standard_decorators) {
        int counts[4] = {0, 0, 0, 0};

        // Count how many initializers there are in each section
        for (Property &prop : this->class_->properties) {
            if (!prop.decorators.empty()) {
                if (std::pair<int, bool> order = fieldOrAccessorOrder(prop.kind, prop.flags); order.second) {
                    counts[order.first]++;
                } else if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                    this->decorator_call_static_method_extra_initializers = true;
                } else {
                    this->decorator_call_instance_method_extra_initializers = true;
                }
            }
        }

        // Give each on an index for the order it will be initialized in
        if (counts[0] > 0 || counts[1] > 0 || counts[2] > 0 || counts[3] > 0) {
            int indices[4] = {0, counts[0], counts[0] + counts[1], counts[0] + counts[1] + counts[2]};
            this->decorator_property_to_initializer_map.clear();

            for (size_t propIndex = 0; propIndex < this->class_->properties.size(); propIndex++) {
        Property &prop = this->class_->properties[static_cast<size_t>(propIndex)];
                if (!prop.decorators.empty()) {
                    if (std::pair<int, bool> order = fieldOrAccessorOrder(prop.kind, prop.flags); order.second) {
                        this->decorator_property_to_initializer_map[static_cast<int>(propIndex)] = indices[order.first];
                        indices[order.first]++;
                    }
                }
            }
        }
    }

    // Evaluate the decorator expressions inline
    if (this->class_->should_lower_standard_decorators && !this->class_->decorators.empty()) {
        std::string name = this->name_to_keep;
        if (name.empty()) {
            name = "class";
        }
        compiler::Ref decoratorsRef = p->generateTempRef(tempRefNeedsDeclare, "_" + name + "_decorators");
        std::vector<Expr> values;
        for (Decorator &decorator : this->class_->decorators) {
            values.push_back(decorator.value);
        }
        logger::Loc atLoc = this->class_->decorators[0].at_loc;
        auto array = std::make_shared<EArray>();
        array->items = values;
        array->is_single_line = true;
        this->computed_property_chain = JoinWithComma(Assign(
            Expr{std::make_shared<EIdentifier>(EIdentifier{decoratorsRef}), atLoc},
            Expr{array, atLoc}),
            this->computed_property_chain);
        p->recordUsage(decoratorsRef);
        this->decorator_class_decorators = Expr{std::make_shared<EIdentifier>(EIdentifier{decoratorsRef}), atLoc};
        p->recordUsage(decoratorsRef);
        this->class_->decorators.clear();
    }

    for (size_t propIndex = 0; propIndex < this->class_->properties.size(); propIndex++) {
        Property prop = this->class_->properties[propIndex];

        if (prop.kind == PropertyKind::kClassStaticBlock) {
            // Drop empty class blocks when minifying
            if (p->options.optionsThatSupportStructuralEquality.minifySyntax &&
                prop.class_static_block->block.stmts.empty()) {
                continue;
            }

            // Lower this block if needed
            if (classLoweringInfo.lowerAllStaticFields) {
                this->lowerStaticBlock(p, prop.loc, *prop.class_static_block);
                continue;
            }

            // Otherwise, keep this property
            properties.push_back(prop);
            continue;
        }

        // Merge parameter decorators with method decorators
        if (p->options.optionsThatSupportStructuralEquality.ts.Parse && IsMethodDefinition(prop.kind)) {
            if (auto fn = Get<EFunction>(prop.value_or_nil.data); fn != nullptr) {
                bool isConstructor = false;
                if (auto key = Get<EString>(prop.key.data); key != nullptr) {
                    isConstructor = helpers::UTF16EqualsString(key->value, "constructor");
                }
                std::vector<Arg> &args = fn->fn.args;
                for (size_t i = 0; i < args.size(); i++) {
                    // Note: Don't modify "args[i].decorators" until after iterating
                    // over it below. Modifying a vector invalidates iterators.
                    for (Decorator &decorator : args[i].decorators) {
                        // Generate a call to "__decorateParam()" for this parameter decorator
                        std::vector<Decorator> *decorators = &prop.decorators;
                        if (isConstructor) {
                            decorators = &this->class_->decorators;
                        }
                        std::vector<Expr> callArgs;
                        callArgs.push_back(Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(i)}),
                            decorator.value.loc});
                        callArgs.push_back(decorator.value);
                        Decorator d;
                        d.value = p->callRuntime(decorator.value.loc, "__decorateParam", callArgs);
                        d.at_loc = decorator.at_loc;
                        decorators->push_back(d);
                    }
                    args[i].decorators.clear();
                }
            }
        }

        propertyAnalysis analysis = this->analyzeProperty(p, prop, classLoweringInfo);

        // When the property key needs to be referenced multiple times, subsequent
        // references may need to reference a temporary variable instead of copying
        // the whole property key expression (since we only want to evaluate side
        // effects once).
        Expr keyExprNoSideEffects = prop.key;
        if (auto it = propertyKeyTempRefs.find(static_cast<int>(propIndex)); it != propertyKeyTempRefs.end()) {
            keyExprNoSideEffects.data = std::make_shared<EIdentifier>(EIdentifier{it->second});
        }

        // Handle TypeScript experimental decorators
        if (!analysis.prop_experimental_decorators.empty()) {
            prop.decorators.clear();

            // Generate a single call to "__decorateClass()" for this property
            logger::Loc loc = prop.key.loc;

            // This code tells "__decorateClass()" if the descriptor should be undefined
            double descriptorKind = 1;
            if (prop.kind == PropertyKind::kField || prop.kind == PropertyKind::kDeclareOrAbstract) {
                descriptorKind = 2;
            }

            // Instance properties use the prototype, static properties use the class
            Expr target;
            if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                target = this->name_func();
            } else {
                auto dot = std::make_shared<EDot>();
                dot->target = this->name_func();
                dot->name = "prototype";
                dot->name_loc = loc;
                target = Expr{dot, loc};
            }

            std::vector<Expr> values;
            for (Decorator &decorator : analysis.prop_experimental_decorators) {
                values.push_back(decorator.value);
            }
            auto array = std::make_shared<EArray>();
            array->items = values;
            std::vector<Expr> callArgs;
            callArgs.push_back(Expr{array, loc});
            callArgs.push_back(target);
            callArgs.push_back(cloneKeyForLowerClass(keyExprNoSideEffects));
            callArgs.push_back(Expr{std::make_shared<ENumber>(ENumber{descriptorKind}), loc});
            Expr decoratorCall = p->callRuntime(loc, "__decorateClass", callArgs);

            // Static decorators are grouped after instance decorators
            if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                this->static_experimental_decorators.push_back(decoratorCall);
            } else {
                this->instance_experimental_decorators.push_back(decoratorCall);
            }
        }

        // Handle JavaScript decorators
        int initializerIndex = -1;
        if (!analysis.prop_decorators.empty()) {
            prop.decorators.clear();
            logger::Loc loc = prop.loc;
            logger::Loc keyLoc = prop.key.loc;
            logger::Loc atLoc = analysis.prop_decorators[0].at_loc;

            // Encode information about this property using bit flags
            int flags = 0;
            switch (prop.kind) {
            case PropertyKind::kMethod:
                flags = 1;
                break;
            case PropertyKind::kGetter:
                flags = 2;
                break;
            case PropertyKind::kSetter:
                flags = 3;
                break;
            case PropertyKind::kAutoAccessor:
                flags = 4;
                break;
            case PropertyKind::kField:
                flags = 5;
                break;
            default:
                break;
            }
            if (flags >= 4) {
                initializerIndex = this->decorator_property_to_initializer_map[static_cast<int>(propIndex)];
            }
            if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                flags |= 8;
            }
            if (analysis.private_member != nullptr) {
                flags |= 16;
            }

            // Start the arguments for the call to "__decorateElement"
            Expr key;
            compiler::Ref decoratorsRef = decoratorTempRefs[static_cast<int>(propIndex)];
            if (this->decorator_context_ref == compiler::kInvalidRef) {
                this->decorator_context_ref = p->generateTempRef(tempRefNeedsDeclare, "_init");
            }
            if (analysis.private_member != nullptr) {
                key = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(
                    p->symbols[analysis.private_member->ref.inner_index].original_name), logger::Loc{}}), loc};
            } else {
                key = cloneKeyForLowerClass(keyExprNoSideEffects);
            }
            std::vector<Expr> args;
            args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{this->decorator_context_ref}), loc});
            args.push_back(Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(flags)}), loc});
            args.push_back(key);
            args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{decoratorsRef}), atLoc});
            p->recordUsage(this->decorator_context_ref);
            p->recordUsage(decoratorsRef);

            // Append any optional additional arguments
            compiler::Ref privateFnRef = compiler::kInvalidRef;
            if (analysis.private_member != nullptr) {
                // Add the "target" argument (the weak set)
                args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{analysis.private_member->ref}), keyLoc});
                p->recordUsage(analysis.private_member->ref);

                // Add the "extra" argument (the function)
                switch (prop.kind) {
                case PropertyKind::kMethod:
                case PropertyKind::kGetter:
                    privateFnRef = p->privateGetters[analysis.private_member->ref];
                    break;
                case PropertyKind::kSetter:
                    privateFnRef = p->privateSetters[analysis.private_member->ref];
                    break;
                default:
                    break;
                }
                if (privateFnRef != compiler::kInvalidRef) {
                    args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{privateFnRef}), keyLoc});
                    p->recordUsage(privateFnRef);
                }
            } else {
                // Add the "target" argument (the class object)
                args.push_back(this->name_func());
            }

            // Auto-accessors will generate a private field for storage. Lower this
            // field, which will generate a WeakMap instance, and then pass the
            // WeakMap instance into the decorator helper so the lowered getter and
            // setter can use it.
            if (prop.kind == PropertyKind::kAutoAccessor) {
                compiler::SymbolKind storageKind;
                if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                    storageKind = compiler::SymbolKind::kPrivateStaticField;
                } else {
                    storageKind = compiler::SymbolKind::kPrivateField;
                }
                compiler::Ref ref = p->newSymbol(storageKind, "#" + p->propertyNameHint(prop.key));
                p->symbols[ref.inner_index].flags = p->symbols[ref.inner_index].flags | compiler::SymbolFlags::kPrivateSymbolMustBeLowered;
                std::shared_ptr<EPrivateIdentifier> autoAccessorPrivate =
                    std::make_shared<EPrivateIdentifier>(EPrivateIdentifier{ref});
                std::tuple<Property, compiler::Ref, bool> lowered =
                    this->lowerField(p, prop, autoAccessorPrivate.get(), false, false, initializerIndex);
                args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{std::get<1>(lowered)}), keyLoc});
                p->recordUsage(std::get<1>(lowered));
            }

            // Assign the result
            Expr element = p->callRuntime(loc, "__decorateElement", args);
            if (privateFnRef != compiler::kInvalidRef) {
                element = Assign(Expr{std::make_shared<EIdentifier>(EIdentifier{privateFnRef}), keyLoc}, element);
                p->recordUsage(privateFnRef);
            } else if (prop.kind == PropertyKind::kAutoAccessor && analysis.private_member != nullptr) {
                compiler::Ref ref = p->generateTempRef(tempRefNeedsDeclare, "");
                compiler::Ref privateGetFnRef = p->generateTempRef(tempRefNeedsDeclare, "_");
                compiler::Ref privateSetFnRef = p->generateTempRef(tempRefNeedsDeclare, "_");
                p->symbols[privateGetFnRef.inner_index].link = p->privateGetters[analysis.private_member->ref];
                p->symbols[privateSetFnRef.inner_index].link = p->privateSetters[analysis.private_member->ref];

                // Unpack the "get" and "set" properties from the returned property descriptor
                auto dotGet = std::make_shared<EDot>();
                dotGet->target = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), loc};
                dotGet->name = "get";
                dotGet->name_loc = loc;
                auto dotSet = std::make_shared<EDot>();
                dotSet->target = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), loc};
                dotSet->name = "set";
                dotSet->name_loc = loc;
                element = JoinWithComma(JoinWithComma(
                    Assign(Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), loc}, element),
                    Assign(Expr{std::make_shared<EIdentifier>(EIdentifier{privateGetFnRef}), keyLoc},
                        Expr{dotGet, loc})),
                    Assign(Expr{std::make_shared<EIdentifier>(EIdentifier{privateSetFnRef}), keyLoc},
                        Expr{dotSet, loc}));
                p->recordUsage(ref);
                p->recordUsage(privateGetFnRef);
                p->recordUsage(ref);
                p->recordUsage(privateSetFnRef);
                p->recordUsage(ref);
            }

            // Put the call to the decorators in the right place
            if (prop.kind == PropertyKind::kField) {
                // Field
                if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                    this->decorator_static_field_elements.push_back(element);
                } else {
                    this->decorator_instance_field_elements.push_back(element);
                }
            } else {
                // Non-field
                if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                    this->decorator_static_non_field_elements.push_back(element);
                } else {
                    this->decorator_instance_non_field_elements.push_back(element);
                }
            }

            // Omit decorated auto-accessors as they will be now generated at run-time instead
            if (prop.kind == PropertyKind::kAutoAccessor) {
                if (analysis.private_member != nullptr) {
                    this->lowerPrivateMethod(p, prop, analysis.private_member);
                }
                continue;
            }
        }

        // Generate get/set methods for auto-accessors
        if (analysis.rewrite_auto_accessor_to_get_set) {
            properties = this->rewriteAutoAccessorToGetSet(p, prop, std::move(properties),
                keyExprNoSideEffects, analysis.must_lower_field, analysis.private_member, result);
            continue;
        }

        // Lower fields
        if ((!IsMethodDefinition(prop.kind) && analysis.must_lower_field) || analysis.static_field_to_block_assign) {
            bool keep = false;
            std::tuple<Property, compiler::Ref, bool> lowered = this->lowerField(p, prop,
                analysis.private_member, analysis.should_omit_field_initializer,
                analysis.static_field_to_block_assign, initializerIndex);
            prop = std::get<0>(lowered);
            keep = std::get<2>(lowered);
            if (!keep) {
                continue;
            }
        }

        // Lower methods
        if (IsMethodDefinition(prop.kind) && this->lowerMethod(p, prop, analysis.private_member)) {
            continue;
        }

        // Keep this property
        properties.push_back(prop);
    }

    // Finish the filtering operation
    this->class_->properties = std::move(properties);
}

void lowerClassContext::lowerStaticBlock(Parser *p, logger::Loc loc, const ClassStaticBlock &block) {
    bool isAllExprs = true;
    std::vector<Expr> allExprs;

    // Are all statements in the block expression statements?
    for (const Stmt &stmt : block.block.stmts) {
        if (Get<SEmpty>(stmt.data) != nullptr) {
            // Omit stray semicolons completely
        } else if (auto s = Get<SExpr>(stmt.data); s != nullptr) {
            allExprs.push_back(s->value);
        } else {
            isAllExprs = false;
            break;
        }
    }

    if (isAllExprs) {
        // I think it should be safe to inline the static block IIFE here
        // since all uses of "this" should have already been replaced by now.
        this->static_members.insert(this->static_members.end(), allExprs.begin(), allExprs.end());
    } else {
        // But if there is a non-expression statement, fall back to using an
        // IIFE since we may be in an expression context and can't use a block.
        auto arrow = std::make_shared<EArrow>();
        arrow->body.loc = block.loc;
        arrow->body.block = block.block;
        auto call = std::make_shared<ECall>();
        call->target = Expr{arrow, loc};
        call->can_be_unwrapped_if_unused =
            p->astHelpers.StmtsCanBeRemovedIfUnused(block.block.stmts, StmtsCanBeRemovedIfUnusedFlags::kNone);
        this->static_members.push_back(Expr{call, loc});
    }
}

std::vector<Property> lowerClassContext::rewriteAutoAccessorToGetSet(Parser *p, Property prop,
    std::vector<Property> properties, Expr keyExprNoSideEffects, bool mustLowerField,
    EPrivateIdentifier *private_member, const visitClassResult &result) {
    compiler::SymbolKind storageKind;
    if (Has(prop.flags, PropertyFlags::kIsStatic)) {
        storageKind = compiler::SymbolKind::kPrivateStaticField;
    } else {
        storageKind = compiler::SymbolKind::kPrivateField;
    }

    // Generate the name of the private field to use for storage
    std::string storageName;
    if (auto kStr = Get<EString>(keyExprNoSideEffects.data); kStr != nullptr) {
        storageName = "#" + helpers::UTF16ToString(kStr->value);
    } else if (auto kPriv = Get<EPrivateIdentifier>(keyExprNoSideEffects.data); kPriv != nullptr) {
        storageName = "#_" + p->symbols[kPriv->ref.inner_index].original_name.substr(1);
    } else {
        storageName = "#" + compiler::kDefaultNameMinifierJS.NumberToMinifiedName(this->auto_accessor_count);
        this->auto_accessor_count++;
    }

    // Generate the symbols we need
    compiler::Ref storageRef = p->newSymbol(storageKind, storageName);
    compiler::Ref argRef = p->newSymbol(compiler::SymbolKind::kOther, "_");
    result.bodyScope->generated.push_back(storageRef);
    Scope *scope = new Scope();
    scope->kind = ScopeKind::kFunctionBody;
    scope->generated.push_back(argRef);
    result.bodyScope->children.push_back(scope);

    // Replace this accessor with other properties
    logger::Loc loc = keyExprNoSideEffects.loc;
    std::shared_ptr<EPrivateIdentifier> storagePrivate = std::make_shared<EPrivateIdentifier>(EPrivateIdentifier{storageRef});
    if (mustLowerField) {
        // Forward the accessor's lowering status on to the storage field. If we
        // don't do this, then we risk having the underlying private symbol
        // behaving differently than if it were authored manually (e.g. being
        // placed outside of the class body, which is a syntax error).
        p->symbols[storageRef.inner_index].flags = p->symbols[storageRef.inner_index].flags | compiler::SymbolFlags::kPrivateSymbolMustBeLowered;
    }
    bool storageNeedsToBeLowered = p->privateSymbolNeedsToBeLowered(storagePrivate.get());

    Property storageProp;
    storageProp.loc = prop.loc;
    storageProp.kind = PropertyKind::kField;
    storageProp.flags = prop.flags & PropertyFlags::kIsStatic;
    storageProp.key = Expr{storagePrivate, loc};
    storageProp.initializer_or_nil = prop.initializer_or_nil;

    if (!mustLowerField) {
        properties.push_back(storageProp);
    } else {
        std::tuple<Property, compiler::Ref, bool> lowered =
            this->lowerField(p, storageProp, storagePrivate.get(), false, false, -1);
        if (std::get<2>(lowered)) {
            properties.push_back(std::get<0>(lowered));
        }
    }

    // Getter
    Expr getExpr;
    if (storageNeedsToBeLowered) {
        getExpr = p->lowerPrivateGet(Expr{kEThisShared, loc}, loc, storagePrivate.get());
    } else {
        p->recordUsage(storageRef);
        auto index = std::make_shared<EIndex>();
        index->target = Expr{kEThisShared, loc};
        index->index = Expr{std::make_shared<EPrivateIdentifier>(EPrivateIdentifier{storageRef}), loc};
        getExpr = Expr{index, loc};
    }

    Property getterProp;
    getterProp.loc = prop.loc;
    getterProp.kind = PropertyKind::kGetter;
    getterProp.flags = prop.flags;
    getterProp.key = prop.key;
    {
        auto fn = std::make_shared<EFunction>();
        fn->fn.body.loc = loc;
        fn->fn.body.block.stmts.push_back(Stmt{std::make_shared<SReturn>(SReturn{getExpr}), loc});
        getterProp.value_or_nil = Expr{fn, loc};
    }
    if (!this->lowerMethod(p, getterProp, private_member)) {
        properties.push_back(getterProp);
    }

    // Setter
    Expr setExpr;
    if (storageNeedsToBeLowered) {
        setExpr = p->lowerPrivateSet(Expr{kEThisShared, loc}, loc, storagePrivate.get(),
            Expr{std::make_shared<EIdentifier>(EIdentifier{argRef}), loc});
    } else {
        p->recordUsage(storageRef);
        p->recordUsage(argRef);
        auto index = std::make_shared<EIndex>();
        index->target = Expr{kEThisShared, loc};
        index->index = Expr{std::make_shared<EPrivateIdentifier>(EPrivateIdentifier{storageRef}), loc};
        setExpr = Assign(Expr{index, loc}, Expr{std::make_shared<EIdentifier>(EIdentifier{argRef}), loc});
    }

    Property setterProp;
    setterProp.loc = prop.loc;
    setterProp.kind = PropertyKind::kSetter;
    setterProp.flags = prop.flags;
    setterProp.key = cloneKeyForLowerClass(keyExprNoSideEffects);
    {
        auto fn = std::make_shared<EFunction>();
        Arg arg;
        arg.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{argRef}), loc};
        fn->fn.args.push_back(arg);
        fn->fn.body.loc = loc;
        fn->fn.body.block.stmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{setExpr}), loc});
        setterProp.value_or_nil = Expr{fn, loc};
    }
    if (!this->lowerMethod(p, setterProp, private_member)) {
        properties.push_back(setterProp);
    }
    return properties;
}

void lowerClassContext::insertInitializersIntoConstructor(Parser *p, classLoweringInfo classLoweringInfo,
    const visitClassResult &result) {
    if (!this->parameter_field_props.empty()) {
        this->class_->properties.insert(this->class_->properties.begin(),
            this->parameter_field_props.begin(), this->parameter_field_props.end());
    }

    if (this->parameter_fields.empty() &&
        !this->decorator_call_instance_method_extra_initializers &&
        this->instance_private_methods.empty() &&
        this->instance_members.empty() &&
        (this->ctor == nullptr || result.superCtorRef == compiler::kInvalidRef)) {
        // No need to generate a constructor
        return;
    }

    // Create a constructor if one doesn't already exist
    if (this->ctor == nullptr) {
        this->ctor = new EFunction();
        this->ctor->fn.body.loc = this->class_loc;

        // Append it to the list to reuse existing allocation space
        std::shared_ptr<EFunction> ctorShared(this->ctor);
        Property ctorProp;
        ctorProp.kind = PropertyKind::kMethod;
        ctorProp.loc = this->class_loc;
        ctorProp.key = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16("constructor"), logger::Loc{}}),
            this->class_loc};
        ctorProp.value_or_nil = Expr{ctorShared, this->class_loc};
        this->class_->properties.push_back(ctorProp);

        // Make sure the constructor has a super() call if needed
        if (!IsNil(this->class_->extends_or_nil.data)) {
            Expr target{kESuperShared, this->class_loc};
            if (classLoweringInfo.shimSuperCtorCalls) {
                p->recordUsage(result.superCtorRef);
                target.data = std::make_shared<EIdentifier>(EIdentifier{result.superCtorRef});
            }
            compiler::Ref argumentsRef = p->newSymbol(compiler::SymbolKind::kUnbound, "arguments");
            p->currentScope->generated.push_back(argumentsRef);
            std::vector<Expr> callArgs;
            callArgs.push_back(Expr{std::make_shared<ESpread>(ESpread{
                Expr{std::make_shared<EIdentifier>(EIdentifier{argumentsRef}), this->class_loc}}), this->class_loc});
            auto call = std::make_shared<ECall>();
            call->target = target;
            call->args = callArgs;
            this->ctor->fn.body.block.stmts.push_back(
                Stmt{std::make_shared<SExpr>(SExpr{Expr{call, this->class_loc}}), this->class_loc});
        }
    }

    // Run instanceMethodExtraInitializers if needed
    Expr decoratorInstanceMethodExtraInitializers;
    if (this->decorator_call_instance_method_extra_initializers) {
        std::vector<Expr> args;
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{this->decorator_context_ref}), this->class_loc});
        args.push_back(Expr{std::make_shared<ENumber>(ENumber{static_cast<double>((2 << 1) | 1)}), this->class_loc});
        args.push_back(Expr{kEThisShared, this->class_loc});
        decoratorInstanceMethodExtraInitializers = p->callRuntime(this->class_loc, "__runInitializers", args);
        p->recordUsage(this->decorator_context_ref);
    }

    // Make sure the instance field initializers come after "super()" since
    // they need "this" to ba available
    std::vector<Stmt> generatedStmts;
    generatedStmts.reserve(this->parameter_fields.size() +
        this->instance_private_methods.size() +
        this->instance_members.size());
    generatedStmts.insert(generatedStmts.end(), this->parameter_fields.begin(), this->parameter_fields.end());
    if (!IsNil(decoratorInstanceMethodExtraInitializers.data)) {
        generatedStmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{decoratorInstanceMethodExtraInitializers}),
            decoratorInstanceMethodExtraInitializers.loc});
    }
    generatedStmts.insert(generatedStmts.end(), this->instance_private_methods.begin(),
        this->instance_private_methods.end());
    generatedStmts.insert(generatedStmts.end(), this->instance_members.begin(), this->instance_members.end());
    p->insertStmtsAfterSuperCall(&this->ctor->fn.body, generatedStmts, result.superCtorRef);

    // Sort the constructor first to match the TypeScript compiler's output
    for (size_t i = 0; i < this->class_->properties.size(); i++) {
        if (Get<EFunction>(this->class_->properties[i].value_or_nil.data) == this->ctor) {
            Property ctorProp = this->class_->properties[i];
            for (size_t j = i; j > 0; j--) {
                this->class_->properties[j] = this->class_->properties[j - 1];
            }
            this->class_->properties[0] = ctorProp;
            break;
        }
    }
}

std::pair<std::vector<Stmt>, Expr> lowerClassContext::finishAndGenerateCode(Parser *p,
    const visitClassResult &result) {
    // When bundling is enabled, we convert top-level class statements to
    // expressions:
    //
    //   // Before
    //   class Foo {
    //     static foo = () => Foo
    //   }
    //   Foo = wrap(Foo)
    //
    //   // After
    //   var _Foo = class _Foo {
    //     static foo = () => _Foo;
    //   };
    //   var Foo = _Foo;
    //   Foo = wrap(Foo);
    //
    // One reason to do this is that guchho's bundler sometimes needs to lazily-
    // evaluate a module. For example, a module may end up being both the target
    // of a dynamic "import()" call and a static "import" statement. Lazy module
    // evaluation is done by wrapping the top-level module code in a closure. To
    // avoid a performance hit for static "import" statements, guchho stores
    // top-level exported symbols outside of the closure and references them
    // directly instead of indirectly.
    //
    // Another reason to do this is that multiple JavaScript VMs have had and
    // continue to have performance issues with TDZ (i.e. "temporal dead zone")
    // checks. These checks validate that a let, or const, or class symbol isn't
    // used before it's initialized. Here are two issues with well-known VMs:
    //
    //   * V8: https://bugs.chromium.org/p/v8/issues/detail?id=13723 (10% slowdown)
    //   * JavaScriptCore: https://bugs.webkit.org/show_bug.cgi?id=199866 (1,000% slowdown!)
    //
    // JavaScriptCore had a severe performance issue as their TDZ implementation
    // had time complexity that was quadratic in the number of variables needing
    // TDZ checks in the same scope (with the top-level scope typically being the
    // worst offender). V8 has ongoing issues with TDZ checks being present
    // throughout the code their JIT generates even when they have already been
    // checked earlier in the same function or when the function in question has
    // already been run (so the checks have already happened).
    //
    // Due to guchho's parallel architecture, we both a) need to transform class
    // statements to variables during parsing and b) don't yet know whether this
    // module will need to be lazily-evaluated or not in the parser. So we always
    // do this just in case it's needed.
    bool mustConvertStmtToExpr = this->kind != classKindExpr && p->currentScope->parent == nullptr &&
        (p->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle ||
            p->willWrapModuleInTryCatchForUsing);

    // Check to see if we have lowered decorators on the class itself
    Expr classDecorators;
    std::vector<Decorator> classExperimentalDecorators;
    if (p->options.optionsThatSupportStructuralEquality.ts.Parse &&
        p->options.optionsThatSupportStructuralEquality.ts.Config.ExperimentalDecorators == config::MaybeBool::kTrue) {
        classExperimentalDecorators = this->class_->decorators;
        this->class_->decorators.clear();
    } else if (this->class_->should_lower_standard_decorators) {
        classDecorators = this->decorator_class_decorators;
    }

    Expr decorateClassExpr;
    if (!IsNil(classDecorators.data)) {
        // Handle JavaScript decorators on the class itself
        if (this->decorator_context_ref == compiler::kInvalidRef) {
            this->decorator_context_ref = p->generateTempRef(tempRefNeedsDeclare, "_init");
        }
        std::vector<Expr> args;
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{this->decorator_context_ref}), this->class_loc});
        args.push_back(Expr{std::make_shared<ENumber>(ENumber{0}), this->class_loc});
        args.push_back(Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(this->name_to_keep), logger::Loc{}}),
            this->class_loc});
        args.push_back(classDecorators);
        args.push_back(this->name_func());
        decorateClassExpr = p->callRuntime(this->class_loc, "__decorateElement", args);
        p->recordUsage(this->decorator_context_ref);
        decorateClassExpr = Assign(this->name_func(), decorateClassExpr);
    } else if (this->decorator_context_ref != compiler::kInvalidRef) {
        // Decorator metadata is present if there are any decorators on the class at all
        std::vector<Expr> args;
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{this->decorator_context_ref}), this->class_loc});
        args.push_back(this->name_func());
        decorateClassExpr = p->callRuntime(this->class_loc, "__decoratorMetadata", args);
    }

    // If this is true, we have removed some code from the class body that could
    // potentially contain an expression that captures the inner class name.
    // In this case we must explicitly store the class to a separate inner class
    // name binding to avoid incorrect behavior if the class is later re-assigned,
    // since the removed code will no longer be in the class body scope.
    bool hasPotentialInnerClassNameEscape = result.innerClassNameRef != compiler::kInvalidRef &&
        (!IsNil(this->computed_property_chain.data) ||
            !this->private_members.empty() ||
            !this->static_private_methods.empty() ||
            !this->static_members.empty() ||

            // TypeScript experimental decorators
            !this->instance_experimental_decorators.empty() ||
            !this->static_experimental_decorators.empty() ||
            !classExperimentalDecorators.empty() ||

            // JavaScript decorators
            this->decorator_context_ref != compiler::kInvalidRef);

    // If we need to represent the class as an expression (even if it's a
    // statement), then generate another symbol to use as the class name
    compiler::LocRef nameForClassDecorators{logger::Loc{}, compiler::kInvalidRef};
    if (!classExperimentalDecorators.empty() || hasPotentialInnerClassNameEscape || mustConvertStmtToExpr) {
        if (this->kind == classKindExpr) {
            // For expressions, the inner and outer class names are the same
            Expr name = this->name_func();
            nameForClassDecorators.loc = name.loc;
            nameForClassDecorators.ref = Get<EIdentifier>(name.data)->ref;
        } else {
            // For statements we need to use the outer class name, not the inner one
            if (this->class_->name != nullptr) {
                nameForClassDecorators = *this->class_->name;
            } else if (this->kind == classKindExportDefaultStmt) {
                nameForClassDecorators = this->default_name;
            } else {
                nameForClassDecorators.loc = this->class_loc;
                nameForClassDecorators.ref = p->generateTempRef(tempRefNoDeclare, "");
            }
            p->recordUsage(nameForClassDecorators.ref);
        }
    }

    std::vector<Expr> prefixExprs;
    std::vector<Expr> suffixExprs;

    // If there are JavaScript decorators, start by allocating a context object
    if (this->decorator_context_ref != compiler::kInvalidRef) {
        Expr base{kENullShared, this->class_loc};
        if (!IsNil(this->class_->extends_or_nil.data)) {
            if (this->extends_ref == compiler::kInvalidRef) {
                this->extends_ref = p->generateTempRef(tempRefNeedsDeclare, "");
                this->class_->extends_or_nil = Assign(
                    Expr{std::make_shared<EIdentifier>(EIdentifier{this->extends_ref}),
                        this->class_->extends_or_nil.loc},
                    this->class_->extends_or_nil);
                p->recordUsage(this->extends_ref);
            }
            base.data = std::make_shared<EIdentifier>(EIdentifier{this->extends_ref});
        }
        std::vector<Expr> args;
        args.push_back(base);
        suffixExprs.push_back(Assign(
            Expr{std::make_shared<EIdentifier>(EIdentifier{this->decorator_context_ref}), this->class_loc},
            p->callRuntime(this->class_loc, "__decoratorStart", args)));
        p->recordUsage(this->decorator_context_ref);
    }

    // Any of the computed property chain that we hoisted out of the class
    // body needs to come before the class expression.
    if (!IsNil(this->computed_property_chain.data)) {
        prefixExprs.push_back(this->computed_property_chain);
    }

    // WeakSets and WeakMaps
    suffixExprs.insert(suffixExprs.end(), this->private_members.begin(), this->private_members.end());

    // Evaluate JavaScript decorators here
    suffixExprs.insert(suffixExprs.end(), this->decorator_static_non_field_elements.begin(),
        this->decorator_static_non_field_elements.end());
    suffixExprs.insert(suffixExprs.end(), this->decorator_instance_non_field_elements.begin(),
        this->decorator_instance_non_field_elements.end());
    suffixExprs.insert(suffixExprs.end(), this->decorator_static_field_elements.begin(),
        this->decorator_static_field_elements.end());
    suffixExprs.insert(suffixExprs.end(), this->decorator_instance_field_elements.begin(),
        this->decorator_instance_field_elements.end());

    // Lowered initializers for static methods (including getters and setters)
    suffixExprs.insert(suffixExprs.end(), this->static_private_methods.begin(),
        this->static_private_methods.end());

    // Run JavaScript class decorators at the end of class initialization
    if (!IsNil(decorateClassExpr.data)) {
        suffixExprs.push_back(decorateClassExpr);
    }

    // For each element initializer of staticMethodExtraInitializers
    if (this->decorator_call_static_method_extra_initializers) {
        std::vector<Expr> args;
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{this->decorator_context_ref}), this->class_loc});
        args.push_back(Expr{std::make_shared<ENumber>(ENumber{static_cast<double>((1 << 1) | 1)}), this->class_loc});
        args.push_back(this->name_func());
        suffixExprs.push_back(p->callRuntime(this->class_loc, "__runInitializers", args));
        p->recordUsage(this->decorator_context_ref);
    }

    // Lowered initializers for static fields, static accessors, and static blocks
    suffixExprs.insert(suffixExprs.end(), this->static_members.begin(), this->static_members.end());

    // The official TypeScript compiler adds generated code after the class body
    // in this exact order. Matching this order is important for correctness.
    suffixExprs.insert(suffixExprs.end(), this->instance_experimental_decorators.begin(),
        this->instance_experimental_decorators.end());
    suffixExprs.insert(suffixExprs.end(), this->static_experimental_decorators.begin(),
        this->static_experimental_decorators.end());

    // For each element initializer of classExtraInitializers
    if (!IsNil(classDecorators.data)) {
        std::vector<Expr> args;
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{this->decorator_context_ref}), this->class_loc});
        args.push_back(Expr{std::make_shared<ENumber>(ENumber{static_cast<double>((0 << 1) | 1)}), this->class_loc});
        args.push_back(this->name_func());
        suffixExprs.push_back(p->callRuntime(this->class_loc, "__runInitializers", args));
        p->recordUsage(this->decorator_context_ref);
    }

    // Run TypeScript experimental class decorators at the end of class initialization
    if (!classExperimentalDecorators.empty()) {
        std::vector<Expr> values;
        for (Decorator &decorator : classExperimentalDecorators) {
            values.push_back(decorator.value);
        }
        auto array = std::make_shared<EArray>();
        array->items = values;
        std::vector<Expr> args;
        args.push_back(Expr{array, this->class_loc});
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{nameForClassDecorators.ref}),
            nameForClassDecorators.loc});
        suffixExprs.push_back(Assign(
            Expr{std::make_shared<EIdentifier>(EIdentifier{nameForClassDecorators.ref}), nameForClassDecorators.loc},
            p->callRuntime(this->class_loc, "__decorateClass", args)));
        p->recordUsage(nameForClassDecorators.ref);
        p->recordUsage(nameForClassDecorators.ref);
    }

    // Our caller expects us to return the same form that was originally given to
    // us. If the class was originally an expression, then return an expression.
    if (this->kind == classKindExpr) {
        // Calling "nameFunc" will replace "classExpr", so make sure to do that first
        // before joining "classExpr" with any other expressions
        Expr nameToJoin;
        if (this->did_capture_class_expr || !suffixExprs.empty()) {
            nameToJoin = this->name_func();
        }

        // Insert expressions on either side of the class as appropriate
        this->class_expr = JoinWithComma(JoinAllWithComma(prefixExprs), this->class_expr);
        this->class_expr = JoinWithComma(this->class_expr, JoinAllWithComma(suffixExprs));

        // Finally join "classExpr" with the variable that holds the class object
        this->class_expr = JoinWithComma(this->class_expr, nameToJoin);
        if (this->wrap_func) {
            this->class_expr = this->wrap_func(this->class_expr);
        }
        return {std::vector<Stmt>{}, this->class_expr};
    }

    // Otherwise, the class was originally a statement. Return an array of
    // statements instead.
    std::vector<Stmt> stmts;
    Stmt outerClassNameDecl;

    // Insert expressions before the class as appropriate
    for (Expr &expr : prefixExprs) {
        stmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{expr}), expr.loc});
    }

    // Handle converting a class statement to a class expression
    if (nameForClassDecorators.ref != compiler::kInvalidRef) {
        std::shared_ptr<EClass> classExpr = std::make_shared<EClass>();
        classExpr->class_ = *this->class_;
        this->class_ = &classExpr->class_;
        Expr init{classExpr, this->class_loc};

        // If the inner class name was referenced, then set the name of the class
        // that we will end up printing to the inner class name. Otherwise if the
        // inner class name was unused, we can just leave it blank.
        if (result.innerClassNameRef != compiler::kInvalidRef) {
            // "class Foo { x = Foo }" => "const Foo = class _Foo { x = _Foo }"
            this->class_->name->ref = result.innerClassNameRef;
        } else {
            // "class Foo {}" => "const Foo = class {}"
            this->class_->name.reset();
        }

        // Generate the class initialization statement
        if (!classExperimentalDecorators.empty()) {
            // If there are class decorators, then we actually need to mutate the
            // immutable "const" binding that shadows everything in the class body.
            // The official TypeScript compiler does this by rewriting all class name
            // references in the class body to another temporary variable. This is
            // basically what we're doing here.
            p->recordUsage(nameForClassDecorators.ref);
            std::vector<Decl> decls;
            Decl decl;
            decl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{nameForClassDecorators.ref}),
                nameForClassDecorators.loc};
            decl.value_or_nil = init;
            decls.push_back(decl);
            auto local = std::make_shared<SLocal>();
            local->kind = p->selectLocalKind(LocalKind::kLet);
            local->is_export = this->kind == classKindExportStmt;
            local->decls = decls;
            stmts.push_back(Stmt{local, this->class_loc});
            if (this->class_->name != nullptr) {
                p->mergeSymbols(this->class_->name->ref, nameForClassDecorators.ref);
                this->class_->name.reset();
            }
        } else if (hasPotentialInnerClassNameEscape) {
            // If the inner class name was used, then we explicitly generate a binding
            // for it. That means the mutable outer class name is separate, and is
            // initialized after all static member initializers have finished.
            compiler::Ref captureRef = p->newSymbol(compiler::SymbolKind::kOther,
                p->symbols[result.innerClassNameRef.inner_index].original_name);
            p->currentScope->generated.push_back(captureRef);
            p->recordDeclaredSymbol(captureRef);
            p->mergeSymbols(result.innerClassNameRef, captureRef);
            LocalKind localKind = LocalKind::kConst;
            if (!IsNil(classDecorators.data)) {
                // Class decorators need to be able to potentially mutate this binding
                localKind = LocalKind::kLet;
            }
            {
                std::vector<Decl> decls;
                Decl decl;
                decl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{captureRef}),
                    nameForClassDecorators.loc};
                decl.value_or_nil = init;
                decls.push_back(decl);
                auto local = std::make_shared<SLocal>();
                local->kind = p->selectLocalKind(localKind);
                local->decls = decls;
                stmts.push_back(Stmt{local, this->class_loc});
            }
            p->recordUsage(nameForClassDecorators.ref);
            p->recordUsage(captureRef);
            std::vector<Decl> decls;
            Decl decl;
            decl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{nameForClassDecorators.ref}),
                nameForClassDecorators.loc};
            decl.value_or_nil = Expr{std::make_shared<EIdentifier>(EIdentifier{captureRef}), this->class_loc};
            decls.push_back(decl);
            auto local = std::make_shared<SLocal>();
            local->kind = p->selectLocalKind(LocalKind::kLet);
            local->is_export = this->kind == classKindExportStmt;
            local->decls = decls;
            outerClassNameDecl = Stmt{local, this->class_loc};
        } else {
            // Otherwise, the inner class name isn't needed and we can just
            // use a single variable declaration for the outer class name.
            p->recordUsage(nameForClassDecorators.ref);
            std::vector<Decl> decls;
            Decl decl;
            decl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{nameForClassDecorators.ref}),
                nameForClassDecorators.loc};
            decl.value_or_nil = init;
            decls.push_back(decl);
            auto local = std::make_shared<SLocal>();
            local->kind = p->selectLocalKind(LocalKind::kLet);
            local->is_export = this->kind == classKindExportStmt;
            local->decls = decls;
            stmts.push_back(Stmt{local, this->class_loc});
        }
    } else {
        // Generate the specific kind of class statement that was passed in to us
        switch (this->kind) {
        case classKindStmt: {
            auto sclass = std::make_shared<SClass>();
            sclass->class_ = *this->class_;
            stmts.push_back(Stmt{sclass, this->class_loc});
            break;
        }
        case classKindExportStmt: {
            auto sclass = std::make_shared<SClass>();
            sclass->class_ = *this->class_;
            sclass->is_export = true;
            stmts.push_back(Stmt{sclass, this->class_loc});
            break;
        }
        case classKindExportDefaultStmt: {
            auto sclass = std::make_shared<SClass>();
            sclass->class_ = *this->class_;
            auto sdefault = std::make_shared<SExportDefault>();
            sdefault->default_name = this->default_name;
            sdefault->value = Stmt{sclass, this->class_loc};
            stmts.push_back(Stmt{sdefault, this->class_loc});
            break;
        }
        default:
            break;
        }

        // The inner class name inside the class statement should be the same as
        // the class statement name itself
        if (this->class_->name != nullptr && result.innerClassNameRef != compiler::kInvalidRef) {
            // If the class body contains a direct eval call, then the inner class
            // name will be marked as "MustNotBeRenamed" (because we have already
            // popped the class body scope) but the outer class name won't be marked
            // as "MustNotBeRenamed" yet (because we haven't yet popped the containing
            // scope). Propagate this flag now before we merge these symbols so we
            // don't end up accidentally renaming the outer class name to the inner
            // class name.
            if (p->currentScope->contains_direct_eval) {
                p->symbols[this->class_->name->ref.inner_index].flags =
                    p->symbols[this->class_->name->ref.inner_index].flags |
                    (p->symbols[result.innerClassNameRef.inner_index].flags &
                        compiler::SymbolFlags::kMustNotBeRenamed);
            }
            p->mergeSymbols(result.innerClassNameRef, this->class_->name->ref);
        }
    }

    // Insert expressions after the class as appropriate
    for (Expr &expr : suffixExprs) {
        stmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{expr}), expr.loc});
    }

    // This must come after the class body initializers have finished
    if (!IsNil(outerClassNameDecl.data)) {
        stmts.push_back(outerClassNameDecl);
    }

    if (nameForClassDecorators.ref != compiler::kInvalidRef && this->kind == classKindExportDefaultStmt) {
        // "export default class x {}" => "class x {} export {x as default}"
        auto sclause = std::make_shared<SExportClause>();
        ClauseItem item;
        item.alias = "default";
        item.name = this->default_name;
        sclause->items.push_back(item);
        stmts.push_back(Stmt{sclause, this->class_loc});
    }

    return {stmts, Expr{}};
}

} // namespace

// =============================================================================
// Parser member functions
// =============================================================================

bool Parser::privateSymbolNeedsToBeLowered(EPrivateIdentifier *priv) {
    compiler::Symbol &symbol = this->symbols[priv->ref.inner_index];
    return compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
               compat::SymbolFeature(symbol.kind)) ||
        compiler::Has(symbol.flags, compiler::SymbolFlags::kPrivateSymbolMustBeLowered);
}

Expr Parser::lowerPrivateBrandCheck(Expr target, logger::Loc loc, EPrivateIdentifier *priv) {
    // "#field in this" => "__privateIn(#field, this)"
    std::vector<Expr> args;
    args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{priv->ref}), loc});
    args.push_back(target);
    return this->callRuntime(loc, "__privateIn", args);
}

Expr Parser::lowerPrivateGet(Expr target, logger::Loc loc, EPrivateIdentifier *priv) {
    switch (this->symbols[priv->ref.inner_index].kind) {
    case compiler::SymbolKind::kPrivateMethod:
    case compiler::SymbolKind::kPrivateStaticMethod: {
        // "this.#method" => "__privateMethod(this, #method, method_fn)"
        compiler::Ref fnRef = this->privateGetters[priv->ref];
        this->recordUsage(fnRef);
        std::vector<Expr> args;
        args.push_back(target);
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{priv->ref}), loc});
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{fnRef}), loc});
        return this->callRuntime(target.loc, "__privateMethod", args);
    }

    case compiler::SymbolKind::kPrivateGet:
    case compiler::SymbolKind::kPrivateStaticGet:
    case compiler::SymbolKind::kPrivateGetSetPair:
    case compiler::SymbolKind::kPrivateStaticGetSetPair: {
        // "this.#getter" => "__privateGet(this, #getter, getter_get)"
        compiler::Ref fnRef = this->privateGetters[priv->ref];
        this->recordUsage(fnRef);
        std::vector<Expr> args;
        args.push_back(target);
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{priv->ref}), loc});
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{fnRef}), loc});
        return this->callRuntime(target.loc, "__privateGet", args);
    }

    default:
        // "this.#field" => "__privateGet(this, #field)"
        std::vector<Expr> args;
        args.push_back(target);
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{priv->ref}), loc});
        return this->callRuntime(target.loc, "__privateGet", args);
    }
}

Expr Parser::lowerPrivateSet(Expr target, logger::Loc loc, EPrivateIdentifier *priv, Expr value) {
    switch (this->symbols[priv->ref.inner_index].kind) {
    case compiler::SymbolKind::kPrivateSet:
    case compiler::SymbolKind::kPrivateStaticSet:
    case compiler::SymbolKind::kPrivateGetSetPair:
    case compiler::SymbolKind::kPrivateStaticGetSetPair: {
        // "this.#setter = 123" => "__privateSet(this, #setter, 123, setter_set)"
        compiler::Ref fnRef = this->privateSetters[priv->ref];
        this->recordUsage(fnRef);
        std::vector<Expr> args;
        args.push_back(target);
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{priv->ref}), loc});
        args.push_back(value);
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{fnRef}), loc});
        return this->callRuntime(target.loc, "__privateSet", args);
    }

    default:
        // "this.#field = 123" => "__privateSet(this, #field, 123)"
        std::vector<Expr> args;
        args.push_back(target);
        args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{priv->ref}), loc});
        args.push_back(value);
        return this->callRuntime(target.loc, "__privateSet", args);
    }
}

Expr Parser::lowerPrivateSetUnOp(Expr target, logger::Loc loc, EPrivateIdentifier *priv, OpCode op) {
    compiler::SymbolKind kind = this->symbols[priv->ref.inner_index].kind;

    // Determine the setter, if any
    Expr setter;
    switch (kind) {
    case compiler::SymbolKind::kPrivateSet:
    case compiler::SymbolKind::kPrivateStaticSet:
    case compiler::SymbolKind::kPrivateGetSetPair:
    case compiler::SymbolKind::kPrivateStaticGetSetPair: {
        compiler::Ref ref = this->privateSetters[priv->ref];
        this->recordUsage(ref);
        setter = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), loc};
    }
    default:
        break;
    }

    // Determine the getter, if any
    Expr getter;
    switch (kind) {
    case compiler::SymbolKind::kPrivateGet:
    case compiler::SymbolKind::kPrivateStaticGet:
    case compiler::SymbolKind::kPrivateGetSetPair:
    case compiler::SymbolKind::kPrivateStaticGetSetPair: {
        compiler::Ref ref = this->privateGetters[priv->ref];
        this->recordUsage(ref);
        getter = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), loc};
    }
    default:
        break;
    }

    // Only include necessary arguments
    std::vector<Expr> args;
    args.push_back(target);
    args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{priv->ref}), loc});
    if (!IsNil(setter.data)) {
        args.push_back(setter);
    }
    if (!IsNil(getter.data)) {
        if (IsNil(setter.data)) {
            args.push_back(Expr{kENullShared, loc});
        }
        args.push_back(getter);
    }

    // "target.#private++" => "__privateWrapper(target, #private, private_set, private_get)._++"
    auto dot = std::make_shared<EDot>();
    dot->target = this->callRuntime(target.loc, "__privateWrapper", args);
    dot->name_loc = target.loc;
    dot->name = "_";
    auto unary = std::make_shared<EUnary>();
    unary->op = op;
    unary->value = Expr{dot, target.loc};
    return Expr{unary, loc};
}

Expr Parser::lowerPrivateSetBinOp(Expr target, logger::Loc loc, EPrivateIdentifier *priv, OpCode op, Expr value) {
    // "target.#private += 123" => "__privateSet(target, #private, __privateGet(target, #private) + 123)"
    std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
        this->captureValueWithPossibleSideEffects(target.loc, 2, target, captureValueMode::valueDefinitelyNotMutated);
    std::function<Expr()> targetFunc = std::move(captured.first);
    std::function<Expr(Expr)> targetWrapFunc = std::move(captured.second);

    // The target of the setter is evaluated first, so give "__privateSet" the
    // temporary ("_a = target") and use the bare temporary in the get:
    // "__privateSet(_a = target, #private, __privateGet(_a, #private) + value)"
    Expr setTarget = targetFunc();
    auto binary = std::make_shared<EBinary>();
    binary->op = op;
    binary->left = this->lowerPrivateGet(targetFunc(), loc, priv);
    binary->right = value;
    return targetWrapFunc(this->lowerPrivateSet(setTarget, loc, priv, Expr{binary, value.loc}));
}

// Returns valid data if target is an expression of the form "foo.#bar" and if
// the language target is such that private members must be lowered
std::tuple<Expr, logger::Loc, EPrivateIdentifier *> Parser::extractPrivateIndex(Expr expr) {
    if (auto index = Get<EIndex>(expr.data); index != nullptr) {
        if (auto privateMember = Get<EPrivateIdentifier>(index->index.data);
            privateMember != nullptr && this->privateSymbolNeedsToBeLowered(privateMember)) {
            return {index->target, index->index.loc, privateMember};
        }
    }
    return {Expr{}, logger::Loc{}, nullptr};
}

// Returns a valid property if target is an expression of the form "super.bar"
// or "super[bar]" and if the situation is such that it must be lowered
Expr Parser::extractSuperProperty(Expr expr) {
    if (auto eDot = Get<EDot>(expr.data); eDot != nullptr) {
        if (this->shouldLowerSuperPropertyAccess(eDot->target)) {
            return Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(eDot->name), logger::Loc{}}), eDot->name_loc};
        }
    } else if (auto eIdx = Get<EIndex>(expr.data); eIdx != nullptr) {
        if (this->shouldLowerSuperPropertyAccess(eIdx->target)) {
            return eIdx->index;
        }
    }
    return Expr{};
}

std::pair<Expr, bool> Parser::lowerSuperPropertyOrPrivateInAssign(Expr expr) {
    bool didLower = false;

    if (auto eSpread = Get<ESpread>(expr.data); eSpread != nullptr) {
        std::pair<Expr, bool> result = this->lowerSuperPropertyOrPrivateInAssign(eSpread->value);
        if (result.second) {
            eSpread->value = result.first;
            didLower = true;
        }
    } else if (auto eDot = Get<EDot>(expr.data); eDot != nullptr) {
        // "[super.foo] = [bar]" => "[__superWrapper(this, 'foo')._] = [bar]"
        if (this->shouldLowerSuperPropertyAccess(eDot->target)) {
            Expr key{std::make_shared<EString>(EString{helpers::StringToUTF16(eDot->name), logger::Loc{}}), eDot->name_loc};
            expr = this->callSuperPropertyWrapper(expr.loc, key);
            didLower = true;
        }
    } else if (auto eIdx = Get<EIndex>(expr.data); eIdx != nullptr) {
        // "[super[foo]] = [bar]" => "[__superWrapper(this, foo)._] = [bar]"
        if (this->shouldLowerSuperPropertyAccess(eIdx->target)) {
            expr = this->callSuperPropertyWrapper(expr.loc, eIdx->index);
            didLower = true;
        } else if (auto privateMember = Get<EPrivateIdentifier>(eIdx->index.data);
            privateMember != nullptr && this->privateSymbolNeedsToBeLowered(privateMember)) {
            // "[a.#b] = [c]" => "[__privateWrapper(a, #b)._] = [c]"
            Expr target;

            switch (this->symbols[privateMember->ref.inner_index].kind) {
            case compiler::SymbolKind::kPrivateSet:
            case compiler::SymbolKind::kPrivateStaticSet:
            case compiler::SymbolKind::kPrivateGetSetPair:
            case compiler::SymbolKind::kPrivateStaticGetSetPair: {
                // "this.#setter" => "__privateWrapper(this, #setter, setter_set)"
                compiler::Ref fnRef = this->privateSetters[privateMember->ref];
                this->recordUsage(fnRef);
                std::vector<Expr> args;
                args.push_back(eIdx->target);
                args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{privateMember->ref}), expr.loc});
                args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{fnRef}), expr.loc});
                target = this->callRuntime(expr.loc, "__privateWrapper", args);
                break;
            }
            default:
                // "this.#field" => "__privateWrapper(this, #field)"
                std::vector<Expr> args;
                args.push_back(eIdx->target);
                args.push_back(Expr{std::make_shared<EIdentifier>(EIdentifier{privateMember->ref}), expr.loc});
                target = this->callRuntime(expr.loc, "__privateWrapper", args);
                break;
            }

            // "__privateWrapper(this, #field)" => "__privateWrapper(this, #field)._"
            auto dot = std::make_shared<EDot>();
            dot->target = target;
            dot->name_loc = expr.loc;
            dot->name = "_";
            expr.data = dot;
            didLower = true;
        }
    } else if (auto eArr = Get<EArray>(expr.data); eArr != nullptr) {
        for (size_t i = 0; i < eArr->items.size(); i++) {
            std::pair<Expr, bool> result = this->lowerSuperPropertyOrPrivateInAssign(eArr->items[i]);
            if (result.second) {
                eArr->items[i] = result.first;
                didLower = true;
            }
        }
    } else if (auto eObj = Get<EObject>(expr.data); eObj != nullptr) {
        for (size_t i = 0; i < eObj->properties.size(); i++) {
            if (!IsNil(eObj->properties[i].value_or_nil.data)) {
                std::pair<Expr, bool> result = this->lowerSuperPropertyOrPrivateInAssign(eObj->properties[i].value_or_nil);
                if (result.second) {
                    eObj->properties[i].value_or_nil = result.first;
                    didLower = true;
                }
            }
        }
    }

    return {expr, didLower};
}

bool Parser::shouldLowerSuperPropertyAccess(Expr expr) {
    if (this->fnOrArrowDataVisit.shouldLowerSuperPropertyAccess) {
        return Get<ESuper>(expr.data) != nullptr;
    }
    return false;
}

Expr Parser::callSuperPropertyWrapper(logger::Loc loc, Expr key) {
    compiler::Ref ref = *this->fnOnlyDataVisit.innerClassNameRef;
    this->recordUsage(ref);
    Expr classExpr{std::make_shared<EIdentifier>(EIdentifier{ref}), loc};
    Expr thisExpr{kEThisShared, loc};

    // Handle "this" in lowered static class field initializers
    if (this->fnOnlyDataVisit.shouldReplaceThisWithInnerClassNameRef) {
        this->recordUsage(ref);
        thisExpr.data = std::make_shared<EIdentifier>(EIdentifier{ref});
    }

    if (!this->fnOnlyDataVisit.isInStaticClassContext) {
        // "super.foo" => "__superWrapper(Class.prototype, this, 'foo')._"
        // "super[foo]" => "__superWrapper(Class.prototype, this, foo)._"
        auto dot = std::make_shared<EDot>();
        dot->target = classExpr;
        dot->name_loc = loc;
        dot->name = "prototype";
        classExpr.data = dot;
    }

    std::vector<Expr> args;
    args.push_back(classExpr);
    args.push_back(thisExpr);
    args.push_back(key);
    auto dot = std::make_shared<EDot>();
    dot->target = this->callRuntime(loc, "__superWrapper", args);
    dot->name_loc = loc;
    dot->name = "_";
    return Expr{dot, loc};
}

Expr Parser::lowerSuperPropertyGet(logger::Loc loc, Expr key) {
    compiler::Ref ref = *this->fnOnlyDataVisit.innerClassNameRef;
    this->recordUsage(ref);
    Expr classExpr{std::make_shared<EIdentifier>(EIdentifier{ref}), loc};
    Expr thisExpr{kEThisShared, loc};

    // Handle "this" in lowered static class field initializers
    if (this->fnOnlyDataVisit.shouldReplaceThisWithInnerClassNameRef) {
        this->recordUsage(ref);
        thisExpr.data = std::make_shared<EIdentifier>(EIdentifier{ref});
    }

    if (!this->fnOnlyDataVisit.isInStaticClassContext) {
        // "super.foo" => "__superGet(Class.prototype, this, 'foo')"
        // "super[foo]" => "__superGet(Class.prototype, this, foo)"
        auto dot = std::make_shared<EDot>();
        dot->target = classExpr;
        dot->name_loc = loc;
        dot->name = "prototype";
        classExpr.data = dot;
    }

    std::vector<Expr> args;
    args.push_back(classExpr);
    args.push_back(thisExpr);
    args.push_back(key);
    return this->callRuntime(loc, "__superGet", args);
}

Expr Parser::lowerSuperPropertySet(logger::Loc loc, Expr key, Expr value) {
    // "super.foo = bar" => "__superSet(Class, this, 'foo', bar)"
    // "super[foo] = bar" => "__superSet(Class, this, foo, bar)"
    compiler::Ref ref = *this->fnOnlyDataVisit.innerClassNameRef;
    this->recordUsage(ref);
    Expr classExpr{std::make_shared<EIdentifier>(EIdentifier{ref}), loc};
    Expr thisExpr{kEThisShared, loc};

    // Handle "this" in lowered static class field initializers
    if (this->fnOnlyDataVisit.shouldReplaceThisWithInnerClassNameRef) {
        this->recordUsage(ref);
        thisExpr.data = std::make_shared<EIdentifier>(EIdentifier{ref});
    }

    if (!this->fnOnlyDataVisit.isInStaticClassContext) {
        // "super.foo = bar" => "__superSet(Class.prototype, this, 'foo', bar)"
        // "super[foo] = bar" => "__superSet(Class.prototype, this, foo, bar)"
        auto dot = std::make_shared<EDot>();
        dot->target = classExpr;
        dot->name_loc = loc;
        dot->name = "prototype";
        classExpr.data = dot;
    }

    std::vector<Expr> args;
    args.push_back(classExpr);
    args.push_back(thisExpr);
    args.push_back(key);
    args.push_back(value);
    return this->callRuntime(loc, "__superSet", args);
}

Expr Parser::lowerSuperPropertySetBinOp(logger::Loc loc, Expr property, OpCode op, Expr value) {
    // "super.foo += bar" => "__superSet(Class, this, 'foo', __superGet(Class, this, 'foo') + bar)"
    // "super[foo] += bar" => "__superSet(Class, this, foo, __superGet(Class, this, foo) + bar)"
    // "super[foo()] += bar" => "__superSet(Class, this, _a = foo(), __superGet(Class, this, _a) + bar)"
    std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captured =
        this->captureValueWithPossibleSideEffects(property.loc, 2, property, captureValueMode::valueDefinitelyNotMutated);
    std::function<Expr()> targetFunc = std::move(captured.first);
    std::function<Expr(Expr)> targetWrapFunc = std::move(captured.second);

    // The key of the setter is evaluated before the get, so give "__superSet"
    // the temporary ("_a = foo()") and use the bare temporary in the get:
    // "__superSet(Class, this, _a = foo(), __superGet(Class, this, _a) + bar)"
    Expr setKey = targetFunc();
    auto binary = std::make_shared<EBinary>();
    binary->op = op;
    binary->left = this->lowerSuperPropertyGet(loc, targetFunc());
    binary->right = value;
    return targetWrapFunc(this->lowerSuperPropertySet(loc, setKey, Expr{binary, value.loc}));
}

void Parser::maybeLowerSuperPropertyGetInsideCall(ECall *call) {
    Expr key;

    if (auto eDot = Get<EDot>(call->target.data); eDot != nullptr) {
        // Lower "super.prop" if necessary
        if (!this->shouldLowerSuperPropertyAccess(eDot->target)) {
            return;
        }
        key = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(eDot->name), logger::Loc{}}), eDot->name_loc};
    } else if (auto eIdx = Get<EIndex>(call->target.data); eIdx != nullptr) {
        // Lower "super[prop]" if necessary
        if (!this->shouldLowerSuperPropertyAccess(eIdx->target)) {
            return;
        }
        key = eIdx->index;
    } else {
        return;
    }

    // "super.foo(a, b)" => "__superGet(Class, this, 'foo').call(this, a, b)"
    auto dot = std::make_shared<EDot>();
    dot->target = this->lowerSuperPropertyGet(call->target.loc, key);
    dot->name_loc = key.loc;
    dot->name = "call";
    call->target.data = dot;
    Expr thisExpr{kEThisShared, call->target.loc};
    call->args.insert(call->args.begin(), thisExpr);
}

classLoweringInfo Parser::computeClassLoweringInfo(Class *cls) {
    classLoweringInfo result{};

    // Name keeping for classes is implemented with a static block. So we need to
    // lower all static fields if static blocks are unsupported so that the name
    // keeping comes first before other static initializers.
    if (this->options.optionsThatSupportStructuralEquality.keepNames &&
        compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
            compat::JSFeature::kClassStaticBlocks)) {
        result.lowerAllStaticFields = true;
    }

    // TypeScript's "experimentalDecorators" feature replaces all references of
    // the class name with the decorated class after class decorators have run.
    // This cannot be done by only reassigning to the class symbol in JavaScript
    // because it's shadowed by the class name within the class body. Instead,
    // we need to hoist all code in static contexts out of the class body so
    // that it's no longer shadowed:
    //
    //   const decorate = x => ({ x })
    //   @decorate
    //   class Foo {
    //     static oldFoo = Foo
    //     static newFoo = () => Foo
    //   }
    //   console.log('This must be false:', Foo.x.oldFoo === Foo.x.newFoo())
    //
    if (this->options.optionsThatSupportStructuralEquality.ts.Parse &&
        this->options.optionsThatSupportStructuralEquality.ts.Config.ExperimentalDecorators == config::MaybeBool::kTrue &&
        !cls->decorators.empty()) {
        result.lowerAllStaticFields = true;
    }

    // If something has decorators, just lower everything for now. It's possible
    // that we could avoid lowering in certain cases, but doing so is very tricky
    // due to the complexity of the decorator specification. The specification is
    // also still evolving so trying to optimize it now is also potentially
    // premature.
    if (cls->should_lower_standard_decorators) {
        for (Property &prop : cls->properties) {
            if (!prop.decorators.empty()) {
                for (Property &prop2 : cls->properties) {
                    if (auto privateMember = Get<EPrivateIdentifier>(prop2.key.data); privateMember != nullptr) {
                        this->symbols[privateMember->ref.inner_index].flags =
                            this->symbols[privateMember->ref.inner_index].flags |
                            compiler::SymbolFlags::kPrivateSymbolMustBeLowered;
                    }
                }
                result.lowerAllStaticFields = true;
                result.lowerAllInstanceFields = true;
                break;
            }
        }
    }

    // Conservatively lower fields of a given type (instance or static) when any
    // member of that type needs to be lowered. This must be done to preserve
    // evaluation order. For example:
    //
    //   class Foo {
    //     #foo = 123
    //     bar = this.#foo
    //   }
    //
    // It would be bad if we transformed that into something like this:
    //
    //   var _foo;
    //   class Foo {
    //     constructor() {
    //       _foo.set(this, 123);
    //     }
    //     bar = __privateGet(this, _foo);
    //   }
    //   _foo = new WeakMap();
    //
    // That evaluates "bar" then "foo" instead of "foo" then "bar" like the
    // original code. We need to do this instead:
    //
    //   var _foo;
    //   class Foo {
    //     constructor() {
    //       _foo.set(this, 123);
    //       __publicField(this, "bar", __privateGet(this, _foo));
    //     }
    //   }
    //   _foo = new WeakMap();
    //
    for (Property &prop : cls->properties) {
        if (prop.kind == PropertyKind::kClassStaticBlock) {
            if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
                compat::JSFeature::kClassStaticBlocks)) {
                result.lowerAllStaticFields = true;
            }
            continue;
        }

        if (auto privateMember = Get<EPrivateIdentifier>(prop.key.data); privateMember != nullptr) {
            if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                if (this->privateSymbolNeedsToBeLowered(privateMember)) {
                    result.lowerAllStaticFields = true;
                }
            } else {
                if (this->privateSymbolNeedsToBeLowered(privateMember)) {
                    result.lowerAllInstanceFields = true;

                    // We can't transform this:
                    //
                    //   class Foo {
                    //     #foo = 123
                    //     static bar = new Foo().#foo
                    //   }
                    //
                    // into this:
                    //
                    //   var _foo;
                    //   const _Foo = class {
                    //     constructor() {
                    //       _foo.set(this, 123);
                    //     }
                    //     static bar = __privateGet(new _Foo(), _foo);
                    //   };
                    //   let Foo = _Foo;
                    //   _foo = new WeakMap();
                    //
                    // because "_Foo" won't be initialized in the initializer for "bar".
                    // So we currently lower all static fields in this case too. This
                    // isn't great and it would be good to find a way to avoid this.
                    // The inner class name symbol substitution mechanism should probably
                    // be rethought.
                    result.lowerAllStaticFields = true;
                }
            }
            continue;
        }

        if (prop.kind == PropertyKind::kAutoAccessor) {
            if (Has(prop.flags, PropertyFlags::kIsStatic)) {
                if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
                    compat::JSFeature::kClassPrivateStaticField)) {
                    result.lowerAllStaticFields = true;
                }
            } else {
                if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
                    compat::JSFeature::kClassPrivateField)) {
                    result.lowerAllInstanceFields = true;
                    result.lowerAllStaticFields = true;
                }
            }
            continue;
        }

        // This doesn't come before the private member check above because
        // unsupported private methods must also trigger field lowering:
        //
        //   class Foo {
        //     bar = this.#foo()
        //     #foo() {}
        //   }
        //
        // It would be bad if we transformed that to something like this:
        //
        //   var _foo, foo_fn;
        //   class Foo {
        //     constructor() {
        //       _foo.add(this);
        //     }
        //     bar = __privateMethod(this, _foo, foo_fn).call(this);
        //   }
        //   _foo = new WeakSet();
        //   foo_fn = function() {
        //   };
        //
        // In that case the initializer of "bar" would fail to call "#foo" because
        // it's only added to the instance in the body of the constructor.
        if (IsMethodDefinition(prop.kind)) {
            // We need to shim "super()" inside the constructor if this is a derived
            // class and the constructor has any parameter properties, since those
            // use "this" and we can only access "this" after "super()" is called
            if (!IsNil(cls->extends_or_nil.data)) {
                if (auto key = Get<EString>(prop.key.data); key != nullptr && helpers::UTF16EqualsString(key->value, "constructor")) {
                    if (auto fn = Get<EFunction>(prop.value_or_nil.data); fn != nullptr) {
                        for (Arg &arg : fn->fn.args) {
                            if (arg.is_typescript_ctor_field) {
                                result.shimSuperCtorCalls = true;
                                break;
                            }
                        }
                    }
                }
            }
            continue;
        }

        if (Has(prop.flags, PropertyFlags::kIsStatic)) {
            // Static fields must be lowered if the target doesn't support them
            if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
                compat::JSFeature::kClassStaticField)) {
                result.lowerAllStaticFields = true;
            }

            // Convert static fields to assignment statements if the TypeScript
            // setting for this is enabled. I don't think this matters for private
            // fields because there's no way for this to call a setter in the base
            // class, so this isn't done for private fields.
            //
            // If class static blocks are supported, then we can do this inline
            // without needing to move the initializers outside of the class body.
            // Otherwise, we need to lower all static class fields.
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && !cls->use_define_for_class_fields &&
                compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
                    compat::JSFeature::kClassStaticBlocks)) {
                result.lowerAllStaticFields = true;
            }
        } else {
            if (this->options.optionsThatSupportStructuralEquality.ts.Parse && !cls->use_define_for_class_fields) {
                // Convert instance fields to assignment statements if the TypeScript
                // setting for this is enabled. I don't think this matters for private
                // fields because there's no way for this to call a setter in the base
                // class, so this isn't done for private fields.
                if (!IsNil(prop.initializer_or_nil.data)) {
                    // We can skip lowering all instance fields if all instance fields
                    // disappear completely when lowered. This happens when
                    // "useDefineForClassFields" is false and there is no initializer.
                    result.lowerAllInstanceFields = true;
                }
            } else if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures,
                compat::JSFeature::kClassField)) {
                // Instance fields must be lowered if the target doesn't support them
                result.lowerAllInstanceFields = true;
            }
        }
    }

    // We need to shim "super()" inside the constructor if this is a derived
    // class and there are any instance fields that need to be lowered, since
    // those use "this" and we can only access "this" after "super()" is called
    if (result.lowerAllInstanceFields && !IsNil(cls->extends_or_nil.data)) {
        result.shimSuperCtorCalls = true;
    }

    return result;
}

std::string Parser::propertyNameHint(Expr key) {
    if (auto kStr = Get<EString>(key.data); kStr != nullptr) {
        return helpers::UTF16ToString(kStr->value);
    } else if (auto kId = Get<EIdentifier>(key.data); kId != nullptr) {
        return this->symbols[kId->ref.inner_index].original_name;
    } else if (auto kPriv = Get<EPrivateIdentifier>(key.data); kPriv != nullptr) {
        return this->symbols[kPriv->ref.inner_index].original_name.substr(1);
    }
    return "";
}

// Apply all relevant transforms to a class object (either a statement or an
// expression) including:
//
//   - Transforming class fields for older environments
//   - Transforming static blocks for older environments
//   - Transforming TypeScript experimental decorators into JavaScript
//   - Transforming TypeScript class fields into assignments for "useDefineForClassFields"
//
// Note that this doesn't transform any nested AST subtrees inside the class
// body (e.g. the contents of initializers, methods, and static blocks). Those
// have already been transformed by "visitClass" by this point. It's done that
// way for performance so that we don't need to do another AST pass.
std::pair<std::vector<Stmt>, Expr> Parser::lowerClass(Stmt stmt, Expr expr, visitClassResult info,
    std::string nameToKeepParam) {
    lowerClassContext ctx{};
    ctx.name_to_keep = std::move(nameToKeepParam);
    ctx.extends_ref = compiler::kInvalidRef;
    ctx.decorator_context_ref = compiler::kInvalidRef;
    ctx.private_instance_method_ref = compiler::kInvalidRef;
    ctx.private_static_method_ref = compiler::kInvalidRef;

    // Unpack the class from the statement or expression
    if (IsNil(stmt.data)) {
        EClass *e = Get<EClass>(expr.data);
        ctx.class_ = &e->class_;
        ctx.class_expr = expr;
        ctx.kind = classKindExpr;
        if (ctx.class_->name != nullptr) {
            compiler::Symbol &symbol = this->symbols[ctx.class_->name->ref.inner_index];
            ctx.name_to_keep = symbol.original_name;

            // The inner class name inside the class expression should be the same as
            // the class expression name itself
            if (info.innerClassNameRef != compiler::kInvalidRef) {
                this->mergeSymbols(info.innerClassNameRef, ctx.class_->name->ref);
            }

            // Remove unused class names when minifying. Check this after we merge in
            // the inner class name above since that will adjust the use count.
            if (this->options.optionsThatSupportStructuralEquality.minifySyntax && symbol.use_count_estimate == 0) {
                ctx.class_->name.reset();
            }
        }
    } else if (auto s = Get<SClass>(stmt.data); s != nullptr) {
        ctx.class_ = &s->class_;
        if (ctx.class_->name != nullptr) {
            ctx.name_to_keep = this->symbols[ctx.class_->name->ref.inner_index].original_name;
        }
        if (s->is_export) {
            ctx.kind = classKindExportStmt;
        } else {
            ctx.kind = classKindStmt;
        }
    } else {
        SExportDefault *exportDefault = Get<SExportDefault>(stmt.data);
        SClass *s2 = Get<SClass>(exportDefault->value.data);
        ctx.class_ = &s2->class_;
        if (ctx.class_->name != nullptr) {
            ctx.name_to_keep = this->symbols[ctx.class_->name->ref.inner_index].original_name;
        }
        ctx.default_name = exportDefault->default_name;
        ctx.kind = classKindExportDefaultStmt;
    }
    if (IsNil(stmt.data)) {
        ctx.class_loc = expr.loc;
    } else {
        ctx.class_loc = stmt.loc;
    }

    classLoweringInfo classLoweringInfo = this->computeClassLoweringInfo(ctx.class_);
    ctx.enableNameCapture(this, info);
    ctx.processProperties(this, classLoweringInfo, info);
    ctx.insertInitializersIntoConstructor(this, classLoweringInfo, info);
    return ctx.finishAndGenerateCode(this, info);
}

// Replace "super()" calls with our shim so that we can guarantee
// that instance field initialization doesn't happen before "super()"
// is called, since at that point "this" isn't available.
void Parser::insertStmtsAfterSuperCall(FnBody *body, std::vector<Stmt> stmtsToInsert, compiler::Ref superCtorRefParam) {
    // If this class has no base class, then there's no "super()" call to handle
    if (superCtorRefParam == compiler::kInvalidRef || this->symbols[superCtorRefParam.inner_index].use_count_estimate == 0) {
        body->block.stmts.insert(body->block.stmts.begin(), stmtsToInsert.begin(), stmtsToInsert.end());
        return;
    }

    // It's likely that there's only one "super()" call, and that it's a
    // top-level expression in the constructor function body. If so, we
    // can generate tighter code for this common case.
    if (this->symbols[superCtorRefParam.inner_index].use_count_estimate == 1) {
        for (size_t i = 0; i < body->block.stmts.size(); i++) {
            Stmt stmt = body->block.stmts[i];
            Expr before;
            logger::Loc callLoc;
            std::shared_ptr<ECall> callData;
            Stmt after;

            if (auto sExpr = GetShared<SExpr>(stmt.data)) {
                std::tuple<Expr, logger::Loc, std::shared_ptr<ECall>, Expr> found =
                    findFirstTopLevelSuperCall(sExpr->value, superCtorRefParam);
                if (std::get<2>(found) != nullptr) {
                    before = std::get<0>(found);
                    callLoc = std::get<1>(found);
                    callData = std::get<2>(found);
                    Expr a = std::get<3>(found);
                    if (!IsNil(a.data)) {
                        sExpr->value = a;
                        after = Stmt{sExpr, a.loc};
                    }
                }
            } else if (auto sReturn = GetShared<SReturn>(stmt.data)) {
                if (!IsNil(sReturn->value_or_nil.data)) {
                    std::tuple<Expr, logger::Loc, std::shared_ptr<ECall>, Expr> found =
                        findFirstTopLevelSuperCall(sReturn->value_or_nil, superCtorRefParam);
                    if (std::get<2>(found) != nullptr && !IsNil(std::get<3>(found).data)) {
                        before = std::get<0>(found);
                        callLoc = std::get<1>(found);
                        callData = std::get<2>(found);
                        Expr a = std::get<3>(found);
                        sReturn->value_or_nil = a;
                        after = Stmt{sReturn, a.loc};
                    }
                }
            } else if (auto sThrow = GetShared<SThrow>(stmt.data)) {
                std::tuple<Expr, logger::Loc, std::shared_ptr<ECall>, Expr> found =
                    findFirstTopLevelSuperCall(sThrow->value, superCtorRefParam);
                if (std::get<2>(found) != nullptr && !IsNil(std::get<3>(found).data)) {
                    before = std::get<0>(found);
                    callLoc = std::get<1>(found);
                    callData = std::get<2>(found);
                    Expr a = std::get<3>(found);
                    sThrow->value = a;
                    after = Stmt{sThrow, a.loc};
                }
            } else if (auto sIf = GetShared<SIf>(stmt.data)) {
                std::tuple<Expr, logger::Loc, std::shared_ptr<ECall>, Expr> found =
                    findFirstTopLevelSuperCall(sIf->test, superCtorRefParam);
                if (std::get<2>(found) != nullptr && !IsNil(std::get<3>(found).data)) {
                    before = std::get<0>(found);
                    callLoc = std::get<1>(found);
                    callData = std::get<2>(found);
                    Expr a = std::get<3>(found);
                    sIf->test = a;
                    after = Stmt{sIf, a.loc};
                }
            } else if (auto sSwitch = GetShared<SSwitch>(stmt.data)) {
                std::tuple<Expr, logger::Loc, std::shared_ptr<ECall>, Expr> found =
                    findFirstTopLevelSuperCall(sSwitch->test, superCtorRefParam);
                if (std::get<2>(found) != nullptr && !IsNil(std::get<3>(found).data)) {
                    before = std::get<0>(found);
                    callLoc = std::get<1>(found);
                    callData = std::get<2>(found);
                    Expr a = std::get<3>(found);
                    sSwitch->test = a;
                    after = Stmt{sSwitch, a.loc};
                }
            } else if (auto sFor = GetShared<SFor>(stmt.data)) {
                if (sFor->init_or_nil != nullptr) {
                    if (SExpr *expr = Get<SExpr>(sFor->init_or_nil->data); expr != nullptr) {
                        std::tuple<Expr, logger::Loc, std::shared_ptr<ECall>, Expr> found =
                            findFirstTopLevelSuperCall(expr->value, superCtorRefParam);
                        if (std::get<2>(found) != nullptr) {
                            before = std::get<0>(found);
                            callLoc = std::get<1>(found);
                            callData = std::get<2>(found);
                            Expr a = std::get<3>(found);
                            if (!IsNil(a.data)) {
                                expr->value = a;
                            } else {
                                sFor->init_or_nil = nullptr;
                            }
                            after = Stmt{sFor, a.loc};
                        }
                    }
                }
            }

            if (callData != nullptr) {
                // Revert "__super()" back to "super()"
                callData->target.data = kESuperShared;
                this->ignoreUsage(superCtorRefParam);

                // Inject "stmtsToInsert" after "super()"
                std::vector<Stmt> stmts;
                for (size_t j = 0; j < i; j++) {
                    stmts.push_back(body->block.stmts[j]);
                }
                if (!IsNil(before.data)) {
                    stmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{before}), before.loc});
                }
                stmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{Expr{callData, callLoc}}), callLoc});
                stmts.insert(stmts.end(), stmtsToInsert.begin(), stmtsToInsert.end());
                if (!IsNil(after.data)) {
                    stmts.push_back(after);
                }
                for (size_t j = i + 1; j < body->block.stmts.size(); j++) {
                    stmts.push_back(body->block.stmts[j]);
                }
                body->block.stmts = std::move(stmts);
                return;
            }
        }
    }

    // Otherwise, inject a generated "__super" helper function at the top of the
    // constructor that looks like this:
    //
    //   var __super = (...args) => {
    //     super(...args);
    //     ...stmtsToInsert...
    //     return this;
    //   };
    //
    compiler::Ref argsRef = this->newSymbol(compiler::SymbolKind::kOther, "args");
    this->currentScope->generated.push_back(argsRef);
    this->recordUsage(argsRef);

    auto superCall = std::make_shared<ECall>();
    superCall->target = Expr{kESuperShared, body->loc};
    std::vector<Expr> callArgs;
    callArgs.push_back(Expr{std::make_shared<ESpread>(ESpread{
        Expr{std::make_shared<EIdentifier>(EIdentifier{argsRef}), body->loc}}), body->loc});
    superCall->args = callArgs;

    std::vector<Stmt> helperStmts;
    helperStmts.push_back(Stmt{std::make_shared<SExpr>(SExpr{Expr{superCall, body->loc}}), body->loc});
    helperStmts.insert(helperStmts.end(), stmtsToInsert.begin(), stmtsToInsert.end());
    helperStmts.push_back(Stmt{std::make_shared<SReturn>(SReturn{Expr{kEThisShared, body->loc}}), body->loc});

    if (this->options.optionsThatSupportStructuralEquality.minifySyntax) {
        helperStmts = this->mangleStmts(helperStmts, stmtsFnBody);
    }

    auto arrow = std::make_shared<EArrow>();
    arrow->has_rest_arg = true;
    arrow->prefer_expr = true;
    Arg arg;
    arg.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{argsRef}), body->loc};
    arrow->args.push_back(arg);
    arrow->body.loc = body->loc;
    arrow->body.block.stmts = std::move(helperStmts);

    std::vector<Decl> decls;
    Decl decl;
    decl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{superCtorRefParam}), body->loc};
    decl.value_or_nil = Expr{arrow, body->loc};
    decls.push_back(decl);
    auto local = std::make_shared<SLocal>();
    local->decls = decls;
    Stmt helperStmt{local, body->loc};

    body->block.stmts.insert(body->block.stmts.begin(), helperStmt);
}

} // namespace guchho::javascript
