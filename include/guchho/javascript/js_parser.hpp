#pragma once

#include <cstdint>
#include <string>
#include <cstdint>
#include <utility>


#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/config.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/sourcemap.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_lexer.hpp"
#include "guchho/javascript/js_helpers.hpp"


namespace guchho::javascript {

    // A special source index used for generated runtime import statements.
    // This value is never equal to the actual runtime source index (which is 0),
    // allowing the bundler to recognize and normalize runtime import records
    // before linking. Any import record with this source index is treated as
    // an internally-generated runtime dependency rather than a user-authored one.
    constexpr uint32_t kRuntimeSourceIndex = 0xFFFD0000;

    // Classifies the syntactic form of a call through an import namespace.
    // This distinction matters because the semantics of `this` binding and
    // argument handling differ between call, new, and JSX tag positions.
    enum class importNamespaceCallKind : uint8_t {
        exprKindCall,    // foo() — regular function call
        exprKindNew,     // new foo() — constructor invocation
        exprKindJSXTag,  // <foo /> — JSX element creation
    };

    // Represents a single call site that invokes a namespace import. The ref
    // points to the namespace symbol and the kind records the call form.
    struct importNamespaceCall {
        compiler::Ref ref;
        importNamespaceCallKind kind;
    };

    inline bool operator==(const importNamespaceCall &a, const importNamespaceCall &b) {
        return a.ref == b.ref && a.kind == b.kind;
    }

    // Hash functor for importNamespaceCall, used as a key in unordered maps.
    struct importNamespaceCallHash {
        size_t operator()(const importNamespaceCall &a) const {
            return (RefHash{}(a.ref) << 1) ^ static_cast<size_t>(a.kind);
        }
    };

    // Tracks a dot-separated identifier chain injected by define substitutions.
    // For example, if a define maps "process.env.NODE_ENV" to a value, the
    // parts vector contains {"process", "env", "NODE_ENV"} and the injectedDefineIndex
    // identifies which define rule produced it.
    struct injectedDotName {
        std::vector<std::string> parts;
        uint32_t injectedDefineIndex;
    };

    // Records the source location of an injected symbol for source map accuracy.
    struct injectedSymbolSource {
        logger::Source source;
        logger::Loc loc;
    };

    // Represents a .then()/.catch() chain on a Promise. When lowering async/await,
    // the parser tracks these chains to rewrite them into state-machine form.
    // nextTarget is the continuation expression, catchLoc points to the catch
    // handler, and hasMultipleArgs/hasCatch describe the chain shape.
    struct thenCatchChain {
        E nextTarget;
        logger::Loc catchLoc;
        bool hasMultipleArgs;
        bool hasCatch;
    };

    // Stores a string literal that will be hoisted as a local variable for
    // Yarn Plug'n'Play compatibility. The loc preserves the original source
    // position for source map generation.
    struct stringLocalForYarnPnP {
        std::vector<uint16_t> value;
        logger::Loc loc;
    };

    // Tracks the named import items belonging to a namespace import statement.
    // The entries map maps local alias names to their source locations. The
    // importRecordIndex identifies which import record this namespace refers to.
    struct namespaceImportItems {
        std::unordered_map<std::string, compiler::LocRef> entries;
        uint32_t importRecordIndex;
    };

    // Represents an import statement that uses glob patterns (e.g., import
    // "./features/*.ts"). The parts vector contains alternating literal and
    // wildcard segments. The assertOrWith field holds any import attributes.
    struct globPatternImport {
        std::shared_ptr<compiler::ImportAssertOrWith> assertOrWith;
        std::vector<helpers::GlobPart> parts;
        std::string name;
        logger::Range approximateRange;
        compiler::Ref ref;
        compiler::ImportKind kind;
        compiler::ImportPhase phase;
    };

    // A single segment of a glob pattern: either literal text or a wildcard.
    struct globPart {
        std::string text;
        bool isWildcard;
    };

    // The result of scanning a file for import and export statements. This is
    // used during the initial parse pass to determine the module type and
    // collect dependency information before the full visit pass.
    struct importsExportsScanResult {
        std::vector<Stmt> stmts;
        bool keptImportEquals = false;
        bool removedImportEquals = false;
    };

    // Options that can be compared for structural equality. This is used to
    // determine whether two parser invocations can share cached results. All
    // fields must be value-comparable (no pointers or references).
    struct optionsThatSupportStructuralEquality {
        std::string originalTargetEnv;
        ModuleTypeData moduleTypeData;
        compat::JSFeature unsupportedJSFeatures;
        compat::JSFeature unsupportedJSFeatureOverrides;
        compat::JSFeature unsupportedJSFeatureOverridesMask;

        config::TSOptions ts;
        config::Mode mode;
        config::Platform platform;
        config::Format outputFormat;
        logger::PathStyle logPathStyle;
        logger::PathStyle codePathStyle;
        bool asciiOnly;
        bool keepNames;
        bool minifySyntax;
        bool minifyIdentifiers;
        bool minifyWhitespace;
        bool omitRuntimeForTests;
        bool omitJSXRuntimeForTests;
        bool ignoreDCEAnnotations;
        bool treeShaking;
        bool dropDebugger;
        bool mangleQuoted;

        bool decodeHydrateRuntimeStateYarnPnP;

        bool operator==(const optionsThatSupportStructuralEquality& other) const;
    };

    // Full parser options. Contains both the structurally-comparable subset
    // and non-comparable options like regex pointers and injected files.
    struct Options {
        std::vector<config::InjectedFile> injectedFiles;
        config::JSXOptions jsx;
        config::TSAlwaysStrict *tsAlwaysStrict;
        std::regex *mangleProps;
        std::regex *reserveProps;
        std::vector<std::string> dropLabels;

        config::ProcessedDefines *defines;

        optionsThatSupportStructuralEquality optionsThatSupportStructuralEquality;

        bool equal(const Options &b) const;
    };

    Options OptionsForYarnPnP();
    Options OptionsFromConfig(config::Options *options);
    bool isSameRegexp(std::regex *a, std::regex *b);
    bool jsxExprsEqual(config::DefineExpr a, config::DefineExpr b);

    // Records information about a runtime helper function call. Global contains
    // the dot-separated path for global helpers (e.g., "__objectSpread"). Runtime
    // is the name of a runtime module function (e.g., "__export").
    struct HelperCall {
        std::vector<std::string> Global;
        std::string Runtime;
    };

    // Controls whether `await` and `yield` are parsed as identifiers, expressions,
    // or are forbidden entirely. This depends on whether the parser is inside an
    // async function, a generator, or neither.
    enum awaitOrYield : uint8_t {
        allowIdent,  // can be used as an identifier name
        allowExpr,   // can be used as an expression (await expr / yield expr)
        forbidAll,   // completely forbidden in this context
    };

    // Classifies which JSX runtime import is needed. The parser selects the
    // appropriate import based on the JSX construct being used.
    enum JSXImport : uint8_t {
        JSXImportJSX,            // jsx() — standard element creation
        JSXImportJSXS,           // jsxs() — element with static children
        JSXImportFragment,       // Fragment — <>...</> wrapper
        JSXImportCreateElement,  // createElement() — legacy JSX transform
    };

    // Controls how identifiers are parsed in different syntactic positions.
    // assignTarget determines if the identifier is on the left side of an
    // assignment. The boolean flags control context-specific behavior.
    struct identifierOpts {
        AssignTarget assignTarget = AssignTarget::kNone;
        bool isCallTarget = false;
        bool isDeleteTarget = false;
        bool preferQuotedKey = false;
        bool wasOriginallyIdentifier = false;
        bool matchAgainstDefines = false;
    };

    // Arrow function argument parsing may encounter syntax that is invalid in
    // arrow context (e.g., await/yield in parameter defaults). These errors
    // are deferred until the arrow body is confirmed, so that non-arrow
    // parses can backtrack without reporting false errors.
    struct deferredArrowArgErrors {
        logger::Range invalidExprAwait;
        logger::Range invalidExprYield;
    };

    // General deferred errors for expression parsing. These are accumulated
    // during speculative parsing and only reported if the parse succeeds.
    struct deferredErrors {
        logger::Range invalidExprDefaultValue;
        logger::Range invalidExprAfterQuestion;
        logger::Range arraySpreadFeature;

        std::vector<logger::Range> invalidParens;

        void mergeInto(deferredErrors *to) const;
    };

    // Bitflags that control which TypeScript type parameter syntaxes are
    // allowed during parsing.
    enum typeParameterFlags : uint8_t {
        allowInOutVarianceAnnotations = 1 << 0,  // TypeScript 4.7: in/out keywords
        allowConstModifier = 1 << 1,              // TypeScript 5.0: const modifier
        allowEmptyTypeParameters = 1 << 2,        // Allow "<>" without any parameters
    };

    // Flags that control how type annotations are skipped. When the parser
    // encounters something that looks like a type, it may need to skip over
    // it without emitting errors.
    enum skipTypeFlags : uint8_t {
        isReturnTypeFlag = 1 << 0,                // currently in return type position
        isIndexSignatureFlag = 1 << 1,            // currently in index signature
        allowTupleLabelsFlag = 1 << 2,            // tuple element labels allowed
        disallowConditionalTypesFlag = 1 << 3,    // conditional types disallowed
    };

    // Result of attempting to skip TypeScript type parameters. This tells the
    // parser whether type parameters were actually present and consumed.
    enum ESkipTypeScript {
        didNotSkipAnything,       // no type parameters found
        definitelyTypeParameters, // type parameters were present and consumed
        skippedTypeParameters,    // consumed something ambiguous (may or may not be types)
    };

    // Bitflags that carry context through the expression parser. These flags
    // influence how expressions are parsed in specific syntactic positions.
    enum exprFlag : uint8_t {
        exprFlagDecorator = 1 << 0,                // parsing a decorator expression
        exprFlagForLoopInit = 1 << 1,              // for-loop initializer position
        exprFlagForAwaitLoopInit = 1 << 2,         // for-await-loop initializer position
        exprFlagAfterQuestionAndBeforeColon = 1 << 3, // ternary middle position
        exprFlagIsNewTarget = 1 << 4,              // new.target expression
    };

    // Options for parsing parenthesized expressions. The asyncRange tracks
    // whether an async keyword appeared before the paren. forceArrowFn forces
    // the expression to be parsed as an arrow function.
    struct parenExprOpts {
        logger::Range asyncRange{};
        bool forceArrowFn{};
        bool isAfterQuestionAndBeforeColon{};
    };

    // Records a syntax feature that was encountered during parsing. Used to
    // emit compatibility warnings when targeting older environments.
    struct syntaxFeature {
        compat::JSFeature feature;
        logger::Range token;
    };

    // Accumulates invalid tokens and syntax features found during parsing.
    struct invalidLog {
        std::vector<logger::Range> invalidTokens;
        std::vector<syntaxFeature> syntaxFeatures;
    };

    // Bitflags that describe the current decorator parsing context. This
    // determines which decorator syntaxes are valid.
    enum decoratorContextFlags : uint8_t {
        decoratorBeforeClassExpr = 1 << 0,  // decorator precedes a class expression
        decoratorInClassExpr = 1 << 1,      // decorator is inside a class body
        decoratorInFnArgs = 1 << 2,         // decorator is on a function parameter
    };

    // Options for parsing object/class properties. Tracks decorators, async/
    // generator status, static/abstract flags, and other context.
    struct propertyOpts {
        std::vector<Decorator> decorators;
        Scope *decoratorScope = nullptr;
        decoratorContextFlags decoratorContext = {};

        logger::Range asyncRange{};
        logger::Range generatorRange{};
        logger::Range tsDeclareRange{};
        logger::Range classKeyword{};
        bool isAsync = false;
        bool isGenerator = false;

        bool isStatic = false;
        bool isTSAbstract = false;
        bool isClass = false;
        bool classHasExtends = false;
    };

    // Options for parsing binding patterns (destructuring targets).
    struct parseBindingOpts {
        bool isUsingStmt = false;
    };

    // Options for visiting binding patterns. duplicateArgCheck is used to
    // detect duplicate parameter names in strict mode.
    struct bindingOpts {
        std::unordered_map<std::string, logger::Range> *duplicateArgCheck = nullptr;
    };

    // Input context for visiting an expression. Controls how the visit pass
    // processes the expression based on its syntactic position.
    struct exprIn {
        bool isMethod = false;
        bool isLoweredPrivateMethod = false;
        bool hasChainParent = false;
        bool storeThisArgForParentOptionalChain = false;
        bool shouldMangleStringsAsProps = false;
        AssignTarget assignTarget = AssignTarget::kNone;
    };

    // Output context from visiting an expression. Carries side-channel
    // information from child to parent, such as `this` binding and
    // optional chain state.
    struct exprOut {
        std::function<Expr()> thisArgFunc;
        std::function<Expr(Expr)> thisArgWrapFunc;
        bool childContainsOptionalChain = false;
        bool callMustBeReplacedWithUndefined = false;
        bool methodCallMustBeReplacedWithUndefined = false;
    };

    // Records whether a property access was originally a dot access or bracket
    // index access. This matters for source map accuracy and for determining
    // whether the property name can be minified.
    enum wasOriginallyDotOrIndex : uint8_t {
        wasOriginallyDot,
        wasOriginallyIndex,
    };

    // Controls whether lexical declarations (let/const/class) are allowed in
    // the current parsing context. Some contexts like case clauses and labels
    // have special rules about lexical declarations.
    enum lexicalDecl : uint8_t {
        lexicalDeclAllowAll,
        lexicalDeclForbid,
        lexicalDeclAllowInCase,
        lexicalDeclAllowFnInsideIf,
        lexicalDeclAllowFnInsideLabel,
    };

    // Holds decorators that have been parsed but not yet attached to a
    // declaration. This allows decorators to be parsed before the declaration
    // they decorate is fully parsed.
    struct deferredDecorators {
        std::vector<Decorator> decorators;
    };

    // Options for parsing class bodies. Tracks decorators and whether this is
    // a TypeScript declare class (which has no body).
    struct parseClassOpts {
        std::vector<Decorator> decorators;
        decoratorContextFlags decoratorContext = {};
        bool isTypeScriptDeclare = false;
    };

    // Distinguishes function declarations from function expressions. This
    // affects hoisting behavior and strict mode validation.
    enum fnKind : uint8_t {
        fnStmt,   // function foo() {} — declaration
        fnExpr,   // (function foo() {}) — expression
    };

    // Classifies the kind of strict mode violation being reported. This is
    // used to select the appropriate error message.
    enum strictModeFeature : uint8_t {
        reservedWord,           // use of reserved word as identifier
        evalOrArguments,        // eval/arguments as variable name
        ifElseFunctionStmt,     // function statement in if/else
        withStatement,          // with statement in strict mode
        forInVarInit,           // var initialization in for-in
        legacyOctalEscape,      // legacy octal escape sequence
        legacyOctalLiteral,     // legacy octal numeric literal
        labelFunctionStmt,      // function statement with label
        deleteBareName,         // delete identifier without quotes
    };

    // Controls how values are captured during lowering. valueDefinitelyNotMutated
    // allows more aggressive optimizations (e.g., inlining). valueCouldBeMutated
    // requires defensive copies.
    enum captureValueMode : uint8_t {
        valueDefinitelyNotMutated = 0,
        valueCouldBeMutated,
    };

    // Options for skipping TypeScript type arguments in expressions. The
    // isInsideJSXElement flag disambiguates `<T>` from JSX tag syntax.
    struct skipTypeScriptTypeArgumentsOpts {
        bool isInsideJSXElement{};
        bool isParseTypeArgumentsInExpression{};
    };

    // Mutable state for parsing function and arrow function bodies. This is
    // passed by pointer through the parse tree so that nested functions can
    // modify the enclosing context (e.g., tracking whether await/yield are
    // allowed).
    struct fnOrArrowDataParse {
        deferredArrowArgErrors *arrowArgErrors = nullptr;
        Scope *decoratorScope = nullptr;
        logger::Range asyncRange{};
        logger::Loc needsAsyncLoc{};
        awaitOrYield await = awaitOrYield::allowIdent;
        awaitOrYield yield = awaitOrYield::allowIdent;
        bool allowSuperCall = false;
        bool allowSuperProperty = false;
        bool isTopLevel = false;
        bool isConstructor = false;
        bool isTypeScriptDeclare = false;
        bool isThisDisallowed = false;
        bool isReturnDisallowed = false;

        bool allowMissingBodyForTypeScript = false;
    };

    // Mutable state for visiting function and arrow function bodies. Tracks
    // control flow context (loops, switches, try/catch) and class inheritance
    // information needed during the visit pass.
    struct fnOrArrowDataVisit {
        int32_t tryBodyCount = 0;
        logger::Loc tryCatchLoc{};

        bool isArrow = false;
        bool isAsync = false;
        bool isGenerator = false;
        bool isInsideLoop = false;
        bool isInsideSwitch = false;
        bool isDerivedClassCtor = false;
        bool isOutsideFnOrArrow = false;
        bool shouldLowerSuperPropertyAccess = false;
    };

    // Function-only visit state. Tracks function-specific concerns like the
    // `arguments` binding, `this` capture, and inner class name references.
    struct fnOnlyDataVisit {
        compiler::Ref *argumentsRef = nullptr;

        compiler::Ref *thisCaptureRef = nullptr;
        compiler::Ref *argumentsCaptureRef = nullptr;

        bool shouldReplaceThisWithInnerClassNameRef = false;

        bool isInStaticClassContext = false;

        compiler::Ref *innerClassNameRef = nullptr;

        bool isInsideAsyncArrowFn = false;

        bool isNewTargetAllowed = false;

        bool isThisNested = false;

        bool hasThisUsage = false;

        bool silenceMessageAboutThisBeingUndefined = false;
    };

    // Sentinel value for the module scope location. Negative values are
    // reserved for special scope markers that are not real source locations.
    const int locModuleScope = -1;

    // Records a scope in the order it was pushed during parsing. This is
    // used by enum lowering to determine the correct scope nesting.
    struct scopeOrder {
        Scope *scope;
        logger::Loc loc;
    };

    // Classification of code reachability. Used by the optimizer to determine
    // whether code can be eliminated.
    enum livenessStatus : int8_t {
        alwaysDead = -1,       // code is unreachable
        livenessUnknown = 0,   // cannot determine at compile time
        alwaysLive = 1,        // code is always reachable
    };

    // Per-case liveness state for switch statement optimization.
    struct switchCaseLiveness {
        livenessStatus status = livenessStatus::livenessUnknown;
        bool canFallThrough = false;
    };

    // Size of the bloom filter used for duplicate case value detection.
    // Must be prime for optimal hash distribution.
    const size_t bloomFilterSize = 251;

    // A case value recorded for duplicate detection. The hash is precomputed
    // for fast rejection of non-duplicate candidates.
    struct duplicateCaseValue {
        Expr value;
        uint32_t hash;
    };

    // Computes a hash for a switch case expression. Returns the hash and
    // whether the expression is eligible for hash-based comparison.
    std::pair<uint32_t, bool> duplicateCaseHash(Expr expr);

    // Checks whether two switch case expressions are semantically equal.
    // Returns (isEqual, hasSideEffects).
    std::pair<bool, bool> duplicateCaseEquals(Expr left, Expr right);

    // Context for duplicate property warnings. Object literals and class
    // bodies have different rules for what counts as a duplicate.
    enum duplicatePropertiesIn : uint8_t {
        duplicatePropertiesInObject,
        duplicatePropertiesInClass,
    };

    // A temporary reference used during lowering. The optional valueOrNil
    // holds the initial value (for initializers), and ref is the symbol.
    struct tempRef {
        Expr valueOrNil;
        compiler::Ref ref;
    };

    // Controls whether a temporary reference needs a variable declaration.
    enum generateTempRefArg {
        tempRefNeedsDeclare,                         // must declare with let/const
        tempRefNoDeclare,                            // no declaration needed
        tempRefNeedsDeclareMayBeCapturedInsideLoop,  // declare with loop capture in mind
    };

    // Classifies the kind of statement list being processed. This affects
    // how variables are hoisted and how control flow is analyzed.
    enum stmtsKind : uint8_t {
        stmtsNormal = 0,     // normal statement list
        stmtsLoopBody,       // loop body (affects break/continue)
        stmtsFnBody,         // function body (affects return/arguments)
    };

    // Result of a symbol lookup during the visit pass.
    struct findSymbolResult {
        compiler::Ref ref;
        logger::Loc declareLoc;
        bool isInsideWithScope;
    };

    // Options for prepending temporary variable declarations to a statement list.
    struct prependTempRefsOpts {
        logger::Loc *fnBodyLoc = nullptr;
        stmtsKind kind = stmtsNormal;
    };

    // Controls how variable declarations are relocated during optimization.
    enum relocateVarsMode : uint8_t {
        relocateVarsNormal,
        relocateVarsForInOrForOf,  // special handling for for-in/for-of
    };

    // Controls object rest element behavior during lowering.
    enum objRestMode : uint8_t {
        objRestMustReturnInitExpr,     // must return the initializer expression
        objRestReturnValueIsUnused,    // initializer value is unused
    };

    // Options for visiting function bodies.
    struct visitFnOpts {
        bool isMethod = false;
        bool isDerivedClassCtor = false;
        bool isLoweredPrivateMethod = false;
    };

    // Options for visiting function arguments. Tracks whether the function
    // has a rest parameter and whether formal parameters are unique.
    struct visitArgsOpts {
        std::vector<Stmt> body;
        Scope *decoratorScope = nullptr;
        bool hasRestArg = false;

        // True if the function is an arrow function or a method, where
        // parameter names are guaranteed unique by the language grammar.
        bool isUniqueFormalParameters = false;
    };

    // Result of visiting a class body. Contains the scope, inner class name
    // reference, and whether the class is side-effect-free (and thus removable
    // if unused).
    struct visitClassResult {
        Scope *bodyScope;
        compiler::Ref innerClassNameRef;
        compiler::Ref superCtorRef;

        // True if the class was determined to be side-effect free before
        // lowering. This is set after visiting the class body but before
        // lowering, since lowering may generate class mutations that cannot
        // be automatically analyzed as side-effect free.
        bool canBeRemovedIfUnused;
    };

    // Information about which class fields need to be lowered. The optimizer
    // uses this to determine the minimal set of transformations required.
    struct classLoweringInfo {
        bool lowerAllInstanceFields;
        bool lowerAllStaticFields;
        bool shimSuperCtorCalls;
    };

    // Controls the order in which typeof string comparisons are generated
    // during constant folding.
    enum typeofStringOrder : uint8_t {
        onlyCheckOriginalOrder = 0,  // only check in original operand order
        checkBothOrders,              // try both (a === "x" and b === "x")
    };

    // Classifies why a file is considered an ES module. This is used for
    // diagnostics and for determining the correct output format.
    enum whyESM : uint8_t {
        whyESMUnknown = 0,
        whyESMExportKeyword,         // file contains "export" keyword
        whyESMImportMeta,            // file uses import.meta
        whyESMTopLevelAwait,         // file uses top-level await
        whyESMFileMJS,               // file has .mjs extension
        whyESMFileMTS,               // file has .mts extension
        whyESMTypeModulePackageJSON, // package.json has "type": "module"
        whyESMImportStatement,       // file uses import statement
    };

    // Result of symbol substitution during single-use inlining.
    enum substituteStatus : uint8_t {
        substituteContinue = 0,  // substitution not attempted
        substituteSuccess,       // symbol was inlined successfully
        substituteFailure,       // substitution failed (symbol has multiple uses)
    };

    // Result of merging duplicate declarations (e.g., two class definitions
    // with the same name in the same scope).
    enum mergeResult {
        mergeForbidden,                    // merging not allowed
        mergeReplaceWithNew,               // replace old with new
        mergeOverwriteWithNew,             // overwrite old, keep new binding
        mergeKeepExisting,                 // keep old, discard new
        mergeBecomePrivateGetSetPair,      // merge into private getter/setter
        mergeBecomePrivateStaticGetSetPair, // merge into static private getter/setter
    };

    // Options for statement parsing. Controls which constructs are allowed
    // and how they are parsed (e.g., whether this is a for-loop initializer,
    // whether exports are expected, etc.).
    struct parseStmtOpts {
        bool isTypeScriptDeclare = false;
        bool isNamespaceScope = false;
        bool isModuleScope = false;
        bool isForLoopInit = false;
        bool isForAwaitLoopInit = false;
        bool isCaseBody = false;
        bool isUsingStmt = false;
        bool isExport = false;
        bool isNameOptional = false;
        bool isExportDefault = false;
        bool allowDirectivePrologue = false;
        bool hasNoSideEffectsComment = false;
        lexicalDecl lexicalDecl = lexicalDeclForbid;
        deferredDecorators *deferredDecorators = nullptr;
    };

    // A vector of scope members used for ordered iteration. The order of
    // insertion is preserved for deterministic output.
    struct scopeMemberArray {
        std::vector<ScopeMember> members;
    };

    bool scopeMemberArrayLess(const ScopeMember &a, const ScopeMember &b);

    struct Parser;

    // Holds state for lowering "using" and "await using" declarations into
    // runtime calls to __using/__callDispose. The context accumulates using
    // declarations across a block and generates the disposal logic when the
    // block exits.
    struct lowerUsingDeclarationContext {
        logger::Loc first_using_loc;
        compiler::Ref stack_ref;
        bool has_await_using{};

        // Scans a list of statements for using declarations, adding them to
        // the context's disposal stack.
        void scanStmts(Parser *p, std::vector<Stmt> stmts);

        // Finalizes the lowering by emitting disposal calls. The shouldHoistFunctions
        // flag controls whether disposal calls are placed before or after hoisted
        // function declarations.
        std::vector<Stmt> finalize(Parser *p, std::vector<Stmt> stmts, bool shouldHoistFunctions);
    };

    struct JSONOptions;

    // Drives the two-phase visit of binary expressions. The first phase
    // (checkAndPrepare) processes the left operand and determines whether
    // short-circuit optimization is possible. The second phase
    // (visitRightAndFinish) processes the right operand and completes the
    // expression.
    struct binaryExprVisitor {
        // Inputs
        std::shared_ptr<EBinary> e;
        logger::Loc loc;
        exprIn in;

        // Input for visiting the left child
        exprIn leftIn;

        // "Local variables" passed from "checkAndPrepare" to "visitRightAndFinish"
        bool isStmtExpr;
        bool oldSilenceWarningAboutThisBeingUndefined;

        Expr checkAndPrepare(Parser *p);
        Expr visitRightAndFinish(Parser *p);
    };

    // Detects duplicate case values in switch statements using a combination
    // of bloom filter (for fast rejection) and exact comparison (for
    // confirmation).
    struct duplicateCaseChecker {
        std::vector<duplicateCaseValue> cases;
        uint8_t bloomFilter[(bloomFilterSize + 7) / 8];

        void reset();
        void check(Parser *p, Expr expr);
    };

    // The main parser. Transforms a token stream from the Lexer into an AST.
    // The parser performs a two-pass process:
    //
    //   Pass 1 (parse): Builds the AST, declares symbols, creates scopes,
    //                    and collects import/export metadata.
    //
    //   Pass 2 (visit): Walks the AST to perform type-checking, error
    //                    reporting, constant folding, and lowering of modern
    //                    syntax to older equivalents.
    //
    // The Parser is stateful and non-reentrant. All fields are public because
    // the parser is used as a shared context between helper functions.
    struct Parser {
        // RAII helper that pops the current scope when the object is destroyed.
        // Used to ensure scope cleanup even when exceptions or early returns
        // occur.
        struct ParserScopePopper {
            Parser *p;
            ~ParserScopePopper() { p->popScope(); }
        };

        Options options;
        logger::Log log;
        logger::Source source;
        logger::LineColumnTracker tracker;
        fnOrArrowDataParse fnOrArrowDataParse;
        fnOnlyDataVisit fnOnlyDataVisit;
        std::vector<std::string> allocatedNames;
        Scope *currentScope;
        std::vector<Scope *> scopesForCurrentPart;
        std::vector<compiler::Symbol> symbols;
        HelperContext astHelpers;
        std::vector<uint32_t> tsUseCounts;
        std::vector<compiler::Ref> injectedDefineSymbols;
        std::unordered_map<compiler::Ref, injectedSymbolSource, RefHash> injectedSymbolSources;
        std::unordered_map<std::string, std::vector<injectedDotName>> injectedDotNames;
        std::unordered_map<std::string, bool> dropLabelsMap;
        std::unordered_map<logger::Loc, std::vector<std::string>, LocHash> exprComments;
        std::unordered_map<std::string, compiler::Ref> mangledProps;
        std::unordered_map<std::string, bool> reservedProps;
        std::unordered_map<compiler::Ref, SymbolUse, RefHash> symbolUses;
        std::unordered_map<compiler::Ref, std::unordered_map<std::string, SymbolUse>, RefHash> importSymbolPropertyUses;
        std::unordered_map<compiler::Ref, SymbolCallUse, RefHash> symbolCallUses;
        std::vector<DeclaredSymbol> declaredSymbols;
        std::vector<globPatternImport> globPatternImports;
        std::unordered_map<std::string, compiler::LocRef> runtimeImports;
        duplicateCaseChecker duplicateCaseChecker;
        std::unordered_map<std::string, bool> unrepresentableIdentifiers;
        std::unordered_map<ENumber *, logger::Range> legacyOctalLiterals;
        std::unordered_map<logger::Loc, std::vector<scopeOrder>, LocHash> scopesInOrderForEnum;
        std::vector<binaryExprVisitor> binaryExprStack;

        std::unordered_map<compiler::Ref, compiler::Ref, RefHash> hoistedRefForSloppyModeBlockFn;

        std::unordered_map<compiler::Ref, compiler::Ref, RefHash> privateGetters;
        std::unordered_map<compiler::Ref, compiler::Ref, RefHash> privateSetters;

        std::unordered_map<compiler::Ref, TSNamespaceMemberData, RefHash> refToTSNamespaceMemberData;
        const EIdentifier *tsNamespaceTarget;
        TSNamespaceMemberData tsNamespaceMemberData;
        std::unordered_map<compiler::Ref, bool, RefHash> emittedNamespaceVars;
        std::unordered_map<compiler::Ref, compiler::Ref, RefHash> isExportedInsideNamespace;
        std::unordered_map<std::string, bool> localTypeNames;
        std::shared_ptr<TSNamespaceMemberNamespace> tsNamespaceMemberDataFresh;
        std::unordered_map<compiler::Ref, std::unordered_map<std::string, TSEnumValue>, RefHash> tsEnums;
        std::unordered_map<compiler::Ref, ConstValue, RefHash> constValues;
        E *propDerivedCtorValue;
        Scope *propMethodDecoratorScope;

        compiler::Ref *enclosingNamespaceArgRef;

        std::vector<compiler::ImportRecord> importRecords;
        std::vector<uint32_t> importRecordsForCurrentPart;
        std::vector<uint32_t> exportStarImportRecords;

        std::unordered_map<compiler::Ref, namespaceImportItems, RefHash> importItemsForNamespace;
        std::unordered_map<compiler::Ref, bool, RefHash> isImportItem;
        std::unordered_map<compiler::Ref, NamedImport, RefHash> namedImports;
        std::unordered_map<std::string, NamedExport> namedExports;
        std::unordered_map<compiler::Ref, std::vector<uint32_t>, RefHash> topLevelSymbolToParts;
        std::unordered_map<importNamespaceCall, bool, importNamespaceCallHash> importNamespaceCCMap;

        std::vector<scopeOrder> scopesInOrder;

        std::string nameToKeep;
        E nameToKeepIsFor;

        E stmtExprValue;
        E callTarget;
        E dotOrIndexTarget;
        E templateTag;
        E deleteTarget;
        S *loopBody;
        E suspiciousLogicalOperatorInsideArrow;
        Scope *moduleScope;

        Expr manifestForYarnPnP;
        std::unordered_map<compiler::Ref, stringLocalForYarnPnP, RefHash> stringLocalsForYarnPnP;

        E awaitTarget;

        thenCatchChain thenCatchChain;

        std::vector<compiler::LocRef> relocatedTopLevelVars;

        std::unordered_map<std::string, bool> lowerAllOfThesePrivateNames;

        std::vector<compiler::Ref> tempLetsToDeclare;
        std::vector<tempRef> tempRefsToDeclare;
        std::vector<tempRef> topLevelTempRefsToDeclare;

        Lexer lexer;

        int parseExperimentalDecoratorNesting;

        int tempRefCount;
        int topLevelTempRefCount;

        int jsxSourceLoc;
        int jsxSourceLine;
        int jsxSourceColumn;

        compiler::Ref exportsRef;
        compiler::Ref requireRef;
        compiler::Ref moduleRef;
        compiler::Ref importMetaRef;
        compiler::Ref promiseRef;
        compiler::Ref regExpRef;
        compiler::Ref bigIntRef;
        compiler::Ref superCtorRef;

        std::unordered_map<std::string, compiler::LocRef> jsxRuntimeImports;
        std::unordered_map<std::string, compiler::LocRef> jsxLegacyImports;

        compiler::Ref weakMapRef;
        compiler::Ref weakSetRef;

        logger::Range esmImportStatementKeyword;
        logger::Range esmImportMeta;
        logger::Range esmExportKeyword;
        logger::Range enclosingClassKeyword;
        logger::Range topLevelAwaitKeyword;
        logger::Range liveTopLevelAwaitKeyword;

        logger::Loc latestArrowArgLoc;
        logger::Loc forbidSuffixAfterAsLoc;
        logger::Loc firstJSXElementLoc;

        fnOrArrowDataVisit fnOrArrowDataVisit;
        int singleStmtDepth;

        logger::Loc afterArrowBodyLoc;

        bool suppressWarningsAboutWeirdCode;

        bool isFileConsideredToHaveESMExports;
        bool isFileConsideredESM;

        bool hasNonLocalExportDeclareInsideNamespace;

        bool shouldFoldTypeScriptConstantExpressions;

        bool allowIn;
        bool hasTopLevelReturn;
        bool latestReturnHadSemicolon;
        bool messageAboutThisIsUndefined;
        bool isControlFlowDead;
        bool shouldAddKeyComment;

        bool willWrapModuleInTryCatchForUsing;

        // Records an export statement in the named exports map. This is called
        // during parsing to track which symbols are exported under which names.
        void recordExport(logger::Loc loc, const std::string &alias, compiler::Ref ref);

        // Checks whether a TypeScript import-equals statement is unused and
        // can be removed. Returns true if the import is eliminated.
        bool checkForUnusedTSImportEquals(SLocal *s, importsExportsScanResult *result);

        // Scans a statement list for unused TypeScript import-equals statements.
        importsExportsScanResult scanForUnusedTSImportEquals(std::vector<Stmt> stmts);

        // Scans a statement list for import and export statements, populating
        // the parser's import/export metadata tables.
        importsExportsScanResult scanForImportsAndExports(std::vector<Stmt> stmts);

        // Generates diagnostic notes for assert type="json" import assertions.
        std::vector<logger::MsgData> notesForAssertTypeJSON(compiler::ImportRecord *record, const std::string &alias);

        // Marks a symbol reference as ignored (not counted as a use). This is
        // used for symbols that appear in type positions or dead code.
        void ignoreUsage(compiler::Ref ref);

        // Converts a symbol use into a function call use, incrementing the call
        // count estimate for the symbol.
        void convertSymbolUseToCall(compiler::Ref ref, bool isSingleNonSpreadArgCall);

        // Handles an import expression with a glob pattern. The prefix and
        // assertOrWith configure the generated import record.
        Expr handleGlobPattern(Expr expr, compiler::ImportKind kind, compiler::ImportPhase phase, const std::string &prefix, const std::shared_ptr<compiler::ImportAssertOrWith> &assertOrWith);

        // Parses a glob pattern from an expression. Returns the parsed parts
        // and the source range.
        std::pair<std::vector<globPart>, logger::Range> globPatternFromExpr(Expr expr);

        // Determines why the current file is considered an ES module.
        std::pair<whyESM, std::vector<logger::MsgData>> whyESModule();

        // Determines why a scope is in strict mode.
        std::pair<std::string, std::vector<logger::MsgData>> whyStrictMode(Scope *scope);

        // Warns when a namespace import is called as a function, new, or JSX tag.
        void warnAboutImportNamespaceCall(Expr target, importNamespaceCallKind kind);

        // Applies statement-level minification optimizations to a list of
        // statements. This includes dead code elimination, if-statement
        // mangling, and switch-case optimization.
        std::vector<Stmt> mangleStmts(std::vector<Stmt> stmts, stmtsKind kind);

        // Warns about duplicate properties in object literals or class bodies.
        void warnAboutDuplicateProperties(std::vector<Property> &properties, duplicatePropertiesIn in);

        // Selects the appropriate local kind, potentially upgrading var to
        // let/const based on the current scope context.
        LocalKind selectLocalKind(LocalKind kind);

        // Pushes a new scope for the parse pass. Returns the index that can
        // be used later to pop or discard the scope.
        int pushScopeForParsePass(ScopeKind kind, logger::Loc loc);

        // Pops the current scope, removing it from the scope stack.
        void popScope();

        // Pops and discards a scope by index, removing it from the scope tree.
        void popAndDiscardScope(int scopeIndex);
        // Pops a scope and flattens its children into the parent scope.
        void popAndFlattenScope(int scopeIndex);

        // Discards all scopes up to (but not including) the given index.
        void discardScopesUpTo(int scopeIndex);

        // Allocates a new symbol with the given kind and name. The symbol is
        // not yet declared in any scope.
        compiler::Ref newSymbol(compiler::SymbolKind kind, const std::string &name);

        // Merges two symbols that refer to the same declaration (e.g., after
        // hoisting). Returns the surviving reference.
        compiler::Ref mergeSymbols(compiler::Ref old, compiler::Ref new_);

        // Determines whether two symbols can be merged in the given scope.
        mergeResult canMergeSymbols(Scope *scope, compiler::SymbolKind existing, compiler::SymbolKind new_);

        // Emits an error when a symbol is declared twice in the same scope.
        void addSymbolAlreadyDeclaredError(const std::string &name, logger::Loc newLoc, logger::Loc oldLoc);

        // Declares a symbol in the current scope. Returns a reference to the
        // declared symbol.
        compiler::Ref declareSymbol(compiler::SymbolKind kind, logger::Loc loc, const std::string &name);

        // Hoists var declarations from block scopes into the enclosing
        // function or module scope.
        void hoistSymbols(Scope *scope);

        // Declares all symbols in a binding pattern (including destructuring).
        void declareBinding(compiler::SymbolKind kind, Binding binding, parseStmtOpts opts);

        // Records a symbol reference as a use. This increments the use count
        // for the symbol.
        void recordUsage(compiler::Ref ref);

        // Ignores the identifier in a dot chain (e.g., obj.method — the "method"
        // part is not counted as a standalone identifier use).
        void ignoreUsageOfIdentifierInDotChain(Expr expr);

        // Generates a require() call for a runtime helper.
        Expr importFromRuntime(logger::Loc loc, const std::string &name);

        // Generates a function call to a runtime helper with the given arguments.
        Expr callRuntime(logger::Loc loc, const std::string &name, std::vector<Expr> &args);

        // Generates an import for a JSX runtime symbol (jsx, jsxs, Fragment, etc.).
        Expr importJSXSymbol(logger::Loc loc, JSXImport jsx);

        // Returns the expression to substitute for require() calls based on
        // the output format.
        Expr valueToSubstituteForRequire(logger::Loc loc);

        // Lazily creates and returns a reference to the Promise constructor.
        compiler::Ref makePromiseRef();

        // Lazily creates and returns a reference to the RegExp constructor.
        compiler::Ref makeRegExpRef();

        // Lazily creates and returns a reference to the BigInt constructor.
        compiler::Ref makeBigIntRef();

        // Stores a name string and returns a reference to it. The name is
        // recovered later with loadNameFromRef.
        compiler::Ref storeNameInRef(MaybeSubstring name);

        // Recovers a name previously stored with storeNameFromRef.
        std::string loadNameFromRef(compiler::Ref ref);

        // Logs deferred errors accumulated during expression parsing.
        void logExprErrors(deferredErrors *errors);

        // Logs deferred arrow argument errors.
        void logDeferredArrowArgErrors(deferredErrors *errors);

        // Logs arrow argument errors after the arrow body is confirmed.
        void logArrowArgErrors(deferredArrowArgErrors *errors);

        // Logs an error when nullish coalescing (??) is mixed with other
        // operators without explicit parenthesization.
        void logNullishCoalescingErrorPrecedenceError(const std::string &op);

        // Logs an error when an assignment targets a define-substituted name.
        void logAssignToDefine(logger::Range r, const std::string &name, Expr expr);

        // Checks for legacy octal numeric literals (e.g., 0777) and emits
        // a warning.
        void checkForLegacyOctalLiteral(E e);

        // Parses a string literal expression, handling escape sequences and
        // template literal semantics.
        Expr parseStringLiteral();

        // Parses a BigInt literal or falls back to a string if BigInt is not
        // supported by the target environment.
        Expr parseBigIntOrStringIfUnsupported();

        // Returns a human-readable name for a property key expression, used
        // in error messages.
        std::string keyNameForError(Expr key);

        // Returns true if the property name is eligible for mangling.
        bool isMangledProp(const std::string &name);

        // Returns or creates a symbol for a mangled property name.
        compiler::Ref symbolForMangledProp(const std::string &name);

        // Reports usage of a private class field or method.
        void reportPrivateNameUsage(const std::string &name);

        // Marks a syntax feature as unsupported for the target environment.
        // Returns true if a warning was emitted.
        bool markSyntaxFeature(compat::JSFeature feature, logger::Range range);

        // Marks an async function declaration. Returns true if a warning was
        // emitted for the target environment.
        bool markAsyncFn(logger::Range range, bool isGenerator);

        // Parses an expression at the given precedence level. This is the
        // main entry point for expression parsing.
        Expr parseExpr(L level);

        // Parses an expression or let/using binding. The deferredErrors are
        // populated if the parse encounters recoverable errors.
        Expr parseExprOrBindings(L level, deferredErrors *errors);

        // Skips a TypeScript type annotation at the given precedence level.
        void skipTypeScriptType(L level);

        // Skips a TypeScript type with additional flags controlling which
        // constructs are allowed.
        void skipTypeScriptTypeWithFlags(L level, skipTypeFlags flags);

        // Skips a TypeScript binding type (used in parameter type annotations).
        void skipTypeScriptBinding();

        // Skips TypeScript function argument types.
        void skipTypeScriptFnArgs();

        // Skips a TypeScript parenthesized or function type.
        void skipTypeScriptParenOrFnType();

        // Skips a TypeScript object type literal.
        void skipTypeScriptObjectType();

        // Attempts to skip TypeScript type parameters (e.g., <T>).
        ESkipTypeScript skipTypeScriptTypeParameters(typeParameterFlags flags);

        // Attempts to skip TypeScript arrow function arguments with
        // backtracking if the parse fails.
        bool trySkipTypeScriptArrowArgsWithBacktracking();

        // Attempts to skip the constraint of an infer type with backtracking.
        bool trySkipTypeScriptConstraintOfInferTypeWithBacktracking(skipTypeFlags flags);

        // Returns true if a type argument list can follow in expression position.
        bool tsCanFollowTypeArgumentsInExpression();

        // Returns true if the current token is a TypeScript binary operator.
        bool tsIsBinaryOperator();

        // Returns true if the current token starts a TypeScript expression.
        bool tsIsStartOfExpression();

        // Returns true if the current token starts a left-hand-side expression.
        bool tsIsStartOfLeftHandSideExpression();

        // Returns true if the next token is an open paren, less-than, or dot.
        bool tsLookAheadNextTokenIsOpenParenOrLessThanOrDot();

        // Returns true if the current token is a valid TypeScript identifier.
        bool tsIsIdentifier();

        // Saves the current source location for attaching expression comments.
        logger::Loc saveExprCommentsHere();

        // Parses statements until the given terminator token is reached.
        std::vector<Stmt> parseStmtsUpTo(T token, parseStmtOpts opts);

        // Parses a single statement based on the current token.
        Stmt parseStmt(parseStmtOpts opts);

        // Adds an import record for an import/require statement. Returns
        // the index of the new record.
        uint32_t addImportRecord(compiler::ImportKind kind, compiler::ImportPhase phase, logger::Range pathRange, const std::string &text, const std::shared_ptr<compiler::ImportAssertOrWith> &assertOrWith, compiler::ImportRecordFlags flags);

        // Parses a binding pattern (identifier, array destructuring, or object
        // destructuring).
        Binding parseBinding(parseBindingOpts opts);

        // Parses a property definition in an object literal or class body.
        // Returns the property and whether it was a shorthand.
        std::pair<Property, bool> parseProperty(logger::Loc startLoc, PropertyKind kind, propertyOpts opts, deferredErrors *errors);

        // Parses a property binding in a destructuring pattern.
        PropertyBinding parsePropertyBinding();

        // Checks whether the next token could be an arrow (=>) after the
        // current token. Used for backtracking in parenthesized expressions.
        bool checkForArrowAfterTheCurrentToken();

        // Parses a dot or mangled property access on a target expression.
        E *dotOrMangledPropParse(Expr target, MaybeSubstring name, logger::Loc nameLoc, OptionalChain optionalChain, wasOriginallyDotOrIndex original);

        // Visits a dot or mangled property access, applying define substitutions.
        E *dotOrMangledPropVisit(Expr target, const std::string &name, logger::Loc nameLoc);

        // Parses an async-prefixed expression (async function, async arrow).
        Expr parseAsyncPrefixExpr(logger::Range asyncRange, L level, exprFlag flags);

        // Parses a function expression (function keyword).
        Expr parseFnExpr(logger::Loc loc, bool isAsync, logger::Range asyncRange);

        // Parses a parenthesized expression, with backtracking for arrow functions.
        Expr parseParenExpr(logger::Loc loc, L level, parenExprOpts opts);

        // Parses a prefix expression (unary operators, await, yield, etc.).
        Expr parsePrefix(L level, deferredErrors *errors, exprFlag flags);

        // Parses decorator expressions (e.g., @decorator).
        std::vector<Decorator> parseDecorators(Scope *scope, logger::Range classKeyword, decoratorContextFlags context);

        // Parses a single decorator expression.
        Expr parseDecorator();

        // Parses a class expression (class keyword in expression position).
        Expr parseClassExpr(std::vector<Decorator> decorators);

        // Parses an expression with additional flags.
        Expr parseExprWithFlags(L level, exprFlag flags);

        // Parses the common expression suffix (member access, calls, etc.).
        Expr parseExprCommon(L level, deferredErrors *errors, exprFlag flags);

        // Parses suffix operators (++, --, member access, calls) on the left
        // operand.
        Expr parseSuffix(Expr left, L level, deferredErrors *errors, exprFlag flags);

        // Parses an expression statement that may be a let/using declaration.
        std::tuple<Expr, Stmt, std::vector<Decl>> parseExprOrLetOrUsingStmt(parseStmtOpts opts);

        // Emits an error when a lexical declaration is used in a forbidden
        // position (e.g., directly inside an if without braces).
        void forbidLexicalDecl(logger::Loc loc);

        // Emits an error when using declarations appear inside a switch case.
        void forbidUsingInSwitch(logger::Loc loc);

        // Parses and declares a list of declarations (let/const/var/using).
        std::vector<Decl> parseAndDeclareDecls(compiler::SymbolKind kind, parseStmtOpts opts);

        // Validates that certain declaration kinds have initializers.
        void requireInitializers(LocalKind kind, std::vector<Decl> decls);

        // Attempts to skip TypeScript type arguments in an expression with
        // backtracking.
        bool trySkipTypeArgumentsInExpressionWithBacktracking();

        // Skips TypeScript type arguments in a call expression.
        bool skipTypeScriptTypeArguments(skipTypeScriptTypeArgumentsOpts opts);

        // Skips a TypeScript type declaration statement (type, interface, etc.).
        void skipTypeScriptTypeStmt(parseStmtOpts opts);

        // Skips a TypeScript interface statement.
        void skipTypeScriptInterfaceStmt(parseStmtOpts opts);

        // Parses a TypeScript enum statement.
        Stmt parseTypeScriptEnumStmt(logger::Loc loc, parseStmtOpts opts);

        // Parses a TypeScript import-equals statement.
        Stmt parseTypeScriptImportEqualsStmt(logger::Loc loc, parseStmtOpts opts, logger::Loc defaultNameLoc, const std::string &defaultName);

        // Parses a TypeScript namespace statement.
        Stmt parseTypeScriptNamespaceStmt(logger::Loc loc, parseStmtOpts opts);

        // Gets or creates the exported members map for a TypeScript namespace.
        std::shared_ptr<std::unordered_map<std::string, TSNamespaceMember>> getOrCreateExportedNamespaceMembers(const std::string &name, bool isExport);

        // Generates the IIFE closure for a TypeScript namespace declaration.
        std::vector<Stmt> generateClosureForTypeScriptNamespaceOrEnum(std::vector<Stmt> stmts, logger::Loc stmtLoc, bool isExport, logger::Loc nameLoc, compiler::Ref nameRef, compiler::Ref argRef, std::vector<Stmt> stmtsInsideClosure);

        // Generates the IIFE closure for a TypeScript enum declaration.
        std::vector<Stmt> generateClosureForTypeScriptEnum(std::vector<Stmt> stmts, logger::Loc stmtLoc, bool isExport, logger::Loc nameLoc, compiler::Ref nameRef, compiler::Ref argRef, std::vector<Expr> exprsInsideClosure, bool allValuesArePure);

        // Parses a JSX namespaced name (e.g., xml:lang).
        std::pair<logger::Range, MaybeSubstring> parseJSXNamespacedName();

        // Parses a JSX tag name, returning the range, tag name, and tag expression.
        std::tuple<logger::Range, std::string, Expr> parseJSXTag();

        // Emits an error when initializers appear in for-in/for-of variable
        // declarations.
        void forbidInitializers(std::vector<Decl> decls, const std::string &loopType, bool isVar);

        // Parses a clause alias (the "as name" part of import/export clauses).
        MaybeSubstring parseClauseAlias(const std::string &kind);

        // Parses an import clause (named imports, default import, namespace import).
        std::pair<std::vector<ClauseItem>, bool> parseImportClause();

        // Parses an export clause (named exports).
        std::pair<std::vector<ClauseItem>, bool> parseExportClause();

        // Checks whether an identifier name cannot be represented in the output
        // format and emits a warning.
        void checkForUnrepresentableIdentifier(logger::Loc loc, const std::string &name);

        // Parses a class declaration statement.
        Stmt parseClassStmt(logger::Loc loc, parseStmtOpts opts);

        // Parses a full class body.
        Class parseClass(logger::Range classKeyword, compiler::LocRef *name, parseClassOpts classOpts);

        // Parses an optional label name.
        compiler::LocRef *parseLabelName();

        // Parses an import/require path string, returning the range, path text,
        // import assertions, and flags.
        std::tuple<logger::Range, std::string, compiler::ImportAssertOrWith *, compiler::ImportRecordFlags> parsePath();

        // Parses and discards an import path (for syntax validation only).
        void parsePathAndDiscard();

        // Warns about the deprecated "assert" keyword in import assertions.
        void maybeWarnAboutAssertKeyword(logger::Loc loc);

        // Parses a function declaration statement.
        Stmt parseFnStmt(logger::Loc loc, parseStmtOpts opts, bool isAsync, logger::Range asyncRange);

        // Parses a function body (formal parameters + body block).
        std::pair<Fn, bool> parseFn(compiler::LocRef *name, logger::Range classKeyword, decoratorContextFlags decoratorContext, struct fnOrArrowDataParse data);

        // Parses the body of a function (block or expression body).
        FnBody parseFnBody(struct fnOrArrowDataParse data);

        // Parses the body of an arrow function.
        EArrow *parseArrowBody(std::vector<Arg> args, struct fnOrArrowDataParse data);

        // Parses the argument list of a function call.
        std::tuple<std::vector<Expr>, logger::Loc, bool> parseCallArgs();

        // Parses a yield expression.
        Expr parseYieldExpr(logger::Loc loc);

        // Returns true if the next tokens could start a binding pattern.
        bool willNeedBindingPattern();

        // Parses an import() expression.
        Expr parseImportExpr(logger::Loc loc, L level);

        // Parses a JSX element expression.
        Expr parseJSXElement(logger::Loc loc);

        // Parses the parts of a template literal (substitutions and tail text).
        std::pair<std::vector<TemplatePart>, logger::Loc> parseTemplateParts(bool includeRaw);

        // Converts an expression to a binding pattern and initializer.
        std::tuple<Binding, Expr, invalidLog> convertExprToBindingAndInitializer(Expr expr, invalidLog invalidLog, bool isSpread);

        // Converts an expression to a binding pattern.
        std::pair<Binding, invalidLog> convertExprToBinding(Expr expr, invalidLog invalidLog);

        // Validates that a function name is legal in its context.
        void validateFunctionName(const Fn &fn, fnKind kind);

        // Validates that a declared symbol name is legal (not a reserved word
        // in strict mode, etc.).
        void validateDeclaredSymbolName(logger::Loc loc, const std::string &name);

        // Generates a temporary reference for use during lowering. The declare
        // flag controls whether a variable declaration is emitted.
        compiler::Ref generateTempRef(generateTempRefArg declare, const std::string &optionalName);

        // Generates a temporary reference scoped to the top level.
        compiler::Ref generateTopLevelTempRef();

        // Pushes a new scope for the visit pass.
        void pushScopeForVisitPass(ScopeKind kind, logger::Loc loc);

        // Looks up a symbol by name in the current scope chain.
        findSymbolResult findSymbol(logger::Loc loc, const std::string &name);

        // Looks up a label symbol by name in the current scope chain.
        std::tuple<compiler::Ref, bool, bool> findLabelSymbol(logger::Loc loc, const std::string &name);

        // Records a symbol as declared in the current part for tree-shaking.
        void recordDeclaredSymbol(compiler::Ref ref);

        // Visits a list of statements and prepends any temporary variable
        // declarations that were generated during lowering.
        std::vector<Stmt> visitStmtsAndPrependTempRefs(std::vector<Stmt> stmts, prependTempRefsOpts opts);

        // Appends a new part to the parts list, flushing any pending temp refs.
        void appendPart(std::vector<Part>& parts, std::vector<Stmt> stmts);

        // Prepares the parser for the visit pass by resetting visit-specific state.
        void prepareForVisitPass();

        // Declares a CommonJS module.exports or require symbol.
        compiler::Ref declareCommonJSSymbol(compiler::SymbolKind kind, const std::string &name);

        // Computes character frequency across the source for name mangling.
        compiler::CharFreq *computeCharacterFrequency();

        // Generates an import statement for a dependency.
        std::pair<std::vector<Part>, uint32_t> generateImportStmt(const std::string &path, logger::Range pathRange, std::vector<std::string> imports, std::vector<Part> parts, std::unordered_map<std::string, compiler::LocRef> symbols, uint32_t *sourceIndex, uint32_t *copySourceIndex);

        // Converts the parsed AST into the final AST structure.
        AST toAST(std::vector<Part> before, std::vector<Part> parts, std::vector<Part> after, const std::string &hashbang, std::vector<std::string> directives);

        // Returns true if using declarations should be lowered in this file.
        bool shouldLowerUsingDeclarations(std::vector<Stmt> stmts);

        // Returns true if the output format requires strict mode.
        bool isStrictModeOutputFormat();

        // Creates a new using declaration lowering context.
        lowerUsingDeclarationContext lowerUsingDeclarationContext();

        // Lowers using declarations in a for-of loop.
        void lowerUsingDeclarationInForOf(logger::Loc loc, SLocal *init, Stmt *body);

        // Visits a list of statements, performing lowering and validation.
        std::vector<Stmt> visitStmts(std::vector<Stmt> stmts, stmtsKind kind);

        // Visits a single statement.
        Stmt visitSingleStmt(Stmt stmt, stmtsKind kind);

        // Visits a loop body, applying loop-specific lowering.
        Stmt visitLoopBody(Stmt stmt);

        // Visits a for-loop initializer, applying special lowering for
        // for-in/for-of.
        Stmt visitForLoopInit(Stmt stmt, bool isInOrOf);

        // Visits an expression, applying all lowering passes.
        Expr visitExpr(Expr expr);

        // Preserves the original name of a function or class expression by
        // wrapping it in an assignment.
        Expr keepExprSymbolName(Expr value, const std::string &name);

        // Preserves the original name of a class or function statement.
        Stmt keepClassOrFnSymbolName(logger::Loc loc, Expr expr, const std::string &name);

        // Visits a binding pattern, declaring symbols and checking for errors.
        void visitBinding(Binding binding, bindingOpts opts);

        // Visits an expression with full input/output context.
        std::pair<Expr, exprOut> visitExprInOut(Expr expr, exprIn in);

        // Lowers a function body, applying async/generator transformations.
        void lowerFunction(bool *isAsync, bool *isGenerator, std::vector<Arg> *args, logger::Loc bodyLoc, SBlock *body, bool *preferExpr, bool *hasRestArg, bool isArrow);

        // Lowers a class declaration/expression, generating the compiled form.
        std::pair<std::vector<Stmt>, Expr> lowerClass(Stmt stmt, Expr expr, visitClassResult info, std::string nameToKeep);

        // Lowers a private field assignment operator (e.g., #x += 1).
        Expr lowerPrivateSetBinOp(Expr target, logger::Loc loc, EPrivateIdentifier *priv, OpCode op, Expr value);

        // Lowers a super property assignment operator (e.g., super.x += 1).
        Expr lowerSuperPropertySetBinOp(logger::Loc loc, Expr property, OpCode op, Expr value);

        // Lowers super property or private field assignment when it appears
        // as the target of a compound assignment.
        std::pair<Expr, bool> lowerSuperPropertyOrPrivateInAssign(Expr expr);

        // Inserts statements after a super() call in a derived class constructor.
        void insertStmtsAfterSuperCall(FnBody *body, std::vector<Stmt> stmtsToInsert, compiler::Ref superCtorRef);

        // Extracts a property name hint from a key expression for diagnostics.
        std::string propertyNameHint(Expr key);

        // Lowers object spread syntax (e.g., { ...obj }) to Object.assign.
        Expr lowerObjectSpread(logger::Loc loc, EObject *e);

        // Lowers template literal tagged templates to function calls.
        Expr lowerTemplateLiteral(logger::Loc loc, ETemplate *e, std::function<Expr()> tagThisFunc, std::function<Expr(Expr)> tagWrapFunc);

        // Lowers a parenthesized optional chain call.
        Expr lowerParenthesizedOptionalChain(logger::Loc loc, ECall *e, exprOut out);

        // Lowers an await expression, potentially wrapping in a helper call.
        std::pair<Expr, exprOut> maybeLowerAwait(logger::Loc loc, EAwait *e);

        // Lowers super property access inside a call expression.
        void maybeLowerSuperPropertyGetInsideCall(ECall *e);

        // Resolves the value of `this` for the current context. Returns the
        // expression and whether a wrapper is needed.
        std::pair<Expr, bool> valueForThis(logger::Loc loc, bool shouldLog, AssignTarget assignTarget, bool isCallTarget, bool isDeleteTarget);

        // Resolves the value of import.meta for the current output format.
        std::pair<Expr, bool> valueForImportMeta(logger::Loc loc);

        // Returns true if the expression is a valid assignment target.
        bool isValidAssignmentTarget(Expr expr);

        // Instantiates an injected dot name as an expression.
        Expr instantiateInjectDotName(logger::Loc loc, injectedDotName name, AssignTarget assignTarget);

        // Instantiates a define expression as an expression.
        Expr instantiateDefineExpr(logger::Loc loc, config::DefineExpr expr, identifierOpts opts);

        // Returns true if an expression matches a dot-separated define pattern.
        bool isDotOrIndexDefineMatch(Expr expr, std::vector<std::string> parts);

        // Potentially rewrites a property access based on define substitutions.
        std::pair<Expr, bool> maybeRewritePropertyAccess(logger::Loc loc, AssignTarget assignTarget, bool isDeleteTarget, Expr target, const std::string &name, logger::Loc nameLoc, bool isCallTarget, bool isTemplateTag, bool preferQuotedKey);

        // Returns true if a super property access should be lowered.
        bool shouldLowerSuperPropertyAccess(Expr expr);

        // Lowers a super property get expression.
        Expr lowerSuperPropertyGet(logger::Loc loc, Expr key);

        // Lowers an optional chain expression (?.).
        std::pair<Expr, exprOut> lowerOptionalChain(Expr expr, exprIn in, exprOut childOut);

        // Returns true if a private symbol needs to be lowered (i.e., it
        // is not natively supported by the target environment).
        bool privateSymbolNeedsToBeLowered(EPrivateIdentifier *priv);

        // Lowers a private field read access.
        Expr lowerPrivateGet(Expr target, logger::Loc loc, EPrivateIdentifier *priv);

        // Extracts a private identifier from an expression (e.g., from
        // a bracket access like obj[#x]).
        std::tuple<Expr, logger::Loc, EPrivateIdentifier *> extractPrivateIndex(Expr expr);

        // Lowers a private field update expression (e.g., #x++).
        Expr lowerPrivateSetUnOp(Expr target, logger::Loc loc, EPrivateIdentifier *priv, OpCode op);

        // Extracts a super property access expression from an expression tree.
        Expr extractSuperProperty(Expr expr);

        // Generates a call to a super property wrapper function.
        Expr callSuperPropertyWrapper(logger::Loc loc, Expr property);

        // Lowers an assignment expression, handling destructuring and rest.
        std::pair<Expr, bool> lowerAssign(Expr rootExpr, Expr rootInit, objRestMode mode);

        // Lowers a nullish coalescing (??) expression to a runtime helper.
        Expr lowerNullishCoalescing(logger::Loc loc, Expr left, Expr right);

        // Lowers a private brand check (e.g., #x in obj).
        Expr lowerPrivateBrandCheck(Expr target, logger::Loc loc, EPrivateIdentifier *priv);

        // Lowers a private field write.
        Expr lowerPrivateSet(Expr target, logger::Loc loc, EPrivateIdentifier *priv, Expr value);

        // Lowers a super property write.
        Expr lowerSuperPropertySet(logger::Loc loc, Expr target, Expr value);

        // Lowers an exponentiation assignment (**=).
        Expr lowerExponentiationAssignmentOperator(logger::Loc loc, EBinary *e);

        // Lowers a nullish coalescing assignment (??=).
        std::pair<Expr, bool> lowerNullishCoalescingAssignmentOperator(logger::Loc loc, EBinary *e);

        // Lowers a logical assignment (&&= or ||=).
        std::pair<Expr, bool> lowerLogicalAssignmentOperator(logger::Loc loc, EBinary *e, OpCode op);

        // Lowers a set operation with binary operator (e.g., += on private fields).
        Expr maybeLowerSetBinOp(Expr left, OpCode op, Expr right);

        // Lowers an assignment operator by wrapping with a callback.
        Expr lowerAssignmentOperator(Expr value, const std::function<Expr(Expr, Expr)> &callback);

        // Lowers a for-await-of loop to a synchronous for-of with Symbol.asyncIterator.
        std::vector<Stmt> lowerForAwaitLoop(logger::Loc loc, SForOf *loop, std::vector<Stmt> stmts);

        // Lowers object rest elements in variable declarations.
        std::vector<Decl> lowerObjectRestInDecls(std::vector<Decl> decls);

        // Lowers object rest elements in a for-loop initializer.
        void lowerObjectRestInForLoopInit(Stmt init, Stmt *body);

        // Lowers object rest elements in a catch binding.
        void lowerObjectRestInCatchBinding(Catch *catch_);

        // Lowers object rest elements to temporary declarations.
        std::pair<std::vector<Decl>, bool> lowerObjectRestToDecls(Expr rootExpr, Expr rootInit, std::vector<Decl> decls);

        // Helper for lowering object rest patterns.
        std::pair<std::function<Expr(Expr)>, bool> lowerObjectRestHelper(Expr rootExpr, Expr rootInit, const std::function<void(Expr, Expr)> &assign, generateTempRefArg declare, objRestMode mode);

        // Captures the key expression for object rest pattern extraction.
        std::pair<Expr, std::function<Expr()>> captureKeyForObjectRest(Expr originalKey);

        // Marks known global constructors (Array, Object, etc.) as pure
        // for tree-shaking purposes.
        void maybeMarkKnownGlobalConstructorAsPure(ENew *e);

        // Handles an identifier reference, applying define substitutions,
        // variable capture, and strict mode checks.
        Expr handleIdentifier(logger::Loc loc, std::shared_ptr<EIdentifier> e, identifierOpts opts);

        // Captures the `arguments` object into a local variable.
        compiler::Ref captureArguments();

        // Captures the `this` value into a local variable.
        compiler::Ref captureThis();

        // Captures a value that may have side effects, returning a getter
        // and optional setter function.
        std::pair<std::function<Expr()>, std::function<Expr(Expr)>> captureValueWithPossibleSideEffects(logger::Loc loc, int count, Expr value, captureValueMode mode);

        // Wraps an expression with an inline enum comment for source maps.
        Expr wrapInlinedEnum(Expr value, const std::string &comment);

        // Checks whether a regular expression is unsupported in the target
        // environment. Returns (isSupported, value, flags).
        std::tuple<std::string, std::string, bool> isUnsupportedRegularExpression(logger::Loc loc, const std::string &value);

        // Returns true if the current scope is in strict mode.
        bool isStrictMode();

        // Visits and appends a statement, performing lowering and validation.
        std::vector<Stmt> visitAndAppendStmt(std::vector<Stmt> stmts, Stmt stmt);

        // Visits a function declaration/expression for validation and lowering.
        void visitFn(Fn *fn, logger::Loc scopeLoc, visitFnOpts opts);

        // Visits function arguments for validation and lowering.
        void visitArgs(std::vector<Arg> &args, visitArgsOpts opts);

        // Visits a class for validation and lowering.
        visitClassResult visitClass(logger::Loc nameScopeLoc, Class *cls, compiler::Ref defaultNameRef, std::string nameToKeep);

        // Computes which class fields need to be lowered based on the target
        // environment.
        classLoweringInfo computeClassLoweringInfo(Class *cls);

        // Visits decorator expressions for validation and lowering.
        std::vector<Decorator> visitDecorators(std::vector<Decorator> decorators, Scope *decoratorScope);

        // Minifies a switch statement by inlining single-expression case bodies.
        std::vector<Stmt> minifySwitchStmt(logger::Loc loc, std::shared_ptr<SSwitch> s, std::vector<Stmt> stmts);

        // Warns about equality checks that may be bugs (e.g., comparing a
        // function to a string).
        bool warnAboutEqualityCheck(const std::string &op, Expr value, logger::Loc afterOpLoc);

        // Warns about typeof comparisons with string literals that may be
        // misspelled.
        void warnAboutTypeofAndString(Expr a, Expr b, typeofStringOrder order);

        // Relocates variable declarations to the top level of the function.
        std::pair<Stmt, bool> maybeRelocateVarsToTopLevel(std::vector<Decl> decls, relocateVarsMode mode);

        // Substitutes a single-use symbol in a statement.
        bool substituteSingleUseSymbolInStmt(Stmt stmt, compiler::Ref ref, Expr replacement);

        // Substitutes a single-use symbol in an expression.
        std::pair<Expr, substituteStatus> substituteSingleUseSymbolInExpr(Expr expr, compiler::Ref ref, Expr replacement, bool replacementCanBeRemoved);

        // Attempts to inline an IIFE (immediately invoked function expression).
        std::pair<Expr, bool> maybeInlineIIFE(logger::Loc loc, ECall *call);

        // Returns true if an IIFE can be removed if its result is unused.
        bool iifeCanBeRemovedIfUnused(std::vector<Arg> args, FnBody body);

        // Transposes an if-expression chain by applying a visitor to each branch.
        Expr maybeTransposeIfExprChain(Expr expr, const std::function<Expr(Expr)> &visit);

        // Mangs an if-statement by inlining single-expression branches.
        std::vector<Stmt> mangleIf(std::vector<Stmt> stmts, logger::Loc loc, std::shared_ptr<SIf> s);

        // Marks a strict mode feature violation and emits the appropriate error.
        void markStrictModeFeature(strictModeFeature feature, logger::Range r, const std::string &text);

        // Returns true if the current TypeScript arrow function is followed by
        // a JSX element.
        bool isTSArrowFnJSX();

        // Attempts to skip TypeScript type parameters followed by an open paren
        // with backtracking.
        ESkipTypeScript trySkipTypeScriptTypeParametersThenOpenParenWithBacktracking();

        // Returns true if the current token is a TypeScript arrow return type
        // after a question mark and before a colon.
        bool isTypeScriptArrowReturnTypeAfterQuestionAndBeforeColon(awaitOrYield await);

        // Attempts to skip a TypeScript arrow return type with backtracking.
        bool trySkipTypeScriptArrowReturnTypeWithBacktracking();

        // Skips a TypeScript return type annotation.
        void skipTypeScriptReturnType();

        // Marks an expression as parenthesized for the optimizer.
        void markExprAsParenthesized(Expr value, logger::Loc loc, bool isAsync);

        // Remaps expression locations in a JSON AST for source map corrections.
        void remapExprLocsInJSON(Expr *expr, std::vector<logger::StringInJSTableEntry> table);

        // Parses a JSON value, returning the expression and whether parsing
        // succeeded.
        static std::pair<Expr, bool> ParseJSON(logger::Log log, logger::Source source, JSONOptions options);

        // Returns true if the expression is a valid JSON value (no function
        // calls, no identifiers, etc.).
        bool IsValidJSON(const Expr& value);
    };


    // -----------------------------------------------------------------------
    // Free functions (also declared as member functions above)
    // -----------------------------------------------------------------------

    // Potentially rewrites a property access based on define substitutions.
    // This is the free-function version of Parser::maybeRewritePropertyAccess.
    std::pair<Expr, bool> maybeRewritePropertyAccess(logger::Loc loc, AssignTarget assignTarget, bool isDeleteTarget, Expr target, const std::string &name, logger::Loc nameLoc, bool isCallTarget, bool isTemplateTag, bool preferQuotedKey);

    // Returns true if a super property access should be lowered. Free-function
    // version of Parser::shouldLowerSuperPropertyAccess.
    bool shouldLowerSuperPropertyAccess(Expr expr);

    // Lowers a super property get expression. Free-function version of
    // Parser::lowerSuperPropertyGet.
    Expr lowerSuperPropertyGet(logger::Loc loc, Expr key);

    // Lowers an optional chain expression. Free-function version of
    // Parser::lowerOptionalChain.
    std::pair<Expr, exprOut> lowerOptionalChain(Expr expr, exprIn in, exprOut childOut);

    // Returns true if a private symbol needs to be lowered. Free-function
    // version of Parser::privateSymbolNeedsToBeLowered.
    bool privateSymbolNeedsToBeLowered(EPrivateIdentifier *priv);

    // Lowers a private field read access. Free-function version of
    // Parser::lowerPrivateGet.
    Expr lowerPrivateGet(Expr target, logger::Loc loc, EPrivateIdentifier *priv);


    // -----------------------------------------------------------------------
    // Switch statement analysis
    // -----------------------------------------------------------------------

    // Analyzes switch statement cases for liveness. Returns per-case liveness
    // information used for dead-code elimination.
    std::vector<switchCaseLiveness> analyzeSwitchCasesForLiveness(SSwitch *s);

    // Returns true if the given statements could cause fall-through to the
    // next case (i.e., they don't end with a jump statement).
    bool caseBodyCouldHaveFallThrough(std::vector<Stmt> stmts);

    // Attempts to inline a single-expression case body into the switch header.
    // Returns the inlined statements and whether inlining was successful.
    std::pair<std::vector<Stmt>, bool> tryToInlineCaseBody(logger::Loc openBraceLoc, std::vector<Stmt> stmts, logger::Loc closeBraceLoc);

    // Returns true if the statement is a jump statement (return, throw,
    // break, continue).
    bool isJumpStatement(S *data);

    // Returns a help text string for a JSX tag or fragment name.
    std::string tagOrFragmentHelpText(const std::string &tag);

    // Returns true if the identifier name is "eval" or "arguments".
    bool isEvalOrArguments(const std::string &name);

    // Returns true if the text contains a closing </script> tag.
    bool containsClosingScriptTag(std::string text);

    // Returns the source location after a binary operator.
    logger::Loc locAfterOp(EBinary *e);

    // Returns true if two jump statements are structurally identical.
    bool jumpStmtsLookTheSame(S *left, S *right);

    // Returns true if the statement affects scope (e.g., contains a
    // with statement or catch binding).
    bool stmtCaresAboutScope(Stmt stmt);

    // Drops the first statement from a body, replacing it with the given
    // statement if provided.
    Stmt dropFirstStatement(Stmt body, Stmt replaceOrNil);

    // Mangs a for-loop by reducing its verbosity.
    void mangleFor(SFor *s);

    // Returns true if the expression is safe to use before a const local
    // prefix (i.e., it has no side effects).
    bool isSafeForConstLocalPrefix(Expr expr);

    // Returns true if any of the statements affect scope.
    bool stmtsCareAboutScope(std::vector<Stmt> stmts);

    // Converts a list of statements into a single statement, wrapping in
    // braces if necessary.
    Stmt stmtsToSingleStmt(logger::Loc loc, std::vector<Stmt> stmts, logger::Loc closeBraceLoc);

    // Appends statements to an if or label body, preserving scope by
    // wrapping in braces when needed.
    std::vector<Stmt> appendIfOrLabelBodyPreservingScope(std::vector<Stmt> stmts, Stmt body);

    // Returns true if the define value can be used as an assignment target.
    bool defineValueCanBeUsedInAssignTarget(const E &data);

    // Returns true if the parameter list is a simple parameter list (no
    // destructuring, no defaults, no rest).
    bool isSimpleParameterList(std::vector<Arg> args, bool hasRestArg);

    // Returns the location of a "use strict" directive in a function body,
    // and whether one was found.
    std::pair<logger::Loc, bool> fnBodyContainsUseStrict(std::vector<Stmt> body);

    // Creates a new Parser instance with the given configuration.
    Parser *newParser(logger::Log log, logger::Source source, Lexer lexer, Options *options);

    // Parses a JavaScript/TypeScript source file into an AST. This is the
    // main entry point for the parser. Returns the AST and whether parsing
    // succeeded without errors.
    std::pair<AST, bool> Parse(logger::Log log, logger::Source source, Options options);

    // Creates an AST for a lazy export expression. The expression is
    // evaluated at link time rather than parse time.
    AST LazyExportAST(logger::Log log, logger::Source source, Options options, Expr expr, HelperCall *helperCall);

    // Creates an AST for a glob pattern resolution. The import records
    // are expanded to match the resolved files.
    AST GlobResolveAST(logger::Log log, logger::Source source, Options options, std::vector<compiler::ImportRecord> importRecords, std::shared_ptr<EObject> object, const std::string &name);

    // Parses a define expression from a string. Returns the parsed
    // expression and whether parsing succeeded.
    std::pair<config::DefineExpr, std::shared_ptr<E>> ParseDefineExpr(const std::string &text);

    // Returns the sorted keys of a map from string to LocRef.
    std::vector<std::string> sortedKeysOfMapStringLocRef(const std::unordered_map<std::string, compiler::LocRef> &in);


    // Options for JSON parsing. Controls feature support, flavor, and
    // error reporting.
    struct JSONOptions {
        // Features the parser uses to decide whether the object key "__proto__"
        // must be made into a computed property for compatibility.
        compat::JSFeature unsupported_js_features{};

        // Whether the input should be parsed as JSON or as TypeScript's JSON
        // superset.
        JSONFlavor flavor{JSONFlavor::kJSON};

        // Added to the end of all error messages for context.
        std::string error_suffix;

        // Controls whether big integer literals are allowed. If false, they
        // are treated as a parse error.
        bool is_for_define{};
    };

    // Parses a global name such as "Math.max" or "import.meta". Returns the
    // dot-separated components and whether parsing succeeded.
    std::pair<std::vector<std::string>, bool> ParseGlobalName(logger::Log log, logger::Source source);

    // Parses an inline source map from a comment. Returns the parsed source
    // map data, or nullptr if no source map was found.
    std::unique_ptr<sourcemap::SourceMapData> ParseSourceMap(logger::Log log, logger::Source source);

}
