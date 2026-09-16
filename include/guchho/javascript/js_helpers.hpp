#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <cstdint>
#include <optional>
#include <utility>

#include "guchho/logger.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/javascript/js_ast.hpp"


namespace guchho::javascript {

    // ---------------------------------------------------------------------------
    // Get<T> — Safe variant element accessor
    // ---------------------------------------------------------------------------

    // Attempts to extract a pointer to a specific type T held within a
    // std::variant that stores std::shared_ptr<T> alternatives.
    //
    // This is the primary way to inspect which concrete AST node type a
    // variant holds. The returned pointer is non-owning and valid only as
    // long as the caller keeps the variant alive.
    //
    // Two overloads are provided: one for const variants (returns const T*)
    // and one for mutable variants (returns T*).
    //
    // Example:
    //   E expr = std::make_shared<ENumber>(ENumber{ .value = 42.0 });
    //   const ENumber* num = Get<ENumber>(expr);
    //   // num != nullptr, num->value == 42.0
    //
    //   E ident = std::make_shared<EIdentifier>(EIdentifier{ ... });
    //   const ENumber* num2 = Get<ENumber>(ident);
    //   // num2 == nullptr (variant holds EIdentifier, not ENumber)
    //
    // Edge cases:
    //   - If the variant holds a null shared_ptr, Get returns nullptr even
    //     if the shared_ptr alternative type matches T.
    //   - If the variant holds a different type, Get returns nullptr.

    template <typename T, typename V>
    const T* Get(const V& data) {
        if (const std::shared_ptr<T>* p = std::get_if<std::shared_ptr<T>>(&data)) {
            if (*p) {
                return p->get();
            }
        }
        return nullptr;
    }

    template <typename T, typename V>
    T* Get(V& data) {
        if (std::shared_ptr<T>* p = std::get_if<std::shared_ptr<T>>(&data)) {
            if (*p) {
                return p->get();
            }
        }
        return nullptr;
    }

    // ---------------------------------------------------------------------------
    // IsNil — Check for missing AST nodes
    // ---------------------------------------------------------------------------

    // Returns true when the given AST variant holds a null shared_ptr.
    // This is used throughout the compiler to detect absent sub-trees,
    // such as a missing else-branch, an omitted initializer, or an
    // empty expression slot.
    //
    // Example:
    //   E missing;  // default-constructed, holds null shared_ptr
    //   IsNil(missing) == true
    //
    //   E lit = std::make_shared<ENull>(ENull{});
    //   IsNil(lit) == false

    bool IsNil(const E& data);
    bool IsNil(const S& data);
    bool IsNil(const B& data);

    // ---------------------------------------------------------------------------
    // Identifier validation
    // ---------------------------------------------------------------------------

    // Returns true if `text` is a valid ECMAScript identifier according to
    // the latest specification. The check follows the Unicode-based rules:
    //   - First character must be an ID_Start or certain special characters.
    //   - Subsequent characters must be ID_Continue or ZWNJ/ZWJ.
    //
    // Example:
    //   IsIdentifier("camelCase") == true
    //   IsIdentifier("123abc") == false
    //   IsIdentifier("_private") == true
    //
    // Edge cases:
    //   - Empty string returns false.
    //   - Identifiers with combining marks or emoji sequences are handled
    //     by the Unicode tables.

    bool IsIdentifier(std::string_view text) noexcept;

    // Same as IsIdentifier but also allows ES5 and ESNext reserved words
    // and future-reserved keywords to pass. This is used when parsing
    // legacy code or when the parser needs to accept a broader set of
    // names (e.g., for property keys in object literals).
    //
    // Example:
    //   IsIdentifierES5AndESNext("class") == true  // "class" is a keyword
    //   IsIdentifierES5AndESNext("yield") == true  // "yield" is reserved
    bool IsIdentifierES5AndESNext(std::string_view text) noexcept;

    // Determines whether a statement should be kept during dead-code
    // elimination even if its result is unused. This catches statements
    // that have observable side effects or are required for correctness
    // (e.g., "debugger" statements, "with" statements).
    //
    // Example:
    //   shouldKeepStmtInDeadControlFlow(debuggerStmt) == true
    //   shouldKeepStmtInDeadControlFlow(exprWithSideEffect) == true
    bool shouldKeepStmtInDeadControlFlow(Stmt stmt);

    // Produces a string that is guaranteed to be a legal JavaScript
    // identifier. If `text` already is a valid identifier it is returned
    // unchanged. Otherwise, invalid characters are escaped or replaced
    // using the prefix to derive a deterministic, collision-resistant name.
    //
    // Example:
    //   ForceValidIdentifier("$", "hello") == "hello"
    //   ForceValidIdentifier("$", "123bad") == "$123bad"
    //
    // Edge cases:
    //   - Very long inputs are truncated to avoid exceeding engine limits.
    //   - The prefix is prepended only when the first character is not a
    //     valid identifier start.
    std::string ForceValidIdentifier(std::string_view prefix, std::string_view text);

    // UTF-16 overloads for identifier validation. These operate on
    // std::span<const uint16_t> which is the native representation of
    // JavaScript strings in the engine.
    bool IsIdentifierUTF16(std::span<const uint16_t> text) noexcept;
    bool IsIdentifierES5AndESNextUTF16(std::span<const uint16_t> text) noexcept;

    // ---------------------------------------------------------------------------
    // Code-point classification helpers
    // ---------------------------------------------------------------------------

    // Returns true if the Unicode code point qualifies as an identifier
    // start character (ID_Start). This includes letters from any script,
    // underscore, and dollar sign.
    //
    // Example:
    //   IsIdentifierStart('A') == true
    //   IsIdentifierStart('0') == false
    //   IsIdentifierStart('_') == true
    //   IsIdentifierStart('$') == true
    bool IsIdentifierStart(char32_t code_point) noexcept;

    // Returns true if the code point qualifies as an identifier continuation
    // character (ID_Continue). This extends ID_Start with digits and
    // combining marks.
    //
    // Example:
    //   IsIdentifierContinue('A') == true
    //   IsIdentifierContinue('0') == true
    //   IsIdentifierContinue('\u0301') == true  // combining acute accent
    bool IsIdentifierContinue(char32_t code_point) noexcept;

    // ES5/ESNext variants that include additional characters allowed by
    // those specification editions (e.g., certain Unicode escape sequences
    // in identifiers).
    bool IsIdentifierStartES5AndESNext(char32_t code_point) noexcept;
    bool IsIdentifierContinueES5AndESNext(char32_t code_point) noexcept;

    // Returns true if the code point is considered whitespace by the
    // ECMAScript specification. This includes space, tab, newline, and
    // other Unicode whitespace characters.
    //
    // Example:
    //   IsWhitespace(' ') == true
    //   IsWhitespace('\t') == true
    //   IsWhitespace('\n') == true
    //   IsWhitespace('a') == false
    bool IsWhitespace(char32_t code_point) noexcept;


    // ---------------------------------------------------------------------------
    // PrimitiveType — Runtime type classification
    // ---------------------------------------------------------------------------

    // Classifies a value into one of the JavaScript primitive types. This
    // is used by the optimizer to determine what transformations are safe
    // for binary operations and comparisons.
    //
    // kUnknown  — type cannot be determined statically
    // kMixed    — value could be one of several types depending on runtime
    // kNull, kUndefined, kBoolean, kNumber, kString, kBigInt — exact type
    enum class PrimitiveType : uint8_t {
        kUnknown,
        kMixed,
        kNull,
        kUndefined,
        kBoolean,
        kNumber,
        kString,
        kBigInt,
    };

    // ---------------------------------------------------------------------------
    // EqualityKind — Comparison operator variant
    // ---------------------------------------------------------------------------

    // Distinguishes between loose (==) and strict (===) equality operators.
    // This matters because loose equality performs type coercion while
    // strict equality does not.
    //
    // Example:
    //   // kLooseEquality:   "1" == 1  => true  (type coercion)
    //   // kStrictEquality:  "1" === 1 => false (no coercion)
    enum class EqualityKind : uint8_t {
        kLooseEquality,
        kStrictEquality,
    };

    // ---------------------------------------------------------------------------
    // StringAdditionKind — String concatenation context
    // ---------------------------------------------------------------------------

    // Classifies how a string addition expression is nested. The optimizer
    // uses this to decide whether to fold adjacent string additions into a
    // single template literal.
    //
    // kNormal          — simple left + right
    // kWithNestedLeft  — left is itself a string addition (a + b) + c
    enum class StringAdditionKind : uint8_t {
        kNormal,
        kWithNestedLeft,
    };

    // ---------------------------------------------------------------------------
    // SideEffects — Purity annotation
    // ---------------------------------------------------------------------------

    // Annotates whether an expression or statement can produce observable
    // side effects. The optimizer uses this to decide whether dead code
    // can be eliminated.
    //
    // kCouldHaveSideEffects — must be kept (function call, property write, etc.)
    // kNoSideEffects        — safe to drop if result is unused
    enum class SideEffects : uint8_t {
        kCouldHaveSideEffects,
        kNoSideEffects,
    };

    // ---------------------------------------------------------------------------
    // StmtsCanBeRemovedIfUnusedFlags — Dead-code elimination control
    // ---------------------------------------------------------------------------

    // Bitflags that control which kinds of statements may be removed during
    // dead-code elimination.
    //
    // kNone                  — default: remove everything unused
    // kKeepExportClauses     — preserve export declarations even if unused
    // kReturnCanBeRemovedIfUnused — allow removing "return" with no value
    enum class StmtsCanBeRemovedIfUnusedFlags : uint8_t {
        kNone = 0,
        kKeepExportClauses = 1 << 0,
        kReturnCanBeRemovedIfUnused = 1 << 1,
    };

    inline StmtsCanBeRemovedIfUnusedFlags operator|(StmtsCanBeRemovedIfUnusedFlags a, StmtsCanBeRemovedIfUnusedFlags b) {
        return static_cast<StmtsCanBeRemovedIfUnusedFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline StmtsCanBeRemovedIfUnusedFlags operator&(StmtsCanBeRemovedIfUnusedFlags a, StmtsCanBeRemovedIfUnusedFlags b) {
        return static_cast<StmtsCanBeRemovedIfUnusedFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    inline bool Has(StmtsCanBeRemovedIfUnusedFlags flags, StmtsCanBeRemovedIfUnusedFlags flag) {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
    }

    // =============================================================================
    // HelperContext — Expression simplification and side-effect analysis
    // =============================================================================

    // HelperContext provides methods for simplifying expressions and
    // determining whether code can be removed. It wraps a callback that
    // tells the optimizer whether a given identifier reference is "unbound"
    // (i.e., could reference a global or be undeclared). This information
    // is critical for correctness: removing an expression that references
    // an unbound identifier could change behavior because the reference
    // itself may throw a ReferenceError at runtime.
    //
    // Typical usage:
    //   auto ctx = MakeHelperContext([](compiler::Ref ref) { ... });
    //   Expr simplified = ctx.SimplifyUnusedExpr(expr, features);
    //
    // The context is stateless beyond the unbound-check callback; it can
    // be created once and reused across many simplification calls.
    struct HelperContext {
        std::function<bool(compiler::Ref)> is_unbound;

        // Attempts to simplify an expression whose result is unused. If the
        // expression is side-effect-free, an empty/undefined placeholder is
        // returned. If side effects exist, they are preserved by rewriting
        // the expression.
        //
        // Example:
        //   // Input:  console.log("hi");   (unused return value)
        //   // Output: console.log("hi");   (kept for side effect)
        //
        //   // Input:  1 + 2;               (unused, no side effects)
        //   // Output: (empty placeholder)
        //
        // Edge cases:
        //   - Expressions with unbound identifiers are never removed.
        //   - Template literals with tagged functions are kept.
        Expr SimplifyUnusedExpr(const Expr& expr, compat::JSFeature unsupported_features);

        // Reduces boolean expressions to their simplest form. Applies rules
        // like !!x => Boolean(x), !true => false, and double-negation
        // elimination.
        //
        // Example:
        //   SimplifyBooleanExpr(ENot(ENot(EIdentifier("x"))))
        //     => EIdentifier("x")
        Expr SimplifyBooleanExpr(const Expr& expr);

        // Returns true if every statement in the vector can be removed if
        // its result is unused. The flags parameter controls which
        // statement types are exempt from removal.
        //
        // Example:
        //   // stmts = [SExpr(ENumber(1)), SExpr(ENumber(2))]
        //   // StmtsCanBeRemovedIfUnused(stmts, kNone) == true
        bool StmtsCanBeRemovedIfUnused(const std::vector<Stmt>& stmts, StmtsCanBeRemovedIfUnusedFlags flags);

        // Returns true if the class declaration can be removed when its
        // binding is unused. This requires that the class has no side
        // effects in its decorators, superclass, or static initializer.
        //
        // Example:
        //   // class Foo { }  (unused, no side effects)
        //   // ClassCanBeRemovedIfUnused(class) == true
        bool ClassCanBeRemovedIfUnused(const Class& class_);

        // Returns true if the expression can be dropped when its value is
        // unused. This is the main entry point for dead-expression analysis.
        //
        // Example:
        //   ExprCanBeRemovedIfUnused(ENumber(5)) == true
        //   ExprCanBeRemovedIfUnused(ECall(EIdentifier("f"))) == false
        bool ExprCanBeRemovedIfUnused(const Expr& expr);

        // Rewrites an if-expression (ternary) to reduce code size. Applies
        // optimizations like converting conditionals to logical operators
        // or short-circuit expressions.
        //
        // Example:
        //   // x ? true : false  =>  !!x
        //   // x ? false : true  =>  !x
        Expr MangleIfExpr(logger::Loc loc, const EIf& e, compat::JSFeature unsupported_features);

    private:
        bool isSideEffectFreeUnboundIdentifierRef(const Expr& value, const Expr& guard_condition, bool is_yes_branch);
    };

    // Factory function that creates a HelperContext with the given unbound-
    // identifier callback. The callback receives a compiler::Ref and must
    // return true if the reference could be unbound (global or undeclared).
    HelperContext MakeHelperContext(std::function<bool(compiler::Ref)> is_unbound);

    // =============================================================================
    // Free functions — Expression building, analysis, and folding
    // =============================================================================

    // Returns true if the expression is a property access (dot or bracket)
    // on some object. This is used to determine whether optional chaining
    // can be applied.
    //
    // Example:
    //   IsPropertyAccess(EDot(EIdentifier("obj"), "x")) == true
    //   IsPropertyAccess(EIdentifier("obj")) == false
    bool IsPropertyAccess(const Expr& expr);

    // Returns true if the expression contains an optional chain (?.). This
    // affects whether further property accesses can be folded into the
    // chain.
    //
    // Example:
    //   // a?.b.c  =>  IsOptionalChain(a) == false, IsOptionalChain(a?.b) == true
    bool IsOptionalChain(const Expr& value);

    // Builds a binary assignment expression: a = b.
    // The result has the same source location as `a`.
    //
    // Example:
    //   Assign(EIdentifier("x"), ENumber(1))
    //     => EBinary { op: kAssign, left: EIdentifier("x"), right: ENumber(1) }
    Expr Assign(const Expr& a, const Expr& b);

    // Builds an expression statement wrapping an assignment: a = b;
    Stmt AssignStmt(const Expr& a, const Expr& b);

    // Builds a logical-not expression: !expr.
    // Does not apply any simplification.
    //
    // Example:
    //   Not(EIdentifier("x")) => EUnary { op: kNot, value: EIdentifier("x") }
    Expr Not(const Expr& expr);

    // Attempts to simplify a not-expression by pushing the negation inward
    // or eliminating double negation. Returns nullopt if no simplification
    // is possible.
    //
    // Example:
    //   MaybeSimplifyNot(ENot(EIdentifier("x")))
    //     => EIdentifier("x")  // double negation eliminated
    //
    //   MaybeSimplifyNot(ENot(EBoolean(true)))
    //     => EBoolean(false)   // constant folded
    //
    //   MaybeSimplifyNot(ECall(EIdentifier("f")))
    //     => nullopt           // cannot simplify
    std::optional<Expr> MaybeSimplifyNot(const Expr& expr);

    // Attempts to fold or simplify an equality comparison (== or ===).
    // Applies constant folding (e.g., "a" === "a" => true) and type-based
    // simplifications (e.g., x === null || x === undefined => x == null).
    //
    // Returns nullopt if no simplification is possible.
    //
    // Example:
    //   MaybeSimplifyEqualityComparison(EBinary { kStrictEq, EString("x"), EString("x") })
    //     => EBoolean(true)
    std::optional<Expr> MaybeSimplifyEqualityComparison(logger::Loc loc, const EBinary& e, compat::JSFeature unsupported_features);

    // Returns true if the expression is a Symbol() or Symbol.for() call.
    // Used to identify symbol instances for special optimization paths.
    //
    // Example:
    //   IsSymbolInstance(ECall(EIdentifier("Symbol"))) == true
    //   IsSymbolInstance(ECall(EDot(EIdentifier("Symbol"), "for"))) == true
    bool IsSymbolInstance(const E& data);

    // Returns true if the expression is a primitive literal (number,
    // string, boolean, null, undefined, bigint). Primitive literals never
    // have side effects and can be freely reordered.
    //
    // Example:
    //   IsPrimitiveLiteral(ENumber(1)) == true
    //   IsPrimitiveLiteral(EString("hello")) == true
    //   IsPrimitiveLiteral(EIdentifier("x")) == false
    bool IsPrimitiveLiteral(const E& data);

    // Given two expressions, determines what type both are known to be at
    // runtime. Returns kUnknown if either type cannot be determined.
    //
    // Example:
    //   MergedKnownPrimitiveTypes(ENumber(1), ENumber(2)) => kNumber
    //   MergedKnownPrimitiveTypes(EString("a"), ENumber(1)) => kMixed
    PrimitiveType MergedKnownPrimitiveTypes(const Expr& a, const Expr& b);

    // Determines the primitive type of an expression if it can be inferred
    // statically. Returns kUnknown for complex expressions.
    //
    // Example:
    //   KnownPrimitiveType(ENumber(1)) => kNumber
    //   KnownPrimitiveType(EString("hi")) => kString
    //   KnownPrimitiveType(ECall(EIdentifier("f"))) => kUnknown
    PrimitiveType KnownPrimitiveType(const E& expr);

    // Returns true if changing === to == (or !== to !=) between two
    // expressions would not change semantics. This is safe when both
    // operands are known to be the same type.
    //
    // Example:
    //   CanChangeStrictToLoose(ENumber(1), ENumber(2)) == true
    //   CanChangeStrictToLoose(ENumber(1), EString("1")) == false
    bool CanChangeStrictToLoose(const Expr& a, const Expr& b);

    // If the expression is a typeof on an unbound identifier (which never
    // throws), returns the type name string. Otherwise returns nullopt.
    //
    // Example:
    //   TypeofWithoutSideEffects(EIdentifier("x"))
    //     => "undefined" (if x is known to be a type expression)
    //
    //   TypeofWithoutSideEffects(ECall(EIdentifier("f")))
    //     => nullopt (typeof on a call could throw)
    std::optional<std::string> TypeofWithoutSideEffects(const E& data);

    // Joins two expressions with a left-associative binary operator. If
    // the right side is itself the same operator, the expressions are
    // flattened into a single chain.
    //
    // Example:
    //   JoinWithLeftAssociativeOp(kAdd, ENumber(1), ENumber(2))
    //     => EBinary { op: kAdd, left: ENumber(1), right: ENumber(2) }
    //
    //   JoinWithLeftAssociativeOp(kAdd, a, EBinary { kAdd, b, c })
    //     => EBinary { op: kAdd, left: EBinary { kAdd, a, b }, right: c }
    Expr JoinWithLeftAssociativeOp(OpCode op, Expr a, Expr b);

    // Joins two expressions with the comma operator: (a, b).
    Expr JoinWithComma(Expr a, Expr b);

    // Joins a vector of expressions with the comma operator:
    //   (all[0], all[1], ..., all[n-1])
    // Returns an empty placeholder if the vector is empty.
    Expr JoinAllWithComma(const std::vector<Expr>& all);

    // Converts a Binding (destructuring pattern) into an equivalent Expr
    // by walking the pattern tree and wrapping identifiers with the
    // provided callback. This is used when a binding pattern needs to
    // appear in expression position (e.g., for source-map-correct
    // rewrites).
    //
    // Example:
    //   // const { a, b } = obj;
    //   // ConvertBindingToExpr(BObject(...), wrap)
    //   //   => EObject { properties: [Property("a", wrap(a)), Property("b", wrap(b))] }
    Expr ConvertBindingToExpr(const Binding& binding, const std::function<Expr(logger::Loc, compiler::Ref)>& wrap_integer);

    // ---------------------------------------------------------------------------
    // Numeric conversions
    // ---------------------------------------------------------------------------

    // Converts a double to int32 per the ECMAScript ToInt32 abstract
    // operation. The conversion wraps around modulo 2^32.
    //
    // Example:
    //   ToInt32(4294967297.0) == 1  // 2^32 + 1 wraps to 1
    //   ToInt32(-1.0) == -1
    //   ToInt32(NaN) == 0
    int32_t ToInt32(double f);

    // Converts a double to uint32 per the ECMAScript ToUint32 abstract
    // operation. The conversion wraps around modulo 2^32 into the range
    // [0, 2^32).
    //
    // Example:
    //   ToUint32(-1.0) == 4294967295  // 2^32 - 1
    //   ToUint32(1.5) == 1
    uint32_t ToUint32(double f);

    // ---------------------------------------------------------------------------
    // Constant evaluation
    // ---------------------------------------------------------------------------

    // Attempts to evaluate an expression as a number without triggering any
    // side effects. Returns nullopt if the expression cannot be statically
    // evaluated.
    //
    // Example:
    //   ToNumberWithoutSideEffects(ENumber(3.14)) => 3.14
    //   ToNumberWithoutSideEffects(EUnary { kNeg, ENumber(5) }) => -5.0
    //   ToNumberWithoutSideEffects(ECall(EIdentifier("f"))) => nullopt
    std::optional<double> ToNumberWithoutSideEffects(const E& data);

    // Attempts to evaluate an expression as a string without triggering any
    // side effects. Returns nullopt if the expression cannot be statically
    // evaluated.
    //
    // Example:
    //   ToStringWithoutSideEffects(EString("hello")) => "hello"
    //   ToStringWithoutSideEffects(ETemplate { head: "hi" }) => "hi"
    //   ToStringWithoutSideEffects(EIdentifier("x")) => nullopt
    std::optional<std::string> ToStringWithoutSideEffects(const E& data);

    // ---------------------------------------------------------------------------
    // Binary operator folding
    // ---------------------------------------------------------------------------

    // Returns true if the binary operator should be folded (constant-folded)
    // during minification. Only operators that produce deterministic results
    // on the same types are folded.
    //
    // Example:
    //   ShouldFoldBinaryOperatorWhenMinifying(EBinary { kAdd, ENumber(1), ENumber(2) })
    //     => true
    bool ShouldFoldBinaryOperatorWhenMinifying(const EBinary& binary);

    // Attempts to constant-fold a binary expression. Both operands must be
    // literals of compatible types. Returns nullopt if folding is not
    // possible.
    //
    // Example:
    //   FoldBinaryOperator(EBinary { kAdd, ENumber(1), ENumber(2) })
    //     => ENumber(3)
    //
    //   FoldBinaryOperator(EBinary { kMul, ENumber(3), ENumber(4) })
    //     => ENumber(12)
    //
    //   FoldBinaryOperator(EBinary { kAdd, EIdentifier("x"), ENumber(1) })
    //     => nullopt  // cannot fold non-literals
    std::optional<Expr> FoldBinaryOperator(logger::Loc loc, const EBinary& e);

    // ---------------------------------------------------------------------------
    // Null/undefined analysis
    // ---------------------------------------------------------------------------

    // Checks if a binary comparison involves null and/or undefined
    // operands. Returns the reordered (left, right) pair if one side is
    // null/undefined, allowing callers to apply null-coalescing or
    // optional-chaining optimizations.
    //
    // Example:
    //   IsBinaryNullAndUndefined(EIdentifier("x"), ENull(), kLooseEq)
    //     => (ENull(), EIdentifier("x"))  // reordered
    //
    //   IsBinaryNullAndUndefined(ENumber(1), ENumber(2), kStrictEq)
    //     => nullopt  // neither side is null/undefined
    std::optional<std::pair<Expr, Expr>> IsBinaryNullAndUndefined(const Expr& left, const Expr& right, OpCode op);

    // ---------------------------------------------------------------------------
    // BigInt comparison
    // ---------------------------------------------------------------------------

    // Compares two BigInt values represented as decimal strings. Returns
    // true if they are equal, false if they are not, or nullopt if the
    // comparison cannot be determined (e.g., invalid BigInt syntax).
    //
    // Example:
    //   CheckEqualityBigInt("123", "123") => true
    //   CheckEqualityBigInt("123", "456") => false
    //   CheckEqualityBigInt("abc", "123") => nullopt  // invalid
    std::optional<bool> CheckEqualityBigInt(const std::string& a, const std::string& b);

    // Attempts to determine the result of an equality comparison between
    // two expressions. Returns nullopt if the result depends on runtime
    // values.
    //
    // Example:
    //   CheckEqualityIfNoSideEffects(ENumber(1), ENumber(1), kStrictEquality)
    //     => true
    //
    //   CheckEqualityIfNoSideEffects(EString("a"), EString("b"), kStrictEquality)
    //     => false
    std::optional<bool> CheckEqualityIfNoSideEffects(const E& left, const E& right, EqualityKind kind);

    // Returns true if two expressions are structurally identical (same
    // kind, same values, same structure). Used by the optimizer to detect
    // redundant operations.
    //
    // Example:
    //   ValuesLookTheSame(ENumber(1), ENumber(1)) == true
    //   ValuesLookTheSame(EString("a"), EString("b")) == false
    //   ValuesLookTheSame(EIdentifier("x"), EIdentifier("x")) == true
    bool ValuesLookTheSame(const E& left, const E& right);

    // ---------------------------------------------------------------------------
    // Optional chaining
    // ---------------------------------------------------------------------------

    // Attempts to insert an optional chain (?.) into an expression based on
    // a null-check condition. Returns true if the transformation was
    // applied, modifying `expr` in place.
    //
    // Example:
    //   // if (x != null) x.y  =>  x?.y
    //   TryToInsertOptionalChain(EBinary { kNe, EIdentifier("x"), ENull() }, expr)
    //     => true, expr is now EDot(EIdentifier("x"), "y", optional_chain=true)
    //
    // Edge cases:
    //   - The test must be a simple null/undefined check.
    //   - The expression must be a property access or call.
    bool TryToInsertOptionalChain(const Expr& test, Expr& expr);

    // ---------------------------------------------------------------------------
    // String/number conversion
    // ---------------------------------------------------------------------------

    // Converts a double to its string representation in the given radix
    // (base). Returns nullopt if the conversion would produce a result
    // that cannot round-trip through Number() safely.
    //
    // Example:
    //   TryToStringOnNumberSafely(255, 16) => "ff"
    //   TryToStringOnNumberSafely(1.5, 10) => "1.5"
    //   TryToStringOnNumberSafely(NaN, 10) => nullopt
    std::optional<std::string> TryToStringOnNumberSafely(double n, int radix);

    // Attempts to fold string addition (concatenation) into a template
    // literal or a single string. Returns nullopt if no folding is
    // possible.
    //
    // Example:
    //   FoldStringAddition(EString("hello"), EString(" world"), kNormal)
    //     => EString("hello world")
    //
    //   FoldStringAddition(ENumber(1), EString(" item"), kNormal)
    //     => nullopt  // cannot fold number + string without evaluation
    std::optional<Expr> FoldStringAddition(Expr left, Expr right, StringAdditionKind kind);

    // Inlines primitive values into a template literal, replacing ${}
    // substitutions with their known values. Returns the simplified
    // expression.
    //
    // Example:
    //   // `hello ${42}`  =>  "hello 42"
    //   InlinePrimitivesIntoTemplate(ETemplate { head: "hello ", parts: [{ value: ENumber(42) }] })
    //     => EString("hello 42")
    Expr InlinePrimitivesIntoTemplate(logger::Loc loc, const ETemplate& e);

    // ---------------------------------------------------------------------------
    // ToNullOrUndefined / ToBoolean with side-effect tracking
    // ---------------------------------------------------------------------------

    // Determines whether an expression is definitely null, definitely
    // undefined, or could be either. Returns a pair of (is_null_or_undef,
    // side_effects).
    //
    // Example:
    //   ToNullOrUndefinedWithSideEffects(ENull())
    //     => (true, kNoSideEffects)
    //
    //   ToNullOrUndefinedWithSideEffects(EIdentifier("x"))
    //     => (false, kNoSideEffects)  // not necessarily null/undef
    //
    //   ToNullOrUndefinedWithSideEffects(ECall(EIdentifier("f")))
    //     => (false, kCouldHaveSideEffects)
    std::optional<std::pair<bool, SideEffects>> ToNullOrUndefinedWithSideEffects(const E& data);

    // Determines the boolean coercion of an expression: true, false, or
    // unknown. Also reports whether the expression has side effects.
    //
    // Example:
    //   ToBooleanWithSideEffects(EBoolean(false)) => (false, kNoSideEffects)
    //   ToBooleanWithSideEffects(ENumber(0))      => (false, kNoSideEffects)
    //   ToBooleanWithSideEffects(EString(""))      => (false, kNoSideEffects)
    //   ToBooleanWithSideEffects(EString("x"))     => (true, kNoSideEffects)
    std::optional<std::pair<bool, SideEffects>> ToBooleanWithSideEffects(const E& data);

    // Attempts to interpret a string as a number. Returns nullopt if the
    // string does not parse as a valid numeric value.
    //
    // Example:
    //   StringToEquivalentNumberValue(u"123") => 123.0
    //   StringToEquivalentNumberValue(u"") => 0.0
    //   StringToEquivalentNumberValue(u"abc") => nullopt
    std::optional<double> StringToEquivalentNumberValue(std::span<const char16_t> value);

    // ---------------------------------------------------------------------------
    // Spread/inline transformations
    // ---------------------------------------------------------------------------

    // Inlines spread elements of array literals. If the spread operand is
    // itself an array literal, its elements are extracted and placed
    // directly into the result.
    //
    // Example:
    //   // [...[1, 2], 3]  =>  [1, 2, 3]
    //   InlineSpreadsOfArrayLiterals({ spread([1, 2]), 3 })
    //     => { 1, 2, 3 }
    //
    // Edge cases:
    //   - Only direct array literals are inlined; variables and function
    //     calls are left as spread elements.
    std::vector<Expr> InlineSpreadsOfArrayLiterals(const std::vector<Expr>& values);

    // Attempts to mangle object spread properties by inlining the spread
    // source's properties directly. This only works when the spread source
    // is an object literal with no computed keys or side effects.
    //
    // Example:
    //   // { ...{ a: 1 }, b: 2 }  =>  { a: 1, b: 2 }
    //   MangleObjectSpread({ spread({ a: 1 }), Property("b", 2) })
    //     => { Property("a", 1), Property("b", 2) }
    std::vector<Property> MangleObjectSpread(const std::vector<Property>& properties);

    // ---------------------------------------------------------------------------
    // Binding traversal helpers
    // ---------------------------------------------------------------------------

    // Iterates over all BIdentifier nodes in a list of declarations,
    // invoking the callback with the source location and identifier ref
    // for each one. This is used by the linker to collect declared symbols
    // without walking the AST manually.
    //
    // Example:
    //   // for (const [a, b] of decls) { ... }
    //   ForEachIdentifierBindingInDecls(decls, [](logger::Loc loc, BIdentifier& id) {
    //       // visit each identifier binding
    //   });
    void ForEachIdentifierBindingInDecls(std::vector<Decl>& decls, const std::function<void(logger::Loc, BIdentifier&)>& callback);

    // Same as above but for a single Binding node. Handles nested
    // destructuring patterns by recursively walking into arrays and objects.
    //
    // Example:
    //   // const { a, b: { c } } = obj;
    //   // ForEachIdentifierBinding(binding, cb) visits a, b (outer key),
    //   // and c.
    void ForEachIdentifierBinding(Binding& binding, const std::function<void(logger::Loc, BIdentifier&)>& callback);

}
