#pragma once

#include <string_view>
#include <string>

#include <optional>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>

#include <any>
#include <atomic>
#include <functional>
#include <regex>
#include <variant>

#include "guchho/compiler.hpp"
#include "guchho/compat.hpp"
#include "guchho/javascript/js_ast.hpp"

namespace guchho::config {

    // DefineFlags
    // -----------
    // Bitfield describing how a user-defined `--define` value may be
    // optimised during the build.  Multiple flags can be combined via
    // bitwise OR.
    //
    //   kCanBeRemovedIfUnused              The entire define may be
    //                                      eliminated when nothing reads it.
    //   kCallCanBeUnwrappedIfUnused        A call expression like
    //                                      `define(...)` may be reduced to
    //                                      just the argument when unused.
    //   kMethodCallsMustBeReplacedWithUndefined  Method calls on the
    //                                      define (e.g. `define.foo()`) must
    //                                      become `undefined.foo()` to
    //                                      preserve side-effect semantics.
    //   kIsSymbolInstance                 The define refers to a Symbol
    //                                      instance and must not be folded
    //                                      into a string literal.
    enum class DefineFlags : uint8_t {
        kNone = 0,
        kCanBeRemovedIfUnused = 1 << 0,
        kCallCanBeUnwrappedIfUnused = 1 << 1,
        kMethodCallsMustBeReplacedWithUndefined = 1 << 2,
        kIsSymbolInstance = 1 << 3,
    };

    inline DefineFlags operator|(DefineFlags a, DefineFlags b) {
        return static_cast<DefineFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline DefineFlags operator&(DefineFlags a, DefineFlags b) {
        return static_cast<DefineFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    // Has
    // ---
    // Returns true when `flags` contains the single `flag` bit.
    //
    // Input:  flags = kCanBeRemovedIfUnused | kCallCanBeUnwrappedIfUnused
    //         flag  = kCanBeRemovedIfUnused
    // Output: true
    //
    // Input:  flags = kNone
    //         flag  = kCanBeRemovedIfUnused
    // Output: false
    inline bool Has(DefineFlags flags, DefineFlags flag) {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
    }

    // DefineExpr
    // ----------
    // Holds the replacement expression for a single `--define` entry.
    // Exactly one of the three members is active:
    //
    //   Constant       A literal JavaScript expression (number, string,
    //                  boolean, etc.) stored as a JS AST node.
    //   Parts          A dotted identifier path like ["process", "env"]
    //                  that is resolved at link time.
    //   InjectedDefineIndex  An index into the InjectedFiles table when
    //                  the define's value comes from an injected file.
    //
    // Input:  Constant = AST for `true`, Parts = {}
    // Output: HasConstant() == true
    //
    // Input:  Constant = nullptr, Parts = {"process", "env"}
    // Output: HasConstant() == false
    struct DefineExpr {
        guchho::javascript::E Constant{};
        
        std::vector<std::string> Parts{};
        compiler::Index32 InjectedDefineIndex{};

        // HasConstant
        // -----------
        // Returns true when this expression holds a constant AST node
        // rather than a dotted path or injected-file reference.
        bool HasConstant() const {
            return std::visit([](const auto& ptr) { return ptr != nullptr; }, Constant);
        }
    };

    // DefineData
    // ----------
    // One user-configurable define entry.  `KeyParts` holds the
    // dotted name (e.g. {"process", "env", "NODE_ENV"}), and
    // `DefineExprData` is the replacement expression.  `Flags`
    // controls optimisation eligibility.
    struct DefineData {
        std::vector<std::string> KeyParts{};
        std::shared_ptr<DefineExpr> DefineExprData{};
        DefineFlags Flags{};
    };

    // MergeDefineData
    // ----------------
    // Combines an older define with a newer one.  The newer value
    // always takes precedence; flags are OR-ed together so that
    // optimisation bits from either definition are preserved.
    //
    // Input:  old = {key="a.b", expr="1"}, new = {key="a.b", expr="2"}
    // Output: {key="a.b", expr="2"}  (new wins)
    DefineData MergeDefineData(DefineData old, DefineData new_data);

    // ProcessedDefines
    // -----------------
    // Two lookup tables built from the user's `--define` entries:
    //
    //   IdentifierDefines  Plain identifier replacements, keyed by
    //                      the joined name (e.g. "process.env.NODE_ENV").
    //   DotDefines         Dotted-path replacements, keyed by each
    //                      segment so that partial matches can be found
    //                      during property access resolution.
    //
    // Input:  userDefines = [{key=["DEBUG"], expr="true"}]
    // Output: IdentifierDefines["DEBUG"] = {expr="true"}
    struct ProcessedDefines {
        std::unordered_map<std::string, DefineData> IdentifierDefines{};
        std::unordered_map<std::string, std::vector<DefineData>> DotDefines{};
    };

    // ProcessDefines
    // ---------------
    // Normalises the user-supplied define list into the two lookup
    // tables that Guchho consults during bundling.  Dotted names are
    // split into parts and indexed under every prefix so that
    // `process.env.NODE_ENV` can be resolved when only `process.env`
    // is encountered.
    //
    // Input:  [{KeyParts=["a","b"], expr="1"}]
    // Output: IdentifierDefines["a.b"] = {expr="1"}
    //         DotDefines["a"] = [{...}]
    //         DotDefines["a.b"] = [{...}]
    ProcessedDefines ProcessDefines(const std::vector<DefineData>& userDefines);

    // MaybeBool
    // ---------
    // A tri-state boolean for options that can be explicitly true,
    // explicitly false, or left at their default.  This lets a
    // tsconfig.json file override only the fields it cares about
    // without forcing the others into a particular state.
    enum class MaybeBool : uint8_t {
        kUnspecified,
        kTrue,
        kFalse,
    };

    // TSJSX
    // -----
    // How Guchho handles JSX in TypeScript inputs.  Each variant
    // selects a different JSX transform and runtime import:
    //
    //   kNone         Do not parse JSX at all.
    //   kPreserve     Leave JSX syntax untouched in the output.
    //   kReactNative  Use React Native's custom JSX transform.
    //   kReact        Use the classic `React.createElement` transform.
    //   kReactJSX     Use the automatic runtime (`jsx-runtime`).
    //   kReactJSXDev  Use the automatic runtime in development mode
    //                 (`jsx-dev-runtime`).
    enum class TSJSX : uint8_t {
        kNone,
        kPreserve,
        kReactNative,
        kReact,
        kReactJSX,
        kReactJSXDev,
    };

    // TSImportsNotUsedAsValues
    // -------------------------
    // Controls what happens to TypeScript `import` statements that
    // are only used as type annotations:
    //
    //   kNone     Use the default behaviour (remove type-only imports).
    //   kRemove   Strip type-only imports entirely.
    //   kPreserve Keep type-only imports in the output.
    //   kError    Emit an error for type-only imports.
    enum class TSImportsNotUsedAsValues : uint8_t {
        kNone,
        kRemove,
        kPreserve,
        kError,
    };

    // TSUnusedImportFlags
    // --------------------
    // Bitfield controlling how unused TypeScript imports are treated
    // during the emit phase:
    //
    //   kKeepStmt    Keep the import statement even if all bindings
    //                are unused (useful for side-effect imports).
    //   kKeepValues  Keep value bindings that are unused but may have
    //                side effects (e.g. enum declarations).
    enum class TSUnusedImportFlags : uint8_t {
        kNone = 0,
        kKeepStmt = 1 << 0,
        kKeepValues = 1 << 1,
    };

    inline TSUnusedImportFlags operator|(TSUnusedImportFlags a, TSUnusedImportFlags b) {
        return static_cast<TSUnusedImportFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline TSUnusedImportFlags operator&(TSUnusedImportFlags a, TSUnusedImportFlags b) {
        return static_cast<TSUnusedImportFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    // Has
    // ---
    // Returns true when `flags` contains the single `flag` bit.
    inline bool Has(TSUnusedImportFlags flags, TSUnusedImportFlags flag) {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
    }

    // TSTarget
    // --------
    // The TypeScript compilation target tier, split at the ES2022
    // boundary.  Guchho uses this to decide which syntax transforms
    // are needed (e.g. top-level await is only available at or above
    // ES2022).
    enum class TSTarget : uint8_t {
        kUnspecified,
        kBelowES2022,
        kAtOrAboveES2022,
    };

    // Platform
    // --------
    // The runtime environment the bundle is intended for:
    //
    //   kBrowser  Browser globals (window, document).
    //   kNode     Node.js globals (require, module, __dirname).
    //   kNeutral  No platform-specific globals; the code is
    //             platform-agnostic.
    enum class Platform : uint8_t {
        kBrowser,
        kNode,
        kNeutral,
    };

    // SourceMap
    // ---------
    // How Guchho emits source maps for the output:
    //
    //   kNone                  No source map at all.
    //   kInline                Source map appended as a base64 data URL
    //                          inside the output file.
    //   kLinkedWithComment     External `.map` file with a
    //                          `//# sourceMappingURL=...` comment.
    //   kExternalWithoutComment External `.map` file, no comment.
    //   kInlineAndExternal     Both an inline map and an external file.
    enum class SourceMap : uint8_t {
        kNone,
        kInline,
        kLinkedWithComment,
        kExternalWithoutComment,
        kInlineAndExternal,
    };

    // LegalComments
    // -------------
    // How license and copyright comments are preserved in the output:
    //
    //   kInline                Keep comments inline where they appear.
    //   kNone                  Strip all legal comments.
    //   kEndOfFile             Move legal comments to the end of the file.
    //   kLinkedWithComment     External file with a reference comment.
    //   kExternalWithoutComment External file, no reference comment.
    enum class LegalComments : uint8_t {
        kInline,
        kNone,
        kEndOfFile,
        kLinkedWithComment,
        kExternalWithoutComment,
    };

    // LegalCommentsHasExternalFile
    // ----------------------------
    // Returns true when the given legal-comments mode produces a
    // separate external file that must be written alongside the
    // main output.
    //
    // Input:  LegalComments::kLinkedWithComment  => true
    // Input:  LegalComments::kInline             => false
    inline bool LegalCommentsHasExternalFile(LegalComments lc) {
        return lc == LegalComments::kLinkedWithComment || lc == LegalComments::kExternalWithoutComment;
    }

    // Loader
    // ------
    // The file-type loader that determines how Guchho parses and
    // embeds a given input file.  Each loader knows how to read its
    // file type, apply the appropriate parser, and produce output in
    // the correct format.  The special `kDefault` loader defers to
    // the platform's default for the file extension, while `kNone`
    // means the file should not be processed at all.
    enum class Loader : uint8_t {
        kNone,
        kBase64,
        kBinary,
        kCopy,
        kCSS,
        kDataURL,
        kDefault,
        kEmpty,
        kFile,
        kGlobalCSS,
        kHTML,
        kJS,
        kJSON,
        kWithTypeJSON,
        kJSX,
        kLocalCSS,
        kText,
        kTS,
        kTSNoAmbiguousLessThan,
        kTSX,
    };

    constexpr size_t kLoaderCount = static_cast<size_t>(Loader::kTSX) + 1;

    // IsTypeScript
    // ------------
    // Returns true when the loader parses TypeScript (kTS, kTSX,
    // kTSNoAmbiguousLessThan).
    bool IsTypeScript(Loader loader);
    // IsCSS
    // -----
    // Returns true when the loader parses CSS (kCSS, kLocalCSS,
    // kGlobalCSS).
    bool IsCSS(Loader loader);
    // CanHaveSourceMap
    // ----------------
    // Returns true when the loader produces output that can carry a
    // source map (JS, TS, CSS loaders).
    bool CanHaveSourceMap(Loader loader);
    // LoaderToString
    // --------------
    // Returns the short string form of a loader, e.g. kJS => "js",
    // kTSX => "tsx".
    std::string_view LoaderToString(Loader loader);

    // Format
    // ------
    // The output module format of the bundle:
    //
    //   kPreserve   Leave import/export syntax as written.
    //   kIIFE       Wrap in an immediately-invoked function expression.
    //   kCommonJS   Convert to `module.exports` / `require()`.
    //   kESModule   Convert to `import` / `export` syntax.
    //   kUMD        Universal Module Definition (supports CJS + AMD +
    //               global).
    //   kAMD        Asynchronous Module Definition.
    //   kSystem     SystemJS module format.
    enum class Format : uint8_t {
        kPreserve,
        kIIFE,
        kCommonJS,
        kESModule,
        kUMD,
        kAMD,
        kSystem,
    };

    // FormatKeepESMImportExportSyntax
    // -------------------------------
    // Returns true when the format preserves ESM import/export syntax
    // as written (preserve or ESM output modes).
    //
    // Input:  Format::kPreserve  => true
    // Input:  Format::kCommonJS  => false
    inline bool FormatKeepESMImportExportSyntax(Format f) {
        return f == Format::kPreserve || f == Format::kESModule;
    }

    // FormatAllowTopLevelAwait
    // ------------------------
    // Returns true when the format supports top-level await.  The
    // wrapper formats (IIFE, CJS, UMD, AMD) are synchronous and
    // cannot express top-level await.
    //
    // Input:  Format::kESModule  => true
    // Input:  Format::kCommonJS  => false
    inline bool FormatAllowTopLevelAwait(Format f) {
        return f == Format::kPreserve || f == Format::kESModule || f == Format::kSystem;
    }

    // FormatToString
    // ---------------
    // Returns the short string form of a format, e.g. kCommonJS =>
    // "cjs", kESModule => "esm".
    std::string_view FormatToString(Format f);

    // Mode
    // ----
    // The overall build mode:
    //
    //   kPassThrough    No transformation; files are copied as-is.
    //   kConvertFormat  Convert between module formats without bundling.
    //   kBundle         Full bundling with tree-shaking and code splitting.
    enum class Mode : uint8_t {
        kPassThrough,
        kConvertFormat,
        kBundle,
    };

    // APICall
    // -------
    // Which public API entry point triggered this build or transform:
    //
    //   kBuildCall      The `build()` API was invoked.
    //   kTransformCall  The `transform()` API was invoked.
    enum class APICall : uint8_t {
        kBuildCall,
        kTransformCall,
    };

    // PathPlaceholder
    // ----------------
    // The kind of placeholder that can appear in an output path
    // template (e.g. "[name]-[hash].[ext]"):
    //
    //   kNoPlaceholder  Literal text, not a placeholder.
    //   kDir            The directory portion of the path.
    //   kName           The file name without extension.
    //   kHash           The content hash for cache-busting.
    //   kExt            The file extension (without dot).
    enum class PathPlaceholder : uint8_t {
        kNoPlaceholder,
        kDir,
        kName,
        kHash,
        kExt,
    };

    // MetafileFormat
    // --------------
    // Whether the metafile (build manifest) is emitted minified or
    // in a human-readable format.
    enum class MetafileFormat : uint8_t {
        kUnminified,
        kMinified,
    };

    // TSConfigJSX
    // -----------
    // JSX settings extracted from a TypeScript `tsconfig.json` file.
    // These control the JSX factory function, fragment factory, and
    // import source used during the automatic JSX transform.
    struct TSConfigJSX {
        std::vector<std::string> JSXFactory{};
        std::vector<std::string> JSXFragmentFactory{};
        std::optional<std::string> JSXImportSource{};
        TSJSX JSX{TSJSX::kNone};
    };

    // TSConfig
    // --------
    // TypeScript compiler options that Guchho honours.  Every field
    // defaults to "unspecified" so that a tsconfig.json can override
    // only the settings it cares about, leaving the rest at their
    // Guchho defaults.
    //
    //   ExperimentalDecorators      Enable legacy decorator syntax.
    //   ImportsNotUsedAsValues      How to handle type-only imports.
    //   PreserveValueImports        Keep value imports even when unused.
    //   Target                      ES target tier (below/at ES2022).
    //   UseDefineForClassFields     Use `define` semantics for class fields.
    //   VerbatimModuleSyntax        Require explicit type annotations on
    //                               imported types.
    struct TSConfig {
        MaybeBool ExperimentalDecorators{MaybeBool::kUnspecified};
        TSImportsNotUsedAsValues ImportsNotUsedAsValues{TSImportsNotUsedAsValues::kNone};
        MaybeBool PreserveValueImports{MaybeBool::kUnspecified};
        TSTarget Target{TSTarget::kUnspecified};
        MaybeBool UseDefineForClassFields{MaybeBool::kUnspecified};
        MaybeBool VerbatimModuleSyntax{MaybeBool::kUnspecified};
    };

    // TSOptions
    // ---------
    // Controls how TypeScript input is processed:
    //
    //   Config              The parsed tsconfig.json settings.
    //   Parse               When true, Guchho parses .ts/.tsx files.
    //   NoAmbiguousLessThan When true, the parser does not treat `<` as
    //                       the start of a JSX element (useful for .ts
    //                       files that contain generic arrow functions).
    struct TSOptions {
        TSConfig Config{};
        bool Parse{};
        bool NoAmbiguousLessThan{};
    };

    // JSXOptions
    // ----------
    // JSX transform settings for the output.  Controls which factory
    // function is used, whether the automatic runtime is enabled, and
    // whether the output is in development mode.
    struct JSXOptions {
        DefineExpr Factory{};
        DefineExpr Fragment{};
        bool Parse{};
        bool Preserve{};
        bool AutomaticRuntime{};
        std::string ImportSource{};
        bool Development{};
        bool SideEffects{};
    };

    // TSAlwaysStrict
    // --------------
    // The synthesized `"use strict"` directive that Guchho may
    // prepend to a file when `alwaysStrict` is enabled in tsconfig.
    // Stores the source location so that diagnostics can point to the
    // correct file and range.
    struct TSAlwaysStrict {
        std::string Name{};
        logger::Source SourceData{};
        logger::Range RangeData{};
        bool Value{};
    };

    // CancelFlag
    // ----------
    // A cooperative cancellation signal shared across an entire build.
    // Any thread can request cancellation by calling `Cancel()`, and
    // all other threads observe it via `DidCancel()`.  The atomic
    // ensures safe cross-thread visibility without locks.
    //
    // Usage:
    //   flag.Cancel();      // request cancellation
    //   flag.DidCancel();   // check if cancellation was requested
    struct CancelFlag {
        std::atomic<uint32_t> flag{0};

        // Cancel
        // ------
        // Requests cancellation by setting the atomic flag.  Safe to
        // call from any thread; subsequent DidCancel() calls on any
        // thread will return true.
        void Cancel() {
            flag.store(1, std::memory_order_release);
        }

        // DidCancel
        // ---------
        // Returns true once cancellation has been requested.  Uses
        // acquire ordering to ensure all memory writes made before
        // Cancel() are visible to the reading thread.
        bool DidCancel() const {
            return flag.load(std::memory_order_acquire) != 0;
        }
    };

    // StdinInfo
    // ---------
    // The stdin input for a `transform()` call.  When a file is read
    // from standard input rather than from the filesystem, this
    // struct carries its contents, source-file name, and the
    // directory used for import resolution.
    struct StdinInfo {
        std::string Contents{};
        std::string SourceFile{};
        std::string AbsResolveDir{};
        Loader Ldr{Loader::kNone};
    };

    // EntryPoint
    // ----------
    // One build entry point: its input file path and the output path
    // (without extension) where Guchho writes the result.  The flag
    // `InputPathInFileNamespace` is set when the input was resolved
    // to a real file on disk rather than a virtual module.
    //
    // Input:  InputPath = "src/index.ts"
    //         OutputPath = "dist/index"
    // Output: Guchho writes "dist/index.js" (or .mjs, etc.)
    struct EntryPoint {
        std::string InputPath{};
        std::string OutputPath{};
        bool InputPathInFileNamespace{};
    };

    // WildcardPattern
    // ---------------
    // One segment of an external-package wildcard pattern.  The
    // pattern matches a package name when it starts with `Prefix`
    // and ends with `Suffix`.  For example, the pattern
    // {Prefix="lodash/", Suffix=""} matches "lodash/cloneDeep".
    struct WildcardPattern {
        std::string Prefix{};
        std::string Suffix{};
    };

    // ExternalMatchers
    // -----------------
    // Holds both exact package names and wildcard patterns that
    // identify external packages.  Exact matches are stored in a map
    // for O(1) lookup; wildcard patterns are checked only when the
    // exact lookup fails.
    //
    // Input:  Exact = {"react": true, "react-dom": true}
    //         Patterns = [{Prefix="lodash/", Suffix=""}]
    // Output: HasMatchers() == true
    struct ExternalMatchers {
        std::unordered_map<std::string, bool> Exact{};
        std::vector<WildcardPattern> Patterns{};

        // HasMatchers
        // -----------
        // Returns true when at least one exact name or wildcard pattern
        // has been configured.
        bool HasMatchers() const {
            return !Exact.empty() || !Patterns.empty();
        }
    };

    // ExternalSettings
    // -----------------
    // External-package matching is split into two stages:
    //
    //   PreResolve   Matches against the raw import specifier before
    //                path resolution (e.g. "react", "lodash/cloneDeep").
    //   PostResolve  Matches against the fully-resolved absolute path
    //                after the module resolver has finished.
    //
    // This two-stage approach lets users externalise packages by name
    // (pre-resolve) or by file path (post-resolve).
    struct ExternalSettings {
        ExternalMatchers PreResolve{};
        ExternalMatchers PostResolve{};
    };

    // PathTemplate
    // ------------
    // One literal or placeholder segment of an output path template.
    // For example, the template "[name]-[hash].[ext]" is represented
    // as four PathTemplate entries:
    //   {Data="", Placeholder=kName}
    //   {Data="-", Placeholder=kNoPlaceholder}
    //   {Data="", Placeholder=kHash}
    //   {Data=".", Placeholder=kNoPlaceholder}
    //   {Data="", Placeholder=kExt}
    struct PathTemplate {
        std::string Data{};
        PathPlaceholder Placeholder{PathPlaceholder::kNoPlaceholder};
    };

    // PathPlaceholders
    // -----------------
    // The concrete values that replace placeholders in a path template
    // during output.  Each pointer may be null; `Get()` returns null
    // for unrecognised placeholders.
    //
    // Input:  placeholder = kName, Name points to "index"
    // Output: Get(kName) returns pointer to "index"
    struct PathPlaceholders {
        std::string* Dir{};
        std::string* Name{};
        std::string* Hash{};
        std::string* Ext{};

        // Get
        // ---
        // Returns the value slot for the given placeholder, or nullptr
        // when the placeholder is not present in the template.
        std::string* Get(PathPlaceholder placeholder) const {
            switch (placeholder) {
                case PathPlaceholder::kDir: return Dir;
                case PathPlaceholder::kName: return Name;
                case PathPlaceholder::kHash: return Hash;
                case PathPlaceholder::kExt: return Ext;
                default: return nullptr;
            }
        }
    };

    // InjectableExport
    // ----------------
    // An export injected from an injected file, carrying its alias
    // name and source location for diagnostics.
    struct InjectableExport {
        std::string Alias{};
        logger::Loc LocData{};
    };

    // InjectedDefine
    // --------------
    // A define whose value comes from an injected file.  The `Data`
    // field holds the parsed JS AST of the injected content, and
    // `Name` is the define name it is assigned to.
    struct InjectedDefine {
        guchho::javascript::E Data{};
        std::string Name{};
        logger::Source SourceData{};
    };

    // InjectedFile
    // ------------
    // A file injected into the bundle, exposing one or more exports
    // under a define name.  The `IsCopyLoader` flag indicates the
    // file should be copied as-is (binary/text) rather than parsed.
    struct InjectedFile {
        std::vector<InjectableExport> Exports{};
        std::string DefineName{};
        logger::Source SourceData{};
        bool IsCopyLoader{};
    };


    // OnStartResult
    // -------------
    // The result of a plugin's `onStart` hook.  Contains any error
    // that was thrown and any diagnostic messages to emit.
    struct OnStartResult {
        std::string ThrownError{};
        std::vector<logger::Msg> Msgs{};
    };

    // OnResolveArgs
    // -------------
    // Input passed to a plugin's `onResolve` hook.  Contains the
    // import specifier, the directory of the importing file, the
    // plugin's private data, and the import kind (entry point,
    // import statement, etc.).
    struct OnResolveArgs {
        std::string PathData{};
        std::string ResolveDir{};
        std::any PluginData{};
        logger::Path Importer{};
        compiler::ImportKind Kind{compiler::ImportKind::kEntryPoint};
        logger::ImportAttributes With{};
    };

    // OnResolveResult
    // ---------------
    // Output a plugin returns from an `onResolve` hook.  When the
    // plugin handles the import it fills in `ResultPath` and
    // optionally marks the import as external or side-effect-free.
    struct OnResolveResult {
        std::string PluginName{};
        std::vector<logger::Msg> Msgs{};
        std::string ThrownError{};
        std::vector<std::string> AbsWatchFiles{};
        std::vector<std::string> AbsWatchDirs{};
        std::any PluginData{};
        logger::Path ResultPath{};
        bool External{};
        bool IsSideEffectFree{};
    };

    // OnLoadArgs
    // ----------
    // Input passed to a plugin's `onLoad` hook.  Contains the plugin's
    // private data and the fully-resolved path of the file to load.
    struct OnLoadArgs {
        std::any PluginData{};
        logger::Path LoadPath{};
    };

    // OnLoadResult
    // ------------
    // Output a plugin returns from an `onLoad` hook.  When the plugin
    // handles the load it provides `Contents` and the loader to use
    // for the returned text.
    struct OnLoadResult {
        std::string PluginName{};
        std::optional<std::string> Contents{};
        std::string AbsResolveDir{};
        std::any PluginData{};
        std::vector<logger::Msg> Msgs{};
        std::string ThrownError{};
        std::vector<std::string> AbsWatchFiles{};
        std::vector<std::string> AbsWatchDirs{};
        Loader ResultLoader{Loader::kNone};
    };

    // Plugin callback type aliases
    // ----------------------------
    // Each hook kind has a corresponding `std::function` type that
    // plugins implement.  The callback signatures match the Args/Result
    // structs above.
    using OnStartCallback = std::function<OnStartResult()>;
    using OnResolveCallback = std::function<OnResolveResult(OnResolveArgs)>;
    using OnLoadCallback = std::function<OnLoadResult(OnLoadArgs)>;

    // OnStart
    // -------
    // A registered `onStart` hook with its callback and the plugin
    // name for diagnostics.
    struct OnStart {
        OnStartCallback Callback{};
        std::string Name{};
    };

    // OnResolve
    // ---------
    // A registered `onResolve` hook with its filter regex (matched
    // against the import specifier), namespace, callback, and plugin
    // name.
    struct OnResolve {
        std::regex Filter{};
        OnResolveCallback Callback{};
        std::string Name{};
        std::string Namespace{};
    };

    // OnLoad
    // ------
    // A registered `onLoad` hook with its filter regex (matched
    // against the resolved path), namespace, callback, and plugin
    // name.
    struct OnLoad {
        std::regex Filter{};
        OnLoadCallback Callback{};
        std::string Name{};
        std::string Namespace{};
    };

    // HtmlTagDescriptor
    // -----------------
    // An element to be injected into the generated HTML document.
    // Mirrors Vite's `HtmlTagDescriptor` interface.  Returned by a
    // plugin's `transformIndexHtml` hook and injected at the
    // requested boundary (head, body, or their prepend variants).
    //
    //   tag        The element name, e.g. "link", "script".
    //   attrs      Key-value attribute pairs (inserted in iteration order).
    //   children   Raw inner HTML; only used for non-void elements.
    //   inject_to  Where in the document to inject the element:
    //                kHead         Before closing </head>.
    //                kHeadPrepend  Directly after opening <head>.
    //                kBody         Before closing </body>.
    //                kBodyPrepend  Directly after opening <body>.
    struct HtmlTagDescriptor {
        std::string tag{};
        std::unordered_map<std::string, std::string> attrs{};
        std::string children{};
        enum InjectTo {
            kHead,
            kHeadPrepend,
            kBody,
            kBodyPrepend,
        } inject_to = kHead;
    };

    // HtmlTransformContext
    // --------------------
    // Contract passed to every `transformIndexHtml` hook invocation.
    // Contains the filename being processed and whether this is a
    // build (as opposed to a dev server request).
    struct HtmlTransformContext {
        std::string filename{};
        bool is_build = true;
    };

    // TransformIndexHtmlHook
    // ----------------------
    // The callback type for Vite's `transformIndexHtml` hook.  Returns
    // either a full replacement HTML string or a vector of tag
    // descriptors to inject into the document.
    using TransformIndexHtmlHook = std::function<
        std::variant<std::string, std::vector<HtmlTagDescriptor>>(
            const std::string& html, const HtmlTransformContext& ctx)>;

    // Plugin
    // ------
    // A named plugin holding its registered `onStart`, `onResolve`,
    // `onLoad`, and `transformIndexHtml` hooks.  Guchho iterates the
    // hook vectors in registration order during the corresponding
    // build phase.
    struct Plugin {
        std::string Name{};
        std::vector<OnStart> OnStartList{};
        std::vector<OnResolve> OnResolveList{};
        std::vector<OnLoad> OnLoadList{};
        TransformIndexHtmlHook TransformIndexHtml{};
    };

    // MangleCacheCallback / ExclusiveMangleCacheUpdateFunc
    // ---------------------------------------------------
    // Callback types for updating the shared mangle cache.  The cache
    // tracks which identifiers have been mangled so that duplicate
    // renames are avoided across files.
    using MangleCacheCallback = std::function<void(
        std::unordered_map<std::string, bool>& mangleCache,
        std::unordered_map<std::string, bool>& cssUsedLocalNames
    )>;
    using ExclusiveMangleCacheUpdateFunc = std::function<void(MangleCacheCallback)>;


    // ResourceHintsConfig
    // -------------------
    // Controls automatic `<link rel="preload|preconnect|dns-prefetch">`
    // injection into the HTML output.  Disabled by default.
    //
    //   enabled       Master switch; all hints are off when false.
    //   preload       Use `rel="preload"` for font hints.
    //   prefetch      Use `rel="prefetch"` for font hints (fallback when
    //                 preload is false).
    //   preconnect    Emit `<link rel="preconnect">` for external origins.
    //   dns_prefetch  Emit `<link rel="dns-prefetch">` for external origins.
    //   fonts         Emit font preload/prefetch hints for local @font-face
    //                 sources (woff2, woff, ttf, otf only; eot is skipped
    //                 because it requires a "?#iefix" fragment).
    struct ResourceHintsConfig {
        bool enabled = false;
        bool preload = false;
        bool prefetch = false;
        bool preconnect = false;
        bool dns_prefetch = false;
        bool fonts = false;
    };

    // CSSLoadingStrategy
    // ------------------
    // How rendered stylesheet `<link>` elements are loaded:
    //
    //   kBlocking     Default: a plain `<link rel="stylesheet">` that
    //                 blocks rendering until the CSS is downloaded.
    //   kNonBlocking  A `<link rel="preload" as="style">` with an
    //                 `onload` handler that swaps to `stylesheet`, plus
    //                 a `<noscript>` fallback.
    //   kMediaSplit   Critical CSS inlined, the rest deferred via
    //                 `media="print" onload="this.media='all'"` (not
    //                 yet wired).
    enum class CSSLoadingStrategy {
        kBlocking,
        kNonBlocking,
        kMediaSplit,
    };


    // AmdOptions
    // ----------
    // Options specific to the AMD output format:
    //
    //   auto_id                      Automatically generate module IDs.
    //   base_path                    Base path for AMD module IDs.
    //   id                           Explicit module ID for the entry point.
    //   define                       The AMD `define` function name.
    //   force_js_extension_for_imports  Append ".js" to all imports.
    struct AmdOptions {
        bool auto_id{};
        std::string base_path{};
        std::string id{};
        std::string define{"define"};
        bool force_js_extension_for_imports{};
    };

    // Options
    // -------
    // The complete set of resolved build options Guchho acts on.
    // Every field has a safe default so the struct can be filled in
    // incrementally and copied freely.
    //
    // This is the single source of truth for all build-time
    // configuration.  Each option is documented inline; groups of
    // related options are separated by blank lines for readability.
    struct Options {
        guchho::javascript::ModuleTypeData ModuleTypeData{};
        ProcessedDefines* Defines{};
        TSAlwaysStrict* TSAlwaysStrictData{};
        // Used on the heap (via shared_ptr) so the struct itself stays copyable.
        std::shared_ptr<std::regex> MangleProps{};
        std::shared_ptr<std::regex> ReserveProps{};
        CancelFlag* CancelFlagData{};
        ExclusiveMangleCacheUpdateFunc ExclusiveMangleCacheUpdate{};

        std::string OriginalTargetEnv{};

        std::vector<std::string> DropLabels{};
        bool                     MainFieldsSet{};
        std::vector<std::string> MainFields{};
        std::vector<std::string> Conditions{};
        std::vector<std::string> AbsNodePaths{};

        Mode BuildMode{Mode::kPassThrough};
        Format OutputFormat{Format::kPreserve};
        bool CodeSplitting{};
        Platform OutputPlatform{Platform::kBrowser};
        bool NeedsMetafile{};
        bool PrettyPrint{};
        SourceMap SourceMapData{SourceMap::kNone};
        bool ExcludeSourcesContent{};

        std::string AbsOutputFile{};
        std::string AbsOutputDir{};
        std::string AbsOutputBase{};
        std::string OutputExtensionJS{};
        std::string OutputExtensionCSS{};
        std::vector<std::string> GlobalName{};
        AmdOptions Amd{};
        bool Extend{};
        bool NoConflict{};
        bool Strict{true};
        std::unordered_map<std::string, std::string> Globals{};
        bool SystemNullSetters{};
        std::string TSConfigPath{};
        std::string TSConfigRaw{};

        ExternalSettings ExternalSettingsData{};
        bool ExternalPackages{};
        std::unordered_map<std::string, std::string> PackageAliases{};
        std::vector<std::string> ExtensionOrder{};
        std::unordered_map<std::string, Loader> ExtensionToLoader{};

        bool PreserveSymlinks{};
        bool MinifyWhitespace{};
        bool MinifyIdentifiers{};
        bool MinifySyntax{};
        bool ProfilerNames{};
        bool WatchMode{};
        bool AllowOverwrite{};
        LegalComments LegalCommentsData{LegalComments::kInline};

        compat::JSFeature UnsupportedJSFeatures{};
        compat::CSSFeature UnsupportedCSSFeatures{};
        compat::JSFeature UnsupportedJSFeatureOverrides{};
        compat::JSFeature UnsupportedJSFeatureOverridesMask{};
        compat::CSSFeature UnsupportedCSSFeatureOverrides{};
        compat::CSSFeature UnsupportedCSSFeatureOverridesMask{};

        TSOptions TS{};

        std::string PublicPath{};
        std::vector<std::string> InjectPaths{};
        std::vector<InjectedDefine> InjectedDefines{};
        std::vector<InjectedFile> InjectedFiles{};

        std::string JSBanner{};
        std::string JSFooter{};
        std::string CSSBanner{};
        std::string CSSFooter{};

        std::vector<PathTemplate> EntryPathTemplate{};
        std::vector<PathTemplate> ChunkPathTemplate{};
        std::vector<PathTemplate> AssetPathTemplate{};

        std::vector<Plugin> Plugins{};
        std::string SourceRoot{};
        StdinInfo* Stdin{};
        JSXOptions JSX{};
        int LineLimit{};

        std::unordered_map<guchho::css::Declarations, compat::CSSPrefix> CSSPrefixData{};

        bool OmitRuntimeForTests{};
        bool OmitJSXRuntimeForTests{};
        bool ASCIIOnly{};
        bool KeepNames{};
        bool IgnoreDCEAnnotations{};
        bool TreeShaking{};
        bool DropDebugger{};
        bool MangleQuoted{};
        bool WriteToStdout{};
        MetafileFormat MetafileFormatData{MetafileFormat::kUnminified};

        logger::PathStyle LogPathStyle{logger::PathStyle::kRelPath};
        logger::PathStyle CodePathStyle{logger::PathStyle::kRelPath};
        logger::PathStyle MetafilePathStyle{logger::PathStyle::kRelPath};
        logger::PathStyle SourcemapPathStyle{logger::PathStyle::kRelPath};

        // DefineMap holds `%KEY%` replacement values for the HTML output
        // pass.  Keys are the names without the `%` delimiters; every
        // `%KEY%` in HTML text nodes and attribute values is replaced
        // (never inside <script>/<style> content, which travels through
        // the JS/CSS pipelines).  Unrecognised `%...%` patterns are
        // left untouched.
        std::unordered_map<std::string, std::string> DefineMap{};
        // CspNonce is a nonce value added to every <script>, <style>,
        // and <link rel="stylesheet|modulepreload|preload"> element.
        // When non-empty, a <meta property="csp-nonce" nonce="..."> tag
        // is injected as the first child of <head>.  User-provided nonce
        // attributes are preserved.  No CSP policy header is generated.
        std::string CspNonce{};
        // MinifyHtml enables HTML minification: whitespace collapsing,
        // comment removal, safe attribute-quote removal, and
        // empty-attribute collapse.
        bool MinifyHtml = false;
        // ResourceHints configures auto-injected resource hints (see
        // ResourceHintsConfig for details on each hint type).
        ResourceHintsConfig ResourceHints{};
        // SRI enables Subresource Integrity: when true, an
        // `integrity="ALGO-<base64>"` attribute is added to every
        // external <script> and <link rel="stylesheet"> element whose
        // content was bundled locally, along with `crossorigin` if
        // absent.  The hash covers the final emitted bytes of the
        // referenced file.  Inline runs and external URLs are never
        // hashed.  Supported algorithms: "sha256", "sha384" (default),
        // "sha512".
        bool SRI = false;
        std::string SRIAlgorithm = "sha384";
        // CSSLoadingStrategyData controls how stylesheet links are
        // emitted (blocking by default).
        CSSLoadingStrategy CSSLoadingStrategyData{CSSLoadingStrategy::kBlocking};
    };


    // LoaderFromFileExtension
    // -----------------------
    // Looks up the loader for a file by its base name and extension
    // map.  The extension map is consulted first; if no match is
    // found, the function falls back to the platform default.
    //
    // Input:  extensionToLoader = {".js": kJS, ".ts": kTS}
    //         base = "index.js"
    // Output: kJS
    Loader LoaderFromFileExtension(const std::unordered_map<std::string, Loader>& extensionToLoader, const std::string& base);

    // ShouldCallRuntimeRequire
    // ------------------------
    // Returns true when a runtime `require()` helper is needed for the
    // given mode and output format.  This is the case when bundling
    // for CommonJS or IIFE formats that need dynamic imports.
    //
    // Input:  mode = kBundle, format = kCommonJS
    // Output: true
    //
    // Input:  mode = kPassThrough, format = kESModule
    // Output: false
    bool ShouldCallRuntimeRequire(Mode mode, Format outputFormat);

    // TemplateToString
    // ----------------
    // Joins a path template back into a single string by
    // concatenating literal segments and placeholder markers.
    //
    // Input:  [{Data="dist/", Placeholder=kNoPlaceholder},
    //          {Data="", Placeholder=kName},
    //          {Data=".js", Placeholder=kNoPlaceholder}]
    // Output: "dist/[name].js"
    std::string TemplateToString(const std::vector<PathTemplate>& tmpl);

    // HasPlaceholder
    // --------------
    // Returns true when the template contains at least one segment
    // with the given placeholder kind.
    //
    // Input:  tmpl = [{Data="[name]-[hash].js"}], placeholder = kHash
    // Output: true
    bool HasPlaceholder(const std::vector<PathTemplate>& tmpl, PathPlaceholder placeholder);

    // SubstituteTemplate
    // ------------------
    // Replaces every placeholder in the template with its concrete
    // value from the `PathPlaceholders` struct.  Literal segments
    // are left unchanged.
    //
    // Input:  tmpl = [{Data="[name]"}, {Data=".js"}]
    //         placeholders.Name = "index"
    // Output: [{Data="index"}, {Data=".js"}]
    std::vector<PathTemplate> SubstituteTemplate(const std::vector<PathTemplate>& tmpl, const PathPlaceholders& placeholders);

    // CompileFilterForPlugin
    // ----------------------
    // Compiles a plugin filter string into a regex, returning nullptr
    // if the filter is not valid.  Emits a diagnostic via the logger
    // when compilation fails.
    //
    // Input:  pluginName = "my-plugin", kind = "onResolve",
    //         filter = ".*\\.ts$"
    // Output: A compiled regex matching ".ts" paths
    std::unique_ptr<std::regex> CompileFilterForPlugin(const std::string& pluginName, const std::string& kind, const std::string& filter);

    // PluginAppliesToPath
    // -------------------
    // Returns true when the given path matches the plugin's filter
    // regex and belongs to the correct namespace.
    //
    // Input:  path = "src/index.ts", filter = ".*\\.ts$",
    //         namespace_ = ""
    // Output: true
    bool PluginAppliesToPath(const logger::Path& path, const std::regex& filter, const std::string& namespace_);

    // PrettyPrintTargetEnvironment
    // ----------------------------
    // Renders the original target environment string with unsupported
    // features marked.  Used for diagnostics and the metafile.
    //
    // Input:  originalTargetEnv = "es2020",
    //         unsupportedJSFeatureOverridesMask = kTopLevelAwait
    // Output: "es2020 (with top-level await overridden)"
    std::string PrettyPrintTargetEnvironment(const std::string& originalTargetEnv, compat::JSFeature unsupportedJSFeatureOverridesMask);

    // MaybeRemoveWhitespace
    // ---------------------
    // Returns the metafile text, stripping whitespace when the format
    // is minified.  When the format is unminified the input is
    // returned as-is.
    //
    // Input:  mf = kMinified, fmt = "{ \"inputs\": {} }"
    // Output: "{\"inputs\":{}}"
    //
    // Input:  mf = kUnminified, fmt = "{ \"inputs\": {} }"
    // Output: "{ \"inputs\": {} }"  (unchanged)
    std::string MaybeRemoveWhitespace(MetafileFormat mf, const std::string& fmt);

    // TSConfigApplyExtendedConfig
    // ---------------------------
    // Merges base tsconfig options into derived, with derived taking
    // precedence.  Only `MaybeBool::kUnspecified` fields in derived
    // are overwritten by the base.
    //
    // Input:  derived.ExperimentalDecorators = kUnspecified
    //         base.ExperimentalDecorators = kTrue
    // Output: derived.ExperimentalDecorators = kTrue
    void TSConfigApplyExtendedConfig(TSConfig& derived, const TSConfig& base);

    // TSConfigUnusedImportFlags
    // -------------------------
    // Computes the unused-import flags for a TypeScript config by
    // examining `verbatimModuleSyntax` and `preserveValueImports`.
    //
    // Input:  cfg.VerbatimModuleSyntax = kTrue
    // Output: TSUnusedImportFlags::kKeepStmt | kKeepValues
    TSUnusedImportFlags TSConfigUnusedImportFlags(const TSConfig& cfg);

    // TSConfigJSXApplyExtendedConfig
    // ------------------------------
    // Merges base JSX config options into derived, following the same
    // precedence rules as TSConfigApplyExtendedConfig.
    void TSConfigJSXApplyExtendedConfig(TSConfigJSX& derived, const TSConfigJSX& base);

    // TSConfigJSXApplyTo
    // ------------------
    // Copies the config's JSX settings into the JSXOptions struct
    // that Guchho uses during the transform phase.
    void TSConfigJSXApplyTo(const TSConfigJSX& tsConfig, JSXOptions& jsxOptions);

}
