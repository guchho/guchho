// ---------------------------------------------------------------------------
// JavaScript helper utilities
//
// This file implements the core helper functions used throughout the Guchho
// JavaScript pipeline: AST simplification, constant folding, side-effect
// analysis, boolean context optimization, template literal inlining, and
// various utility routines for expression/statement manipulation.
//
// Key design principle: most mutation-free functions are explicitly designed
// to be callable after the AST has been frozen (i.e. after parsing ends).
// This allows the optimizer, mangler, and printer to run without requiring
// a full AST clone pass.
// ---------------------------------------------------------------------------

#include "guchho/javascript/js_helpers.hpp"
#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/javascript/js_ast.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace guchho::javascript {

    // -----------------------------------------------------------------------
    // Nil checks
    // -----------------------------------------------------------------------

    // Returns true if the expression variant holds a null shared_ptr.
    // This is the canonical way to test for "empty" expression nodes.
    bool IsNil(const E& data) {
        return std::visit([](const auto& ptr) { return !ptr; }, data);
    }

    // Returns true if the statement variant holds a null shared_ptr.
    bool IsNil(const S& data) {
        return std::visit([](const auto& ptr) { return !ptr; }, data);
    }

    // Returns true if the binding variant holds a null shared_ptr.
    bool IsNil(const B& data) {
        return std::visit([](const auto& ptr) { return !ptr; }, data);
    }

    // -----------------------------------------------------------------------
    // Binding traversal helpers
    // -----------------------------------------------------------------------

    // Recursively collects every identifier binding found inside the given
    // binding pattern. Array/object destructuring is traversed recursively;
    // spread elements and default values are ignored.
    //
    // Example:
    //   Binding = [a, b, ...c]  =>  identifiers = [a, b, c]
    //   Binding = {x, y: z}     =>  identifiers = [x, z]
    void findIdentifiersInBinding(Binding binding, std::vector<Decl>& identifiers) {
        if (auto bi = Get<BIdentifier>(binding.data); bi != nullptr) {
            identifiers.push_back(Decl{binding, Expr{}});
        } else if (auto ba = Get<BArray>(binding.data); ba != nullptr) {
            for (const ArrayBinding& item : ba->items) {
                findIdentifiersInBinding(item.binding, identifiers);
            }
        } else if (auto bo = Get<BObject>(binding.data); bo != nullptr) {
            for (const PropertyBinding& property : bo->properties) {
                findIdentifiersInBinding(property.value, identifiers);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Dead-control-flow statement retention
    // -----------------------------------------------------------------------

    // Returns true if any statement in the vector should be kept even though
    // control flow has been determined to be dead. This is used by the dead
    // code elimination pass: expressions with side effects, throw statements,
    // break/continue/return, and class declarations are preserved.
    bool shouldKeepStmtsInDeadControlFlow(std::vector<Stmt>& stmts) {
        for (Stmt& child : stmts) {
            if (shouldKeepStmtInDeadControlFlow(child)) {
                return true;
            }
        }
        return false;
    }

    // Determines whether a single statement inside dead control flow must be
    // retained. Side-effect-free expressions, empty statements, and class
    // declarations are dropped. Variable declarations keep only their
    // identifiers (no initializers), since the initializer may have side
    // effects. Compound statements (blocks, try, if, loops, labels) are
    // handled recursively.
    bool shouldKeepStmtInDeadControlFlow(Stmt stmt) {
        S& s = stmt.data;
        if (Get<SEmpty>(s) != nullptr || Get<SExpr>(s) != nullptr || Get<SThrow>(s) != nullptr ||
            Get<SReturn>(s) != nullptr || Get<SBreak>(s) != nullptr || Get<SContinue>(s) != nullptr ||
            Get<SClass>(s) != nullptr || Get<SDebugger>(s) != nullptr) {
            // Omit these statements entirely
            return false;
        }

        if (auto sl = Get<SLocal>(s); sl != nullptr) {
            if (sl->kind != LocalKind::kVar) {
                // Omit these statements entirely
                return false;
            }

            // Omit everything except the identifiers
            std::vector<Decl> identifiers;
            for (Decl& decl : sl->decls) {
                findIdentifiersInBinding(decl.binding, identifiers);
            }
            if (identifiers.empty()) {
                return false;
            }
            sl->decls = identifiers;
            return true;
        }

        if (auto sb = Get<SBlock>(s); sb != nullptr) {
            return shouldKeepStmtsInDeadControlFlow(sb->stmts);
        }

        if (auto st = Get<STry>(s); st != nullptr) {
            return shouldKeepStmtsInDeadControlFlow(st->block.stmts) ||
                (st->catch_block != nullptr && shouldKeepStmtsInDeadControlFlow(st->catch_block->block.stmts)) ||
                (st->finally_block != nullptr && shouldKeepStmtsInDeadControlFlow(st->finally_block->block.stmts));
        }

        if (auto si = Get<SIf>(s); si != nullptr) {
            return shouldKeepStmtInDeadControlFlow(si->yes) ||
                (si->no_or_nil != nullptr && shouldKeepStmtInDeadControlFlow(*si->no_or_nil));
        }

        if (auto sw = Get<SWhile>(s); sw != nullptr) {
            return shouldKeepStmtInDeadControlFlow(sw->body);
        }

        if (auto sw = Get<SDoWhile>(s); sw != nullptr) {
            return shouldKeepStmtInDeadControlFlow(sw->body);
        }

        if (auto sf = Get<SFor>(s); sf != nullptr) {
            return (sf->init_or_nil != nullptr && shouldKeepStmtInDeadControlFlow(*sf->init_or_nil)) ||
                shouldKeepStmtInDeadControlFlow(sf->body);
        }

        if (auto sf = Get<SForIn>(s); sf != nullptr) {
            return (sf->init != nullptr && shouldKeepStmtInDeadControlFlow(*sf->init)) ||
                shouldKeepStmtInDeadControlFlow(sf->body);
        }

        if (auto sf = Get<SForOf>(s); sf != nullptr) {
            return (sf->init != nullptr && shouldKeepStmtInDeadControlFlow(*sf->init)) ||
                shouldKeepStmtInDeadControlFlow(sf->body);
        }

        if (auto sl = Get<SLabel>(s); sl != nullptr) {
            return shouldKeepStmtInDeadControlFlow(sl->stmt);
        }

        // Everything else must be kept
        return true;
    }

    // -----------------------------------------------------------------------
    // Expression equality
    // -----------------------------------------------------------------------

    // Compares two expressions for structural equality by location and pointer
    // identity of the underlying data. Two nil expressions are always equal.
    // This is a quick check used by the optimizer to detect whether an AST
    // node was mutated.
    //
    // Example:
    //   ExprEquals(Expr{}, Expr{})  =>  true  (both nil)
    //   ExprEquals(x, x)            =>  true  (same pointer)
    bool ExprEquals(const Expr& a, const Expr& b) {
        bool a_nil = IsNil(a.data);
        bool b_nil = IsNil(b.data);
        if (a_nil || b_nil) {
            return a_nil && b_nil;
        }
        return a.loc.start == b.loc.start && a.data == b.data;
    }

    // -----------------------------------------------------------------------
    // String utilities
    // -----------------------------------------------------------------------

    // Concatenates two UTF-16 strings. Trivial wrapper used throughout the
    // codebase for building up string values during constant folding.
    //
    // Example:
    //   JoinStrings(u"hello", u" world")  =>  u"hello world"
    std::u16string JoinStrings(const std::u16string& a, const std::u16string& b) {
        return a + b;
    }

    // Joins a vector of strings with commas, producing output suitable for
    // arguments lists, array patterns, etc.
    //
    // Example:
    //   JoinStringsWithComma({"a", "b", "c"})  =>  "a,b,c"
    //   JoinStringsWithComma({})                =>  ""
    std::string JoinStringsWithComma(const std::vector<std::string>& items) {
        std::string result;
        for (size_t i = 0; i < items.size(); i++) {
            if (i > 0) {
                result += ',';
            }
            result += items[i];
        }
        return result;
    }

    // Converts a signed 64-bit integer to a string representation in the
    // given radix (base 2-36). Radix 10 is handled as a fast path via
    // std::to_string. Negative values are formatted with a leading '-'.
    //
    // Example:
    //   FormatInt(255, 16)  =>  "ff"
    //   FormatInt(-42, 10)  =>  "-42"
    //   FormatInt(8, 2)     =>  "1000"
    std::string FormatInt(int64_t value, int radix) {
        if (radix == 10) {
            return std::to_string(value);
        }
        static const char* kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
        bool negative = value < 0;
        uint64_t magnitude = negative ? static_cast<uint64_t>(-(value + 1)) + 1 : static_cast<uint64_t>(value);
        std::string result;
        do {
            result.push_back(kDigits[magnitude % static_cast<uint64_t>(radix)]);
            magnitude /= static_cast<uint64_t>(radix);
        } while (magnitude != 0);
        if (negative) {
            result.push_back('-');
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    // -----------------------------------------------------------------------
    // UCS-2 string comparison
    // -----------------------------------------------------------------------

    // Lexicographic comparison of two UCS-2 spans. Returns a negative value
    // if a < b, zero if a == b, and a positive value if a > b. Used by the
    // constant folder to evaluate relational operators on string literals.
    //
    // Example:
    //   StringCompareUCS2(u"abc", u"abd")  =>  negative
    //   StringCompareUCS2(u"abc", u"abc")  =>  0
    //   StringCompareUCS2(u"abc", u"ab")   =>  positive
    int StringCompareUCS2(std::span<const char16_t> a, std::span<const char16_t> b) {
        if (a.size() > 0x10000000ULL || b.size() > 0x10000000ULL) {
            fprintf(stderr, "DBG StringCompareUCS2 HUGE SIZE %zu %zu\n", a.size(), b.size());
        }
        size_t n = a.size() < b.size() ? a.size() : b.size();
        for (size_t i = 0; i < n; i++) {
            int delta = static_cast<int>(a[i]) - static_cast<int>(b[i]);
            if (delta != 0) {
                return delta;
            }
        }
        return static_cast<int>(a.size()) - static_cast<int>(b.size());
    }

    // -----------------------------------------------------------------------
    // Numeric extraction
    // -----------------------------------------------------------------------

    // Attempts to extract a numeric constant from an expression node.
    // Transparently unwraps EAnnotation and EInlinedEnum wrappers.
    //
    // Example:
    //   ExtractNumericValue(ENumber{42.0})  =>  42.0
    //   ExtractNumericValue(EString{u"abc"}) =>  nullopt
    std::optional<double> ExtractNumericValue(const E& data) {
        if (const EAnnotation* e = Get<EAnnotation>(data)) {
            return ExtractNumericValue(e->value.data);
        }
        if (const EInlinedEnum* e = Get<EInlinedEnum>(data)) {
            return ExtractNumericValue(e->value.data);
        }
        if (const ENumber* e = Get<ENumber>(data)) {
            return e->value;
        }
        return std::nullopt;
    }

    // Attempts to extract numeric constants from both the left and right
    // operands of a binary expression. Returns nullopt if either operand
    // is not a numeric constant.
    //
    // Example:
    //   ExtractNumericValues(ENumber{1}, ENumber{2})  =>  pair(1.0, 2.0)
    //   ExtractNumericValues(ENumber{1}, EString{u"a"}) =>  nullopt
    std::optional<std::pair<double, double>> ExtractNumericValues(const Expr& left, const Expr& right) {
        std::optional<double> a = ExtractNumericValue(left.data);
        if (a) {
            std::optional<double> b = ExtractNumericValue(right.data);
            if (b) {
                return std::make_pair(*a, *b);
            }
        }
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // String extraction
    // -----------------------------------------------------------------------

    // Attempts to extract a string constant from an expression node.
    // Transparently unwraps EAnnotation and EInlinedEnum wrappers.
    //
    // Example:
    //   ExtractStringValue(EString{u"hello"})  =>  u"hello"
    //   ExtractStringValue(ENumber{42})         =>  nullopt
    std::optional<std::u16string> ExtractStringValue(const E& data) {
        if (const EAnnotation* e = Get<EAnnotation>(data)) {
            return ExtractStringValue(e->value.data);
        }
        if (const EInlinedEnum* e = Get<EInlinedEnum>(data)) {
            return ExtractStringValue(e->value.data);
        }
        if (const EString* e = Get<EString>(data)) {
            return e->value;
        }
        return std::nullopt;
    }

    // Attempts to extract string constants from both the left and right
    // operands. Returns nullopt if either operand is not a string constant.
    //
    // Example:
    //   ExtractStringValues(EString{u"a"}, EString{u"b"})  =>  pair(u"a", u"b")
    std::optional<std::pair<std::u16string, std::u16string>> ExtractStringValues(const Expr& left, const Expr& right) {
        std::optional<std::u16string> a = ExtractStringValue(left.data);
        if (a) {
            std::optional<std::u16string> b = ExtractStringValue(right.data);
            if (b) {
                return std::make_pair(*a, *b);
            }
        }
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Unused-string-addition chain simplification
    // -----------------------------------------------------------------------

    // Simplifies a chain of string addition expressions when the result is
    // unused. This allows the optimizer to strip string concatenation from
    // dead code while preserving any side-effectful subexpressions.
    //
    // The second element of the returned pair indicates whether the left
    // side was simplified to an empty string.
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    //
    // Example:
    //   SimplifyUnusedStringAdditionChain(EString{u"x"})
    //     =>  (EString{u""}, true)
    //   SimplifyUnusedStringAdditionChain(EString{u"x"} + y)
    //     =>  (EString{u""} + y, true)
    std::pair<Expr, bool> SimplifyUnusedStringAdditionChain(Expr expr) {
        if (Get<EString>(expr.data) != nullptr) {
            // "'x' + y" => "'' + y"
            return {Expr{std::make_shared<EString>(), expr.loc}, true};
        }

        if (const EBinary* e = Get<EBinary>(expr.data)) {
            if (e->op == OpCode::kBinOpAdd) {
                std::pair<Expr, bool> left = SimplifyUnusedStringAdditionChain(e->left);

                if (const EString* right = Get<EString>(e->right.data)) {
                    // "('' + x) + 'y'" => "'' + x"
                    if (left.second) {
                        return left;
                    }

                    // "x + 'y'" => "x + ''"
                    if (!left.second && !right->value.empty()) {
                        return {Expr{std::make_shared<EBinary>(EBinary{left.first, Expr{std::make_shared<EString>(), e->right.loc}, OpCode::kBinOpAdd}), expr.loc}, true};
                    }
                }

                // Don't mutate the original AST
                if (!ExprEquals(left.first, e->left)) {
                    expr.data = std::make_shared<EBinary>(EBinary{left.first, e->right, OpCode::kBinOpAdd});
                }

                return {expr, left.second};
            }
        }

        return {expr, false};
    }

    // -----------------------------------------------------------------------
    // Addition preprocessing
    // -----------------------------------------------------------------------

    // Pre-processes an operand of a binary `+` expression before constant
    // folding. Transparently unwraps EInlinedEnum nodes. Converts array
    // literals to their string representation (joining items with commas)
    // and converts empty object literals to the string "[object Object]".
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    //
    // Example:
    //   FoldAdditionPreProcess(EArray{[1, 2, 3]})  =>  EString{u"1,2,3"}
    //   FoldAdditionPreProcess(EObject{})            =>  EString{u"[object Object]"}
    Expr FoldAdditionPreProcess(Expr expr) {
        if (const EInlinedEnum* ie = Get<EInlinedEnum>(expr.data)) {
            // "See through" inline enum constants
            expr = ie->value;
        } else if (const EArray* arr = Get<EArray>(expr.data)) {
            // "[] + x" => "'' + x"
            // "[1,2] + x" => "'1,2' + x"
            std::vector<std::string> items;
            items.reserve(arr->items.size());
            for (Expr item : arr->items) {
                if (Get<EUndefined>(item.data) != nullptr || Get<ENull>(item.data) != nullptr) {
                    items.push_back("");
                    continue;
                }
                if (std::optional<std::string> str = ToStringWithoutSideEffects(item.data)) {
                    item = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(*str), {}, {}, {}, {}}), item.loc};
                }
                const EString* str = Get<EString>(item.data);
                if (str == nullptr) {
                    break;
                }
                items.push_back(helpers::UTF16ToString(str->value));
            }
            if (items.size() == arr->items.size()) {
                expr = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(JoinStringsWithComma(items)), {}, {}, {}, {}}), expr.loc};
            }
        } else if (const EObject* obj = Get<EObject>(expr.data)) {
            // "{} + x" => "'[object Object]' + x"
            if (obj->properties.empty()) {
                expr = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16("[object Object]"), {}, {}, {}, {}}), expr.loc};
            }
        }
        return expr;
    }



    // -----------------------------------------------------------------------
    // HelperContext construction
    // -----------------------------------------------------------------------

    // Constructs a HelperContext from a predicate that determines whether a
    // given binding reference is "unbound" (i.e. not declared in the current
    // scope). This is used for side-effect analysis and tree-shaking.
    HelperContext MakeHelperContext(std::function<bool(compiler::Ref)> is_unbound) {
        return HelperContext{std::move(is_unbound)};
    }

    // -----------------------------------------------------------------------
    // Property-access detection
    // -----------------------------------------------------------------------

    // Returns true if the expression is a dot or index property access.
    // When called as a function, such expressions capture the target as "this".
    //
    // Example:
    //   IsPropertyAccess(a.b)    =>  true
    //   IsPropertyAccess(a[b])   =>  true
    //   IsPropertyAccess(a())    =>  false
    bool IsPropertyAccess(const Expr& expr) {
        return Get<EDot>(expr.data) != nullptr || Get<EIndex>(expr.data) != nullptr;
    }

    // Returns true if the expression is part of an optional chain (i.e. uses
    // the ?. operator). This applies to dot access, index access, and call
    // expressions that have the optional_chain flag set.
    //
    // Example:
    //   IsOptionalChain(a?.b)   =>  true
    //   IsOptionalChain(a?.b()) =>  true
    //   IsOptionalChain(a.b)    =>  false
    bool IsOptionalChain(const Expr& value) {
        if (const EDot* e = Get<EDot>(value.data)) {
            return e->optional_chain != OptionalChain::kNone;
        }
        if (const EIndex* e = Get<EIndex>(value.data)) {
            return e->optional_chain != OptionalChain::kNone;
        }
        if (const ECall* e = Get<ECall>(value.data)) {
            return e->optional_chain != OptionalChain::kNone;
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // Assignment helpers
    // -----------------------------------------------------------------------

    // Creates an assignment expression: `a = b`.
    //
    // Example:
    //   Assign(x, ENumber{1})  =>  x = 1
    Expr Assign(const Expr& a, const Expr& b) {
        return Expr{std::make_shared<EBinary>(EBinary{a, b, OpCode::kBinOpAssign}), a.loc};
    }

    // Creates an expression statement wrapping an assignment: `a = b;`.
    //
    // Example:
    //   AssignStmt(x, ENumber{1})  =>  x = 1;
    Stmt AssignStmt(const Expr& a, const Expr& b) {
        return Stmt{std::make_shared<SExpr>(SExpr{Assign(a, b)}), a.loc};
    }

    // -----------------------------------------------------------------------
    // Logical NOT operator
    // -----------------------------------------------------------------------

    // Wraps the provided expression in the "!" prefix operator. The expression
    // will potentially be simplified to avoid generating unnecessary extra "!"
    // operators. For example, calling this with "!!x" will return "!x" instead
    // of returning "!!!x".
    //
    // Example:
    //   Not(EBoolean{true})   =>  EBoolean{false}
    //   Not(Not(x))           =>  Not(x)  (not !!x)
    Expr Not(const Expr& expr) {
        if (std::optional<Expr> result = MaybeSimplifyNot(expr)) {
            return *result;
        }
        return Expr{std::make_shared<EUnary>(EUnary{expr, OpCode::kUnOpNot}), expr.loc};
    }

    // Attempts to simplify the "!" prefix operator applied to the given
    // expression. Returns the simplified form if possible, or nullopt if no
    // simplification is applicable.
    //
    // This is separated from Not() to avoid allocation on failure when the
    // caller does not need the result.
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    //
    // Example:
    //   MaybeSimplifyNot(ENull{})       =>  EBoolean{true}
    //   MaybeSimplifyNot(ENumber{0})    =>  EBoolean{true}
    //   MaybeSimplifyNot(ENumber{42})   =>  EBoolean{false}
    //   MaybeSimplifyNot(EBoolean{x})   =>  nullopt
    std::optional<Expr> MaybeSimplifyNot(const Expr& expr) {
        if (const EAnnotation* ann = Get<EAnnotation>(expr.data)) {
            return MaybeSimplifyNot(ann->value);
        }

        if (const EInlinedEnum* ie = Get<EInlinedEnum>(expr.data)) {
            if (std::optional<Expr> value = MaybeSimplifyNot(ie->value)) {
                return value;
            }
        } else if (Get<ENull>(expr.data) != nullptr || Get<EUndefined>(expr.data) != nullptr) {
            return Expr{std::make_shared<EBoolean>(EBoolean{true}), expr.loc};
        } else if (const EBoolean* bool_node = Get<EBoolean>(expr.data)) {
            return Expr{std::make_shared<EBoolean>(EBoolean{!bool_node->value}), expr.loc};
        } else if (const ENumber* num = Get<ENumber>(expr.data)) {
            return Expr{std::make_shared<EBoolean>(EBoolean{num->value == 0 || std::isnan(num->value)}), expr.loc};
        } else if (const EBigInt* bi = Get<EBigInt>(expr.data)) {
            if (std::optional<bool> equal = CheckEqualityBigInt(bi->value, "0")) {
                return Expr{std::make_shared<EBoolean>(EBoolean{*equal}), expr.loc};
            }
        } else if (const EString* str = Get<EString>(expr.data)) {
            return Expr{std::make_shared<EBoolean>(EBoolean{str->value.empty()}), expr.loc};
        } else if (Get<EFunction>(expr.data) != nullptr || Get<EArrow>(expr.data) != nullptr || Get<ERegExp>(expr.data) != nullptr) {
            return Expr{std::make_shared<EBoolean>(EBoolean{false}), expr.loc};
        } else if (const EUnary* unary = Get<EUnary>(expr.data)) {
            // "!!!a" => "!a"
            if (unary->op == OpCode::kUnOpNot && KnownPrimitiveType(unary->value.data) == PrimitiveType::kBoolean) {
                return unary->value;
            }
        } else if (const EBinary* binary = Get<EBinary>(expr.data)) {
            // Make sure that these transformations are all safe for special values.
            // For example, "!(a < b)" is not the same as "a >= b" if a and/or b are
            // NaN (or undefined, or null, or possibly other problem cases too).
            switch (binary->op) {
                case OpCode::kBinOpLooseEq:
                    // "!(a == b)" => "a != b"
                    return Expr{std::make_shared<EBinary>(EBinary{binary->left, binary->right, OpCode::kBinOpLooseNe}), expr.loc};

                case OpCode::kBinOpLooseNe:
                    // "!(a != b)" => "a == b"
                    return Expr{std::make_shared<EBinary>(EBinary{binary->left, binary->right, OpCode::kBinOpLooseEq}), expr.loc};

                case OpCode::kBinOpStrictEq:
                    // "!(a === b)" => "a !== b"
                    return Expr{std::make_shared<EBinary>(EBinary{binary->left, binary->right, OpCode::kBinOpStrictNe}), expr.loc};

                case OpCode::kBinOpStrictNe:
                    // "!(a !== b)" => "a === b"
                    return Expr{std::make_shared<EBinary>(EBinary{binary->left, binary->right, OpCode::kBinOpStrictEq}), expr.loc};

                case OpCode::kBinOpComma:
                    // "!(a, b)" => "a, !b"
                    return Expr{std::make_shared<EBinary>(EBinary{binary->left, Not(binary->right), OpCode::kBinOpComma}), expr.loc};
                default:
                    break;
            }
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Equality comparison simplification
    // -----------------------------------------------------------------------

    // Attempts to simplify equality/inequality comparisons against primitive
    // values. This handles cases such as:
    //   "!x === true"   =>  "!x"
    //   "!x !== true"   =>  "!!x"
    //   "typeof x != 'undefined'"  =>  "typeof x < 'u'"
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    std::optional<Expr> MaybeSimplifyEqualityComparison(logger::Loc loc, const EBinary& e, compat::JSFeature unsupported_features) {
        Expr value = e.left;
        Expr primitive = e.right;

        // Detect when the primitive comes first and flip the order of our checks
        if (IsPrimitiveLiteral(value.data)) {
            std::swap(value, primitive);
        }

        // "!x === true" => "!x"
        // "!x === false" => "!!x"
        // "!x !== true" => "!!x"
        // "!x !== false" => "!x"
        if (const EBoolean* boolean = Get<EBoolean>(primitive.data)) {
            if (KnownPrimitiveType(value.data) == PrimitiveType::kBoolean) {
                if (boolean->value == (e.op == OpCode::kBinOpLooseNe || e.op == OpCode::kBinOpStrictNe)) {
                    return Not(value);
                } else {
                    return value;
                }
            }
        }

        // "typeof x != 'undefined'" => "typeof x < 'u'"
        // "typeof x == 'undefined'" => "typeof x > 'u'"
        if (!compat::Has(unsupported_features, compat::JSFeature::kTypeofExoticObjectIsObject)) {
            // Only do this optimization if we know that the "typeof" operator won't
            // return something random. The only case of this happening was Internet
            // Explorer returning "unknown" for some objects, which messes with this
            // optimization. So we don't do this when targeting Internet Explorer.
            if (const EUnary* typeof_unary = Get<EUnary>(value.data)) {
                if (typeof_unary->op == OpCode::kUnOpTypeof) {
                    if (const EString* str = Get<EString>(primitive.data)) {
                        if (helpers::UTF16EqualsString(str->value, "undefined")) {
                            bool flip = ExprEquals(value, e.right);
                            OpCode op = OpCode::kBinOpLt;
                            if ((e.op == OpCode::kBinOpLooseEq || e.op == OpCode::kBinOpStrictEq) != flip) {
                                op = OpCode::kBinOpGt;
                            }
                            primitive = Expr{std::make_shared<EString>(EString{u"u", {}, {}, {}, {}}), primitive.loc};
                            if (flip) {
                                std::swap(value, primitive);
                            }
                            return Expr{std::make_shared<EBinary>(EBinary{value, primitive, op}), loc};
                        }
                    }
                }
            }
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Symbol instance detection
    // -----------------------------------------------------------------------

    // Returns true if the expression is a well-known Symbol instance
    // (e.g. Symbol.iterator). This is used to determine whether a computed
    // property key is a side-effect-free symbol access.
    //
    // Example:
    //   IsSymbolInstance(Symbol.iterator)  =>  true
    //   IsSymbolInstance("foo")            =>  false
    bool IsSymbolInstance(const E& data) {
        if (const EDot* e = Get<EDot>(data)) {
            return e->is_symbol_instance;
        }
        if (const EIndex* e = Get<EIndex>(data)) {
            return e->is_symbol_instance;
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // Primitive literal detection
    // -----------------------------------------------------------------------

    // Returns true if the expression is a primitive literal: null, undefined,
    // string, boolean, number, or bigint. Annotations and inline enums are
    // transparently unwrapped.
    //
    // Example:
    //   IsPrimitiveLiteral(ENumber{42})     =>  true
    //   IsPrimitiveLiteral(EString{u"hi"})  =>  true
    //   IsPrimitiveLiteral(EObject{})        =>  false
    bool IsPrimitiveLiteral(const E& data) {
        if (const EAnnotation* e = Get<EAnnotation>(data)) {
            return IsPrimitiveLiteral(e->value.data);
        }
        if (const EInlinedEnum* e = Get<EInlinedEnum>(data)) {
            return IsPrimitiveLiteral(e->value.data);
        }
        return Get<ENull>(data) != nullptr || Get<EUndefined>(data) != nullptr || Get<EString>(data) != nullptr ||
            Get<EBoolean>(data) != nullptr || Get<ENumber>(data) != nullptr || Get<EBigInt>(data) != nullptr;
    }

    // -----------------------------------------------------------------------
    // Primitive type inference
    // -----------------------------------------------------------------------

    // Merges two known primitive types. Returns kUnknown if either type is
    // unknown. If both are known but different, returns kMixed (meaning the
    // result is definitely some kind of primitive but the specific kind
    // depends on the branch taken).
    //
    // Example:
    //   MergedKnownPrimitiveTypes(ENumber{1}, ENumber{2})  =>  kNumber
    //   MergedKnownPrimitiveTypes(ENumber{1}, EBoolean{1})  =>  kMixed
    //   MergedKnownPrimitiveTypes(ENumber{1}, EObject{})     =>  kUnknown
    PrimitiveType MergedKnownPrimitiveTypes(const Expr& a, const Expr& b) {
        PrimitiveType x = KnownPrimitiveType(a.data);
        if (x == PrimitiveType::kUnknown) {
            return PrimitiveType::kUnknown;
        }

        PrimitiveType y = KnownPrimitiveType(b.data);
        if (y == PrimitiveType::kUnknown) {
            return PrimitiveType::kUnknown;
        }

        if (x == y) {
            return x;
        }
        return PrimitiveType::kMixed; // Definitely some kind of primitive
    }

    // Infers the known primitive type of an expression at compile time. This
    // function does NOT say whether the expression has side effects -- it only
    // reports the type if it can be statically determined.
    //
    // Annotations and inline enums are transparently unwrapped. Ternary
    // expressions return the merged type of both branches. Unary and binary
    // operators return the type implied by their result.
    //
    // Example:
    //   KnownPrimitiveType(ENumber{42})       =>  kNumber
    //   KnownPrimitiveType(EString{u"hi"})    =>  kString
    //   KnownPrimitiveType(EIf{test, x, y})   =>  MergedKnownPrimitiveTypes(x, y)
    //   KnownPrimitiveType(EUnary{typeof, _}) =>  kString
    PrimitiveType KnownPrimitiveType(const E& expr) {
        if (const EAnnotation* e = Get<EAnnotation>(expr)) {
            return KnownPrimitiveType(e->value.data);
        }
        if (const EInlinedEnum* e = Get<EInlinedEnum>(expr)) {
            return KnownPrimitiveType(e->value.data);
        }
        if (Get<ENull>(expr) != nullptr) {
            return PrimitiveType::kNull;
        }
        if (Get<EUndefined>(expr) != nullptr) {
            return PrimitiveType::kUndefined;
        }
        if (Get<EBoolean>(expr) != nullptr) {
            return PrimitiveType::kBoolean;
        }
        if (Get<ENumber>(expr) != nullptr) {
            return PrimitiveType::kNumber;
        }
        if (Get<EString>(expr) != nullptr) {
            return PrimitiveType::kString;
        }
        if (Get<EBigInt>(expr) != nullptr) {
            return PrimitiveType::kBigInt;
        }
        if (const ETemplate* e = Get<ETemplate>(expr)) {
            if (IsNil(e->tag_or_nil.data)) {
                return PrimitiveType::kString;
            }
        } else if (const EIf* eIf = Get<EIf>(expr)) {
            return MergedKnownPrimitiveTypes(eIf->yes, eIf->no);
        } else if (const EUnary* eUn = Get<EUnary>(expr)) {
            switch (eUn->op) {
                case OpCode::kUnOpVoid:
                    return PrimitiveType::kUndefined;

                case OpCode::kUnOpTypeof:
                    return PrimitiveType::kString;

                case OpCode::kUnOpNot:
                case OpCode::kUnOpDelete:
                    return PrimitiveType::kBoolean;

                case OpCode::kUnOpPos:
                    return PrimitiveType::kNumber; // Cannot be bigint because that throws an exception

                case OpCode::kUnOpNeg:
                case OpCode::kUnOpCpl: {
                    PrimitiveType value = KnownPrimitiveType(eUn->value.data);
                    if (value == PrimitiveType::kBigInt) {
                        return PrimitiveType::kBigInt;
                    }
                    if (value != PrimitiveType::kUnknown && value != PrimitiveType::kMixed) {
                        return PrimitiveType::kNumber;
                    }
                    return PrimitiveType::kMixed; // Can be number or bigint
                }

                case OpCode::kUnOpPreDec:
                case OpCode::kUnOpPreInc:
                case OpCode::kUnOpPostDec:
                case OpCode::kUnOpPostInc:
                    return PrimitiveType::kMixed; // Can be number or bigint
                default:
                    break;     
            }
        } else if (const EBinary* eBin = Get<EBinary>(expr)) {
            switch (eBin->op) {
                case OpCode::kBinOpStrictEq:
                case OpCode::kBinOpStrictNe:
                case OpCode::kBinOpLooseEq:
                case OpCode::kBinOpLooseNe:
                case OpCode::kBinOpLt:
                case OpCode::kBinOpGt:
                case OpCode::kBinOpLe:
                case OpCode::kBinOpGe:
                case OpCode::kBinOpInstanceof:
                case OpCode::kBinOpIn:
                    return PrimitiveType::kBoolean;

                case OpCode::kBinOpLogicalOr:
                case OpCode::kBinOpLogicalAnd:
                    return MergedKnownPrimitiveTypes(eBin->left, eBin->right);

                case OpCode::kBinOpNullishCoalescing: {
                    PrimitiveType left = KnownPrimitiveType(eBin->left.data);
                    PrimitiveType right = KnownPrimitiveType(eBin->right.data);
                    if (left == PrimitiveType::kNull || left == PrimitiveType::kUndefined) {
                        return right;
                    }
                    if (left != PrimitiveType::kUnknown) {
                        if (left != PrimitiveType::kMixed) {
                            return left; // Definitely not null or undefined
                        }
                        if (right != PrimitiveType::kUnknown) {
                            return PrimitiveType::kMixed; // Definitely some kind of primitive
                        }
                    }
                    break;
                }

                case OpCode::kBinOpAdd: {
                    PrimitiveType left = KnownPrimitiveType(eBin->left.data);
                    PrimitiveType right = KnownPrimitiveType(eBin->right.data);
                    if (left == PrimitiveType::kString || right == PrimitiveType::kString) {
                        return PrimitiveType::kString;
                    }
                    if (left == PrimitiveType::kBigInt && right == PrimitiveType::kBigInt) {
                        return PrimitiveType::kBigInt;
                    }
                    if (left != PrimitiveType::kUnknown && left != PrimitiveType::kMixed && left != PrimitiveType::kBigInt &&
                        right != PrimitiveType::kUnknown && right != PrimitiveType::kMixed && right != PrimitiveType::kBigInt) {
                        return PrimitiveType::kNumber;
                    }
                    return PrimitiveType::kMixed; // Can be number or bigint or string (or an exception)
                }

                case OpCode::kBinOpAddAssign: {
                    PrimitiveType right = KnownPrimitiveType(eBin->right.data);
                    if (right == PrimitiveType::kString) {
                        return PrimitiveType::kString;
                    }
                    return PrimitiveType::kMixed; // Can be number or bigint or string (or an exception)
                }

                case OpCode::kBinOpSub:
                case OpCode::kBinOpSubAssign:
                case OpCode::kBinOpMul:
                case OpCode::kBinOpMulAssign:
                case OpCode::kBinOpDiv:
                case OpCode::kBinOpDivAssign:
                case OpCode::kBinOpRem:
                case OpCode::kBinOpRemAssign:
                case OpCode::kBinOpPow:
                case OpCode::kBinOpPowAssign:
                case OpCode::kBinOpBitwiseAnd:
                case OpCode::kBinOpBitwiseAndAssign:
                case OpCode::kBinOpBitwiseOr:
                case OpCode::kBinOpBitwiseOrAssign:
                case OpCode::kBinOpBitwiseXor:
                case OpCode::kBinOpBitwiseXorAssign:
                case OpCode::kBinOpShl:
                case OpCode::kBinOpShlAssign:
                case OpCode::kBinOpShr:
                case OpCode::kBinOpShrAssign:
                case OpCode::kBinOpUShr:
                case OpCode::kBinOpUShrAssign:
                    return PrimitiveType::kMixed; // Can be number or bigint (or an exception)

                case OpCode::kBinOpAssign:
                case OpCode::kBinOpComma:
                    return KnownPrimitiveType(eBin->right.data);
                default:
                    break;     
            }
        }

        return PrimitiveType::kUnknown;
    }

    // -----------------------------------------------------------------------
    // Strict-to-loose equality conversion
    // -----------------------------------------------------------------------

    // Returns true if a strict equality comparison (===) between two
    // expressions can be safely converted to a loose comparison (==). This
    // is only safe when both operands have the same statically-known primitive
    // type and that type is neither unknown nor mixed.
    //
    // Example:
    //   CanChangeStrictToLoose(ENumber{1}, ENumber{2})  =>  true
    //   CanChangeStrictToLoose(EString{u"x"}, ENumber{1}) =>  false
    bool CanChangeStrictToLoose(const Expr& a, const Expr& b) {
        PrimitiveType x = KnownPrimitiveType(a.data);
        PrimitiveType y = KnownPrimitiveType(b.data);
        return x == y && x != PrimitiveType::kUnknown && x != PrimitiveType::kMixed;
    }

    // -----------------------------------------------------------------------
    // Typeof without side effects
    // -----------------------------------------------------------------------

    // Returns the known result of the "typeof" operator on an expression, if
    // the expression has no side effects. This allows the optimizer to replace
    // "typeof x" with a string literal when the type is statically known.
    //
    // Example:
    //   TypeofWithoutSideEffects(ENull{})       =>  "object"
    //   TypeofWithoutSideEffects(EUndefined{})  =>  "undefined"
    //   TypeofWithoutSideEffects(EBoolean{true})=>  "boolean"
    //   TypeofWithoutSideEffects(ENumber{42})    =>  "number"
    std::optional<std::string> TypeofWithoutSideEffects(const E& data) {
        if (const EAnnotation* e = Get<EAnnotation>(data)) {
            if (Has(e->flags, AnnotationFlags::kCanBeRemovedIfUnused)) {
                return TypeofWithoutSideEffects(e->value.data);
            }
        } else if (const EInlinedEnum* eInlinedEnum = Get<EInlinedEnum>(data)) {
            return TypeofWithoutSideEffects(eInlinedEnum->value.data);
        } else if (Get<ENull>(data) != nullptr) {
            return std::string("object");
        } else if (Get<EUndefined>(data) != nullptr) {
            return std::string("undefined");
        } else if (Get<EBoolean>(data) != nullptr) {
            return std::string("boolean");
        } else if (Get<ENumber>(data) != nullptr) {
            return std::string("number");
        } else if (Get<EBigInt>(data) != nullptr) {
            return std::string("bigint");
        } else if (Get<EString>(data) != nullptr) {
            return std::string("string");
        } else if (Get<EFunction>(data) != nullptr || Get<EArrow>(data) != nullptr) {
            return std::string("function");
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Left-associative operator joining
    // -----------------------------------------------------------------------

    // "Rotates" the AST to exploit left-associativity of a binary operator,
    // avoiding unnecessary parentheses. When the right operand is itself the
    // same operator, the tree is restructured so that the left-associative
    // grouping is explicit.
    //
    // IMPORTANT: Only call this with operators that are actually associative.
    // The "+" operator is NOT associative for floating-point numbers (due to
    // rounding).
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    //
    // Example:
    //   JoinWithLeftAssociativeOp(kBinOpBitwiseOr, a, b | c)  =>  (a | b) | c
    Expr JoinWithLeftAssociativeOp(OpCode op, Expr a, Expr b) {
        // "(a, b) op c" => "a, b op c"
        if (const EBinary* comma = Get<EBinary>(a.data)) {
            if (comma->op == OpCode::kBinOpComma) {
                // Don't mutate the original AST
                EBinary clone = *comma;
                clone.right = JoinWithLeftAssociativeOp(op, clone.right, b);
                return Expr{std::make_shared<EBinary>(clone), a.loc};
            }
        }

        // "a op (b op c)" => "(a op b) op c"
        // "a op (b op (c op d))" => "((a op b) op c) op d"
        while (true) {
            if (const EBinary* binary = Get<EBinary>(b.data)) {
                if (binary->op == op) {
                    a = JoinWithLeftAssociativeOp(op, a, binary->left);
                    Expr newB = binary->right;
                    b = newB;
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        // "a op b" => "a op b"
        // "(a op b) op c" => "(a op b) op c"
        return Expr{std::make_shared<EBinary>(EBinary{a, b, op}), a.loc};
    }

    // Joins two expressions with the comma operator, handling nil operands.
    //
    // Example:
    //   JoinWithComma(nil, x)       =>  x
    //   JoinWithComma(x, nil)       =>  x
    //   JoinWithComma(x, y)         =>  (x, y)
    Expr JoinWithComma(Expr a, Expr b) {
        if (IsNil(a.data)) {
            return b;
        }
        if (IsNil(b.data)) {
            return a;
        }
        return Expr{std::make_shared<EBinary>(EBinary{a, b, OpCode::kBinOpComma}), a.loc};
    }

    // Joins a vector of expressions with the comma operator. Nil expressions
    // are skipped. Returns a single nil expression if all inputs are nil.
    //
    // Example:
    //   JoinAllWithComma({x, y, z})  =>  (x, y, z)
    //   JoinAllWithComma({})          =>  nil
    Expr JoinAllWithComma(const std::vector<Expr>& all) {
        Expr result;
        for (const Expr& value : all) {
            result = JoinWithComma(result, value);
        }
        return result;
    }

    // -----------------------------------------------------------------------
    // Binding-to-expression conversion
    // -----------------------------------------------------------------------

    // Converts a destructuring binding pattern into an expression equivalent.
    // This is used when lowering destructuring assignments or parameters.
    // An optional wrap_identifier callback allows the caller to control how
    // identifier bindings are represented in the output.
    //
    // Example:
    //   ConvertBindingToExpr([a, b])  =>  [a, b]  (as EArray)
    //   ConvertBindingToExpr({x, y})  =>  {x, y}  (as EObject)
    Expr ConvertBindingToExpr(const Binding& binding, const std::function<Expr(logger::Loc, compiler::Ref)>& wrap_identifier) {
        logger::Loc loc = binding.loc;

        if (Get<BMissing>(binding.data) != nullptr) {
            return Expr{kEMissingShared, loc};
        }

        if (const BIdentifier* b = Get<BIdentifier>(binding.data)) {
            if (wrap_identifier) {
                return wrap_identifier(loc, b->ref);
            }
            return Expr{std::make_shared<EIdentifier>(EIdentifier{b->ref}), loc};
        }

        if (const BArray* b = Get<BArray>(binding.data)) {
            std::vector<Expr> exprs;
            exprs.reserve(b->items.size());
            for (size_t i = 0; i < b->items.size(); i++) {
                const ArrayBinding& item = b->items[i];
                Expr expr = ConvertBindingToExpr(item.binding, wrap_identifier);
                if (b->has_spread && i + 1 == b->items.size()) {
                    expr = Expr{std::make_shared<ESpread>(ESpread{expr}), expr.loc};
                } else if (!IsNil(item.default_value_or_nil.data)) {
                    expr = Assign(expr, item.default_value_or_nil);
                }
                exprs.push_back(expr);
            }
            return Expr{std::make_shared<EArray>(EArray{exprs, logger::Loc{}, logger::Loc{}, b->is_single_line}), loc};
        }

        if (const BObject* b = Get<BObject>(binding.data)) {
            std::vector<Property> properties;
            properties.reserve(b->properties.size());
            for (const PropertyBinding& property : b->properties) {
                Expr value = ConvertBindingToExpr(property.value, wrap_identifier);
                PropertyKind kind = PropertyKind::kField;
                if (property.is_spread) {
                    kind = PropertyKind::kSpread;
                }
                PropertyFlags flags = PropertyFlags::kNone;
                if (property.is_computed) {
                    flags |= PropertyFlags::kIsComputed;
                }
                properties.push_back(Property{nullptr, property.key, value, property.default_value_or_nil, {}, logger::Loc{}, logger::Loc{}, kind, flags});
            }
            return Expr{std::make_shared<EObject>(EObject{properties, logger::Loc{}, logger::Loc{}, b->is_single_line}), loc};
        }

        throw std::runtime_error("Internal error");
    }

    // -----------------------------------------------------------------------
    // Unused expression simplification (HelperContext)
    // -----------------------------------------------------------------------

    // Simplifies an expression whose result is unused. Returns a nil expression
    // if the expression can be totally removed. Otherwise, returns a simplified
    // form that preserves side effects but drops the unused result.
    //
    // This handles: pure call removal, IIFE shortening, unused array/object
    // elements, ternary branch simplification, logical operator short-circuit
    // optimization, and optional chain insertion.
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    Expr HelperContext::SimplifyUnusedExpr(const Expr& expr, compat::JSFeature unsupported_features) {
        if (const EAnnotation* e = Get<EAnnotation>(expr.data)) {
            if (Has(e->flags, AnnotationFlags::kCanBeRemovedIfUnused)) {
                return Expr{};
            }
        } else if (const EInlinedEnum* eInlinedEnum = Get<EInlinedEnum>(expr.data)) {
            return SimplifyUnusedExpr(eInlinedEnum->value, unsupported_features);
        } else if (Get<ENull>(expr.data) != nullptr || Get<EUndefined>(expr.data) != nullptr || Get<EMissing>(expr.data) != nullptr ||
                Get<EBoolean>(expr.data) != nullptr || Get<ENumber>(expr.data) != nullptr || Get<EBigInt>(expr.data) != nullptr ||
                Get<EString>(expr.data) != nullptr || Get<EThis>(expr.data) != nullptr || Get<ERegExp>(expr.data) != nullptr ||
                Get<EFunction>(expr.data) != nullptr || Get<EArrow>(expr.data) != nullptr || Get<EImportMeta>(expr.data) != nullptr) {
            return Expr{};
        } else if (const EDot* eDot = Get<EDot>(expr.data)) {
            if (eDot->can_be_removed_if_unused) {
                return Expr{};
            }
        } else if (const EIdentifier* eIdent = Get<EIdentifier>(expr.data)) {
            if (eIdent->must_keep_due_to_with_stmt) {
                // Keep the identifier
            } else if (eIdent->can_be_removed_if_unused || !is_unbound(eIdent->ref)) {
                return Expr{};
            }
        } else if (const ETemplate* eTempl = Get<ETemplate>(expr.data)) {
            if (IsNil(eTempl->tag_or_nil.data)) {
                Expr comma;
                logger::Loc template_loc;
                std::unique_ptr<ETemplate> template_node;
                for (const TemplatePart& part : eTempl->parts) {
                    // If we know this value is some kind of primitive, then we know that
                    // "ToString" has no side effects and can be avoided.
                    if (KnownPrimitiveType(part.value.data) != PrimitiveType::kUnknown) {
                        if (template_node) {
                            comma = JoinWithComma(comma, Expr{std::make_shared<ETemplate>(*template_node), template_loc});
                            template_node.reset();
                        }
                        comma = JoinWithComma(comma, SimplifyUnusedExpr(part.value, unsupported_features));
                        continue;
                    }

                    // Make sure "ToString" is still evaluated on the value. We can't use
                    // string addition here because that may evaluate "ValueOf" instead.
                    if (!template_node) {
                        template_node = std::make_unique<ETemplate>();
                        template_loc = part.value.loc;
                    }
                    template_node->parts.push_back(TemplatePart{part.value, {}, {}, true, {}});
                }
                if (template_node) {
                    comma = JoinWithComma(comma, Expr{std::make_shared<ETemplate>(*template_node), template_loc});
                }
                return comma;
            } else if (eTempl->can_be_unwrapped_if_unused) {
                // If the function call was annotated as being able to be removed if the
                // result is unused, then we can remove it and just keep the arguments.
                // Note that there are no implicit "ToString" operations for tagged
                // template literals.
                Expr comma;
                for (const TemplatePart& part : eTempl->parts) {
                    comma = JoinWithComma(comma, SimplifyUnusedExpr(part.value, unsupported_features));
                }
                return comma;
            }
        } else if (const EArray* eArr = Get<EArray>(expr.data)) {
            // Arrays with "..." spread expressions can't be unwrapped because the
            // "..." triggers code evaluation via iterators. In that case, just trim
            // the other items instead and leave the array expression there.
            for (const Expr& spread : eArr->items) {
                if (Get<ESpread>(spread.data) != nullptr) {
                    std::vector<Expr> items;
                    items.reserve(eArr->items.size());
                    for (const Expr& item : eArr->items) {
                        Expr simplified = SimplifyUnusedExpr(item, unsupported_features);
                        if (!IsNil(simplified.data)) {
                            items.push_back(simplified);
                        }
                    }

                    // Don't mutate the original AST
                    EArray clone = *eArr;
                    clone.items = std::move(items);
                    return Expr{std::make_shared<EArray>(clone), expr.loc};
                }
            }

            // Otherwise, the array can be completely removed. We only need to keep any
            // array items with side effects. Apply this simplification recursively.
            Expr result;
            for (const Expr& item : eArr->items) {
                result = JoinWithComma(result, SimplifyUnusedExpr(item, unsupported_features));
            }
            return result;
        } else if (const EObject* eObj = Get<EObject>(expr.data)) {
            // Objects with "..." spread expressions can't be unwrapped because the
            // "..." triggers code evaluation via getters. In that case, just trim
            // the other items instead and leave the object expression there.
            for (const Property& spread : eObj->properties) {
                if (spread.kind == PropertyKind::kSpread) {
                    std::vector<Property> properties;
                    properties.reserve(eObj->properties.size());
                    for (Property property : eObj->properties) {
                        // Spread properties must always be evaluated
                        if (property.kind != PropertyKind::kSpread) {
                            Expr value = SimplifyUnusedExpr(property.value_or_nil, unsupported_features);
                            if (!IsNil(value.data)) {
                                // Keep the value
                                property.value_or_nil = value;
                            } else if (!Has(property.flags, PropertyFlags::kIsComputed)) {
                                // Skip this property if the key doesn't need to be computed
                                continue;
                            } else {
                                // Replace values without side effects with "0" because it's short
                                property.value_or_nil = Expr{std::make_shared<ENumber>(), property.value_or_nil.loc};
                            }
                        }
                        properties.push_back(property);
                    }

                    // Don't mutate the original AST
                    EObject clone = *eObj;
                    clone.properties = std::move(properties);
                    return Expr{std::make_shared<EObject>(clone), expr.loc};
                }
            }

            // Otherwise, the object can be completely removed. We only need to keep any
            // object properties with side effects. Apply this simplification recursively.
            Expr result;
            for (const Property& property : eObj->properties) {
                if (Has(property.flags, PropertyFlags::kIsComputed)) {
                    // Make sure "ToString" is still evaluated on the key
                    result = JoinWithComma(result, Expr{std::make_shared<EBinary>(EBinary{
                                                        property.key,
                                                        Expr{std::make_shared<EString>(), property.key.loc},
                                                        OpCode::kBinOpAdd,
                                                    }),
                                                        property.key.loc});
                }
                result = JoinWithComma(result, SimplifyUnusedExpr(property.value_or_nil, unsupported_features));
            }
            return result;
        } else if (const EIf* eIf = Get<EIf>(expr.data)) {
            Expr yes = SimplifyUnusedExpr(eIf->yes, unsupported_features);
            Expr no = SimplifyUnusedExpr(eIf->no, unsupported_features);

            // "foo() ? 1 : 2" => "foo()"
            if (IsNil(yes.data) && IsNil(no.data)) {
                return SimplifyUnusedExpr(eIf->test, unsupported_features);
            }

            // "foo() ? 1 : bar()" => "foo() || bar()"
            if (IsNil(yes.data)) {
                return JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, eIf->test, no);
            }

            // "foo() ? bar() : 2" => "foo() && bar()"
            if (IsNil(no.data)) {
                return JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, eIf->test, yes);
            }

            if (!ExprEquals(yes, eIf->yes) || !ExprEquals(no, eIf->no)) {
                return Expr{std::make_shared<EIf>(EIf{eIf->test, yes, no}), expr.loc};
            }
        } else if (const EUnary* eUn = Get<EUnary>(expr.data)) {
            switch (eUn->op) {
                // These operators must not have any type conversions that can execute code
                // such as "toString" or "valueOf". They must also never throw any exceptions.
                case OpCode::kUnOpVoid:
                case OpCode::kUnOpNot:
                    return SimplifyUnusedExpr(eUn->value, unsupported_features);

                case OpCode::kUnOpNeg:
                    if (Get<EBigInt>(eUn->value.data) != nullptr) {
                        // Consider negated bigints to have no side effects
                        return Expr{};
                    }
                    break;

                case OpCode::kUnOpTypeof:
                    if (Get<EIdentifier>(eUn->value.data) != nullptr && eUn->was_originally_typeof_identifier) {
                        // "typeof x" must not be transformed into "x" since doing so could
                        // cause an exception to be thrown. Instead we can just remove it since
                        // "typeof x" is special-cased in the standard to never throw.
                        return Expr{};
                    }
                    return SimplifyUnusedExpr(eUn->value, unsupported_features);
                default:
                    break;     
            }
        } else if (const EBinary* eBin = Get<EBinary>(expr.data)) {
            Expr left = eBin->left;
            Expr right = eBin->right;

            switch (eBin->op) {
                // These operators must not have any type conversions that can execute code
                // such as "toString" or "valueOf". They must also never throw any exceptions.
                case OpCode::kBinOpStrictEq:
                case OpCode::kBinOpStrictNe:
                case OpCode::kBinOpComma:
                    return JoinWithComma(SimplifyUnusedExpr(left, unsupported_features), SimplifyUnusedExpr(right, unsupported_features));

                // We can simplify "==" and "!=" even though they can call "toString" and/or
                // "valueOf" if we can statically determine that the types of both sides are
                // primitives. In that case there won't be any chance for user-defined
                // "toString" and/or "valueOf" to be called.
                case OpCode::kBinOpLooseEq:
                case OpCode::kBinOpLooseNe:
                    if (MergedKnownPrimitiveTypes(left, right) != PrimitiveType::kUnknown) {
                        return JoinWithComma(SimplifyUnusedExpr(left, unsupported_features), SimplifyUnusedExpr(right, unsupported_features));
                    }
                    break;

                case OpCode::kBinOpLogicalAnd:
                case OpCode::kBinOpLogicalOr:
                case OpCode::kBinOpNullishCoalescing: {
                    // If this is a boolean logical operation and the result is unused, then
                    // we know the left operand will only be used for its boolean value and
                    // can be simplified under that assumption
                    if (eBin->op != OpCode::kBinOpNullishCoalescing) {
                        left = SimplifyBooleanExpr(left);
                    }

                    // Preserve short-circuit behavior: the left expression is only unused if
                    // the right expression can be completely removed. Otherwise, the left
                    // expression is important for the branch.
                    right = SimplifyUnusedExpr(right, unsupported_features);
                    if (IsNil(right.data)) {
                        return SimplifyUnusedExpr(left, unsupported_features);
                    }

                    // Try to take advantage of the optional chain operator to shorten code
                    if (!compat::Has(unsupported_features, compat::JSFeature::kOptionalChain)) {
                        if (const EBinary* binary = Get<EBinary>(left.data)) {
                            // "a != null && a.b()" => "a?.b()"
                            // "a == null || a.b()" => "a?.b()"
                            if ((binary->op == OpCode::kBinOpLooseNe && eBin->op == OpCode::kBinOpLogicalAnd) ||
                                (binary->op == OpCode::kBinOpLooseEq && eBin->op == OpCode::kBinOpLogicalOr)) {
                                Expr test;
                                if (Get<ENull>(binary->right.data) != nullptr) {
                                    test = binary->left;
                                } else if (Get<ENull>(binary->left.data) != nullptr) {
                                    test = binary->right;
                                }

                                // Note: Technically unbound identifiers can refer to a getter on
                                // the global object and that getter can have side effects that can
                                // be observed if we run that getter once instead of twice. But this
                                // seems like terrible coding practice and very unlikely to come up
                                // in real software, so we deliberately ignore this possibility and
                                // optimize for size instead of for this obscure edge case.
                                if (const EIdentifier* id = Get<EIdentifier>(test.data)) {
                                    if (!id->must_keep_due_to_with_stmt && TryToInsertOptionalChain(test, right)) {
                                        return right;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }

                case OpCode::kBinOpAdd: {
                    std::pair<Expr, bool> result = SimplifyUnusedStringAdditionChain(expr);
                    if (result.second) {
                        return result.first;
                    }
                    break;
                }
                default:
                    break; 
            }

            if (!ExprEquals(left, eBin->left) || !ExprEquals(right, eBin->right)) {
                return Expr{std::make_shared<EBinary>(EBinary{left, right, eBin->op}), expr.loc};
            }
        } else if (const ECall* eCall = Get<ECall>(expr.data)) {
            // A call that has been marked "__PURE__" can be removed if all arguments
            // can be removed. The annotation causes us to ignore the target.
            if (eCall->can_be_unwrapped_if_unused) {
                Expr result;
                for (Expr arg : eCall->args) {
                    if (Get<ESpread>(arg.data) != nullptr) {
                        arg.data = std::make_shared<EArray>(EArray{std::vector<Expr>{arg}, logger::Loc{}, logger::Loc{}, true});
                    }
                    result = JoinWithComma(result, SimplifyUnusedExpr(arg, unsupported_features));
                }
                return result;
            }

            // Attempt to shorten IIFEs
            if (eCall->args.empty()) {
                if (const EFunction* target = Get<EFunction>(eCall->target.data)) {
                    if (target->fn.args.empty()) {
                        // Just delete "(function() {})()" completely
                        if (target->fn.body.block.stmts.empty()) {
                            return Expr{};
                        }
                    }
                } else if (const EArrow* arrowTarget = Get<EArrow>(eCall->target.data)) {
                    if (arrowTarget->args.empty()) {
                        // Just delete "(() => {})()" completely
                        if (arrowTarget->body.block.stmts.empty()) {
                            return Expr{};
                        }

                        if (arrowTarget->body.block.stmts.size() == 1) {
                            const S& s_data = arrowTarget->body.block.stmts[0].data;
                            if (const SExpr* s = Get<SExpr>(s_data)) {
                                if (!arrowTarget->is_async) {
                                    // Replace "(() => { foo() })()" with "foo()"
                                    return s->value;
                                } else {
                                    // Replace "(async () => { foo() })()" with "(async () => foo())()"
                                    EArrow clone = *arrowTarget;
                                    clone.body.block.stmts[0].data = std::make_shared<SReturn>(SReturn{s->value});
                                    clone.prefer_expr = true;
                                    return Expr{std::make_shared<ECall>(ECall{
                                                Expr{std::make_shared<EArrow>(clone), eCall->target.loc},
                                                {},
                                                logger::Loc{},
                                                OptionalChain::kNone,
                                                CallKind::kNormal,
                                                false,
                                                false,
                                            }),
                                                expr.loc};
                                }
                            } else if (const SReturn* sRet = Get<SReturn>(s_data)) {
                                if (!arrowTarget->is_async) {
                                    // Replace "(() => foo())()" with "foo()"
                                    return sRet->value_or_nil;
                                }
                            }
                        }
                    }
                }
            }
        } else if (const ENew* eNew = Get<ENew>(expr.data)) {
            // A constructor call that has been marked "__PURE__" can be removed if all
            // arguments can be removed. The annotation causes us to ignore the target.
            if (eNew->can_be_unwrapped_if_unused) {
                Expr result;
                for (Expr arg : eNew->args) {
                    if (Get<ESpread>(arg.data) != nullptr) {
                        arg.data = std::make_shared<EArray>(EArray{std::vector<Expr>{arg}, logger::Loc{}, logger::Loc{}, true});
                    }
                    result = JoinWithComma(result, SimplifyUnusedExpr(arg, unsupported_features));
                }
                return result;
            }
        }

        return expr;
    }

    // -----------------------------------------------------------------------
    // Integer conversions (ToInt32 / ToUint32)
    // -----------------------------------------------------------------------

    // Implements the ECMAScript ToInt32 abstract operation. Converts a
    // double-precision floating-point value to a signed 32-bit integer using
    // the standard modulo-arithmetic algorithm.
    //
    // Example:
    //   ToInt32(3.14)     =>  3
    //   ToInt32(-3.14)    =>  -3
    //   ToInt32(2^32)     =>  0
    //   ToInt32(NaN)      =>  0
    //   ToInt32(Infinity) =>  0
    int32_t ToInt32(double f) {
        // The easy way
        if (f >= static_cast<double>(INT32_MIN) && f <= static_cast<double>(INT32_MAX)) {
            int32_t i = static_cast<int32_t>(f);
            if (static_cast<double>(i) == f) {
                return i;
            }
        }

        // Special-case non-finite numbers (casting them is unspecified behavior)
        if (std::isnan(f) || std::isinf(f)) {
            return 0;
        }

        // The hard way
        double m = std::fmod(std::fabs(f), 4294967296.0);
        uint32_t u = static_cast<uint32_t>(m);
        if (std::signbit(f)) {
            return static_cast<int32_t>(0u - u);
        }
        return static_cast<int32_t>(u);
    }

    // Implements the ECMAScript ToUint32 abstract operation. Converts a
    // double to an unsigned 32-bit integer by first converting via ToInt32.
    //
    // Example:
    //   ToUint32(3.14)    =>  3
    //   ToUint32(-1)      =>  4294967295
    uint32_t ToUint32(double f) {
        return static_cast<uint32_t>(ToInt32(f));
    }

    // -----------------------------------------------------------------------
    // Int32/Uint32 type inference
    // -----------------------------------------------------------------------

    // Returns true if the expression is guaranteed to produce an int32 or
    // uint32 value (and therefore cannot be NaN). This is used to determine
    // when equality tests against zero can be simplified in boolean context.
    //
    // Currently recognizes: unsigned right-shift (>>>), logical &&/|| where
    // both branches are int32/uint32, and ternaries where both branches are
    // int32/uint32.
    //
    // Example:
    //   IsInt32OrUint32(a >>> b)  =>  true
    //   IsInt32OrUint32(a + b)    =>  false
    bool IsInt32OrUint32(const E& data) {
        if (const EBinary* e = Get<EBinary>(data)) {
            switch (e->op) {
                case OpCode::kBinOpUShr: // This is the only bitwise operator that can't return a bigint (because it throws instead)
                    return true;

                case OpCode::kBinOpLogicalOr:
                case OpCode::kBinOpLogicalAnd:
                    return IsInt32OrUint32(e->left.data) && IsInt32OrUint32(e->right.data);

                default:
                    break;
            }
        } else if (const EIf* eIf = Get<EIf>(data)) {
            return IsInt32OrUint32(eIf->yes.data) && IsInt32OrUint32(eIf->no.data);
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // Compile-time number evaluation
    // -----------------------------------------------------------------------

    // Attempts to evaluate an expression as a number at compile time, without
    // side effects. Returns nullopt if the expression cannot be reduced to a
    // number.
    //
    // Example:
    //   ToNumberWithoutSideEffects(ENull{})     =>  0.0
    //   ToNumberWithoutSideEffects(EUndefined{}) =>  NaN
    //   ToNumberWithoutSideEffects(ENumber{42})  =>  42.0
    //   ToNumberWithoutSideEffects(EString{u""}) =>  0.0
    //   ToNumberWithoutSideEffects(EString{u"1"}) =>  1.0
    //   ToNumberWithoutSideEffects(EObject{})     =>  nullopt
    std::optional<double> ToNumberWithoutSideEffects(const E& data) {
        if (const EAnnotation* e = Get<EAnnotation>(data)) {
            return ToNumberWithoutSideEffects(e->value.data);
        }
        if (const EInlinedEnum* e = Get<EInlinedEnum>(data)) {
            return ToNumberWithoutSideEffects(e->value.data);
        }
        if (Get<ENull>(data) != nullptr) {
            return 0.0;
        }
        if (Get<EUndefined>(data) != nullptr || Get<ERegExp>(data) != nullptr) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        if (const EArray* e = Get<EArray>(data)) {
            if (e->items.empty()) {
                // "+[]" => "0"
                return 0.0;
            }
        }
        if (const EObject* e = Get<EObject>(data)) {
            if (e->properties.empty()) {
                // "+{}" => "NaN"
                return std::numeric_limits<double>::quiet_NaN();
            }
        }
        if (const EBoolean* e = Get<EBoolean>(data)) {
            return e->value ? 1.0 : 0.0;
        }
        if (const ENumber* e = Get<ENumber>(data)) {
            return e->value;
        }
        if (const EString* e = Get<EString>(data)) {
            // "+''" => "0"
            if (e->value.empty()) {
                return 0.0;
            }

            // "+'1'" => "1"
            if (std::optional<double> num = StringToEquivalentNumberValue(e->value)) {
                return *num;
            }
        }
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Compile-time string evaluation
    // -----------------------------------------------------------------------

    // Attempts to evaluate an expression as a string at compile time. Returns
    // nullopt if the expression cannot be reduced to a string.
    //
    // Example:
    //   ToStringWithoutSideEffects(ENull{})       =>  "null"
    //   ToStringWithoutSideEffects(EUndefined{})  =>  "undefined"
    //   ToStringWithoutSideEffects(EBoolean{true})=>  "true"
    //   ToStringWithoutSideEffects(ENumber{42})    =>  "42"
    std::optional<std::string> ToStringWithoutSideEffects(const E& data) {
        if (Get<ENull>(data) != nullptr) {
            return std::string("null");
        }
        if (Get<EUndefined>(data) != nullptr) {
            return std::string("undefined");
        }
        if (const EBoolean* e = Get<EBoolean>(data)) {
            return std::string(e->value ? "true" : "false");
        }
        if (const EBigInt* e = Get<EBigInt>(data)) {
            // Only do this if there is no radix
            if (e->value.size() < 2 || e->value[0] != '0') {
                return e->value;
            }
        }
        if (const ENumber* e = Get<ENumber>(data)) {
            if (std::optional<std::string> str = TryToStringOnNumberSafely(e->value, 10)) {
                return str;
            }
        }
        if (const ERegExp* e = Get<ERegExp>(data)) {
            return e->value;
        }
        if (const EDot* e = Get<EDot>(data)) {
            // This is dumb but some JavaScript obfuscators use this to generate string literals
            if (e->name == "constructor") {
                if (Get<EString>(e->target.data) != nullptr) {
                    return std::string("function String() { [native code] }");
                }
                if (Get<ERegExp>(e->target.data) != nullptr) {
                    return std::string("function RegExp() { [native code] }");
                }
            }
        }
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Approximate printed integer character count
    // -----------------------------------------------------------------------

    // Returns the approximate number of characters needed to print the given
    // integer value in decimal. This is used by the constant folder to decide
    // whether folding a binary operator would produce shorter output.
    //
    // Example:
    //   ApproximatePrintedIntCharCount(42)     =>  2
    //   ApproximatePrintedIntCharCount(-100)   =>  4
    //   ApproximatePrintedIntCharCount(0)      =>  1
    int ApproximatePrintedIntCharCount(double int_value) {
        int count = 1 + static_cast<int>(std::max(0.0, std::floor(std::log10(std::abs(int_value)))));
        if (int_value < 0) {
            count++;
        }
        return count;
    }

    // -----------------------------------------------------------------------
    // Binary operator folding decision
    // -----------------------------------------------------------------------

    // Determines whether a binary operator should be folded during
    // minification. Returns true if folding the operator with constant
    // operands would always produce output that is no larger than the
    // original expression.
    //
    // Example:
    //   ShouldFoldBinaryOperatorWhenMinifying(EBinary{1, 2, kBinOpAdd})  =>  true
    //   ShouldFoldBinaryOperatorWhenMinifying(EBinary{a, b, kBinOpMul})  =>  false
    bool ShouldFoldBinaryOperatorWhenMinifying(const EBinary& binary) {
        switch (binary.op) {
            // Equality tests should always result in smaller code when folded
            case OpCode::kBinOpLooseEq:
            case OpCode::kBinOpLooseNe:
            case OpCode::kBinOpStrictEq:
            case OpCode::kBinOpStrictNe:

            // Minification always folds right signed shift operations since they are
            // unlikely to result in larger output. Note: ">>>" could result in
            // bigger output such as "-1 >>> 0" becoming "4294967295".
            case OpCode::kBinOpShr:

            // Minification always folds the following bitwise operations since they
            // are unlikely to result in larger output.
            case OpCode::kBinOpBitwiseAnd:
            case OpCode::kBinOpBitwiseOr:
            case OpCode::kBinOpBitwiseXor:
            case OpCode::kBinOpLt:
            case OpCode::kBinOpGt:
            case OpCode::kBinOpLe:
            case OpCode::kBinOpGe:
                return true;

            case OpCode::kBinOpAdd: {
                // Addition of small-ish integers can definitely be folded without issues
                // "1 + 2" => "3"
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(binary.left, binary.right)) {
                    if (nums->first == std::trunc(nums->first) && std::fabs(nums->first) <= 0xFFFFFFFF &&
                        nums->second == std::trunc(nums->second) && std::fabs(nums->second) <= 0xFFFFFFFF) {
                        return true;
                    }
                }

                // String addition should pretty much always be more compact when folded
                if (ExtractStringValues(binary.left, binary.right)) {
                    return true;
                }
                break;
            }

            case OpCode::kBinOpSub: {
                // Subtraction of small-ish integers can definitely be folded without issues
                // "3 - 1" => "2"
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(binary.left, binary.right)) {
                    if (nums->first == std::trunc(nums->first) && std::fabs(nums->first) <= 0xFFFFFFFF &&
                        nums->second == std::trunc(nums->second) && std::fabs(nums->second) <= 0xFFFFFFFF) {
                        return true;
                    }
                }
                break;
            }

            case OpCode::kBinOpMul: {
                // Allow multiplication of small-ish integers to be folded
                // "1 * 2" => "3"
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(binary.left, binary.right)) {
                    if (nums->first == std::trunc(nums->first) && std::fabs(nums->first) <= 0xFF &&
                        nums->second == std::trunc(nums->second) && std::fabs(nums->second) <= 0xFF) {
                        return true;
                    }
                }
                break;
            }

            case OpCode::kBinOpDiv: {
                // "0/0" => "NaN"
                // "1/0" => "Infinity"
                // "1/-0" => "-Infinity"
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(binary.left, binary.right)) {
                    if (nums->second == 0) {
                        return true;
                    }
                }
                break;
            }

            case OpCode::kBinOpShl: {
                // "1 << 3" => "8"
                // "1 << 24" => "1 << 24" (since "1<<24" is shorter than "16777216")
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(binary.left, binary.right)) {
                    int left_len = ApproximatePrintedIntCharCount(nums->first);
                    int right_len = ApproximatePrintedIntCharCount(nums->second);
                    int32_t shifted = static_cast<int32_t>(static_cast<uint32_t>(ToInt32(nums->first)) << (ToUint32(nums->second) & 31));
                    int result_len = ApproximatePrintedIntCharCount(static_cast<double>(shifted));
                    return result_len <= left_len + 2 + right_len;
                }
                break;
            }

            case OpCode::kBinOpUShr: {
                // "10 >>> 1" => "5"
                // "-1 >>> 0" => "-1 >>> 0" (since "-1>>>0" is shorter than "4294967295")
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(binary.left, binary.right)) {
                    int left_len = ApproximatePrintedIntCharCount(nums->first);
                    int right_len = ApproximatePrintedIntCharCount(nums->second);
                    uint32_t shifted = ToUint32(nums->first) >> (ToUint32(nums->second) & 31);
                    int result_len = ApproximatePrintedIntCharCount(static_cast<double>(shifted));
                    return result_len <= left_len + 3 + right_len;
                }
                break;
            }

            case OpCode::kBinOpLogicalAnd:
            case OpCode::kBinOpLogicalOr:
            case OpCode::kBinOpNullishCoalescing:
                if (IsPrimitiveLiteral(binary.left.data)) {
                    return true;
                }
                break;

            default:
                    break;     
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // Binary operator folding
    // -----------------------------------------------------------------------

    // Attempts to constant-fold a binary expression. Returns the folded
    // result if both operands are compile-time constants, or nullopt if the
    // expression cannot be folded.
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    //
    // Example:
    //   FoldBinaryOperator(EBinary{ENumber{1}, ENumber{2}, kBinOpAdd})
    //     =>  ENumber{3}
    //   FoldBinaryOperator(EBinary{EString{u"a"}, EString{u"b"}, kBinOpAdd})
    //     =>  EString{u"ab"}
    std::optional<Expr> FoldBinaryOperator(logger::Loc loc, const EBinary& e) {
        switch (e.op) {
            case OpCode::kBinOpAdd:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<ENumber>(ENumber{nums->first + nums->second}), loc};
                }
                if (std::optional<std::pair<std::u16string, std::u16string>> strs = ExtractStringValues(e.left, e.right)) {
                    return Expr{std::make_shared<EString>(EString{JoinStrings(strs->first, strs->second), {}, {}, {}, {}}), loc};
                }
                break;

            case OpCode::kBinOpSub:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<ENumber>(ENumber{nums->first - nums->second}), loc};
                }
                break;

            case OpCode::kBinOpMul:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<ENumber>(ENumber{nums->first * nums->second}), loc};
                }
                break;

            case OpCode::kBinOpDiv:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<ENumber>(ENumber{nums->first / nums->second}), loc};
                }
                break;

            case OpCode::kBinOpRem:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<ENumber>(ENumber{std::fmod(nums->first, nums->second)}), loc};
                }
                break;

            case OpCode::kBinOpPow:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<ENumber>(ENumber{std::pow(nums->first, nums->second)}), loc};
                }
                break;

            case OpCode::kBinOpShl:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    int32_t shifted = static_cast<int32_t>(static_cast<uint32_t>(ToInt32(nums->first)) << (ToUint32(nums->second) & 31));
                    return Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(shifted)}), loc};
                }
                break;

            case OpCode::kBinOpShr:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    int32_t shifted = ToInt32(nums->first) >> (ToUint32(nums->second) & 31);
                    return Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(shifted)}), loc};
                }
                break;

            case OpCode::kBinOpUShr:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    uint32_t shifted = ToUint32(nums->first) >> (ToUint32(nums->second) & 31);
                    return Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(shifted)}), loc};
                }
                break;

            case OpCode::kBinOpBitwiseAnd:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    int32_t result = static_cast<int32_t>(static_cast<uint32_t>(ToInt32(nums->first)) & static_cast<uint32_t>(ToInt32(nums->second)));
                    return Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(result)}), loc};
                }
                break;

            case OpCode::kBinOpBitwiseOr:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    int32_t result = static_cast<int32_t>(static_cast<uint32_t>(ToInt32(nums->first)) | static_cast<uint32_t>(ToInt32(nums->second)));
                    return Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(result)}), loc};
                }
                break;

            case OpCode::kBinOpBitwiseXor:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    int32_t result = static_cast<int32_t>(static_cast<uint32_t>(ToInt32(nums->first)) ^ static_cast<uint32_t>(ToInt32(nums->second)));
                    return Expr{std::make_shared<ENumber>(ENumber{static_cast<double>(result)}), loc};
                }
                break;

            case OpCode::kBinOpLt:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{nums->first < nums->second}), loc};
                }
                if (std::optional<std::pair<std::u16string, std::u16string>> strs = ExtractStringValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{StringCompareUCS2(strs->first, strs->second) < 0}), loc};
                }
                break;

            case OpCode::kBinOpGt:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{nums->first > nums->second}), loc};
                }
                if (std::optional<std::pair<std::u16string, std::u16string>> strs = ExtractStringValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{StringCompareUCS2(strs->first, strs->second) > 0}), loc};
                }
                break;

            case OpCode::kBinOpLe:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{nums->first <= nums->second}), loc};
                }
                if (std::optional<std::pair<std::u16string, std::u16string>> strs = ExtractStringValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{StringCompareUCS2(strs->first, strs->second) <= 0}), loc};
                }
                break;

            case OpCode::kBinOpGe:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{nums->first >= nums->second}), loc};
                }
                if (std::optional<std::pair<std::u16string, std::u16string>> strs = ExtractStringValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{StringCompareUCS2(strs->first, strs->second) >= 0}), loc};
                }
                break;

            case OpCode::kBinOpLooseEq:
            case OpCode::kBinOpStrictEq:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{nums->first == nums->second}), loc};
                }
                if (std::optional<std::pair<std::u16string, std::u16string>> strs = ExtractStringValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{StringCompareUCS2(strs->first, strs->second) == 0}), loc};
                }
                break;

            case OpCode::kBinOpLooseNe:
            case OpCode::kBinOpStrictNe:
                if (std::optional<std::pair<double, double>> nums = ExtractNumericValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{nums->first != nums->second}), loc};
                }
                if (std::optional<std::pair<std::u16string, std::u16string>> strs = ExtractStringValues(e.left, e.right)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{StringCompareUCS2(strs->first, strs->second) != 0}), loc};
                }
                break;

            case OpCode::kBinOpLogicalAnd: {
                if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(e.left.data)) {
                    if (!boolean->first) {
                        return e.left;
                    } else if (boolean->second == SideEffects::kNoSideEffects) {
                        return e.right;
                    }
                }
                break;
            }

            case OpCode::kBinOpLogicalOr: {
                if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(e.left.data)) {
                    if (boolean->first) {
                        return e.left;
                    } else if (boolean->second == SideEffects::kNoSideEffects) {
                        return e.right;
                    }
                }
                break;
            }

            case OpCode::kBinOpNullishCoalescing: {
                if (std::optional<std::pair<bool, SideEffects>> is_null_or_undefined = ToNullOrUndefinedWithSideEffects(e.left.data)) {
                    if (!is_null_or_undefined->first) {
                        return e.left;
                    } else if (is_null_or_undefined->second == SideEffects::kNoSideEffects) {
                        return e.right;
                    }
                }
                break;
            }

            default:
                break; 
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Null/undefined pattern detection
    // -----------------------------------------------------------------------

    // Detects the pattern "a === null || a === void 0" (in either order).
    // Returns the identifier expression and the null/undefined literal if
    // the pattern matches. This is used to recognize null-guard patterns
    // that can be converted to optional chaining or nullish coalescing.
    //
    // Example:
    //   IsBinaryNullAndUndefined(a===null, a===void0, kBinOpStrictEq)
    //     =>  pair(a, null)
    //   IsBinaryNullAndUndefined(a===void0, a===null, kBinOpStrictEq)
    //     =>  pair(a, void0)
    std::optional<std::pair<Expr, Expr>> IsBinaryNullAndUndefined(const Expr& left, const Expr& right, OpCode op) {
        if (const EBinary* a = Get<EBinary>(left.data)) {
            if (a->op == op) {
                if (const EBinary* b = Get<EBinary>(right.data)) {
                    if (b->op == op) {
                        Expr id_a = a->left;
                        Expr eq_a = a->right;
                        Expr id_b = b->left;
                        Expr eq_b = b->right;

                        // Detect when the identifier comes second and flip the order of our checks
                        if (Get<EIdentifier>(eq_a.data) != nullptr) {
                            std::swap(id_a, eq_a);
                        }
                        if (Get<EIdentifier>(eq_b.data) != nullptr) {
                            std::swap(id_b, eq_b);
                        }

                        if (const EIdentifier* id_a_node = Get<EIdentifier>(id_a.data)) {
                            if (const EIdentifier* id_b_node = Get<EIdentifier>(id_b.data)) {
                                if (id_a_node->ref == id_b_node->ref) {
                                    // "a === null || a === void 0"
                                    if (Get<ENull>(eq_a.data) != nullptr) {
                                        if (Get<EUndefined>(eq_b.data) != nullptr) {
                                            return std::make_pair(a->left, a->right);
                                        }
                                    }

                                    // "a === void 0 || a === null"
                                    if (Get<EUndefined>(eq_a.data) != nullptr) {
                                        if (Get<ENull>(eq_b.data) != nullptr) {
                                            return std::make_pair(b->left, b->right);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Bigint equality check
    // -----------------------------------------------------------------------

    // Checks whether two bigint literal strings are equal. Returns true if
    // they are provably equal, false if provably unequal, or nullopt if the
    // comparison cannot be resolved at compile time.
    //
    // Leading zeros are only permitted in radix-prefixed forms (e.g. "0x1"),
    // so canonical-form literals can be compared directly as strings.
    //
    // Example:
    //   CheckEqualityBigInt("42", "42")  =>  true
    //   CheckEqualityBigInt("42", "43")  =>  false
    //   CheckEqualityBigInt("0x1", "1")  =>  nullopt
    std::optional<bool> CheckEqualityBigInt(const std::string& a, const std::string& b) {
        // Equal literals are always equal
        if (a == b) {
            return true;
        }

        // Unequal literals are unequal if neither has a radix. Leading zeros are
        // disallowed in bigint literals without a radix, so in this case we know
        // each value is in canonical form.
        if ((a.size() < 2 || a[0] != '0') && (b.size() < 2 || b[0] != '0')) {
            return false;
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Equality analysis without side effects
    // -----------------------------------------------------------------------

    // Analyzes whether two expressions are equal or unequal at compile time,
    // without performing any side effects. Returns:
    //   - true  if the expressions are provably equal
    //   - false if the expressions are provably unequal
    //   - nullopt if no determination can be made
    //
    // Handles all primitive types: null, undefined, boolean, number, bigint,
    // and string. Both strict (===) and loose (==) equality are supported.
    //
    // Example:
    //   CheckEqualityIfNoSideEffects(ENull{}, EUndefined{}, kLooseEquality)
    //     =>  true
    //   CheckEqualityIfNoSideEffects(ENumber{1}, ENumber{2}, kStrictEquality)
    //     =>  false
    std::optional<bool> CheckEqualityIfNoSideEffects(const E& left, const E& right, EqualityKind kind) {
        if (const EInlinedEnum* r = Get<EInlinedEnum>(right)) {
            return CheckEqualityIfNoSideEffects(left, r->value.data, kind);
        }

        if (const EInlinedEnum* l = Get<EInlinedEnum>(left)) {
            return CheckEqualityIfNoSideEffects(l->value.data, right, kind);
        }

        if (Get<ENull>(left) != nullptr) {
            if (Get<ENull>(right) != nullptr) {
                // "null === null" is true
                return true;
            } else if (Get<EUndefined>(right) != nullptr) {
                // "null == undefined" is true
                // "null === undefined" is false
                return kind == EqualityKind::kLooseEquality;
            } else if (IsPrimitiveLiteral(right)) {
                // "null == (not null or undefined)" is false
                return false;
            }
        } else if (Get<EUndefined>(left) != nullptr) {
            if (Get<EUndefined>(right) != nullptr) {
                // "undefined === undefined" is true
                return true;
            } else if (Get<ENull>(right) != nullptr) {
                // "undefined == null" is true
                // "undefined === null" is false
                return kind == EqualityKind::kLooseEquality;
            } else if (IsPrimitiveLiteral(right)) {
                // "undefined == (not null or undefined)" is false
                return false;
            }
        } else if (const EBoolean* l = Get<EBoolean>(left)) {
            if (const EBoolean* rBool = Get<EBoolean>(right)) {
                // "false === false" is true
                // "false === true" is false
                return l->value == rBool->value;
            } else if (const ENumber* rNum = Get<ENumber>(right)) {
                if (kind == EqualityKind::kLooseEquality) {
                    if (l->value) {
                        // "true == 1" is true
                        return rNum->value == 1;
                    } else {
                        // "false == 0" is true
                        return rNum->value == 0;
                    }
                } else {
                    // "true === 1" is false
                    // "false === 0" is false
                    return false;
                }
            } else if (Get<ENull>(right) != nullptr || Get<EUndefined>(right) != nullptr) {
                // "(not null or undefined) == undefined" is false
                return false;
            } else if (kind == EqualityKind::kStrictEquality && IsPrimitiveLiteral(right)) {
                // "boolean === (not boolean)" is false
                return false;
            }
        } else if (const ENumber* lNum = Get<ENumber>(left)) {
            if (const ENumber* rNum = Get<ENumber>(right)) {
                // "0 === 0" is true
                // "0 === 1" is false
                return lNum->value == rNum->value;
            } else if (const EBoolean* rBool = Get<EBoolean>(right)) {
                if (kind == EqualityKind::kLooseEquality) {
                    if (rBool->value) {
                        // "1 == true" is true
                        return lNum->value == 1;
                    } else {
                        // "0 == false" is true
                        return lNum->value == 0;
                    }
                } else {
                    // "1 === true" is false
                    // "0 === false" is false
                    return false;
                }
            } else if (Get<ENull>(right) != nullptr || Get<EUndefined>(right) != nullptr) {
                // "(not null or undefined) == undefined" is false
                return false;
            } else if (kind == EqualityKind::kStrictEquality && IsPrimitiveLiteral(right)) {
                // "number === (not number)" is false
                return false;
            }
        } else if (const EBigInt* lBigInt = Get<EBigInt>(left)) {
            if (const EBigInt* rBigInt = Get<EBigInt>(right)) {
                // "0n === 0n" is true
                // "0n === 1n" is false
                return CheckEqualityBigInt(lBigInt->value, rBigInt->value);
            } else if (Get<ENull>(right) != nullptr || Get<EUndefined>(right) != nullptr) {
                // "(not null or undefined) == undefined" is false
                return false;
            } else if (kind == EqualityKind::kStrictEquality && IsPrimitiveLiteral(right)) {
                // "bigint === (not bigint)" is false
                return false;
            }
        } else if (const EString* lStr = Get<EString>(left)) {
            if (const EString* rStr = Get<EString>(right)) {
                // "'a' === 'a'" is true
                // "'a' === 'b'" is false
                return helpers::UTF16EqualsUTF16(lStr->value, rStr->value);
            } else if (Get<ENull>(right) != nullptr || Get<EUndefined>(right) != nullptr) {
                // "(not null or undefined) == undefined" is false
                return false;
            } else if (kind == EqualityKind::kStrictEquality && IsPrimitiveLiteral(right)) {
                // "string === (not string)" is false
                return false;
            }
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Structural value comparison
    // -----------------------------------------------------------------------

    // Compares two expressions for structural equality, handling annotations,
    // inline enums, identifiers, property access, index access, ternaries,
    // unary/binary operators, calls, and primitive values. Negative and
    // positive zero are treated as distinct (per the ECMAScript specification).
    //
    // This is used by the optimizer to detect equivalent subexpressions when
    // simplifying if-expressions, merging branches, and other transformations.
    //
    // Example:
    //   ValuesLookTheSame(a.b, a.b)  =>  true
    //   ValuesLookTheSame(0, -0)     =>  false
    bool ValuesLookTheSame(const E& left, const E& right) {
        if (const EInlinedEnum* b = Get<EInlinedEnum>(right)) {
            return ValuesLookTheSame(left, b->value.data);
        }

        if (const EInlinedEnum* a = Get<EInlinedEnum>(left)) {
            return ValuesLookTheSame(a->value.data, right);
        }

        if (const EIdentifier* a = Get<EIdentifier>(left)) {
            if (const EIdentifier* b = Get<EIdentifier>(right)) {
                if (a->ref == b->ref) {
                    return true;
                }
            }
        } else if (const EDot* aDot = Get<EDot>(left)) {
            if (const EDot* bDot = Get<EDot>(right)) {
                if (aDot->HasSameFlagsAs(*bDot) && aDot->name == bDot->name && ValuesLookTheSame(aDot->target.data, bDot->target.data)) {
                    return true;
                }
            }
        } else if (const EIndex* aIdx = Get<EIndex>(left)) {
            if (const EIndex* bIdx = Get<EIndex>(right)) {
                if (aIdx->HasSameFlagsAs(*bIdx) && ValuesLookTheSame(aIdx->target.data, bIdx->target.data) &&
                    ValuesLookTheSame(aIdx->index.data, bIdx->index.data)) {
                    return true;
                }
            }
        } else if (const EIf* aIf = Get<EIf>(left)) {
            if (const EIf* bIf = Get<EIf>(right)) {
                if (ValuesLookTheSame(aIf->test.data, bIf->test.data) && ValuesLookTheSame(aIf->yes.data, bIf->yes.data) &&
                    ValuesLookTheSame(aIf->no.data, bIf->no.data)) {
                    return true;
                }
            }
        } else if (const EUnary* aUn = Get<EUnary>(left)) {
            if (const EUnary* bUn = Get<EUnary>(right)) {
                if (aUn->op == bUn->op && ValuesLookTheSame(aUn->value.data, bUn->value.data)) {
                    return true;
                }
            }
        } else if (const EBinary* aBin = Get<EBinary>(left)) {
            if (const EBinary* bBin = Get<EBinary>(right)) {
                if (aBin->op == bBin->op && ValuesLookTheSame(aBin->left.data, bBin->left.data) &&
                    ValuesLookTheSame(aBin->right.data, bBin->right.data)) {
                    return true;
                }
            }
        } else if (const ECall* aCall = Get<ECall>(left)) {
            if (const ECall* bCall = Get<ECall>(right)) {
                if (aCall->HasSameFlagsAs(*bCall) && aCall->args.size() == bCall->args.size() &&
                    ValuesLookTheSame(aCall->target.data, bCall->target.data)) {
                    for (size_t i = 0; i < aCall->args.size(); i++) {
                        if (!ValuesLookTheSame(aCall->args[i].data, bCall->args[i].data)) {
                            return false;
                        }
                    }
                    return true;
                }
            }
        } else if (const ENumber* aNum = Get<ENumber>(left)) {
            // Special-case to distinguish between negative an non-negative zero when mangling
            // "a ? -0 : 0" => "a ? -0 : 0"
            // https://developer.mozilla.org/en-US/docs/Web/JavaScript/Equality_comparisons_and_sameness
            if (const ENumber* bNum = Get<ENumber>(right)) {
                if (aNum->value == 0 && bNum->value == 0 && std::signbit(aNum->value) != std::signbit(bNum->value)) {
                    return false;
                }
            }
        }

        std::optional<bool> equal = CheckEqualityIfNoSideEffects(left, right, EqualityKind::kStrictEquality);
        return equal.has_value() && *equal;
    }

    // -----------------------------------------------------------------------
    // Optional chain insertion
    // -----------------------------------------------------------------------

    // Attempts to convert a null-guard pattern into an optional chain. If the
    // test expression matches the target of a dot/index/call expression, the
    // optional_chain flag is set on that node.
    //
    // Returns true if the conversion was successful.
    //
    // Example:
    //   TryToInsertOptionalChain(x, x.y)  =>  x?.y  (returns true)
    bool TryToInsertOptionalChain(const Expr& test, Expr& expr) {
        if (EDot* e = Get<EDot>(expr.data)) {
            if (ValuesLookTheSame(test.data, e->target.data)) {
                e->optional_chain = OptionalChain::kStart;
                return true;
            }
            if (TryToInsertOptionalChain(test, e->target)) {
                if (e->optional_chain == OptionalChain::kNone) {
                    e->optional_chain = OptionalChain::kContinue;
                }
                return true;
            }
        } else if (EIndex* eIdx = Get<EIndex>(expr.data)) {
            if (ValuesLookTheSame(test.data, eIdx->target.data)) {
                eIdx->optional_chain = OptionalChain::kStart;
                return true;
            }
            if (TryToInsertOptionalChain(test, eIdx->target)) {
                if (eIdx->optional_chain == OptionalChain::kNone) {
                    eIdx->optional_chain = OptionalChain::kContinue;
                }
                return true;
            }
        } else if (ECall* eCall = Get<ECall>(expr.data)) {
            if (ValuesLookTheSame(test.data, eCall->target.data)) {
                eCall->optional_chain = OptionalChain::kStart;
                return true;
            }
            if (TryToInsertOptionalChain(test, eCall->target)) {
                if (eCall->optional_chain == OptionalChain::kNone) {
                    eCall->optional_chain = OptionalChain::kContinue;
                }
                return true;
            }
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // Safe number-to-string conversion
    // -----------------------------------------------------------------------

    // Attempts to convert a number to its string representation in the given
    // radix. Only performs the conversion when it can be guaranteed to produce
    // the same result as a real JavaScript VM. This conservative approach
    // avoids correctness bugs in TypeScript enum constant expression handling.
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    //
    // Example:
    //   TryToStringOnNumberSafely(42, 10)        =>  "42"
    //   TryToStringOnNumberSafely(NaN, 10)       =>  "NaN"
    //   TryToStringOnNumberSafely(1.5, 10)       =>  nullopt
    std::optional<std::string> TryToStringOnNumberSafely(double n, int radix) {
        if (n >= static_cast<double>(INT32_MIN) && n <= static_cast<double>(INT32_MAX)) {
            int32_t i = static_cast<int32_t>(n);
            if (static_cast<double>(i) == n) {
                return FormatInt(static_cast<int64_t>(i), radix);
            }
        }
        if (std::isnan(n)) {
            return std::string("NaN");
        }
        if (std::isinf(n) && n > 0) {
            return std::string("Infinity");
        }
        if (std::isinf(n) && n < 0) {
            return std::string("-Infinity");
        }
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // String addition folding
    // -----------------------------------------------------------------------

    // Attempts to fold a binary `+` expression involving strings and/or
    // templates. Handles cases such as:
    //   "x" + "y"           =>  "xy"
    //   "x" + `y${z}`       =>  `xy${z}`
    //   "`${x}` + "y"`      =>  "`${x}y`"
    //   "`${a}b` + `x${y}`" =>  "`${a}bx${y}`"
    //   "" + typeof x        =>  typeof x
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    std::optional<Expr> FoldStringAddition(Expr left, Expr right, StringAdditionKind kind) {
        left = FoldAdditionPreProcess(std::move(left));
        right = FoldAdditionPreProcess(std::move(right));

        // Transforming the left operand into a string is not safe if it comes from
        // a nested AST node. The following transforms are invalid:
        //
        //   "0 + 1 + 'x'" => "0 + '1x'"
        //   "0 + 1 + `${x}`" => "0 + `1${x}`"
        //
        if (kind != StringAdditionKind::kWithNestedLeft) {
            if (Get<EString>(right.data) != nullptr || Get<ETemplate>(right.data) != nullptr) {
                if (std::optional<std::string> str = ToStringWithoutSideEffects(left.data)) {
                    left = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(*str), {}, {}, {}, {}}), left.loc};
                }
            }
        }

        if (const EString* l = Get<EString>(left.data)) {
            // "'x' + 0" => "'x' + '0'"
            if (std::optional<std::string> str = ToStringWithoutSideEffects(right.data)) {
                    right = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(*str), {}, {}, {}, {}}), right.loc};
            }

            if (const EString* r = Get<EString>(right.data)) {
                // "'x' + 'y'" => "'xy'"
                return Expr{std::make_shared<EString>(EString{JoinStrings(l->value, r->value), logger::Loc{}, l->prefer_template || r->prefer_template, {}, {}}), left.loc};
            }
            if (const ETemplate* rTempl = Get<ETemplate>(right.data)) {
                if (IsNil(rTempl->tag_or_nil.data)) {
                    // "'x' + `y${z}`" => "`xy${z}`"
                    return Expr{std::make_shared<ETemplate>(ETemplate{Expr{}, {}, JoinStrings(l->value, rTempl->head_cooked), rTempl->parts, left.loc, logger::Loc{}}), left.loc};
                }
            }

            // "'' + typeof x" => "typeof x"
            if (l->value.empty() && KnownPrimitiveType(right.data) == PrimitiveType::kString) {
                return right;
            }
        } else if (const ETemplate* lTempl = Get<ETemplate>(left.data)) {
            if (IsNil(lTempl->tag_or_nil.data)) {
                // "`${x}` + 0" => "`${x}` + '0'"
                if (std::optional<std::string> str = ToStringWithoutSideEffects(right.data)) {
                right = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(*str), {}, {}, {}, {}}), right.loc};
                }

                if (const EString* rStr = Get<EString>(right.data)) {
                    // "`${x}y` + 'z'" => "`${x}yz`"
                    size_t n = lTempl->parts.size();
                    std::u16string head = lTempl->head_cooked;
                    std::vector<TemplatePart> parts = lTempl->parts;
                    if (n == 0) {
                        head = JoinStrings(head, rStr->value);
                    } else {
                        parts[n - 1].tail_cooked = JoinStrings(parts[n - 1].tail_cooked, rStr->value);
                    }
                    return Expr{std::make_shared<ETemplate>(ETemplate{Expr{}, {}, head, parts, lTempl->head_loc, logger::Loc{}}), left.loc};
                }
                if (const ETemplate* rTempl = Get<ETemplate>(right.data)) {
                    if (IsNil(rTempl->tag_or_nil.data)) {
                        // "`${a}b` + `x${y}`" => "`${a}bx${y}`"
                        size_t n = lTempl->parts.size();
                        std::u16string head = lTempl->head_cooked;
                        std::vector<TemplatePart> parts = lTempl->parts;
                        parts.insert(parts.end(), rTempl->parts.begin(), rTempl->parts.end());
                        if (n == 0) {
                            head = JoinStrings(head, rTempl->head_cooked);
                        } else {
                            parts[n - 1].tail_cooked = JoinStrings(parts[n - 1].tail_cooked, rTempl->head_cooked);
                        }
                        return Expr{std::make_shared<ETemplate>(ETemplate{Expr{}, {}, head, parts, lTempl->head_loc, logger::Loc{}}), left.loc};
                    }
                }
            }
        }

        // "typeof x + ''" => "typeof x"
        if (const EString* r = Get<EString>(right.data)) {
            if (r->value.empty() && KnownPrimitiveType(left.data) == PrimitiveType::kString) {
                return left;
            }
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Template literal inlining
    // -----------------------------------------------------------------------

    // Inlines primitive-valued expressions into template literal parts,
    // collapsing string constants into the template head/tail. If all parts
    // are inlined, the template is converted to a plain string literal.
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    //
    // Example:
    //   InlinePrimitivesIntoTemplate("`a${'b'}c`")  =>  "`abc`"
    //   InlinePrimitivesIntoTemplate("`a${x}c`")     =>  "`a${x}c`"  (unchanged)
    Expr InlinePrimitivesIntoTemplate(logger::Loc loc, const ETemplate& e) {
        // Can't inline strings if there's a custom template tag
        if (!IsNil(e.tag_or_nil.data)) {
            return Expr{std::make_shared<ETemplate>(e), loc};
        }

        std::u16string head_cooked = e.head_cooked;
        std::vector<TemplatePart> parts;
        parts.reserve(e.parts.size());

        for (TemplatePart part : e.parts) {
            if (const EInlinedEnum* value = Get<EInlinedEnum>(part.value.data)) {
                part.value = value->value;
            }
            if (std::optional<std::string> str = ToStringWithoutSideEffects(part.value.data)) {
                part.value = Expr{std::make_shared<EString>(EString{helpers::StringToUTF16(*str), {}, {}, {}, {}}), part.value.loc};
            }
            if (const EString* str = Get<EString>(part.value.data)) {
                if (parts.empty()) {
                    head_cooked += str->value;
                    head_cooked += part.tail_cooked;
                } else {
                    TemplatePart& prev_part = parts[parts.size() - 1];
                    prev_part.tail_cooked += str->value;
                    prev_part.tail_cooked += part.tail_cooked;
                }
            } else {
                parts.push_back(part);
            }
        }

        // Become a plain string if there are no substitutions
        if (parts.empty()) {
            return Expr{std::make_shared<EString>(EString{head_cooked, logger::Loc{}, true, {}, {}}), loc};
        }

        return Expr{std::make_shared<ETemplate>(ETemplate{Expr{}, {}, head_cooked, parts, e.head_loc, logger::Loc{}}), loc};
    }

    // -----------------------------------------------------------------------
    // Null/undefined side-effect analysis
    // -----------------------------------------------------------------------

    // Determines whether an expression is always null/undefined, never
    // null/undefined, or indeterminate. Also reports whether the expression
    // has side effects.
    //
    // Returns a pair of (is_null_or_undefined, side_effects). If the result
    // is nullopt, no determination can be made.
    //
    // Example:
    //   ToNullOrUndefinedWithSideEffects(ENull{})     =>  (true, kNoSideEffects)
    //   ToNullOrUndefinedWithSideEffects(ENumber{1})   =>  (false, kNoSideEffects)
    //   ToNullOrUndefinedWithSideEffects(EUnary{void, x}) =>  (true, kCouldHaveSideEffects)
    std::optional<std::pair<bool, SideEffects>> ToNullOrUndefinedWithSideEffects(const E& data) {
        if (const EAnnotation* e = Get<EAnnotation>(data)) {
            std::optional<std::pair<bool, SideEffects>> result = ToNullOrUndefinedWithSideEffects(e->value.data);
            if (result) {
                if (Has(e->flags, AnnotationFlags::kCanBeRemovedIfUnused)) {
                    result->second = SideEffects::kNoSideEffects;
                }
            }
            return result;
        }
        if (const EInlinedEnum* e = Get<EInlinedEnum>(data)) {
            return ToNullOrUndefinedWithSideEffects(e->value.data);
        }

        // Never null or undefined
        if (Get<EBoolean>(data) != nullptr || Get<ENumber>(data) != nullptr || Get<EString>(data) != nullptr ||
            Get<ERegExp>(data) != nullptr || Get<EFunction>(data) != nullptr || Get<EArrow>(data) != nullptr ||
            Get<EBigInt>(data) != nullptr) {
            return std::make_pair(false, SideEffects::kNoSideEffects);
        }

        // Never null or undefined
        if (Get<EObject>(data) != nullptr || Get<EArray>(data) != nullptr || Get<EClass>(data) != nullptr) {
            return std::make_pair(false, SideEffects::kCouldHaveSideEffects);
        }

        // Always null or undefined
        if (Get<ENull>(data) != nullptr || Get<EUndefined>(data) != nullptr) {
            return std::make_pair(true, SideEffects::kNoSideEffects);
        }

        if (const EUnary* e = Get<EUnary>(data)) {
            switch (e->op) {
                // Always number or bigint
                case OpCode::kUnOpPos:
                case OpCode::kUnOpNeg:
                case OpCode::kUnOpCpl:
                case OpCode::kUnOpPreDec:
                case OpCode::kUnOpPreInc:
                case OpCode::kUnOpPostDec:
                case OpCode::kUnOpPostInc:
                // Always boolean
                case OpCode::kUnOpNot:
                case OpCode::kUnOpDelete:
                    return std::make_pair(false, SideEffects::kCouldHaveSideEffects);

                // Always boolean
                case OpCode::kUnOpTypeof:
                    if (e->was_originally_typeof_identifier) {
                        // Expressions such as "typeof x" never have any side effects
                        return std::make_pair(false, SideEffects::kNoSideEffects);
                    }
                    return std::make_pair(false, SideEffects::kCouldHaveSideEffects);

                // Always undefined
                case OpCode::kUnOpVoid:
                    return std::make_pair(true, SideEffects::kCouldHaveSideEffects);
                default:
                    break;     
            }
        }

        if (const EBinary* e = Get<EBinary>(data)) {
            switch (e->op) {
                // Always string or number or bigint
                case OpCode::kBinOpAdd:
                case OpCode::kBinOpAddAssign:
                // Always number or bigint
                case OpCode::kBinOpSub:
                case OpCode::kBinOpMul:
                case OpCode::kBinOpDiv:
                case OpCode::kBinOpRem:
                case OpCode::kBinOpPow:
                case OpCode::kBinOpSubAssign:
                case OpCode::kBinOpMulAssign:
                case OpCode::kBinOpDivAssign:
                case OpCode::kBinOpRemAssign:
                case OpCode::kBinOpPowAssign:
                case OpCode::kBinOpShl:
                case OpCode::kBinOpShr:
                case OpCode::kBinOpUShr:
                case OpCode::kBinOpShlAssign:
                case OpCode::kBinOpShrAssign:
                case OpCode::kBinOpUShrAssign:
                case OpCode::kBinOpBitwiseOr:
                case OpCode::kBinOpBitwiseAnd:
                case OpCode::kBinOpBitwiseXor:
                case OpCode::kBinOpBitwiseOrAssign:
                case OpCode::kBinOpBitwiseAndAssign:
                case OpCode::kBinOpBitwiseXorAssign:
                // Always boolean
                case OpCode::kBinOpLt:
                case OpCode::kBinOpLe:
                case OpCode::kBinOpGt:
                case OpCode::kBinOpGe:
                case OpCode::kBinOpIn:
                case OpCode::kBinOpInstanceof:
                case OpCode::kBinOpLooseEq:
                case OpCode::kBinOpLooseNe:
                case OpCode::kBinOpStrictEq:
                case OpCode::kBinOpStrictNe:
                    return std::make_pair(false, SideEffects::kCouldHaveSideEffects);

                case OpCode::kBinOpComma: {
                    std::optional<std::pair<bool, SideEffects>> result = ToNullOrUndefinedWithSideEffects(e->right.data);
                    if (result) {
                        return std::make_pair(result->first, SideEffects::kCouldHaveSideEffects);
                    }
                    break;
                }
                default:
                    break; 
            }
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Boolean side-effect analysis
    // -----------------------------------------------------------------------

    // Determines whether an expression is always truthy, always falsy, or
    // indeterminate, along with its side-effect status. This is used by the
    // optimizer to simplify conditional expressions, logical operators, and
    // boolean contexts.
    //
    // Returns a pair of (is_truthy, side_effects). If the result is nullopt,
    // no determination can be made.
    //
    // Example:
    //   ToBooleanWithSideEffects(ENull{})        =>  (false, kNoSideEffects)
    //   ToBooleanWithSideEffects(ENumber{0})      =>  (false, kNoSideEffects)
    //   ToBooleanWithSideEffects(ENumber{42})     =>  (true, kNoSideEffects)
    //   ToBooleanWithSideEffects(EString{u""})    =>  (false, kNoSideEffects)
    //   ToBooleanWithSideEffects(EObject{})        =>  (true, kCouldHaveSideEffects)
    std::optional<std::pair<bool, SideEffects>> ToBooleanWithSideEffects(const E& data) {
        if (const EAnnotation* e = Get<EAnnotation>(data)) {
            std::optional<std::pair<bool, SideEffects>> result = ToBooleanWithSideEffects(e->value.data);
            if (result) {
                if (Has(e->flags, AnnotationFlags::kCanBeRemovedIfUnused)) {
                    result->second = SideEffects::kNoSideEffects;
                }
            }
            return result;
        }
        if (const EInlinedEnum* e = Get<EInlinedEnum>(data)) {
            return ToBooleanWithSideEffects(e->value.data);
        }

        if (Get<ENull>(data) != nullptr || Get<EUndefined>(data) != nullptr) {
            return std::make_pair(false, SideEffects::kNoSideEffects);
        }
        if (const EBoolean* e = Get<EBoolean>(data)) {
            return std::make_pair(e->value, SideEffects::kNoSideEffects);
        }
        if (const ENumber* e = Get<ENumber>(data)) {
            return std::make_pair(e->value != 0 && !std::isnan(e->value), SideEffects::kNoSideEffects);
        }
        if (const EBigInt* e = Get<EBigInt>(data)) {
            std::optional<bool> equal = CheckEqualityBigInt(e->value, "0");
            if (equal) {
                return std::make_pair(!*equal, SideEffects::kNoSideEffects);
            }
            return std::nullopt;
        }
        if (const EString* e = Get<EString>(data)) {
            return std::make_pair(!e->value.empty(), SideEffects::kNoSideEffects);
        }
        if (Get<EFunction>(data) != nullptr || Get<EArrow>(data) != nullptr || Get<ERegExp>(data) != nullptr) {
            return std::make_pair(true, SideEffects::kNoSideEffects);
        }
        if (Get<EObject>(data) != nullptr || Get<EArray>(data) != nullptr || Get<EClass>(data) != nullptr) {
            return std::make_pair(true, SideEffects::kCouldHaveSideEffects);
        }

        if (const EUnary* e = Get<EUnary>(data)) {
            switch (e->op) {
                case OpCode::kUnOpVoid:
                    return std::make_pair(false, SideEffects::kCouldHaveSideEffects);

                case OpCode::kUnOpTypeof:
                    // Never an empty string
                    if (e->was_originally_typeof_identifier) {
                        // Expressions such as "typeof x" never have any side effects
                        return std::make_pair(true, SideEffects::kNoSideEffects);
                    }
                    return std::make_pair(true, SideEffects::kCouldHaveSideEffects);

                case OpCode::kUnOpNot: {
                    std::optional<std::pair<bool, SideEffects>> result = ToBooleanWithSideEffects(e->value.data);
                    if (result) {
                        return std::make_pair(!result->first, result->second);
                    }
                    break;
                }

                default:
                    break; 
            }
        }

        if (const EBinary* e = Get<EBinary>(data)) {
            switch (e->op) {
                case OpCode::kBinOpLogicalOr:
                    // "anything || truthy" is truthy
                    if (std::optional<std::pair<bool, SideEffects>> result = ToBooleanWithSideEffects(e->right.data)) {
                        if (result->first) {
                            return std::make_pair(true, SideEffects::kCouldHaveSideEffects);
                        }
                    }
                    break;

                case OpCode::kBinOpLogicalAnd:
                    // "anything && falsy" is falsy
                    if (std::optional<std::pair<bool, SideEffects>> result = ToBooleanWithSideEffects(e->right.data)) {
                        if (!result->first) {
                            return std::make_pair(false, SideEffects::kCouldHaveSideEffects);
                        }
                    }
                    break;

                case OpCode::kBinOpComma:
                    // "anything, truthy/falsy" is truthy/falsy
                    if (std::optional<std::pair<bool, SideEffects>> result = ToBooleanWithSideEffects(e->right.data)) {
                        return std::make_pair(result->first, SideEffects::kCouldHaveSideEffects);
                    }
                    break;

                default:
                    break; 
            }
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Boolean context simplification
    // -----------------------------------------------------------------------

    // Simplifies an expression when it is known to be used in a boolean
    // context (e.g. as the test of an if-statement or while-loop). This
    // eliminates redundant double-negations, collapses constant ternaries,
    // and eliminates unnecessary comparisons.
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    //
    // Example:
    //   SimplifyBooleanExpr(!(!x))     =>  x
    //   SimplifyBooleanExpr(!!x)       =>  x
    //   SimplifyBooleanExpr(!![])      =>  true
    Expr HelperContext::SimplifyBooleanExpr(const Expr& expr) {
        if (const EUnary* e = Get<EUnary>(expr.data)) {
            if (e->op == OpCode::kUnOpNot) {
                // "!!a" => "a"
                if (const EUnary* e2 = Get<EUnary>(e->value.data)) {
                    if (e2->op == OpCode::kUnOpNot) {
                        return SimplifyBooleanExpr(e2->value);
                    }
                }

                // "!!!a" => "!a"
                return Expr{std::make_shared<EUnary>(EUnary{SimplifyBooleanExpr(e->value), OpCode::kUnOpNot}), expr.loc};
            }
        } else if (const EBinary* eBin = Get<EBinary>(expr.data)) {
            Expr left = eBin->left;
            Expr right = eBin->right;

            switch (eBin->op) {
                case OpCode::kBinOpStrictEq:
                case OpCode::kBinOpStrictNe:
                case OpCode::kBinOpLooseEq:
                case OpCode::kBinOpLooseNe: {
                    std::optional<double> r = ExtractNumericValue(right.data);
                    if (r && *r == 0 && IsInt32OrUint32(left.data)) {
                        // If the left is guaranteed to be an integer (e.g. not NaN,
                        // Infinity, or a non-numeric value) then a test against zero
                        // in a boolean context is unnecessary because the value is
                        // only truthy if it's not zero.
                        if (eBin->op == OpCode::kBinOpStrictNe || eBin->op == OpCode::kBinOpLooseNe) {
                            // "if ((a >>> b) !== 0)" => "if (a >>> b)"
                            return left;
                        } else {
                            // "if ((a >>> b) === 0)" => "if (!(a >>> b))"
                            return Not(left);
                        }
                    }
                    break;
                }

                case OpCode::kBinOpLogicalAnd:
                    // "if (!!a && !!b)" => "if (a && b)"
                    left = SimplifyBooleanExpr(left);
                    right = SimplifyBooleanExpr(right);

                    if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(right.data)) {
                        if (boolean->first && boolean->second == SideEffects::kNoSideEffects) {
                            // "if (anything && truthyNoSideEffects)" => "if (anything)"
                            return left;
                        }
                    }
                    break;

                case OpCode::kBinOpLogicalOr:
                    // "if (!!a || !!b)" => "if (a || b)"
                    left = SimplifyBooleanExpr(left);
                    right = SimplifyBooleanExpr(right);

                    if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(right.data)) {
                        if (!boolean->first && boolean->second == SideEffects::kNoSideEffects) {
                            // "if (anything || falsyNoSideEffects)" => "if (anything)"
                            return left;
                        }
                    }
                    break;

                default:
                    break; 
            }

            if (!ExprEquals(left, eBin->left) || !ExprEquals(right, eBin->right)) {
                return Expr{std::make_shared<EBinary>(EBinary{left, right, eBin->op}), expr.loc};
            }
        } else if (const EIf* eIf = Get<EIf>(expr.data)) {
            // "if (a ? !!b : !!c)" => "if (a ? b : c)"
            Expr yes = SimplifyBooleanExpr(eIf->yes);
            Expr no = SimplifyBooleanExpr(eIf->no);

            if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(yes.data)) {
                if (boolean->second == SideEffects::kNoSideEffects) {
                    if (boolean->first) {
                        // "if (anything1 ? truthyNoSideEffects : anything2)" => "if (anything1 || anything2)"
                        return JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, eIf->test, no);
                    } else {
                        // "if (anything1 ? falsyNoSideEffects : anything2)" => "if (!anything1 || anything2)"
                        return JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, Not(eIf->test), no);
                    }
                }
            }

            if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(no.data)) {
                if (boolean->second == SideEffects::kNoSideEffects) {
                    if (boolean->first) {
                        // "if (anything1 ? anything2 : truthyNoSideEffects)" => "if (!anything1 || anything2)"
                        return JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, Not(eIf->test), yes);
                    } else {
                        // "if (anything1 ? anything2 : falsyNoSideEffects)" => "if (anything1 && anything2)"
                        return JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, eIf->test, yes);
                    }
                }
            }

            if (!ExprEquals(yes, eIf->yes) || !ExprEquals(no, eIf->no)) {
                return Expr{std::make_shared<EIf>(EIf{eIf->test, yes, no}), expr.loc};
            }
        } else {
            // "!![]" => "true"
            if (std::optional<std::pair<bool, SideEffects>> boolean = ToBooleanWithSideEffects(expr.data)) {
                if (boolean->second == SideEffects::kNoSideEffects || ExprCanBeRemovedIfUnused(expr)) {
                    return Expr{std::make_shared<EBoolean>(EBoolean{boolean->first}), expr.loc};
                }
            }
        }

        return expr;
    }

    // -----------------------------------------------------------------------
    // Statement side-effect analysis
    // -----------------------------------------------------------------------

    // Determines whether a block of statements can be safely removed if all
    // results are unused. Checks each statement recursively for side effects,
    // including function declarations, imports, class declarations, return
    // statements, variable declarations, try blocks, and exports.
    //
    // The flags parameter controls special behavior:
    //   - kReturnCanBeRemovedIfUnused: allow return statements to be dropped
    //   - kKeepExportClauses: preserve export clauses even if unused
    bool HelperContext::StmtsCanBeRemovedIfUnused(const std::vector<Stmt>& stmts, StmtsCanBeRemovedIfUnusedFlags flags) {
        for (const Stmt& stmt : stmts) {
            if (Get<SFunction>(stmt.data) != nullptr || Get<SEmpty>(stmt.data) != nullptr) {
                // These never have side effects
            } else if (Get<SImport>(stmt.data) != nullptr) {
                // Let these be removed if they are unused. Note that we also need to
                // check if the imported file is marked as "sideEffects: false" before we
                // can remove a SImport statement. Otherwise the import must be kept for
                // its side effects.
            } else if (const SClass* s = Get<SClass>(stmt.data)) {
                if (!ClassCanBeRemovedIfUnused(s->class_)) {
                    return false;
                }
            } else if (const SReturn* sReturn = Get<SReturn>(stmt.data)) {
                if (!Has(flags, StmtsCanBeRemovedIfUnusedFlags::kReturnCanBeRemovedIfUnused) ||
                    (!IsNil(sReturn->value_or_nil.data) && !ExprCanBeRemovedIfUnused(sReturn->value_or_nil))) {
                    return false;
                }
            } else if (const SExpr* sExpr = Get<SExpr>(stmt.data)) {
                if (!ExprCanBeRemovedIfUnused(sExpr->value)) {
                    if (sExpr->is_from_class_or_fn_that_can_be_removed_if_unused) {
                        // This statement was automatically generated when lowering a class
                        // or function that we were able to analyze as having no side effects
                        // before lowering. So we consider it to be removable. The assumption
                        // here is that we are seeing at least all of the statements from the
                        // class lowering operation all at once (although we may possibly be
                        // seeing even more statements than that). Since we're making a binary
                        // all-or-nothing decision about the side effects of these statements,
                        // we can safely consider these to be side-effect free because we
                        // aren't in danger of partially dropping some of the class setup code.
                    } else {
                        return false;
                    }
                }
            } else if (const SLocal* sLocal = Get<SLocal>(stmt.data)) {
                // "await" is a side effect because it affects code timing
                if (sLocal->kind == LocalKind::kAwaitUsing) {
                    return false;
                }

                for (const Decl& decl : sLocal->decls) {
                    // Check that the bindings are side-effect free
                    if (Get<BIdentifier>(decl.binding.data) != nullptr) {
                        // An identifier binding has no side effects
                    } else if (const BArray* binding = Get<BArray>(decl.binding.data)) {
                        // Destructuring the initializer has no side effects if the
                        // initializer is an array, since we assume the iterator is then
                        // the built-in side-effect free array iterator.
                        if (Get<EArray>(decl.value_or_nil.data) != nullptr) {
                            for (const ArrayBinding& item : binding->items) {
                                if (!IsNil(item.default_value_or_nil.data) && !ExprCanBeRemovedIfUnused(item.default_value_or_nil)) {
                                    return false;
                                }

                                if (Get<BIdentifier>(item.binding.data) != nullptr || Get<BMissing>(item.binding.data) != nullptr) {
                                    // Right now we only handle an array pattern with identifier
                                    // bindings or with empty holes (i.e. "missing" elements)
                                } else {
                                    return false;
                                }
                            }
                        } else {
                            return false;
                        }
                    } else {
                        // Consider anything else to potentially have side effects
                        return false;
                    }

                    // Check that the initializer is side-effect free
                    if (!IsNil(decl.value_or_nil.data)) {
                        if (!ExprCanBeRemovedIfUnused(decl.value_or_nil)) {
                            return false;
                        }

                        // "using" declarations are only side-effect free if they are initialized to null or undefined
                        if (IsUsing(sLocal->kind)) {
                            if (PrimitiveType t = KnownPrimitiveType(decl.value_or_nil.data); t != PrimitiveType::kNull && t != PrimitiveType::kUndefined) {
                                return false;
                            }
                        }
                    }
                }
            } else if (const STry* sTry = Get<STry>(stmt.data)) {
                if (!StmtsCanBeRemovedIfUnused(sTry->block.stmts, StmtsCanBeRemovedIfUnusedFlags::kNone) ||
                    (sTry->finally_block != nullptr && !StmtsCanBeRemovedIfUnused(sTry->finally_block->block.stmts, StmtsCanBeRemovedIfUnusedFlags::kNone))) {
                    return false;
                }
            } else if (Get<SExportFrom>(stmt.data) != nullptr) {
                // Exports are tracked separately, so this isn't necessary
            } else if (Get<SExportClause>(stmt.data) != nullptr) {
                if (Has(flags, StmtsCanBeRemovedIfUnusedFlags::kKeepExportClauses)) {
                    return false;
                }
            } else if (const SExportDefault* sExportDefault = Get<SExportDefault>(stmt.data)) {
                const S& s2Data = sExportDefault->value.data;
                if (const SExpr* s2Expr = Get<SExpr>(s2Data)) {
                    if (!ExprCanBeRemovedIfUnused(s2Expr->value)) {
                        return false;
                    }
                } else if (Get<SFunction>(s2Data) != nullptr) {
                    // These never have side effects
                } else if (const SClass* s2Class = Get<SClass>(s2Data)) {
                    if (!ClassCanBeRemovedIfUnused(s2Class->class_)) {
                        return false;
                    }
                } else {
                    throw std::runtime_error("Internal error");
                }
            } else {
                // Assume that all statements not explicitly special-cased here have side
                // effects, and cannot be removed even if unused
                return false;
            }
        }

        return true;
    }

    // -----------------------------------------------------------------------
    // Class side-effect analysis
    // -----------------------------------------------------------------------

    // Determines whether a class declaration can be safely removed if its
    // value is unused. Checks for decorators, computed property keys with
    // non-primitive values, static blocks, and legacy TypeScript class field
    // semantics that can trigger getters.
    bool HelperContext::ClassCanBeRemovedIfUnused(const Class& class_) {
        if (!class_.decorators.empty()) {
            return false;
        }

        // Note: This check is incorrect. Extending a non-constructible object can
        // throw an error, which is a side effect:
        //
        //   async function x() {}
        //   class y extends x {}
        //
        // But refusing to tree-shake every class with a base class is not a useful
        // thing for a bundler to do. So we pretend that this edge case doesn't
        // exist. At the time of writing, both Rollup and Terser don't consider this
        // to be a side effect either.
        if (!IsNil(class_.extends_or_nil.data) && !ExprCanBeRemovedIfUnused(class_.extends_or_nil)) {
            return false;
        }

        for (const Property& property : class_.properties) {
            if (property.kind == PropertyKind::kClassStaticBlock) {
                if (!StmtsCanBeRemovedIfUnused(property.class_static_block->block.stmts, StmtsCanBeRemovedIfUnusedFlags::kNone)) {
                    return false;
                }
                continue;
            }

            if (!property.decorators.empty()) {
                return false;
            }

            if (Has(property.flags, PropertyFlags::kIsComputed) && !IsPrimitiveLiteral(property.key.data) && !IsSymbolInstance(property.key.data)) {
                return false;
            }

            if (IsMethodDefinition(property.kind)) {
                if (const EFunction* fn = Get<EFunction>(property.value_or_nil.data)) {
                    for (const Arg& arg : fn->fn.args) {
                        if (!arg.decorators.empty()) {
                            return false;
                        }
                    }
                }
            }

            if (Has(property.flags, PropertyFlags::kIsStatic)) {
                if (!IsNil(property.value_or_nil.data) && !ExprCanBeRemovedIfUnused(property.value_or_nil)) {
                    return false;
                }

                if (!IsNil(property.initializer_or_nil.data) && !ExprCanBeRemovedIfUnused(property.initializer_or_nil)) {
                    return false;
                }

                // Legacy TypeScript static class fields are considered to have side
                // effects because they use assign semantics, not define semantics, and
                // that can trigger getters. For example:
                //
                //   class Foo {
                //     static set foo(x) { importantSideEffect(x) }
                //   }
                //   class Bar extends Foo {
                //     foo = 1
                //   }
                //
                // This happens in TypeScript when "useDefineForClassFields" is disabled
                // because TypeScript (and esbuild) transforms the above class into this:
                //
                //   class Foo {
                //     static set foo(x) { importantSideEffect(x); }
                //   }
                //   class Bar extends Foo {
                //   }
                //   Bar.foo = 1;
                //
                // Note that it's not possible to analyze the base class to determine that
                // these assignments are side-effect free. For example:
                //
                //   // Some code that already ran before your code
                //   Object.defineProperty(Object.prototype, 'foo', {
                //     set(x) { imporantSideEffect(x) }
                //   })
                //
                //   // Your code
                //   class Foo {
                //     static foo = 1
                //   }
                //
                if (property.kind == PropertyKind::kField && !class_.use_define_for_class_fields) {
                    return false;
                }
            }
        }

        return true;
    }

    // -----------------------------------------------------------------------
    // Expression side-effect analysis
    // -----------------------------------------------------------------------

    // Determines whether an expression can be safely removed if its value is
    // unused. This is the core side-effect analysis used by the tree-shaking
    // and dead-code elimination passes.
    //
    // Handles: annotations, inline enums, primitive literals, property access,
    // class expressions, identifier references (with unbound-identifier checks),
    // import references, ternary expressions, arrays, objects, calls marked
    // __PURE__, new expressions marked __PURE__, unary operators, binary
    // operators, and template literals.
    //
    // Note: Unbound identifiers may have side effects via ReferenceError or
    // getters on the global object, so they are conservatively treated as
    // having side effects unless guarded by a typeof check.
    bool HelperContext::ExprCanBeRemovedIfUnused(const Expr& expr) {
        if (const EAnnotation* e = Get<EAnnotation>(expr.data)) {
            return Has(e->flags, AnnotationFlags::kCanBeRemovedIfUnused);
        } else if (const EInlinedEnum* eInlinedEnum = Get<EInlinedEnum>(expr.data)) {
            return ExprCanBeRemovedIfUnused(eInlinedEnum->value);
        } else if (Get<ENull>(expr.data) != nullptr || Get<EUndefined>(expr.data) != nullptr || Get<EMissing>(expr.data) != nullptr ||
                Get<EBoolean>(expr.data) != nullptr || Get<ENumber>(expr.data) != nullptr || Get<EBigInt>(expr.data) != nullptr ||
                Get<EString>(expr.data) != nullptr || Get<EThis>(expr.data) != nullptr || Get<ERegExp>(expr.data) != nullptr ||
                Get<EFunction>(expr.data) != nullptr || Get<EArrow>(expr.data) != nullptr || Get<EImportMeta>(expr.data) != nullptr) {
            return true;
        } else if (const EDot* eDot = Get<EDot>(expr.data)) {
            return eDot->can_be_removed_if_unused;
        } else if (const EClass* eClass = Get<EClass>(expr.data)) {
            return ClassCanBeRemovedIfUnused(eClass->class_);
        } else if (const EIdentifier* eIdent = Get<EIdentifier>(expr.data)) {
            if (eIdent->must_keep_due_to_with_stmt) {
                return false;
            }

            // Unbound identifiers cannot be removed because they can have side effects.
            // One possible side effect is throwing a ReferenceError if they don't exist.
            // Another one is a getter with side effects on the global object:
            //
            //   Object.defineProperty(globalThis, 'x', {
            //     get() {
            //       sideEffect();
            //     },
            //   });
            //
            // Be very careful about this possibility. It's tempting to treat all
            // identifier expressions as not having side effects but that's wrong. We
            // must make sure they have been declared by the code we are currently
            // compiling before we can tell that they have no side effects.
            //
            // Note that we currently ignore ReferenceErrors due to TDZ access. This is
            // incorrect but proper TDZ analysis is very complicated and would have to
            // be very conservative, which would inhibit a lot of optimizations of code
            // inside closures. This may need to be revisited if it proves problematic.
            if (eIdent->can_be_removed_if_unused || !is_unbound(eIdent->ref)) {
                return true;
            }
        } else if (Get<EImportIdentifier>(expr.data) != nullptr) {
            // References to an ES6 import item are always side-effect free in an
            // ECMAScript environment.
            //
            // They could technically have side effects if the imported module is a
            // CommonJS module and the import item was translated to a property access
            // (which esbuild's bundler does) and the property has a getter with side
            // effects.
            //
            // But this is very unlikely and respecting this edge case would mean
            // disabling tree shaking of all code that references an export from a
            // CommonJS module. It would also likely violate the expectations of some
            // developers because the code *looks* like it should be able to be tree
            // shaken.
            //
            // So we deliberately ignore this edge case and always treat import item
            // references as being side-effect free.
            return true;
        } else if (const EIf* eIf = Get<EIf>(expr.data)) {
            return ExprCanBeRemovedIfUnused(eIf->test) &&
                ((isSideEffectFreeUnboundIdentifierRef(eIf->yes, eIf->test, true) || ExprCanBeRemovedIfUnused(eIf->yes)) &&
                (isSideEffectFreeUnboundIdentifierRef(eIf->no, eIf->test, false) || ExprCanBeRemovedIfUnused(eIf->no)));
        } else if (const EArray* eArr = Get<EArray>(expr.data)) {
            for (Expr item : eArr->items) {
                if (const ESpread* spread = Get<ESpread>(item.data)) {
                    if (Get<EArray>(spread->value.data) != nullptr) {
                        // Spread of an inline array such as "[...[x]]" is side-effect free
                        item = spread->value;
                    }
                }

                if (!ExprCanBeRemovedIfUnused(item)) {
                    return false;
                }
            }
            return true;
        } else if (const EObject* eObj = Get<EObject>(expr.data)) {
            for (const Property& property : eObj->properties) {
                // The key must still be evaluated if it's computed or a spread
                if (property.kind == PropertyKind::kSpread) {
                    return false;
                }
                if (Has(property.flags, PropertyFlags::kIsComputed) && !IsPrimitiveLiteral(property.key.data) && !IsSymbolInstance(property.key.data)) {
                    return false;
                }
                if (!IsNil(property.value_or_nil.data) && !ExprCanBeRemovedIfUnused(property.value_or_nil)) {
                    return false;
                }
            }
            return true;
        } else if (const ECall* eCall = Get<ECall>(expr.data)) {
            // A call that has been marked "__PURE__" can be removed if all arguments
            // can be removed. The annotation causes us to ignore the target.
            if (eCall->can_be_unwrapped_if_unused) {
                for (const Expr& arg : eCall->args) {
                    if (!ExprCanBeRemovedIfUnused(arg)) {
                        return false;
                    }
                }
                return true;
            }
        } else if (const ENew* eNew = Get<ENew>(expr.data)) {
            // A constructor call that has been marked "__PURE__" can be removed if all
            // arguments can be removed. The annotation causes us to ignore the target.
            if (eNew->can_be_unwrapped_if_unused) {
                for (const Expr& arg : eNew->args) {
                    if (!ExprCanBeRemovedIfUnused(arg)) {
                        return false;
                    }
                }
                return true;
            }
        } else if (const EUnary* eUn = Get<EUnary>(expr.data)) {
            switch (eUn->op) {
                // These operators must not have any type conversions that can execute code
                // such as "toString" or "valueOf". They must also never throw any exceptions.
                case OpCode::kUnOpVoid:
                case OpCode::kUnOpNot:
                    return ExprCanBeRemovedIfUnused(eUn->value);

                case OpCode::kUnOpNeg:
                    if (Get<EBigInt>(eUn->value.data) != nullptr) {
                        // Consider negated bigints to have no side effects
                        return true;
                    }
                    break;

                // The "typeof" operator doesn't do any type conversions so it can be removed
                // if the result is unused and the operand has no side effects. However, it
                // has a special case where if the operand is an identifier expression such
                // as "typeof x" and "x" doesn't exist, no reference error is thrown so the
                // operation has no side effects.
                case OpCode::kUnOpTypeof:
                    if (Get<EIdentifier>(eUn->value.data) != nullptr && eUn->was_originally_typeof_identifier) {
                        // Expressions such as "typeof x" never have any side effects
                        return true;
                    }
                    return ExprCanBeRemovedIfUnused(eUn->value);
                default:
                    break;     
            }
        } else if (const EBinary* eBin = Get<EBinary>(expr.data)) {
            switch (eBin->op) {
                // These operators must not have any type conversions that can execute code
                // such as "toString" or "valueOf". They must also never throw any exceptions.
                case OpCode::kBinOpStrictEq:
                case OpCode::kBinOpStrictNe:
                case OpCode::kBinOpComma:
                case OpCode::kBinOpNullishCoalescing:
                    return ExprCanBeRemovedIfUnused(eBin->left) && ExprCanBeRemovedIfUnused(eBin->right);

                // Special-case "||" to make sure "typeof x === 'undefined' || x" can be removed
                case OpCode::kBinOpLogicalOr:
                    return ExprCanBeRemovedIfUnused(eBin->left) &&
                        (isSideEffectFreeUnboundIdentifierRef(eBin->right, eBin->left, false) || ExprCanBeRemovedIfUnused(eBin->right));

                // Special-case "&&" to make sure "typeof x !== 'undefined' && x" can be removed
                case OpCode::kBinOpLogicalAnd:
                    return ExprCanBeRemovedIfUnused(eBin->left) &&
                        (isSideEffectFreeUnboundIdentifierRef(eBin->right, eBin->left, true) || ExprCanBeRemovedIfUnused(eBin->right));

                // For "==" and "!=", pretend the operator was actually "===" or "!==". If
                // we know that we can convert it to "==" or "!=", then we can consider the
                // operator itself to have no side effects. This matters because our mangle
                // logic will convert "typeof x === 'object'" into "typeof x == 'object'"
                // and since "typeof x === 'object'" is considered to be side-effect free,
                // we must also consider "typeof x == 'object'" to be side-effect free.
                case OpCode::kBinOpLooseEq:
                case OpCode::kBinOpLooseNe:
                    return CanChangeStrictToLoose(eBin->left, eBin->right) && ExprCanBeRemovedIfUnused(eBin->left) && ExprCanBeRemovedIfUnused(eBin->right);

                // Special-case "<" and ">" with string, number, or bigint arguments
                case OpCode::kBinOpLt:
                case OpCode::kBinOpGt:
                case OpCode::kBinOpLe:
                case OpCode::kBinOpGe: {
                    PrimitiveType left = KnownPrimitiveType(eBin->left.data);
                    switch (left) {
                        case PrimitiveType::kString:
                        case PrimitiveType::kNumber:
                        case PrimitiveType::kBigInt:
                            return KnownPrimitiveType(eBin->right.data) == left && ExprCanBeRemovedIfUnused(eBin->left) && ExprCanBeRemovedIfUnused(eBin->right);
                        default:
                            break;
                    }
                }
                default:
                    break; 
            }
        } else if (const ETemplate* eTempl = Get<ETemplate>(expr.data)) {
            // A template can be removed if it has no tag and every value has no side
            // effects and results in some kind of primitive, since all primitives
            // have a "ToString" operation with no side effects.
            if (IsNil(eTempl->tag_or_nil.data) || eTempl->can_be_unwrapped_if_unused) {
                for (const TemplatePart& part : eTempl->parts) {
                    if (!ExprCanBeRemovedIfUnused(part.value) || KnownPrimitiveType(part.value.data) == PrimitiveType::kUnknown) {
                        return false;
                    }
                }
                return true;
            }
        }

        // Assume all other expression types have side effects and cannot be removed
        return false;
    }

    // -----------------------------------------------------------------------
    // Guarded unbound identifier analysis
    // -----------------------------------------------------------------------

    // Checks whether a reference to an unbound identifier is safe to remove
    // when guarded by a typeof check or comparison against zero. This handles
    // patterns such as:
    //   typeof x !== 'undefined' ? x : null
    //   typeof x < 'u' ? x : null
    //   (a >>> b) !== 0 ? a >>> b : default
    //
    // The is_yes_branch parameter indicates whether the value expression is
    // in the true branch (is_yes_branch=true) or false branch of the ternary.
    bool HelperContext::isSideEffectFreeUnboundIdentifierRef(const Expr& value, const Expr& guard_condition, bool is_yes_branch) {
        if (const EIdentifier* id = Get<EIdentifier>(value.data)) {
            if (is_unbound(id->ref)) {
                if (const EBinary* binary = Get<EBinary>(guard_condition.data)) {
                    switch (binary->op) {
                        case OpCode::kBinOpStrictEq:
                        case OpCode::kBinOpStrictNe:
                        case OpCode::kBinOpLooseEq:
                        case OpCode::kBinOpLooseNe: {
                            // Pattern match for "typeof x !== <string>"
                            Expr typeof_expr = binary->left;
                            Expr string_expr = binary->right;
                            if (Get<EString>(typeof_expr.data) != nullptr) {
                                std::swap(typeof_expr, string_expr);
                            }
                            if (const EUnary* typeof_node = Get<EUnary>(typeof_expr.data)) {
                                if (typeof_node->op == OpCode::kUnOpTypeof && typeof_node->was_originally_typeof_identifier) {
                                    if (const EString* text = Get<EString>(string_expr.data)) {
                                        // In "typeof x !== 'undefined' ? x : null", the reference to "x" is side-effect free
                                        // In "typeof x === 'object' ? x : null", the reference to "x" is side-effect free
                                        if ((helpers::UTF16EqualsString(text->value, "undefined") == is_yes_branch) ==
                                            (binary->op == OpCode::kBinOpStrictNe || binary->op == OpCode::kBinOpLooseNe)) {
                                            if (const EIdentifier* id2 = Get<EIdentifier>(typeof_node->value.data)) {
                                                if (id2->ref == id->ref) {
                                                    return true;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            break;
                        }

                        case OpCode::kBinOpLt:
                        case OpCode::kBinOpGt:
                        case OpCode::kBinOpLe:
                        case OpCode::kBinOpGe: {
                            // Pattern match for "typeof x < <string>"
                            Expr typeof_expr = binary->left;
                            Expr string_expr = binary->right;
                            if (Get<EString>(typeof_expr.data) != nullptr) {
                                std::swap(typeof_expr, string_expr);
                                is_yes_branch = !is_yes_branch;
                            }
                            if (const EUnary* typeof_node = Get<EUnary>(typeof_expr.data)) {
                                if (typeof_node->op == OpCode::kUnOpTypeof && typeof_node->was_originally_typeof_identifier) {
                                    if (const EString* text = Get<EString>(string_expr.data)) {
                                        if (helpers::UTF16EqualsString(text->value, "u")) {
                                            // In "typeof x < 'u' ? x : null", the reference to "x" is side-effect free
                                            // In "typeof x > 'u' ? x : null", the reference to "x" is side-effect free
                                            if (is_yes_branch == (binary->op == OpCode::kBinOpLt || binary->op == OpCode::kBinOpLe)) {
                                                if (const EIdentifier* id2 = Get<EIdentifier>(typeof_node->value.data)) {
                                                    if (id2->ref == id->ref) {
                                                        return true;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            break;
                        }
                        default:
                            break; 
                    }
                }
            }
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // String-to-number value conversion
    // -----------------------------------------------------------------------

    // Attempts to convert a UTF-16 string to its numeric equivalent at compile
    // time. Only handles integer strings (with optional leading '-'). Returns
    // nullopt if the string cannot be parsed as a number, or if the parsed
    // value does not round-trip back to the same string representation.
    //
    // Example:
    //   StringToEquivalentNumberValue(u"42")     =>  42.0
    //   StringToEquivalentNumberValue(u"-7")     =>  -7.0
    //   StringToEquivalentNumberValue(u"abc")    =>  nullopt
    //   StringToEquivalentNumberValue(u"1.5")    =>  nullopt
    std::optional<double> StringToEquivalentNumberValue(std::span<const char16_t> value) {
        if (value.size() > 0x10000000ULL) {
            fprintf(stderr, "DBG StringToEquivalentNumberValue HUGE SIZE %zu\n", value.size());
        }
        if (!value.empty()) {
            uint32_t accumulator = 0;
            bool is_negative = false;
            size_t start = 0;

            if (value[0] == u'-' && value.size() > 1) {
                is_negative = true;
                start++;
            }

            for (size_t i = start; i < value.size(); i++) {
                char16_t c = value[i];
                if (c < u'0' || c > u'9') {
                    return std::nullopt;
                }
                accumulator = accumulator * 10u + (static_cast<uint32_t>(c) - static_cast<uint32_t>(u'0'));
            }

            int32_t int_value = is_negative ? static_cast<int32_t>(0u - accumulator) : static_cast<int32_t>(accumulator);

            if (helpers::UTF16EqualsString(value, std::to_string(static_cast<int64_t>(int_value)))) {
                return static_cast<double>(int_value);
            }
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Array spread inlining
    // -----------------------------------------------------------------------

    // Inlines spread expressions of array literals into the surrounding
    // array. Missing elements (holes) are converted to undefined.
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    //
    // Example:
    //   InlineSpreadsOfArrayLiterals([1, ...[2, 3], 4])
    //     =>  [1, 2, 3, 4]
    std::vector<Expr> InlineSpreadsOfArrayLiterals(const std::vector<Expr>& values) {
        std::vector<Expr> results;
        for (const Expr& value : values) {
            if (const ESpread* spread = Get<ESpread>(value.data)) {
                if (const EArray* array = Get<EArray>(spread->value.data)) {
                    for (const Expr& item : array->items) {
                        if (Get<EMissing>(item.data) != nullptr) {
                            results.push_back(Expr{kEUndefinedShared, item.loc});
                        } else {
                            results.push_back(item);
                        }
                    }
                    continue;
                }
            }
            results.push_back(value);
        }
        return results;
    }

    // -----------------------------------------------------------------------
    // Object spread inlining
    // -----------------------------------------------------------------------

    // Inlines spread expressions of object literals when the spread value is
    // a known object literal without getters, setters, or __proto__ keys.
    // Primitive spread values (which have no own properties) are skipped
    // entirely.
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    //
    // Example:
    //   MangleObjectSpread([{...{a: 1, b: 2}, c: 3}])
    //     =>  [{a: 1}, {b: 2}, {c: 3}]
    std::vector<Property> MangleObjectSpread(const std::vector<Property>& properties) {
        std::vector<Property> result;
        for (Property property : properties) {
            if (property.kind == PropertyKind::kSpread) {
                if (Get<EBoolean>(property.value_or_nil.data) != nullptr || Get<ENull>(property.value_or_nil.data) != nullptr ||
                    Get<EUndefined>(property.value_or_nil.data) != nullptr || Get<ENumber>(property.value_or_nil.data) != nullptr ||
                    Get<EBigInt>(property.value_or_nil.data) != nullptr || Get<ERegExp>(property.value_or_nil.data) != nullptr ||
                    Get<EFunction>(property.value_or_nil.data) != nullptr || Get<EArrow>(property.value_or_nil.data) != nullptr) {
                    // This value is ignored because it doesn't have any of its own properties
                    continue;
                }

                if (const EObject* v = Get<EObject>(property.value_or_nil.data)) {
                    for (size_t i = 0; i < v->properties.size(); i++) {
                        const Property& p = v->properties[i];

                        // Getters are evaluated at iteration time. The property
                        // descriptor is not inlined into the caller. Since we are not
                        // evaluating code at compile time, just bail if we hit one
                        // and preserve the spread with the remaining properties.
                        if (p.kind == PropertyKind::kGetter || p.kind == PropertyKind::kSetter) {
                            // Don't mutate the original AST
                            EObject clone = *v;
                            clone.properties = std::vector<Property>(std::next(v->properties.begin(), static_cast<std::ptrdiff_t>(i)), v->properties.end());
                            property.value_or_nil = Expr{std::make_shared<EObject>(clone), property.value_or_nil.loc};
                            result.push_back(property);
                            break;
                        }

                        // Also bail if we hit a verbatim "__proto__" key. This will
                        // actually set the prototype of the object being spread so
                        // inlining it is not correct.
                        if (p.kind == PropertyKind::kField && !Has(p.flags, PropertyFlags::kIsComputed)) {
                            if (const EString* str = Get<EString>(p.key.data)) {
                                if (helpers::UTF16EqualsString(str->value, "__proto__")) {
                                    // Don't mutate the original AST
                                    EObject clone = *v;
                                    clone.properties = std::vector<Property>(std::next(v->properties.begin(), static_cast<std::ptrdiff_t>(i)), v->properties.end());
                                    property.value_or_nil = Expr{std::make_shared<EObject>(clone), property.value_or_nil.loc};
                                    result.push_back(property);
                                    break;
                                }
                            }
                        }

                        result.push_back(p);
                    }
                    continue;
                }
            }
            result.push_back(property);
        }
        return result;
    }

    // -----------------------------------------------------------------------
    // If-expression mangling
    // -----------------------------------------------------------------------

    // Applies various simplification and mangling transforms to an if-expression.
    // This includes:
    //   - Flipping branches when the test is negated
    //   - Eliminating branches with identical values
    //   - Converting ternaries to boolean expressions
    //   - Merging nested ternaries with common branches
    //   - Converting null-guard patterns to ?? or ?.
    //   - Hoisting common call arguments
    //
    // This function intentionally avoids mutating the input AST so it can be
    // called after the AST has been frozen (i.e. after parsing ends).
    Expr HelperContext::MangleIfExpr(logger::Loc loc, const EIf& e, compat::JSFeature unsupported_features) {
        Expr test = e.test;
        Expr yes = e.yes;
        Expr no = e.no;

        // "(a, b) ? c : d" => "a, b ? c : d"
        if (const EBinary* comma = Get<EBinary>(test.data)) {
            if (comma->op == OpCode::kBinOpComma) {
                EIf inner{comma->right, yes, no};
                return JoinWithComma(comma->left, MangleIfExpr(comma->right.loc, inner, unsupported_features));
            }
        }

        // "!a ? b : c" => "a ? c : b"
        if (const EUnary* not_unary = Get<EUnary>(test.data)) {
            if (not_unary->op == OpCode::kUnOpNot) {
                test = not_unary->value;
                std::swap(yes, no);
            }
        }

        if (ValuesLookTheSame(yes.data, no.data)) {
            // "/* @__PURE__ */ a() ? b : b" => "b"
            if (ExprCanBeRemovedIfUnused(test)) {
                return yes;
            }

            // "a ? b : b" => "a, b"
            return JoinWithComma(test, yes);
        }

        // "a ? true : false" => "!!a"
        // "a ? false : true" => "!a"
        if (const EBoolean* y = Get<EBoolean>(yes.data)) {
            if (const EBoolean* n = Get<EBoolean>(no.data)) {
                if (y->value && !n->value) {
                    return Not(Not(test));
                }
                if (!y->value && n->value) {
                    return Not(test);
                }
            }
        }

        if (const EIdentifier* id = Get<EIdentifier>(test.data)) {
            // "a ? a : b" => "a || b"
            if (const EIdentifier* id2 = Get<EIdentifier>(yes.data)) {
                if (id->ref == id2->ref) {
                    return JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, test, no);
                }
            }

            // "a ? b : a" => "a && b"
            if (const EIdentifier* id2 = Get<EIdentifier>(no.data)) {
                if (id->ref == id2->ref) {
                    return JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, test, yes);
                }
            }
        }

        // "a ? b ? c : d : d" => "a && b ? c : d"
        if (const EIf* yes_if = Get<EIf>(yes.data)) {
            if (ValuesLookTheSame(yes_if->no.data, no.data)) {
                return Expr{std::make_shared<EIf>(EIf{JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, test, yes_if->test), yes_if->yes, no}), loc};
            }
        }

        // "a ? b : c ? b : d" => "a || c ? b : d"
        if (const EIf* no_if = Get<EIf>(no.data)) {
            if (ValuesLookTheSame(yes.data, no_if->yes.data)) {
                return Expr{std::make_shared<EIf>(EIf{JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, test, no_if->test), yes, no_if->no}), loc};
            }
        }

        // "a ? c : (b, c)" => "(a || b), c"
        if (const EBinary* comma = Get<EBinary>(no.data)) {
            if (comma->op == OpCode::kBinOpComma && ValuesLookTheSame(yes.data, comma->right.data)) {
                return JoinWithComma(
                    JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, test, comma->left),
                    comma->right);
            }
        }

        // "a ? (b, c) : c" => "(a && b), c"
        if (const EBinary* comma = Get<EBinary>(yes.data)) {
            if (comma->op == OpCode::kBinOpComma && ValuesLookTheSame(comma->right.data, no.data)) {
                return JoinWithComma(
                    JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, test, comma->left),
                    comma->right);
            }
        }

        // "a ? b || c : c" => "(a && b) || c"
        if (const EBinary* binary = Get<EBinary>(yes.data)) {
            if (binary->op == OpCode::kBinOpLogicalOr && ValuesLookTheSame(binary->right.data, no.data)) {
                return Expr{std::make_shared<EBinary>(EBinary{
                            JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalAnd, test, binary->left),
                            binary->right,
                            OpCode::kBinOpLogicalOr,
                        }),
                            loc};
            }
        }

        // "a ? c : b && c" => "(a || b) && c"
        if (const EBinary* binary = Get<EBinary>(no.data)) {
            if (binary->op == OpCode::kBinOpLogicalAnd && ValuesLookTheSame(yes.data, binary->right.data)) {
                return Expr{std::make_shared<EBinary>(EBinary{
                            JoinWithLeftAssociativeOp(OpCode::kBinOpLogicalOr, test, binary->left),
                            binary->right,
                            OpCode::kBinOpLogicalAnd,
                        }),
                            loc};
            }
        }

        // "a ? b(c, d) : b(e, d)" => "b(a ? c : e, d)"
        if (const ECall* y = Get<ECall>(yes.data)) {
            if (!y->args.empty()) {
                if (const ECall* n = Get<ECall>(no.data)) {
                    if (n->args.size() == y->args.size() && y->HasSameFlagsAs(*n) &&
                        ValuesLookTheSame(y->target.data, n->target.data)) {
                        // Only do this if the condition can be reordered past the call target
                        // without side effects. For example, if the test or the call target is
                        // an unbound identifier, reordering could potentially mean evaluating
                        // the code could throw a different ReferenceError.
                        if (ExprCanBeRemovedIfUnused(test) && ExprCanBeRemovedIfUnused(y->target)) {
                            bool same_tail_args = true;
                            for (size_t i = 1; i < y->args.size(); i++) {
                                if (!ValuesLookTheSame(y->args[i].data, n->args[i].data)) {
                                    same_tail_args = false;
                                    break;
                                }
                            }
                            if (same_tail_args) {
                                const ESpread* yes_spread = Get<ESpread>(y->args[0].data);
                                const ESpread* no_spread = Get<ESpread>(n->args[0].data);

                                // "a ? b(...c) : b(...e)" => "b(...a ? c : e)"
                                if (yes_spread != nullptr && no_spread != nullptr) {
                                    // Don't mutate the original AST
                                    EIf temp{test, yes_spread->value, no_spread->value};
                                    ECall clone = *y;
                                    clone.args = y->args;
                                    clone.args[0] = Expr{std::make_shared<ESpread>(ESpread{MangleIfExpr(loc, temp, unsupported_features)}), loc};
                                    return Expr{std::make_shared<ECall>(clone), loc};
                                }

                                // "a ? b(c) : b(e)" => "b(a ? c : e)"
                                if (yes_spread == nullptr && no_spread == nullptr) {
                                    // Don't mutate the original AST
                                    EIf temp{test, y->args[0], n->args[0]};
                                    ECall clone = *y;
                                    clone.args = y->args;
                                    clone.args[0] = MangleIfExpr(loc, temp, unsupported_features);
                                    return Expr{std::make_shared<ECall>(clone), loc};
                                }
                            }
                        }
                    }
                }
            }
        }

        // Try using the "??" or "?." operators
        if (const EBinary* binary = Get<EBinary>(test.data)) {
            Expr check;
            Expr when_null;
            Expr when_non_null;

            switch (binary->op) {
                case OpCode::kBinOpLooseEq:
                    if (Get<ENull>(binary->right.data) != nullptr) {
                        // "a == null ? _ : _"
                        check = binary->left;
                        when_null = yes;
                        when_non_null = no;
                    } else if (Get<ENull>(binary->left.data) != nullptr) {
                        // "null == a ? _ : _"
                        check = binary->right;
                        when_null = yes;
                        when_non_null = no;
                    }
                    break;

                case OpCode::kBinOpLooseNe:
                    if (Get<ENull>(binary->right.data) != nullptr) {
                        // "a != null ? _ : _"
                        check = binary->left;
                        when_non_null = yes;
                        when_null = no;
                    } else if (Get<ENull>(binary->left.data) != nullptr) {
                        // "null != a ? _ : _"
                        check = binary->right;
                        when_non_null = yes;
                        when_null = no;
                    }
                    break;
                default:
                    break; 
            }

            if (ExprCanBeRemovedIfUnused(check)) {
                // "a != null ? a : b" => "a ?? b"
                if (!compat::Has(unsupported_features, compat::JSFeature::kNullishCoalescing) && ValuesLookTheSame(check.data, when_non_null.data)) {
                    return JoinWithLeftAssociativeOp(OpCode::kBinOpNullishCoalescing, check, when_null);
                }

                // "a != null ? a.b.c[d](e) : undefined" => "a?.b.c[d](e)"
                if (!compat::Has(unsupported_features, compat::JSFeature::kOptionalChain)) {
                    if (Get<EUndefined>(when_null.data) != nullptr && TryToInsertOptionalChain(check, when_non_null)) {
                        return when_non_null;
                    }
                }
            }
        }

        // Don't mutate the original AST
        if (!ExprEquals(test, e.test) || !ExprEquals(yes, e.yes) || !ExprEquals(no, e.no)) {
            return Expr{std::make_shared<EIf>(EIf{test, yes, no}), loc};
        }

        return Expr{std::make_shared<EIf>(e), loc};
    }

    // -----------------------------------------------------------------------
    // Identifier binding traversal
    // -----------------------------------------------------------------------

    // Calls the given callback for every identifier binding found in the
    // declaration list. This is used by the renamer to update binding names.
    void ForEachIdentifierBindingInDecls(std::vector<Decl>& decls, const std::function<void(logger::Loc, BIdentifier&)>& callback) {
        for (Decl& decl : decls) {
            ForEachIdentifierBinding(decl.binding, callback);
        }
    }

    // Recursively traverses a binding pattern, calling the callback for each
    // identifier binding found. Handles missing bindings, array destructuring,
    // and object destructuring. Non-identifier bindings (e.g. nested patterns)
    // are traversed recursively.
    void ForEachIdentifierBinding(
        Binding& binding,
        const std::function<void(logger::Loc, BIdentifier&)>& callback) {
        std::visit(
            [&](const auto& node) {
                using T = std::decay_t<decltype(node)>;

                if constexpr (std::is_same_v<T, std::shared_ptr<BMissing>>) {
                    // Nothing

                } else if constexpr (std::is_same_v<T, std::shared_ptr<BIdentifier>>) {
                    callback(binding.loc, *node);

                } else if constexpr (std::is_same_v<T, std::shared_ptr<BArray>>) {
                    for (auto& item : node->items) {
                        ForEachIdentifierBinding(item.binding, callback);
                    }

                } else if constexpr (std::is_same_v<T, std::shared_ptr<BObject>>) {
                    for (auto& property : node->properties) {
                        ForEachIdentifierBinding(property.value, callback);
                    }

                } else {
                    std::abort();
                }
            },
            binding.data);
    }

}
