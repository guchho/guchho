#include "guchho/javascript/js_ast.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>

#include <string>
#include <system_error>
#include <variant>

namespace guchho::javascript {

// =============================================================================
// Operator classification helpers
// =============================================================================

// Returns true if the operator is a prefix unary operator. Prefix operators
// appear before their operand (e.g., -x, !x, typeof x). Postfix operators
// appear after (e.g., x++, x--).
//
// Example:
//   IsPrefix(OpCode::kUnOpNeg) == true   // -x
//   IsPrefix(OpCode::kUnOpPostDec) == false  // x--
bool IsPrefix(OpCode op) {
    return static_cast<uint8_t>(op) < static_cast<uint8_t>(OpCode::kUnOpPostDec);
}

// Returns the assignment target type for a unary operator. Only increment
// and decrement operators are assignment targets; all others return kNone.
//
// Example:
//   UnaryAssignTarget(OpCode::kUnOpPreInc) == AssignTarget::kUpdate   // ++x
//   UnaryAssignTarget(OpCode::kUnOpNeg) == AssignTarget::kNone        // -x
AssignTarget UnaryAssignTarget(OpCode op) {
    uint8_t value = static_cast<uint8_t>(op);
    if (value >= static_cast<uint8_t>(OpCode::kUnOpPreDec) &&
        value <= static_cast<uint8_t>(OpCode::kUnOpPostInc)) {
        return AssignTarget::kUpdate;
    }
    return AssignTarget::kNone;
}

// Returns true if the binary operator is left-associative. Left-associative
// operators group from left to right: a + b + c means (a + b) + c. The
// exponentiation operator (**) is an exception — it is right-associative.
//
// Example:
//   IsLeftAssociative(OpCode::kBinOpAdd) == true    // a + b + c
//   IsLeftAssociative(OpCode::kBinOpPow) == false   // a ** b ** c
bool IsLeftAssociative(OpCode op) {
    uint8_t value = static_cast<uint8_t>(op);
    return value >= static_cast<uint8_t>(OpCode::kBinOpAdd) &&
           value < static_cast<uint8_t>(OpCode::kBinOpComma) &&
           op != OpCode::kBinOpPow;
}

// Returns true if the binary operator is right-associative. Right-associative
// operators group from right to left: a = b = c means a = (b = c). Assignment
// operators and exponentiation are right-associative.
//
// Example:
//   IsRightAssociative(OpCode::kBinOpAssign) == true  // a = b = c
//   IsRightAssociative(OpCode::kBinOpPow) == true     // a ** b ** c
//   IsRightAssociative(OpCode::kBinOpAdd) == false    // a + b + c
bool IsRightAssociative(OpCode op) {
    uint8_t value = static_cast<uint8_t>(op);
    return value >= static_cast<uint8_t>(OpCode::kBinOpAssign) || op == OpCode::kBinOpPow;
}

// Returns the assignment target type for a binary operator. Simple assignment
// (=) returns kReplace (the target is fully replaced). Compound assignment
// operators (+=, -=, etc.) return kUpdate (the target is read and modified).
// Non-assignment operators return kNone.
//
// Example:
//   BinaryAssignTarget(OpCode::kBinOpAssign) == AssignTarget::kReplace    // a = b
//   BinaryAssignTarget(OpCode::kBinOpAddAssign) == AssignTarget::kUpdate // a += b
//   BinaryAssignTarget(OpCode::kBinOpAdd) == AssignTarget::kNone         // a + b
AssignTarget BinaryAssignTarget(OpCode op) {
    if (op == OpCode::kBinOpAssign) {
        return AssignTarget::kReplace;
    }
    if (static_cast<uint8_t>(op) > static_cast<uint8_t>(OpCode::kBinOpAssign)) {
        return AssignTarget::kUpdate;
    }
    return AssignTarget::kNone;
}

// Returns true if the operator uses short-circuit evaluation. Short-circuit
// operators may not evaluate their right operand depending on the left
// operand's value. This affects side-effect analysis and dead-code elimination.
//
// Example:
//   IsShortCircuit(OpCode::kBinOpLogicalOr) == true     // a || b
//   IsShortCircuit(OpCode::kBinOpLogicalAnd) == true    // a && b
//   IsShortCircuit(OpCode::kBinOpNullishCoalescing) == true  // a ?? b
//   IsShortCircuit(OpCode::kBinOpAdd) == false           // a + b
bool IsShortCircuit(OpCode op) {
    switch (op) {
        case OpCode::kBinOpLogicalOr:
        case OpCode::kBinOpLogicalOrAssign:
        case OpCode::kBinOpLogicalAnd:
        case OpCode::kBinOpLogicalAndAssign:
        case OpCode::kBinOpNullishCoalescing:
        case OpCode::kBinOpNullishCoalescingAssign:
            return true;
        default:
            return false;
    }
}

// =============================================================================
// Operator table
// =============================================================================

// Lookup table mapping each operator token to its text, precedence level,
// and whether it is a keyword operator. The table is indexed by opcode
// value (cast to uint8_t) so that the opcode can be used to look up the
// corresponding text and precedence.
//
// The table is organized into sections:
//   - Prefix unary operators (kPrefix precedence)
//   - Prefix update operators (++x, --x)
//   - Postfix update operators (x++, x--)
//   - Left-associative binary operators (+, -, *, etc.)
//   - Non-associative operators (,)
//   - Right-associative assignment operators (=, +=, etc.)
[[maybe_unused]] const OpTableEntry kOpTable[] = {
    // Prefix
    {"+", L::kPrefix, false},
    {"-", L::kPrefix, false},
    {"~", L::kPrefix, false},
    {"!", L::kPrefix, false},
    {"void", L::kPrefix, true},
    {"typeof", L::kPrefix, true},
    {"delete", L::kPrefix, true},

    // Prefix update
    {"--", L::kPrefix, false},
    {"++", L::kPrefix, false},

    // Postfix update
    {"--", L::kPostfix, false},
    {"++", L::kPostfix, false},

    // Left-associative
    {"+", L::kAdd, false},
    {"-", L::kAdd, false},
    {"*", L::kMultiply, false},
    {"/", L::kMultiply, false},
    {"%", L::kMultiply, false},
    {"**", L::kExponentiation, false}, // Right-associative
    {"<", L::kCompare, false},
    {"<=", L::kCompare, false},
    {">", L::kCompare, false},
    {">=", L::kCompare, false},
    {"in", L::kCompare, true},
    {"instanceof", L::kCompare, true},
    {"<<", L::kShift, false},
    {">>", L::kShift, false},
    {">>>", L::kShift, false},
    {"==", L::kEquals, false},
    {"!=", L::kEquals, false},
    {"===", L::kEquals, false},
    {"!==", L::kEquals, false},
    {"??", L::kNullishCoalescing, false},
    {"||", L::kLogicalOr, false},
    {"&&", L::kLogicalAnd, false},
    {"|", L::kBitwiseOr, false},
    {"&", L::kBitwiseAnd, false},
    {"^", L::kBitwiseXor, false},

    // Non-associative
    {",", L::kComma, false},

    // Right-associative
    {"=", L::kAssign, false},
    {"+=", L::kAssign, false},
    {"-=", L::kAssign, false},
    {"*=", L::kAssign, false},
    {"/=", L::kAssign, false},
    {"%=", L::kAssign, false},
    {"**=", L::kAssign, false},
    {"<<=", L::kAssign, false},
    {">>=", L::kAssign, false},
    {">>>=", L::kAssign, false},
    {"|=", L::kAssign, false},
    {"&=", L::kAssign, false},
    {"^=", L::kAssign, false},
    {"\?\?=", L::kAssign, false},
    {"||=", L::kAssign, false},
    {"&&=", L::kAssign, false},
};

// =============================================================================
// Property and scope classification
// =============================================================================

// Returns true if the property kind is a method definition (method, getter,
// or setter). Data properties and spread elements are not method definitions.
//
// Example:
//   IsMethodDefinition(PropertyKind::kMethod) == true
//   IsMethodDefinition(PropertyKind::kGetter) == true
//   IsMethodDefinition(PropertyKind::kField) == false
bool IsMethodDefinition(PropertyKind kind) {
    return kind == PropertyKind::kMethod || kind == PropertyKind::kGetter ||
           kind == PropertyKind::kSetter;
}

// Returns true if the local kind is a "using" or "await using" declaration.
// These are explicit resource management declarations that require special
// disposal handling at block exit.
//
// Example:
//   IsUsing(LocalKind::kUsing) == true
//   IsUsing(LocalKind::kAwaitUsing) == true
//   IsUsing(LocalKind::kConst) == false
bool IsUsing(LocalKind kind) {
    return static_cast<uint8_t>(kind) >= static_cast<uint8_t>(LocalKind::kUsing);
}

// Returns true if the scope kind stops variable hoisting. Entry scopes
// (modules, TypeScript enums/namespaces) and function scopes prevent
// var declarations from leaking into parent scopes.
//
// Example:
//   StopsHoisting(ScopeKind::kEntry) == true
//   StopsHoisting(ScopeKind::kFunctionBody) == true
//   StopsHoisting(ScopeKind::kBlock) == false
bool StopsHoisting(ScopeKind kind) {
    return static_cast<uint8_t>(kind) >= static_cast<uint8_t>(ScopeKind::kEntry);
}

// =============================================================================
// Module type classification
// =============================================================================

// Returns true if the export kind is dynamic (CommonJS or ESM with dynamic
// fallback). Dynamic exports cannot be statically analyzed for tree shaking.
//
// Example:
//   IsDynamic(ExportsKind::kCommonJS) == true
//   IsDynamic(ExportsKind::kESM) == false
//   IsDynamic(ExportsKind::kESMWithDynamicFallback) == true
bool IsDynamic(ExportsKind kind) {
    return kind == ExportsKind::kCommonJS || kind == ExportsKind::kESMWithDynamicFallback;
}

// Returns true if the module type is CommonJS (any variant: .cjs, .cts,
// or package.json "type": "commonjs").
//
// Example:
//   IsCommonJS(ModuleType::kCommonJS_CJS) == true
//   IsCommonJS(ModuleType::kESM_MJS) == false
bool IsCommonJS(ModuleType mt) {
    uint8_t value = static_cast<uint8_t>(mt);
    return value >= static_cast<uint8_t>(ModuleType::kCommonJS_CJS) &&
           value <= static_cast<uint8_t>(ModuleType::kCommonJS_PackageJSON);
}

// Returns true if the module type is ESM (any variant: .mjs, .mts, or
// package.json "type": "module").
//
// Example:
//   IsESM(ModuleType::kESM_MJS) == true
//   IsESM(ModuleType::kCommonJS_CJS) == false
bool IsESM(ModuleType mt) {
    uint8_t value = static_cast<uint8_t>(mt);
    return value >= static_cast<uint8_t>(ModuleType::kESM_MJS) &&
           value <= static_cast<uint8_t>(ModuleType::kESM_PackageJSON);
}

// =============================================================================
// Shared singletons
// =============================================================================

// Pre-allocated shared pointers for the most commonly used AST nodes.
// Using shared_ptr for these avoids allocating a separate heap object for
// every occurrence of null, undefined, this, empty blocks, etc.
const std::shared_ptr<BMissing> kBMissingShared = std::make_shared<BMissing>();
const std::shared_ptr<EMissing> kEMissingShared = std::make_shared<EMissing>();
const std::shared_ptr<ENull> kENullShared = std::make_shared<ENull>();
const std::shared_ptr<ESuper> kESuperShared = std::make_shared<ESuper>();
const std::shared_ptr<EThis> kEThisShared = std::make_shared<EThis>();
const std::shared_ptr<EUndefined> kEUndefinedShared = std::make_shared<EUndefined>();
const std::shared_ptr<SDebugger> kSDebuggerShared = std::make_shared<SDebugger>();
const std::shared_ptr<SEmpty> kSEmptyShared = std::make_shared<SEmpty>();
const std::shared_ptr<STypeScript> kSTypeScriptShared = std::make_shared<STypeScript>();
const std::shared_ptr<STypeScript> kSTypeScriptSharedWasDeclareClass =
    std::make_shared<STypeScript>(STypeScript{true});

// =============================================================================
// AST node comparison methods
// =============================================================================

// Returns true if two ECall nodes have the same optimization-relevant flags.
// This is used by the optimizer to determine whether two call expressions
// can be treated identically for purposes of DCE and optional-chain lowering.
//
// Example:
//   ECall a; a.optional_chain = kNone; a.kind = kNormal;
//   ECall b; b.optional_chain = kNone; b.kind = kNormal;
//   a.HasSameFlagsAs(b) == true
bool ECall::HasSameFlagsAs(const ECall& b) const {
    return optional_chain == b.optional_chain &&
           kind == b.kind &&
           can_be_unwrapped_if_unused == b.can_be_unwrapped_if_unused;
}

// Returns true if two EDot nodes have the same optimization-relevant flags.
// The flags control whether the property access can be removed if unused
// and whether a call on this dot can be unwrapped.
//
// Example:
//   EDot a; a.can_be_removed_if_unused = true;
//   EDot b; b.can_be_removed_if_unused = true;
//   a.HasSameFlagsAs(b) == true
bool EDot::HasSameFlagsAs(const EDot& b) const {
    return optional_chain == b.optional_chain &&
           can_be_removed_if_unused == b.can_be_removed_if_unused &&
           call_can_be_unwrapped_if_unused == b.call_can_be_unwrapped_if_unused &&
           is_symbol_instance == b.is_symbol_instance;
}

// Returns true if two EIndex nodes have the same optimization-relevant flags.
bool EIndex::HasSameFlagsAs(const EIndex& b) const {
    return optional_chain == b.optional_chain &&
           can_be_removed_if_unused == b.can_be_removed_if_unused &&
           call_can_be_unwrapped_if_unused == b.call_can_be_unwrapped_if_unused &&
           is_symbol_instance == b.is_symbol_instance;
}

// =============================================================================
// Scope methods
// =============================================================================

// Recursively sets the strict mode status on this scope and all descendant
// scopes that are currently in sloppy mode. If a scope already has strict
// mode enabled (from "use strict" or from being inside a class/module), it
// is left unchanged. This is used when entering a class body or module,
// which implicitly enables strict mode for all nested scopes.
//
// Example:
//   // Scope tree: root (sloppy) -> child (sloppy) -> grandchild (strict)
//   root.RecursiveSetStrictMode(kExplicitStrictMode)
//   // After: root (strict) -> child (strict) -> grandchild (strict, unchanged)
void Scope::RecursiveSetStrictMode(StrictModeKind new_kind) {
    if (strict_mode == StrictModeKind::kSloppyMode) {
        strict_mode = new_kind;
        for (Scope* child : children) {
            child->RecursiveSetStrictMode(new_kind);
        }
    }
}

// =============================================================================
// Float conversion helpers
// =============================================================================

// Returns the length of the shortest decimal representation of a double
// value. This is used to decide whether a numeric constant is small enough
// to inline into the output without bloating code size.
//
// Example:
//   ShortestFloatLength(3.14) == 4  // "3.14"
//   ShortestFloatLength(1.0) == 1   // "1"
//   ShortestFloatLength(1e20) == 5  // "1e+20"
int ShortestFloatLength(double value) {
    char buf[32];
    auto result = std::to_chars(buf, buf + sizeof(buf), value);
    if (result.ec != std::errc()) {
        return static_cast<int>(sizeof(buf));
    }
    return static_cast<int>(result.ptr - buf);
}

// Returns true if the double value is an integer that fits in int64_t and
// can be losslessly converted. This is used to decide whether a numeric
// constant can be inlined without precision loss.
//
// Example:
//   IsInlinableInteger(42.0) == true
//   IsInlinableInteger(3.14) == false
//   IsInlinableInteger(1e20) == false (too large for int64_t)
//   IsInlinableInteger(std::numeric_limits<double>::quiet_NaN()) == false
bool IsInlinableInteger(double value) {
    if (!std::isfinite(value)) {
        return false;
    }
    constexpr double kInt64MinAsDouble = static_cast<double>(INT64_MIN);
    constexpr double kInt64MaxAsDouble = 9223372036854775808.0; // 2^63
    if (value < kInt64MinAsDouble || value >= kInt64MaxAsDouble) {
        return false;
    }
    int64_t as_int = static_cast<int64_t>(value);
    return static_cast<double>(as_int) == value;
}

// =============================================================================
// Constant value conversion
// =============================================================================

// Attempts to extract a compile-time constant value from an expression node.
// Returns a ConstValue with kind != kNone if the expression is a simple
// literal that can be inlined. Complex expressions, function calls, and
// identifiers return kNone.
//
// The function deliberately inlines only small constants to avoid bloating
// the output. Numbers with short decimal representations (<= 8 chars) and
// strings with 3 or fewer characters are inlined.
//
// Example:
//   ExprToConstValue(ENull()) => ConstValue{ kind = kNull }
//   ExprToConstValue(ENumber(42)) => ConstValue{ number = 42, kind = kNumber }
//   ExprToConstValue(EString("hi")) => ConstValue{ string = "hi", kind = kString }
//   ExprToConstValue(ECall(...)) => ConstValue{ kind = kNone }
ConstValue ExprToConstValue(const Expr& expr) {
    return std::visit([](const auto& e) -> ConstValue {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, std::shared_ptr<ENull>>) {
            return ConstValue{.number = 0, .string = {}, .kind = ConstValueKind::kNull};
        } else if constexpr (std::is_same_v<T, std::shared_ptr<EUndefined>>) {
            return ConstValue{.number = 0, .string = {}, .kind = ConstValueKind::kUndefined};
        } else if constexpr (std::is_same_v<T, std::shared_ptr<EBoolean>>) {
            return e->value ? ConstValue{.number = 0, .string = {}, .kind = ConstValueKind::kTrue}
                            : ConstValue{.number = 0, .string = {}, .kind = ConstValueKind::kFalse};
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ENumber>>) {
            double value = e->value;
            if (IsInlinableInteger(value) || ShortestFloatLength(value) <= 8) {
                return ConstValue{.number = value, .string = {}, .kind = ConstValueKind::kNumber};
            }
        } else if constexpr (std::is_same_v<T, std::shared_ptr<EString>>) {
            if (e->value.size() <= 3) {
                return ConstValue{.number = 0, .string = e->value, .kind = ConstValueKind::kString};
            }
        } else if constexpr (std::is_same_v<T, std::shared_ptr<EBigInt>>) {
            // BigInts are deliberately not inlined because they can be
            // arbitrarily long.
        }
        return ConstValue{};
    }, expr.data);
}

// Converts a compile-time constant value back into an expression node. The
// source location is attached to the generated expression for source-map
// accuracy.
//
// Example:
//   ConstValueToExpr(loc, ConstValue{ .kind = kNull })
//     => Expr{ kENullShared, loc }
//
//   ConstValueToExpr(loc, ConstValue{ .number = 42, .kind = kNumber })
//     => Expr{ make_shared<ENumber>(42), loc }
//
// Throws std::runtime_error if the ConstValue has kind == kNone.
Expr ConstValueToExpr(logger::Loc loc, const ConstValue& value) {
    switch (value.kind) {
        case ConstValueKind::kNull:
            return Expr{kENullShared, loc};

        case ConstValueKind::kUndefined:
            return Expr{kEUndefinedShared, loc};

        case ConstValueKind::kTrue:
            return Expr{std::make_shared<EBoolean>(EBoolean{true}), loc};

        case ConstValueKind::kFalse:
            return Expr{std::make_shared<EBoolean>(EBoolean{false}), loc};

        case ConstValueKind::kNumber:
            return Expr{std::make_shared<ENumber>(ENumber{value.number}), loc};

        case ConstValueKind::kString:
            return Expr{std::make_shared<EString>(EString{value.string, {}, {}, {}, {}}), loc};

        default:
            throw std::runtime_error("Internal error: invalid constant value");
    }
}

// =============================================================================
// Identifier generation
// =============================================================================

// Generates a human-readable name suffix from a file path for use in
// auto-generated identifiers. For example, instead of the CommonJS wrapper
// for a file being called "require273", it can be called "require_react"
// instead. This function generates the path-specific part of these
// identifiers.
//
// The function handles both absolute OS-specific paths and platform-
// independent paths from source code. If the base name is "index", the
// parent directory name is used instead, since many npm packages use
// "index.js" as the default entry point.
//
// These generated names are cosmetic only and still go through the renaming
// logic to avoid symbol name collisions.
//
// Example:
//   GenerateNonUniqueNameFromPath("src/components/App.tsx") => "App"
//   GenerateNonUniqueNameFromPath("node_modules/react/index.js") => "react"
//   GenerateNonUniqueNameFromPath("lib/utils.js") => "utils"
std::string GenerateNonUniqueNameFromPath(const std::string& path) {
    std::string dir, base, ext;
    logger::PlatformIndependentPathDirBaseExt(path, dir, base, ext);

    if (base == "index") {
        std::string dir2, dir_base, dir_ext;
        logger::PlatformIndependentPathDirBaseExt(dir, dir2, dir_base, dir_ext);
        if (!dir_base.empty()) {
            base = dir_base;
        }
    }

    return EnsureValidIdentifier(base);
}

// Converts a string into a valid JavaScript identifier by stripping or
// replacing invalid characters. Only ASCII letters, digits (after the first
// character), and underscores are kept. Invalid characters between valid
// characters are replaced with underscores.
//
// This function intentionally produces only ASCII identifiers to avoid
// issues with non-BMP code points in environments that don't support
// bracketed Unicode escapes.
//
// Example:
//   EnsureValidIdentifier("camelCase") => "camelCase"
//   EnsureValidIdentifier("123abc") => "abc"
//   EnsureValidIdentifier("my-var") => "myvar"
//   EnsureValidIdentifier("") => "_"
std::string EnsureValidIdentifier(const std::string& base) {
    std::string result;
    bool needs_gap = false;
    for (std::string_view s = base; !s.empty();) {
        auto [c, size] = helpers::DecodeWTF8Rune(s);
        s = s.substr(size > 0 ? static_cast<size_t>(size) : 1);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (!result.empty() && c >= '0' && c <= '9')) {
            if (needs_gap) {
                result.push_back('_');
                needs_gap = false;
            }
            result.push_back(static_cast<char>(c));
        } else if (!result.empty()) {
            needs_gap = true;
        }
    }

    if (result.empty()) {
        return "_";
    }
    return result;
}

}
