#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "guchho/logger.hpp"
#include "guchho/compiler.hpp"

namespace guchho::javascript {

// ============================================================================
// Operator Precedence
// ============================================================================

// Every binary and unary operator has a precedence level. The parser uses
// these to decide whether the next operator binds tighter or looser than the
// current one. Levels increase from lowest (comma, evaluated last) to highest
// (member access, evaluated first). Two operators at the same level rely on
// associativity (left or right) to break the tie.
//
// The enum values themselves are compared numerically:
//   L::kMultiply > L::kAdd   =>  true  (multiply binds tighter)
//   L::kAssign > L::kConditional  =>  false (assign is lower)
enum class L : uint8_t {
    kLowest,
    kComma,
    kSpread,
    kYield,
    kAssign,
    kConditional,
    kNullishCoalescing,
    kLogicalOr,
    kLogicalAnd,
    kBitwiseOr,
    kBitwiseXor,
    kBitwiseAnd,
    kEquals,
    kCompare,
    kShift,
    kAdd,
    kMultiply,
    kExponentiation,
    kPrefix,
    kPostfix,
    kNew,
    kCall,
    kMember,
};

// ============================================================================
// Assignment Target Classification
// ============================================================================

// When the parser encounters an assignment-like expression, it needs to know
// what kind of target appears on the left-hand side. This drives error
// reporting and code generation decisions.
//
// kNone        – not a valid target (e.g., a function call like f() = 1)
// kReplace     – simple assignment target (a = 1, obj.x = 2)
// kUpdate      – compound assignment target (a += 1, a <<= 2)
enum class AssignTarget : uint8_t {
    kNone,
    kReplace,
    kUpdate,
};

// ============================================================================
// Opcodes
// ============================================================================

// Every operator in the language is encoded as a compact opcode. The opcode
// encodes both the operation and its syntactic position (unary prefix,
// unary postfix, or binary). Code generation switches on the opcode to
// emit the correct instruction sequence.
//
// Opcodes are grouped by category: unary prefix ops come first, then unary
// postfix ops, then binary ops, and finally assignment ops (which are also
// binary but have special semantics).
enum class OpCode : uint8_t {
    kUnOpPos,
    kUnOpNeg,
    kUnOpCpl,
    kUnOpNot,
    kUnOpVoid,
    kUnOpTypeof,
    kUnOpDelete,

    kUnOpPreDec,
    kUnOpPreInc,

    kUnOpPostDec,
    kUnOpPostInc,

    kBinOpAdd,
    kBinOpSub,
    kBinOpMul,
    kBinOpDiv,
    kBinOpRem,
    kBinOpPow,
    kBinOpLt,
    kBinOpLe,
    kBinOpGt,
    kBinOpGe,
    kBinOpIn,
    kBinOpInstanceof,
    kBinOpShl,
    kBinOpShr,
    kBinOpUShr,
    kBinOpLooseEq,
    kBinOpLooseNe,
    kBinOpStrictEq,
    kBinOpStrictNe,
    kBinOpNullishCoalescing,
    kBinOpLogicalOr,
    kBinOpLogicalAnd,
    kBinOpBitwiseOr,
    kBinOpBitwiseAnd,
    kBinOpBitwiseXor,

    kBinOpComma,

    kBinOpAssign,
    kBinOpAddAssign,
    kBinOpSubAssign,
    kBinOpMulAssign,
    kBinOpDivAssign,
    kBinOpRemAssign,
    kBinOpPowAssign,
    kBinOpShlAssign,
    kBinOpShrAssign,
    kBinOpUShrAssign,
    kBinOpBitwiseOrAssign,
    kBinOpBitwiseAndAssign,
    kBinOpBitwiseXorAssign,
    kBinOpNullishCoalescingAssign,
    kBinOpLogicalOrAssign,
    kBinOpLogicalAndAssign,
};

bool IsPrefix(OpCode op);
AssignTarget UnaryAssignTarget(OpCode op);
bool IsLeftAssociative(OpCode op);
bool IsRightAssociative(OpCode op);
AssignTarget BinaryAssignTarget(OpCode op);
bool IsShortCircuit(OpCode op);

// ============================================================================
// Property Classification
// ============================================================================

// Object literals and class bodies contain properties of different kinds.
// This enum distinguishes data fields from methods, getters/setters,
// spread elements, and TypeScript-specific constructs. Code generation and
// minification rules differ for each kind (for example, methods can have
// their names mangled but getters/setters cannot).
enum class PropertyKind : uint8_t {
    kField,
    kMethod,
    kGetter,
    kSetter,
    kAutoAccessor,
    kSpread,
    kDeclareOrAbstract,
    kClassStaticBlock,
};

// Bitflags attached to properties. Multiple flags can be combined with |.
//
// kIsComputed      – the key was written with brackets: { [expr]: val }
// kIsStatic        – the property lives on the constructor, not instances
// kWasShorthand    – originally written as { x } instead of { x: x }
// kPreferQuotedKey – preserve quotes around the key during printing
enum class PropertyFlags : uint8_t {
    kNone = 0,
    kIsComputed = 1 << 0,
    kIsStatic = 1 << 1,
    kWasShorthand = 1 << 2,
    kPreferQuotedKey = 1 << 3,
};

inline PropertyFlags operator|(PropertyFlags a, PropertyFlags b) {
    return static_cast<PropertyFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline PropertyFlags operator&(PropertyFlags a, PropertyFlags b) {
    return static_cast<PropertyFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline PropertyFlags& operator|=(PropertyFlags& a, PropertyFlags b) {
    a = a | b;
    return a;
}

// Test whether a specific flag is set in a combined flags value.
// Example: Has(PropertyFlags::kIsComputed | PropertyFlags::kIsStatic, PropertyFlags::kIsStatic) => true
inline bool Has(PropertyFlags flags, PropertyFlags flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// ============================================================================
// Call Classification
// ============================================================================

// Distinguishes the syntactic form of a function call. This matters for
// "this" binding semantics and for dead-code elimination.
//
// kNormal                             – plain call: f()
// kDirectEval                         – direct eval(): eval("code")
// kTargetWasOriginallyPropertyAccess  – call on a member: a.b()
//   After optimization the target may be rewritten, but "this" must
//   still point to the original receiver object.
enum class CallKind : uint8_t {
    kNormal,
    kDirectEval,
    kTargetWasOriginallyPropertyAccess,
};

// ============================================================================
// Optional Chaining
// ============================================================================

// Tracks how ?. propagates through a chain of member accesses and calls.
// When the runtime hits a short-circuit point, the entire chain evaluates
// to undefined without calling any further getters or side effects.
//
// kNone     – ordinary access, no optional chaining active
// kStart    – first ?. in the chain: a?.b  (the ?. itself)
// kContinue – subsequent access after ?: a?.b.c  (the .c part)
//
// Parentheses reset the chain:
//   (a?.b).c  =>  a is kStart, .c is kNone
enum class OptionalChain : uint8_t {
    kNone,
    kStart,
    kContinue,
};

// ============================================================================
// Annotation Flags
// ============================================================================

// Flags for type annotations and other compiler directives that may appear
// in the source. These annotations are stripped during output but affect
// how the compiler treats the surrounding code.
enum class AnnotationFlags : uint8_t {
    kNone = 0,
    kCanBeRemovedIfUnused = 1 << 0,
};

inline AnnotationFlags operator|(AnnotationFlags a, AnnotationFlags b) {
    return static_cast<AnnotationFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline AnnotationFlags operator&(AnnotationFlags a, AnnotationFlags b) {
    return static_cast<AnnotationFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool Has(AnnotationFlags flags, AnnotationFlags flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// ============================================================================
// Local Variable Kind
// ============================================================================

// Distinguishes the different declaration keywords that introduce local
// variables. Each kind has different scoping and initialization rules.
//
// kVar          – function-scoped, hoisted, initialized to undefined
// kLet          – block-scoped, not hoisted, TDZ before initialization
// kConst        – block-scoped, not hoisted, TDZ, cannot be reassigned
// kUsing        – block-scoped, disposed at block exit (explicit resource mgmt)
// kAwaitUsing   – like kUsing but disposal is awaited asynchronously
enum class LocalKind : uint8_t {
    kVar,
    kLet,
    kConst,
    kUsing,
    kAwaitUsing,
};

bool IsUsing(LocalKind kind);

// ============================================================================
// Scope Classification
// ============================================================================

// Scopes form a tree that mirrors the nesting of blocks and functions in the
// source. The kind of scope determines whether declarations inside it are
// visible to enclosing scopes (hoisting) and whether certain constructs
// (like direct eval) are allowed.
//
// Entry scopes (modules, TS enums/namespaces) and function scopes stop
// hoisting: a declaration inside a function body does not leak into the
// enclosing block. Block scopes, on the other hand, allow var-declarations
// to hoist into the nearest function or module scope.
enum class ScopeKind : uint8_t {
    kBlock,
    kWith,
    kLabel,
    kClassName,
    kClassBody,
    kCatchBinding,
    kEntry,
    kFunctionArgs,
    kFunctionBody,
    kClassStaticInit,
};

bool StopsHoisting(ScopeKind kind);

// ============================================================================
// Strict Mode
// ============================================================================

// JavaScript has two execution modes: sloppy (the default) and strict.
// Strict mode enables additional static checks (no with, no duplicate
// params, no octal literals, etc.). The parser tracks how strict mode
// was enabled because different origins affect which warnings to emit.
enum class StrictModeKind : uint8_t {
    kSloppyMode,
    kExplicitStrictMode,
    kImplicitStrictModeClass,
    kImplicitStrictModeESM,
    kImplicitStrictModeTSAlwaysStrict,
    kImplicitStrictModeJSXAutomaticRuntime,
};

// ============================================================================
// Export Classification
// ============================================================================

// Determines how a file's exports are structured. This drives the linking
// strategy: CommonJS wraps in a closure, ESM can be tree-shaken, and
// dynamic fallback requires a runtime object for unknown export names.
enum class ExportsKind : uint8_t {
    kNone,
    kCommonJS,
    kESM,
    kESMWithDynamicFallback,
};

bool IsDynamic(ExportsKind kind);

// ============================================================================
// Module Type
// ============================================================================

// The module system a file belongs to, inferred from its file extension
// or from the "type" field in the nearest package.json. This determines
// whether require() and import statements are allowed and how they behave.
enum class ModuleType : uint8_t {
    kUnknown,
    kCommonJS_CJS,
    kCommonJS_CTS,
    kCommonJS_PackageJSON,
    kESM_MJS,
    kESM_MTS,
    kESM_PackageJSON,
};

bool IsCommonJS(ModuleType mt);
bool IsESM(ModuleType mt);

// ============================================================================
// Constant Value Kinds
// ============================================================================

// Classifies the kind of a compile-time known constant value. Used for
// cross-module inlining of enum members and const variables.
enum class ConstValueKind : uint8_t {
    kNone,
    kNull,
    kUndefined,
    kTrue,
    kFalse,
    kNumber,
    kString,
};

// ============================================================================
// Operator Table
// ============================================================================

// Lookup table that maps each operator token to its human-readable text,
// precedence level, and whether it is a keyword operator ("in" and
// "instanceof" are keywords, "+" is not). The parser uses this to resolve
// precedence during expression parsing, and the printer uses it to
// reconstruct operator text from opcodes.
struct OpTableEntry {
    std::string_view text;
    L level;
    bool is_keyword;
};

extern const OpTableEntry kOpTable[];

// ============================================================================
// Forward Declarations
// ============================================================================

// The three main AST node families: Binding (B), Expression (E), and
// Statement (S). Each uses a std::variant of shared_ptr to its concrete
// node types. Shared pointers allow nodes to be freely moved and cloned
// without invalidating references held elsewhere in the tree.
struct Binding;
struct Expr;
struct Stmt;

struct BMissing;
struct BIdentifier;
struct BArray;
struct BObject;

struct EArray;
struct EUnary;
struct EBinary;
struct EBoolean;
struct ESuper;
struct ENull;
struct EUndefined;
struct EThis;
struct ENew;
struct ENewTarget;
struct EImportMeta;
struct ECall;
struct EDot;
struct EIndex;
struct EArrow;
struct EFunction;
struct EClass;
struct EIdentifier;
struct EImportIdentifier;
struct EPrivateIdentifier;
struct ENameOfSymbol;
struct EJSXElement;
struct EJSXText;
struct EMissing;
struct ENumber;
struct EBigInt;
struct EObject;
struct ESpread;
struct EString;
struct ETemplate;
struct ERegExp;
struct EInlinedEnum;
struct EAnnotation;
struct EAwait;
struct EYield;
struct EIf;
struct ERequireString;
struct ERequireResolveString;
struct EImportString;
struct EImportCall;

struct SBlock;
struct SComment;
struct SDebugger;
struct SDirective;
struct SEmpty;
struct STypeScript;
struct SExportClause;
struct SExportFrom;
struct SExportDefault;
struct SExportStar;
struct SExportEquals;
struct SLazyExport;
struct SExpr;
struct SEnum;
struct SNamespace;
struct SFunction;
struct SClass;
struct SLabel;
struct SIf;
struct SFor;
struct SForIn;
struct SForOf;
struct SDoWhile;
struct SWhile;
struct SWith;
struct STry;
struct SSwitch;
struct SImport;
struct SReturn;
struct SThrow;
struct SLocal;
struct SBreak;
struct SContinue;

struct TSNamespaceMemberProperty;
struct TSNamespaceMemberNamespace;
struct TSNamespaceMemberEnumNumber;
struct TSNamespaceMemberEnumString;

struct ClassStaticBlock;
struct Decorator;
struct Property;
struct PropertyBinding;
struct Arg;
struct Fn;
struct FnBody;
struct Class;
struct ArrayBinding;
struct TemplatePart;
struct EnumValue;
struct Catch;
struct Finally;
struct Case;
struct ClauseItem;
struct Decl;
struct ExportStarAlias;
struct Scope;
struct ScopeMember;
struct TSNamespaceScope;
struct TSNamespaceMember;
struct ModuleTypeData;
struct NamedImport;
struct NamedExport;
struct Dependency;
struct DeclaredSymbol;
struct SymbolUse;
struct SymbolCallUse;
struct Part;
struct TSEnumValue;
struct ConstValue;
struct AST;

// ============================================================================
// Binding Pattern (B)
// ============================================================================

// A binding pattern is the left-hand side of a declaration or destructuring
// target. It tells the runtime where to store the incoming value.
//
// Example: const { a, b: [x, y] } = obj;
//   The outer pattern is BObject with two properties:
//     "a"  -> BIdentifier (name "a")
//     "b:" -> BArray containing two BIdentifier entries
//
// BMissing represents an omitted slot in array destructuring:
//   const [a, , b] = [1, 2, 3];  // the second , creates a BMissing
using B = std::variant<
    std::shared_ptr<BMissing>, std::shared_ptr<BIdentifier>,
    std::shared_ptr<BArray>, std::shared_ptr<BObject>>;

// ============================================================================
// Expression Variant (E)
// ============================================================================

// The expression variant holds any of the ~40 expression node types in the
// language. Expressions produce values and may have side effects. The
// variant discriminates at runtime which concrete type is stored.
//
// Example:
//   1 + 2          => EBinary { op: kBinOpAdd, left: ENumber(1), right: ENumber(2) }
//   arr[0]         => EIndex { target: EIdentifier("arr"), index: ENumber(0) }
//   (x) => x * 2   => EArrow { args: [BIdentifier("x")], body: ... }
using E = std::variant<
    std::shared_ptr<EArray>, std::shared_ptr<EUnary>, std::shared_ptr<EBinary>,
    std::shared_ptr<EBoolean>, std::shared_ptr<ESuper>, std::shared_ptr<ENull>,
    std::shared_ptr<EUndefined>, std::shared_ptr<EThis>, std::shared_ptr<ENew>,
    std::shared_ptr<ENewTarget>, std::shared_ptr<EImportMeta>, std::shared_ptr<ECall>,
    std::shared_ptr<EDot>, std::shared_ptr<EIndex>, std::shared_ptr<EArrow>,
    std::shared_ptr<EFunction>, std::shared_ptr<EClass>, std::shared_ptr<EIdentifier>,
    std::shared_ptr<EImportIdentifier>, std::shared_ptr<EPrivateIdentifier>,
    std::shared_ptr<ENameOfSymbol>, std::shared_ptr<EJSXElement>, std::shared_ptr<EJSXText>,
    std::shared_ptr<EMissing>, std::shared_ptr<ENumber>, std::shared_ptr<EBigInt>,
    std::shared_ptr<EObject>, std::shared_ptr<ESpread>, std::shared_ptr<EString>,
    std::shared_ptr<ETemplate>, std::shared_ptr<ERegExp>, std::shared_ptr<EInlinedEnum>,
    std::shared_ptr<EAnnotation>, std::shared_ptr<EAwait>, std::shared_ptr<EYield>,
    std::shared_ptr<EIf>, std::shared_ptr<ERequireString>,
    std::shared_ptr<ERequireResolveString>, std::shared_ptr<EImportString>,
    std::shared_ptr<EImportCall>>;

// ============================================================================
// Statement Variant (S)
// ============================================================================

// The statement variant holds any of the ~30 statement node types. Statements
// perform actions, declare bindings, or control execution flow. They do not
// produce values (unlike expressions).
//
// Example:
//   if (x > 0) { return x; }  => SIf { test: EBinary(>, x, 0), yes: SBlock([SReturn(x)]) }
//   const y = 5;               => SLocal { decls: [Decl(BIdentifier("y"), ENumber(5))], kind: kConst }
using S = std::variant<
    std::shared_ptr<SBlock>, std::shared_ptr<SComment>, std::shared_ptr<SDebugger>,
    std::shared_ptr<SDirective>, std::shared_ptr<SEmpty>, std::shared_ptr<STypeScript>,
    std::shared_ptr<SExportClause>, std::shared_ptr<SExportFrom>,
    std::shared_ptr<SExportDefault>, std::shared_ptr<SExportStar>,
    std::shared_ptr<SExportEquals>, std::shared_ptr<SLazyExport>,
    std::shared_ptr<SExpr>, std::shared_ptr<SEnum>, std::shared_ptr<SNamespace>,
    std::shared_ptr<SFunction>, std::shared_ptr<SClass>, std::shared_ptr<SLabel>,
    std::shared_ptr<SIf>, std::shared_ptr<SFor>, std::shared_ptr<SForIn>,
    std::shared_ptr<SForOf>, std::shared_ptr<SDoWhile>, std::shared_ptr<SWhile>,
    std::shared_ptr<SWith>, std::shared_ptr<STry>, std::shared_ptr<SSwitch>,
    std::shared_ptr<SImport>, std::shared_ptr<SReturn>, std::shared_ptr<SThrow>,
    std::shared_ptr<SLocal>, std::shared_ptr<SBreak>, std::shared_ptr<SContinue>>;

// TypeScript namespace members store their data in a variant that covers
// all possible member kinds: a property binding, a nested namespace, or
// a numeric/string enum value.
using TSNamespaceMemberData = std::variant<
    std::shared_ptr<TSNamespaceMemberProperty>,
    std::shared_ptr<TSNamespaceMemberNamespace>,
    std::shared_ptr<TSNamespaceMemberEnumNumber>,
    std::shared_ptr<TSNamespaceMemberEnumString>>;

// ============================================================================
// AST Wrapper Nodes
// ============================================================================

// Wraps a binding node with source location. Every node in the AST tree
// carries a Loc so that error messages and source maps can point back to
// the original source position.
struct Binding {
    B data;
    logger::Loc loc;
};

// Wraps an expression node with source location. Includes a custom copy
// assignment operator that avoids a use-after-free: std::variant's
// cross-type assignment destroys the target before copying the source, so
// if the source lives inside the target, the copy reads freed memory.
// This cannot happen with garbage-collected runtimes, but in C++ we must
// copy first, then assign.
struct Expr {
    E data;
    logger::Loc loc;

    Expr() = default;
    Expr(E data, logger::Loc loc) : data(std::move(data)), loc(loc) {}
    Expr(const Expr&) = default;
    Expr(Expr&&) = default;

    Expr& operator=(const Expr& other) {
        E copy = other.data;
        data = std::move(copy);
        loc = other.loc;
        return *this;
    }
};

// Wraps a statement node with source location.
struct Stmt {
    S data;
    logger::Loc loc;
};

// A block statement: { stmt1; stmt2; ... }.
// Tracks the location of the closing brace for accurate source mapping.
struct SBlock {
    std::vector<Stmt> stmts;
    logger::Loc close_brace_loc;
};

// A contiguous span of source text with its position. Used for comments,
// pragmas, and other textual content that must be preserved verbatim in the
// output with correct source-map offsets.
struct Span {
    std::string text;
    logger::Range range;
};

// Returns true if a PropertyKind corresponds to a method definition in the
// ES6 spec: regular methods, generator methods, async methods, async
// generator methods, getters, and setters.
//
// Example:
//   IsMethodDefinition(kMethod)   => true
//   IsMethodDefinition(kField)    => false
//   IsMethodDefinition(kGetter)   => true
bool IsMethodDefinition(PropertyKind kind);

// A static initialization block inside a class body:
//   class Foo { static { ... } }
// The block is executed once when the class is created, in top-down order
// among all static blocks.
struct ClassStaticBlock {
    SBlock block;
    logger::Loc loc;
};

// A decorator attached to a class, method, field, or accessor:
//   @logged class Foo { ... }
// at_loc points to the '@' character. omit_newline_after suppresses the
// line break between the decorator and the decorated element when printing
// (used when the decorator is the last thing on its line).
struct Decorator {
    Expr value;
    logger::Loc at_loc;
    bool omit_newline_after{};
};

// Represents a single property in an object literal or class body. The key
// is the property name (literal or computed). value_or_nil holds the value
// for data properties and the function body for methods/getters/setters.
// initializer_or_nil holds default values in destructuring patterns and
// class field initializers.
//
// Example – object literal { x: 1, get y() { return 2; } }:
//   Property 1: kind=kField, key=EString("x"), value_or_nil=ENumber(1)
//   Property 2: kind=kGetter, key=EString("y"), value_or_nil=EFunction(...)
struct Property {
    std::shared_ptr<ClassStaticBlock> class_static_block;

    Expr key;

    Expr value_or_nil;

    Expr initializer_or_nil;

    std::vector<Decorator> decorators;

    logger::Loc loc;
    logger::Loc close_bracket_loc;
    PropertyKind kind{};
    PropertyFlags flags{};
};

// A property in a destructuring binding pattern. The key identifies which
// element of the source object to extract, and the value is the binding
// target that receives the extracted value.
struct PropertyBinding {
    Expr key;
    Binding value;
    Expr default_value_or_nil;
    logger::Loc loc;
    logger::Loc close_bracket_loc;
    bool is_computed{};
    bool is_spread{};
    bool prefer_quoted_key{};
};

// A single parameter in a function's formal parameter list. Each Arg wraps
// a binding pattern (which may be complex in the case of destructuring)
// and an optional default value expression. Decorators on parameters are
// used by TypeScript and Angular-style frameworks.
struct Arg {
    Binding binding;
    Expr default_or_nil;
    std::vector<Decorator> decorators;

    bool is_typescript_ctor_field{};
};

// The body of a function, wrapped in a block. The loc tracks the position
// of the opening brace so source maps can map into the function body.
struct FnBody {
    SBlock block;
    logger::Loc loc;
};

// Represents a function declaration, function expression, or the body of
// an arrow function. The name is absent for anonymous function expressions
// and arrow functions. Arguments may include rest parameters, defaults,
// and destructuring patterns.
//
// Example: async function* gen(x, ...rest) { yield x; }
//   fn.name       = "gen"
//   fn.is_async   = true
//   fn.is_generator = true
//   fn.args       = [Arg(BIdentifier("x")), Arg(BIdentifier("rest"), is_rest=true)]
//   fn.body       = FnBody(block=[SYield(ENumber(1))])
struct Fn {
    std::shared_ptr<compiler::LocRef> name;
    std::vector<Arg> args;
    FnBody body;
    compiler::Ref arguments_ref;
    logger::Loc open_paren_loc;

    bool is_async{};
    bool is_generator{};
    bool has_rest_arg{};
    bool has_if_scope{};
    bool has_no_side_effects_comment{};
    bool is_unique_formal_parameters{};
};

// Represents a class declaration or expression. The extends_or_nil field
// holds the superclass expression (null for classes with no extends clause).
// Properties contains methods, fields, accessors, and static blocks.
// Decorators are the list of class-level decorators.
//
// Example: class Foo extends Bar { x = 1; method() {} }
//   class_.name         = "Foo"
//   class_.extends_or_nil = EIdentifier("Bar")
//   class_.properties   = [Property(kField, "x"), Property(kMethod, "method")]
struct Class {
    std::vector<Decorator> decorators;
    std::shared_ptr<compiler::LocRef> name;
    Expr extends_or_nil;
    std::vector<Property> properties;
    logger::Range class_keyword;
    logger::Loc body_loc;
    logger::Loc close_brace_loc;

    bool should_lower_standard_decorators{};
    bool use_define_for_class_fields{};
};

// A single element in an array destructuring pattern. Each entry pairs a
// binding target with an optional default value. The binding may be a
// simple identifier or another nested pattern.
struct ArrayBinding {
    Binding binding;
    Expr default_value_or_nil;
    logger::Loc loc;
};

// An omitted element in an array destructuring pattern:
//   const [a, , b] = [1, 2, 3];
// The second position produces a BMissing with no data.
struct BMissing {};

// A simple identifier binding: the most common case.
//   const x = 1;   =>  BIdentifier { ref: "x" }
struct BIdentifier {
    compiler::Ref ref;
};

// An array destructuring binding:
//   const [a, b = 2] = arr;
//   BArray { items: [ArrayBinding(a), ArrayBinding(b, default=2)], has_spread: false }
struct BArray {
    std::vector<ArrayBinding> items;
    logger::Loc close_bracket_loc;
    bool has_spread{};
    bool is_single_line{};
};

// An object destructuring binding:
//   const { x, y: renamed } = obj;
//   BObject { properties: [PropertyBinding("x"), PropertyBinding("y", value="renamed")] }
struct BObject {
    std::vector<PropertyBinding> properties;
    logger::Loc close_brace_loc;
    bool is_single_line{};
};

// ============================================================================
// Template Literals
// ============================================================================

// Represents one substitution part of a template literal: ${expression}.
// For tagged templates, tail_raw holds the raw (unedited) text of the tail
// portion. For untagged templates, tail_cooked holds the decoded string
// value (with escape sequences processed). tail_cooked_is_valid is false
// when the tail contains invalid escape sequences that make the cooked
// value undefined.
struct TemplatePart {
    Expr value;
    std::string tail_raw;
    std::u16string tail_cooked;
    bool tail_cooked_is_valid{true};
    logger::Loc tail_loc;
};

// ============================================================================
// Error Handling
// ============================================================================

// A catch clause in a try-catch statement. binding_or_nil is the error
// variable (omitted in "catch { }" syntax). The block contains the handler
// body.
struct Catch {
    Binding binding_or_nil;
    SBlock block;
    logger::Loc loc;
    logger::Loc block_loc;
};

// A finally clause in a try-finally or try-catch-finally statement.
// The block always executes, whether the try block succeeds or throws.
struct Finally {
    SBlock block;
    logger::Loc loc;
};

// A case in a switch statement. value_or_nil is null for the default case.
// body holds the statements that execute when the test value matches.
struct Case {
    Expr value_or_nil;
    std::vector<Stmt> body;
    logger::Loc loc;
};

// An item in an export clause like "export { foo as bar }".
// alias is the exported name, original_name is the local name. For
// re-exports ("export { foo as bar } from 'mod'"), both names are aliases
// into the foreign module.
struct ClauseItem {
    std::string alias;
    std::string original_name;

    logger::Loc alias_loc;
    compiler::LocRef name;
};

// A single declarator inside a variable statement:
//   const x = 1, y = 2;  =>  two Decl entries, each with binding + value
struct Decl {
    Binding binding;
    Expr value_or_nil;
};

// A member of a TypeScript enum declaration:
//   enum Color { Red, Green, Blue }
//   enum Flags { A = 1 << 0, B = 1 << 1 }
// value_or_nil is present when the enum member has an explicit initializer.
struct EnumValue {
    Expr value_or_nil;
    std::u16string name;
    compiler::Ref ref;
    logger::Loc loc;
};

// The alias in "export * as ns from 'mod'".
struct ExportStarAlias {
    std::string original_name;

    logger::Loc loc;
};

// ============================================================================
// Expression Nodes
// ============================================================================

// An array literal: [1, 2, ...rest]
// items contains each element expression (including spread elements).
// comma_after_spread tracks the trailing comma after a spread for source maps.
struct EArray {
    std::vector<Expr> items;
    logger::Loc comma_after_spread;
    logger::Loc close_bracket_loc;
    bool is_single_line{};
    bool is_parenthesized{};
};

// A unary expression: +x, -x, ~x, !x, void x, typeof x, delete x,
// ++x (prefix), --x (prefix), x++ (postfix), x-- (postfix).
// The op field selects the operation. Two boolean flags track semantic
// properties that affect optimization:
//
// was_originally_typeof_identifier:
//   typeof x      => true   (safe to simplify)
//   typeof (0, x) => false  (must not become "typeof x" if x is unbound,
//                             because the comma expression suppresses the
//                             ReferenceError that typeof would normally
//                             avoid for undeclared names)
//
// was_originally_delete_of_identifier_or_property_access:
//   delete x.y    => true
//   delete (0, x) => false  (cannot simplify to "delete x" because
//                             "delete x" is a syntax error in strict mode)
struct EUnary {
    Expr value;
    OpCode op{};

    bool was_originally_typeof_identifier{};

    bool was_originally_delete_of_identifier_or_property_access{};
};

// A binary expression: left op right.
// Example: a + b  =>  EBinary { left: EIdentifier("a"), op: kBinOpAdd, right: EIdentifier("b") }
struct EBinary {
    Expr left;
    Expr right;
    OpCode op{};
};

struct EBoolean {
    bool value{};
};

struct EMissing {};

struct ESuper {};

struct ENull {};

struct EUndefined {};

struct EThis {};

struct ENewTarget {
    logger::Range range;
};

struct EImportMeta {
    int32_t range_len{};
};

// A "new" expression: new Foo(1, 2).
// can_be_unwrapped_if_unused is true when the constructor has no side
// effects beyond the allocation, so if the result is unused the entire
// expression can be dropped.
struct ENew {
    Expr target;
    std::vector<Expr> args;

    logger::Loc close_paren_loc;
    bool is_multi_line{};

    bool can_be_unwrapped_if_unused{};
};

// A function call expression: target(args).
// can_be_unwrapped_if_unused is true when the call is annotated with
// @__PURE__ or #__PURE__, meaning it can be removed if its result is
// unused. Important: when the call is removed, any arguments that have
// side effects must still be evaluated and preserved.
//
// Example:
//   pureFn(1, 2)  =>  ECall { target: pureFn, args: [1, 2], can_be_unwrapped_if_unused: true }
struct ECall {
    Expr target;
    std::vector<Expr> args;
    logger::Loc close_paren_loc;
    OptionalChain optional_chain{};
    CallKind kind{};
    bool is_multi_line{};

    bool can_be_unwrapped_if_unused{};

    bool HasSameFlagsAs(const ECall& b) const;
};

// A dot property access: obj.name.
// can_be_removed_if_unused is true when reading the property has no side
// effects (no getter). call_can_be_unwrapped_if_unused is true when the
// entire expression (obj.name(args)) can be removed if unused.
// is_symbol_instance tracks whether this looks like Symbol.iterator access
// for purposes of iterator protocol optimization.
struct EDot {
    Expr target;
    std::string name;
    logger::Loc name_loc;
    OptionalChain optional_chain{};

    bool can_be_removed_if_unused{};
    bool call_can_be_unwrapped_if_unused{};
    bool is_symbol_instance{};

    bool HasSameFlagsAs(const EDot& b) const;
};

// A bracket property access: obj[expr].
// Same flags as EDot but the index expression is an arbitrary Expr rather
// than a literal name.
struct EIndex {
    Expr target;
    Expr index;
    logger::Loc close_bracket_loc;
    OptionalChain optional_chain{};

    bool can_be_removed_if_unused{};
    bool call_can_be_unwrapped_if_unused{};
    bool is_symbol_instance{};

    bool HasSameFlagsAs(const EIndex& b) const;
};

// An arrow function expression: (x) => x * 2  or  async (x) => { return x; }
// prefer_expr: when true, the printer prefers the concise body form (=> expr)
//   even if the block form was originally used.
// is_parenthesized: tracks whether the arrow was written inside parentheses.
//   V8 uses parentheses as a fast-parse hint to skip lazy pre-parsing.
struct EArrow {
    std::vector<Arg> args;
    FnBody body;

    bool is_async{};
    bool has_rest_arg{};
    bool prefer_expr{};

    bool is_parenthesized{};

    bool has_no_side_effects_comment{};
};

// A function expression: function foo() {} or (function () {}).
// is_parenthesized tracks whether the function was written inside parentheses.
//   V8 skips lazy pre-parsing of parenthesized functions for faster startup.
struct EFunction {
    Fn fn;

    bool is_parenthesized{};
};

struct EClass {
    Class class_;
};

// A simple identifier reference: variable name, parameter, etc.
// must_keep_due_to_with_stmt: when a "with" statement is present, identifiers
//   that might refer to properties of the with-scope object must not be
//   renamed or eliminated.
// can_be_removed_if_unused: when true, this reference is dead and can be
//   eliminated if no side effects are required.
// call_can_be_unwrapped_if_unused: when the identifier is used as a call
//   target (f()), this flag on f indicates the call can be dropped.
struct EIdentifier {
    compiler::Ref ref;

    bool must_keep_due_to_with_stmt{};
    bool can_be_removed_if_unused{};
    bool call_can_be_unwrapped_if_unused{};
};

// A reference to an ES6 import binding, distinct from EImportIdentifier.
// When the imported symbol belongs to the same module group, it can be
// inlined directly. When it comes from an external module, it becomes a
// property access on the namespace object. Keeping these as a separate
// type prevents accidentally treating an import reference as a plain
// identifier in shorthand syntax optimizations.
struct EImportIdentifier {
    compiler::Ref ref;
    bool prefer_quoted_key{};

    bool was_originally_identifier{};
};

// A class-private identifier: #name. Used in computed property positions
// (EIndex and Property) to reference private fields and methods.
struct EPrivateIdentifier {
    compiler::Ref ref;
};

// A property name eligible for mangling during minification. The referenced
// symbol is a SymbolMangledProp that will be renamed to a shorter name.
// @__KEY__ comments in the source mark which properties are candidates.
struct ENameOfSymbol {
    compiler::Ref ref;
    bool has_property_key_comment{};
};

// A JSX element: <div className="box">Hello</div>
// tag_or_nil is the element name (null for fragments <>).
// properties contains attributes like className, id, etc.
// nullable_children holds child expressions and text nodes.
// Empty entries in nullable_children represent JSXChildExpression nodes
// (the "{}" in <a>{}</a>); these are only present when JSX preservation
// is enabled and are ignored during JS transformation.
struct EJSXElement {
    Expr tag_or_nil;
    std::vector<Property> properties;

    std::vector<Expr> nullable_children;

    logger::Loc close_loc;
    bool is_tag_single_line{};
};

// Raw JSX text content, stored verbatim. The JSX specification does not
// define how text should be interpreted (Babel and TypeScript differ in
// newline handling), so the preserve transform reproduces the original
// source text exactly.
struct EJSXText {
    std::string raw;
};

struct ENumber {
    double value{};
};

struct EBigInt {
    std::string value;
};

// An object literal: { key: value, ...spread }
// Properties may be data properties, methods, getters, setters, or spread
// elements. is_parenthesized tracks whether the object was wrapped in
// parentheses (affects parsing of comma expressions).
struct EObject {
    std::vector<Property> properties;
    logger::Loc comma_after_spread;
    logger::Loc close_brace_loc;
    bool is_single_line{};
    bool is_parenthesized{};
};

// A spread element: ...expr
struct ESpread {
    Expr value;
};

// Represents both regular string literals ("hello") and no-substitution
// template literals (`hello`). Using a single type reduces the number of
// cases that string optimization code must handle. prefer_template causes
// the printer to output backtick form even for plain strings.
struct EString {
    std::u16string value;
    logger::Loc legacy_octal_loc;
    bool prefer_template{};
    bool has_property_key_comment{};
    bool contains_unique_key{};
};

// A template literal with substitutions: `hello ${name}`
// tag_or_nil holds the tag function for tagged templates: tag`text`
// head_raw/head_cooked hold the first text segment (before any ${}).
// parts holds the substitution expressions and their adjacent tail text.
// tag_was_originally_property_access tracks whether the tag was a property
// access (a.b``). If optimization changes the tag, the "this" binding
// must be preserved using the comma-operator indirection.
struct ETemplate {
    Expr tag_or_nil;
    std::string head_raw;
    std::u16string head_cooked;
    std::vector<TemplatePart> parts;
    logger::Loc head_loc;
    logger::Loc legacy_octal_loc;

    bool can_be_unwrapped_if_unused{};

    bool tag_was_originally_property_access{};

    bool head_cooked_is_valid{true};
};

struct ERegExp {
    std::string value;
};

// An inlined enum value. After TypeScript enum inlining, references to
// enum members are replaced with their constant values. The comment field
// preserves the original enum member name for debugging and source maps.
struct EInlinedEnum {
    Expr value;
    std::string comment;
};

// A type annotation wrapper. Annotations are stripped during output but
// affect type checking and dead code elimination.
struct EAnnotation {
    Expr value;
    AnnotationFlags flags{};
};

struct EAwait {
    Expr value;
};

struct EYield {
    Expr value_or_nil;
    bool is_star{};
};

// A ternary conditional expression: test ? yes : no.
struct EIf {
    Expr test;
    Expr yes;
    Expr no;
};

// A require() call with a string literal argument: require('./module').
// import_record_index identifies the dependency in the import record table.
struct ERequireString {
    uint32_t import_record_index{};
    logger::Loc close_paren_loc;
};

// A require.resolve() call with a string literal argument.
struct ERequireResolveString {
    uint32_t import_record_index{};
    logger::Loc close_paren_loc;
};

// A static import() call with a string literal argument: import('./lazy').
struct EImportString {
    uint32_t import_record_index{};
    logger::Loc close_paren_loc;
};

// A dynamic import() call with a non-literal argument:
//   import(specifier, { type: 'module' })
// The specifier is an arbitrary expression. phase indicates whether this
// is a source-phase or defer-phase import.
struct EImportCall {
    Expr expr;
    Expr options_or_nil;
    logger::Loc close_paren_loc;
    compiler::ImportPhase phase{};
};

// ============================================================================
// Statement Nodes
// ============================================================================

struct SEmpty {};

// A placeholder for TypeScript type-only declarations that are erased
// during compilation. was_declare_class specifically tracks "declare class"
// statements, which affect class instantiation semantics.
struct STypeScript {
    bool was_declare_class{};
};

struct SComment {
    std::string text;
    bool is_legal_comment{};
};

struct SDebugger {};

struct SDirective {
    std::u16string value;
    logger::Loc legacy_octal_loc;
};

struct SExportClause {
    std::vector<ClauseItem> items;
    bool is_single_line{};
};

struct SExportFrom {
    std::vector<ClauseItem> items;
    compiler::Ref namespace_ref;
    uint32_t import_record_index{};
    bool is_single_line{};
};

struct SExportDefault {
    Stmt value;
    compiler::LocRef default_name;
};

struct SExportStar {
    std::shared_ptr<ExportStarAlias> alias;
    compiler::Ref namespace_ref;
    uint32_t import_record_index{};
};

// TypeScript "export = value;" statement. This is CommonJS-style default
// export used by TypeScript, equivalent to "module.exports = value".
struct SExportEquals {
    Expr value;
};

// A deferred export decision. The parser cannot know at parse time whether
// a file uses CommonJS or ESM exports, so it stores the value here and
// the linker resolves it to either "module.exports" or "export default"
// based on the file's export kind.
struct SLazyExport {
    Expr value;
};

// An expression statement: expr;
// is_from_class_or_fn_that_can_be_removed_if_unused is set for auto-generated
// expressions from class syntax lowering (field initializations, name
// property calls, etc.). These prevent side-effect analysis from marking
// the class as pure, since each expression mutates it.
struct SExpr {
    Expr value;

    bool is_from_class_or_fn_that_can_be_removed_if_unused{};
};

// A TypeScript enum declaration:
//   enum Direction { Up, Down, Left, Right }
// The compiled form wraps the enum in an IIFE that defines the enum object.
struct SEnum {
    std::vector<EnumValue> values;
    compiler::LocRef name;
    compiler::Ref arg;
    bool is_export{};
};

// A TypeScript namespace declaration:
//   namespace Util { export function helper() {} }
// The compiled form wraps the namespace body in an IIFE.
struct SNamespace {
    std::vector<Stmt> stmts;
    compiler::LocRef name;
    compiler::Ref arg;
    bool is_export{};
};

struct SFunction {
    Fn fn;
    bool is_export{};
};

struct SClass {
    Class class_;
    bool is_export{};
};

struct SLabel {
    Stmt stmt;
    compiler::LocRef name;
    bool is_single_line_stmt{};
};

struct SIf {
    Expr test;
    Stmt yes;
    std::shared_ptr<Stmt> no_or_nil;
    bool is_single_line_yes{};
    bool is_single_line_no{};
};

struct SFor {
    std::shared_ptr<Stmt> init_or_nil;
    Expr test_or_nil;
    Expr update_or_nil;
    Stmt body;
    bool is_single_line_body{};
    bool is_lowered_for_await{};
};

struct SForIn {
    std::shared_ptr<Stmt> init;
    Expr value;
    Stmt body;
    bool is_single_line_body{};
};

struct SForOf {
    std::shared_ptr<Stmt> init;
    Expr value;
    Stmt body;
    logger::Range await;
    bool is_single_line_body{};
};

struct SDoWhile {
    Stmt body;
    Expr test;
};

struct SWhile {
    Expr test;
    Stmt body;
    bool is_single_line_body{};
};

struct SWith {
    Expr value;
    Stmt body;
    logger::Loc body_loc;
    bool is_single_line_body{};
};

struct STry {
    std::shared_ptr<Catch> catch_block;
    std::shared_ptr<Finally> finally_block;
    SBlock block;
    logger::Loc block_loc;
};

struct SSwitch {
    Expr test;
    std::vector<Case> cases;
    logger::Loc body_loc;
    logger::Loc close_brace_loc;
};

// Represents all forms of ES6 import statements:
//   import 'path'
//   import { item1, item2 } from 'path'
//   import * as ns from 'path'
//   import defaultItem, { item1, item2 } from 'path'
//   import defaultItem, * as ns from 'path'
// All parts are optional and combinable, except that the named clause and
// star import cannot coexist in the same statement.
struct SImport {
    std::shared_ptr<compiler::LocRef> default_name;
    std::shared_ptr<std::vector<ClauseItem>> items;
    std::shared_ptr<logger::Loc> star_name_loc;

    compiler::Ref namespace_ref;

    uint32_t import_record_index{};
    bool is_single_line{};
};

struct SReturn {
    Expr value_or_nil;
};

struct SThrow {
    Expr value;
};

// A variable declaration statement: var/let/const/using/await using.
// Each Decl in the vector is one declarator (e.g., const a = 1, b = 2
// produces two Decl entries). is_export is true for "export const ...".
struct SLocal {
    std::vector<Decl> decls;
    LocalKind kind{};
    bool is_export{};

    bool was_ts_import_equals{};
};

struct SBreak {
    std::shared_ptr<compiler::LocRef> label;
};

struct SContinue {
    std::shared_ptr<compiler::LocRef> label;
};

// ============================================================================
// Shared Singletons
// ============================================================================

// Pre-allocated shared pointers for the most common AST nodes. Using
// shared_ptr for these avoids allocating a separate heap object for every
// occurrence of null, undefined, this, empty blocks, etc. All code paths
// that create these nodes should use the shared versions.
extern const std::shared_ptr<BMissing> kBMissingShared;
extern const std::shared_ptr<EMissing> kEMissingShared;
extern const std::shared_ptr<ENull> kENullShared;
extern const std::shared_ptr<ESuper> kESuperShared;
extern const std::shared_ptr<EThis> kEThisShared;
extern const std::shared_ptr<EUndefined> kEUndefinedShared;
extern const std::shared_ptr<SDebugger> kSDebuggerShared;
extern const std::shared_ptr<SEmpty> kSEmptyShared;
extern const std::shared_ptr<STypeScript> kSTypeScriptShared;
extern const std::shared_ptr<STypeScript> kSTypeScriptSharedWasDeclareClass;

// ============================================================================
// Scopes
// ============================================================================

struct ScopeMember {
    compiler::Ref ref;
    logger::Loc loc;
};

// A lexical scope in the AST. Scopes form a parent/child tree that mirrors
// the nesting of blocks, functions, and modules. Each scope tracks:
//   - its members: variables declared directly inside it
//   - strict mode status: whether "use strict" is in effect
//   - scope kind: block, function, module, etc.
//
// contains_direct_eval is set when the scope contains a direct eval("...")
// call. This prevents all symbols in the scope from being renamed, because
// eval can access them by name at runtime.
struct Scope {
    TSNamespaceScope* ts_namespace{};

    Scope* parent{};
    std::vector<Scope*> children;
    std::unordered_map<std::string, ScopeMember> members;
    std::vector<ScopeMember> replaced;
    std::vector<compiler::Ref> generated;

    logger::Loc use_strict_loc;

    compiler::LocRef label;
    bool label_stmt_is_loop{};

    bool contains_direct_eval{};

    bool forbid_arguments{};

    bool is_after_const_local_prefix{};

    StrictModeKind strict_mode{};
    ScopeKind kind{};

    void RecursiveSetStrictMode(StrictModeKind new_kind);
};

// ============================================================================
// TypeScript Namespace Support
// ============================================================================

struct TSNamespaceMember {
    TSNamespaceMemberData data;
    logger::Loc loc;
    bool is_enum_value{};
};

struct TSNamespaceMemberProperty {};

// Represents a nested namespace declaration. The exported_members map is
// shared (via shared_ptr) across sibling namespace blocks so that all
// declarations with the same name merge into a single namespace.
struct TSNamespaceMemberNamespace {
    std::shared_ptr<std::unordered_map<std::string, TSNamespaceMember>> exported_members;
};

struct TSNamespaceMemberEnumNumber {
    double value{};
};

struct TSNamespaceMemberEnumString {
    std::u16string value;
};

// Manages scope merging for TypeScript namespaces and enums. Multiple blocks
// sharing the same name (e.g., two "namespace Foo { }" declarations) must
// share their exported members so that cross-block references work:
//
//   enum Foo { A = 3 }
//   enum Foo { B = A + 1 }   // A is visible from the sibling block
//
// lazily_generated_property_accesses converts direct references into property
// accesses on the compiled IIFE argument (e.g., "y" becomes "x3.y").
struct TSNamespaceScope {
    std::shared_ptr<std::unordered_map<std::string, TSNamespaceMember>> exported_members;

    std::unordered_map<std::string, compiler::Ref> lazily_generated_property_accesses;

    compiler::Ref arg_ref;

    // When true, this scope is an enum (not a namespace). Enum and namespace
    // scopes do not cross-reference each other; they only merge within
    // their own kind.
    bool is_enum_scope{};
};

struct ModuleTypeData {
    const logger::Source* source{};
    logger::Range range;
    ModuleType type{};
};

// Index of the auto-generated part that contains the __export() call. This
// part creates property getters on the exports object for ES6 named exports
// and CommonJS modules. Every file has exactly one, even if it has no
// statements.
constexpr uint32_t kNSExportPartIndex = 0;

struct TSEnumValue {
    std::u16string string;
    double number{};
    bool is_string{};
};

struct ConstValue {
    double number{};
    std::u16string string;
    ConstValueKind kind{};
};

// Attempts to evaluate an expression at compile time. Returns a ConstValue
// with kind != kNone if the expression is a compile-time constant, or
// kind == kNone if it cannot be statically evaluated.
//
// Example:
//   ExprToConstValue(ENumber(42))    =>  ConstValue { number: 42, kind: kNumber }
//   ExprToConstValue(EString("hi"))  =>  ConstValue { string: "hi", kind: kString }
//   ExprToConstValue(ENull())        =>  ConstValue { kind: kNull }
//   ExprToConstValue(ECall(...))     =>  ConstValue { kind: kNone }
ConstValue ExprToConstValue(const Expr& expr);

// Converts a compile-time constant value back into an expression node.
// Used when inlining constant values (e.g., enum members) at their use
// sites.
//
// Example:
//   ConstValueToExpr(loc, ConstValue { number: 42, kind: kNumber })
//     =>  ENumber { value: 42 }
//   ConstValueToExpr(loc, ConstValue { string: "hi", kind: kString })
//     =>  EString { value: u"hi" }
Expr ConstValueToExpr(logger::Loc loc, const ConstValue& value);

// ============================================================================
// Import / Export Metadata
// ============================================================================

struct NamedImport {
    std::string alias;

    std::vector<uint32_t> local_parts_with_uses;

    logger::Loc alias_loc;
    compiler::Ref namespace_ref;
    uint32_t import_record_index{};

    bool alias_is_star{};

    bool is_exported{};
};

struct NamedExport {
    compiler::Ref ref;
    logger::Loc alias_loc;
};

// Hash functor for compiler::Ref, used as key in unordered_maps. Combines
// source_index and inner_index into a single 64-bit hash to minimize
// collisions.
struct RefHash {
    size_t operator()(const compiler::Ref& r) const {
        return (static_cast<size_t>(r.source_index) << 32) | r.inner_index;
    }
};

struct LocHash {
    size_t operator()(const logger::Loc& l) const {
        return std::hash<int32_t>{}(l.start);
    }
};

struct Dependency {
    uint32_t source_index{};
    uint32_t part_index{};
};

struct DeclaredSymbol {
    compiler::Ref ref;
    bool is_top_level{};
};

struct SymbolUse {
    uint32_t count_estimate{};
};

struct SymbolCallUse {
    uint32_t call_count_estimate{};
    uint32_t single_arg_non_spread_call_count_estimate{};
};

// ============================================================================
// Parts (Tree Shaking Unit)
// ============================================================================

// A part is a contiguous sequence of top-level statements within a file.
// Parts are the fundamental unit of tree shaking (unused parts are dropped)
// and code splitting (parts from the same file can land in different output
// chunks). Each part tracks:
//
//   - which symbols it declares and uses
//   - dependencies on other parts (within the same file)
//   - import records it references
//   - symbol usage counts for frequency-based renaming
//
// A part with can_be_removed_if_unused=true can be safely dropped if no
// other reachable part references its declared symbols.
struct Part {
    std::vector<Stmt> stmts;
    std::vector<Scope*> scopes;

    std::vector<uint32_t> import_record_indices;

    std::vector<DeclaredSymbol> declared_symbols;

    std::unordered_map<compiler::Ref, SymbolUse, RefHash> symbol_uses;

    std::unordered_map<compiler::Ref, SymbolCallUse, RefHash> symbol_call_uses;

    std::unordered_map<compiler::Ref, std::unordered_map<std::string, SymbolUse>, RefHash> import_symbol_property_uses;

    std::vector<Dependency> dependencies;

    bool can_be_removed_if_unused{};

    bool force_tree_shaking{};

    bool is_live{};
};

// ============================================================================
// Top-Level AST
// ============================================================================

// The root of the AST for a single JavaScript/TypeScript file. Contains all
// parts, symbol definitions, scope information, import/export metadata, and
// optimization hints. The parser populates most fields during parsing; the
// linker enriches it with cross-module information during the linking pass.
struct AST {
    ModuleTypeData module_type_data;
    std::vector<Part> parts;
    std::vector<compiler::Symbol> symbols;
    std::unordered_map<logger::Loc, std::vector<std::string>, LocHash> expr_comments;
    Scope* module_scope{};
    std::shared_ptr<compiler::CharFreq> char_freq;

    Expr manifest_for_yarn_pnp;

    std::string hashbang;
    std::vector<std::string> directives;
    std::string url_for_css;

    // Maps top-level symbols to the parts that declare them. Populated by
    // the parser and treated as immutable afterward. The linker overlays
    // cross-module information via a separate method without mutating this
    // map.
    std::unordered_map<compiler::Ref, std::vector<uint32_t>, RefHash> top_level_symbol_to_parts_from_parser;

    // TypeScript enum constants for cross-module inlining. Each enum's
    // members are stored as name->value mappings. The linker fills this
    // during the linking pass.
    std::unordered_map<compiler::Ref, std::unordered_map<std::string, TSEnumValue>, RefHash> ts_enums;

    // Known inlinable constant values for cross-module propagation of
    // const variables and enum members.
    std::unordered_map<compiler::Ref, ConstValue, RefHash> const_values;

    // Properties whose names are mangled to shorter identifiers during
    // minification. Maps original property name to the symbol ref.
    std::unordered_map<std::string, compiler::Ref> mangled_props;

    // Existing property names in the source that must not collide with
    // generated mangled names during minification.
    std::unordered_map<std::string, bool> reserved_props;

    // Import records stored at the AST level for efficient manipulation
    // without full AST traversal.
    std::vector<compiler::ImportRecord> import_records;

    // Import/export metadata filled during parsing (which is parallelized
    // for performance).
    std::unordered_map<compiler::Ref, NamedImport, RefHash> named_imports;
    std::unordered_map<std::string, NamedExport> named_exports;
    std::vector<uint32_t> export_star_import_records;

    Span source_map_comment;

    // ES6 feature detection ranges. A non-empty range (len > 0) indicates
    // the feature was found. Ranges are stored for diagnostic messages that
    // point to the offending keyword.
    logger::Range export_keyword;
    logger::Range top_level_await_keyword;
    logger::Range live_top_level_await_keyword;

    compiler::Ref exports_ref;
    compiler::Ref module_ref;
    compiler::Ref wrapper_ref;

    int32_t approximate_line_count{};
    compiler::SlotCounts nested_scope_slot_counts;
    bool has_lazy_export{};

    // CommonJS feature flags. Files that use module.exports or require()
    // cannot be flat-bundled and must be wrapped in their own closure.
    bool uses_exports_ref{};
    bool uses_module_ref{};
    ExportsKind exports_kind{};
};

// ============================================================================
// Utility Functions
// ============================================================================

// Generates a human-readable name suffix from a file path for use in
// auto-generated identifiers. The result is cosmetic only (e.g.,
// "require_react" instead of "require273") and still goes through the
// renaming logic to avoid collisions.
//
// Example:
//   GenerateNonUniqueNameFromPath("src/components/App.tsx")  =>  "app"
//   GenerateNonUniqueNameFromPath("lib/utils/index.js")     =>  "index"
std::string GenerateNonUniqueNameFromPath(const std::string& path);

// Validates that a string is a legal JavaScript identifier. If the input
// is already valid, it is returned unchanged. If not, invalid characters
// are escaped so that the result is a valid identifier. This is used when
// converting property accesses to dot notation and when generating
// identifiers from user-provided names.
//
// Example:
//   EnsureValidIdentifier("foo")       =>  "foo"
//   EnsureValidIdentifier("123abc")    =>  "\\31 23abc"
//   EnsureValidIdentifier("my-var")    =>  "my\\45var"
std::string EnsureValidIdentifier(const std::string& base);

}
