#pragma once

#include <functional>
#include <vector>
#include <string>
#include <mutex>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <format>

namespace guchho::logger {

    constexpr bool kSupportsColorEscapes = true;
    constexpr int  kDefaultTerminalWidth = 80;
    constexpr int  kExtraMarginChars = 9;
    constexpr int  kSizeWarningThreshold = 1024 * 1024;

    // Log severity levels, ordered so that a message is displayed only when
    // its level is numerically >= the current threshold. The progression
    // from kVerbose to kSilent lets callers filter by noise level:
    //
    //   kNone     - used for messages without a severity (e.g. raw output)
    //   kVerbose  - extremely detailed diagnostic tracing
    //   kDebug    - development-oriented internal state dumps
    //   kInfo     - normal progress indicators ("building foo.js...")
    //   kWarning  - suspicious code that may still be intentional
    //   kError    - unrecoverable problems that prevent a successful build
    //   kSilent   - suppresses all output
    //
    // Setting level to kWarning suppresses kInfo, kDebug, and kVerbose
    // while still showing kWarning and kError messages.
    enum class LogLevel : int8_t {
        kNone,
        kVerbose,
        kDebug,
        kInfo,
        kWarning,
        kError,
        kSilent,
    };

    // Identifies a specific warning or informational message so that its
    // display level can be overridden by the user. For example, --warning-level
    // kJS_BigInt=error promotes that specific warning to an error. Messages
    // without an ID (errors, debug output) use kNone and cannot be overridden.
    //
    // Configuration-file message groups (kPackageJSON_*, kTSConfigJSON_*,
    // kGuchhoJSON_*, kGuchhoConfig_*) are bounded by FIRST/LAST sentinel
    // values so iteration over a contiguous block is straightforward.
    enum class MsgID : uint8_t {
        kNone,

        // JavaScript
        kJS_AssertToWith,
        kJS_AssertTypeJSON,
        kJS_AssignToConstant,
        kJS_AssignToDefine,
        kJS_AssignToImport,
        kJS_BigInt,
        kJS_CallImportNamespace,
        kJS_ClassNameWillThrow,
        kJS_CommonJSVariableInESM,
        kJS_DeleteSuperProperty,
        kJS_DirectEval,
        kJS_DuplicateCase,
        kJS_DuplicateClassMember,
        kJS_DuplicateObjectKey,
        kJS_EmptyImportMeta,
        kJS_EqualsNaN,
        kJS_EqualsNegativeZero,
        kJS_EqualsNewObject,
        kJS_HTMLCommentInJS,
        kJS_ImpossibleTypeof,
        kJS_IndirectRequire,
        kJS_PrivateNameWillThrow,
        kJS_SemicolonAfterReturn,
        kJS_SuspiciousBooleanNot,
        kJS_SuspiciousDefine,
        kJS_SuspiciousLogicalOperator,
        kJS_SuspiciousNullishCoalescing,
        kJS_ThisIsUndefinedInESM,
        kJS_UnsupportedDynamicImport,
        kJS_UnsupportedJSXComment,
        kJS_UnsupportedRegExp,
        kJS_UnsupportedRequireCall,

        // CSS
        kCSS_CSSSyntaxError,
        kCSS_InvalidAtCharset,
        kCSS_InvalidAtImport,
        kCSS_InvalidAtLayer,
        kCSS_InvalidCalc,
        kCSS_JSCommentInCSS,
        kCSS_UndefinedComposesFrom,
        kCSS_UnsupportedAtCharset,
        kCSS_UnsupportedAtNamespace,
        kCSS_UnsupportedCSSProperty,
        kCSS_UnsupportedCSSNesting,

        // HTML
        kHTML_ParseWarning,
        kHTML_InvalidResourceURL,
        kHTML_ImportMapReordered,

        // Bundler
        kBundler_AmbiguousReexport,
        kBundler_DifferentPathCase,
        kBundler_EmptyGlob,
        kBundler_IgnoredBareImport,
        kBundler_IgnoredDynamicImport,
        kBundler_ImportIsUndefined,
        kBundler_RequireResolveNotExternal,

        // Source maps
        kSourceMap_InvalidSourceMappings,
        kSourceMap_MissingSourceMap,
        kSourceMap_UnsupportedSourceMapComment,

        // package.json
        kPackageJSON_FIRST,
        kPackageJSON_DeadCondition,
        kPackageJSON_InvalidBrowser,
        kPackageJSON_InvalidImportsOrExports,
        kPackageJSON_InvalidSideEffects,
        kPackageJSON_InvalidType,
        kPackageJSON_LAST,

        // tsconfig.json
        kTSConfigJSON_FIRST,
        kTSConfigJSON_Cycle,
        kTSConfigJSON_InvalidImportsNotUsedAsValues,
        kTSConfigJSON_InvalidJSX,
        kTSConfigJSON_InvalidPaths,
        kTSConfigJSON_InvalidTarget,
        kTSConfigJSON_InvalidTopLevelOption,
        kTSConfigJSON_Missing,
        kTSConfigJSON_LAST,

        // guchho.json
        kGuchhoJSON_FIRST,
        kGuchhoJSON_InvalidFormat,
        kGuchhoJSON_InvalidLogLevel,
        kGuchhoJSON_InvalidPlatform,
        kGuchhoJSON_InvalidSourcemap,
        kGuchhoJSON_InvalidTarget,
        kGuchhoJSON_UnknownField,
        kGuchhoJSON_Missing,
        kGuchhoJSON_ParseError,
        kGuchhoJSON_LAST,

        // guchho.config.js
        kGuchhoConfig_FIRST,
        kGuchhoConfig_NodeNotFound,
        kGuchhoConfig_SpawnFailed,
        kGuchhoConfig_EvalFailed,
        kGuchhoConfig_InvalidOutput,
        kGuchhoConfig_PluginsIgnored,
        kGuchhoConfig_LAST,

        kEND,
    };

    // The visual category of a log message, determining its terminal color
    // and prefix label. KError messages are always shown and printed in red;
    // kWarning in yellow; kInfo in green. kNote provides supplementary
    // context below a primary message. kDebug and kVerbose are only visible
    // when the log level is set accordingly.
    enum class MsgKind : uint8_t {
        kError,
        kWarning,
        kInfo,
        kNote,
        kDebug,
        kVerbose,
    };

    // Bit flags that modify how a path is displayed in diagnostics.
    // kPathDisabled suppresses the path entirely from output.
    enum class PathFlags : uint8_t {
        kPathDisabled = 1 << 0,
    };

    // Controls whether messages are buffered in memory or printed immediately.
    //
    //   kDeferLogAll              - hold back every message regardless of severity;
    //                              suitable for batch replay at the end of a build
    //   kDeferLogNoVerboseOrDebug - emit Verbose and Debug messages immediately
    //                              (useful for live progress indicators) while
    //                              deferring Info/Warning/Error for later
    enum class DeferLogKind : uint8_t {
        kDeferLogAll,
        kDeferLogNoVerboseOrDebug,
    };

    // A 0-based byte offset into a source file. This is the atomic unit of
    // source location; all line/column computations are derived from it on
    // demand by LineColumnTracker.
    struct Loc {
        int32_t start{};
    };

    inline bool operator==(Loc a, Loc b) {
        return a.start == b.start;
    }

    inline bool operator!=(Loc a, Loc b) {
        return !(a == b);
    }


    // A contiguous region of a source file identified by a starting byte
    // offset and a length in bytes. Together with Loc, this is the primary
    // mechanism for pinpointing the exact span of source text a diagnostic
    // refers to.
    //
    //   Range{Loc{10}, 5}  covers bytes 10, 11, 12, 13, 14 (inclusive).
    struct Range {
        Loc     loc{};
        int32_t len{};

        // Returns the byte offset one past the end of the range.
        //
        //   Range{Loc{10}, 5}.End()  ->  15
        int32_t End() const;

        // Expands this range to also cover `b`. If `b` is already fully
        // contained within the current range, the range is unchanged.
        //
        //   r = Range{Loc{10}, 5};
        //   r.ExpandBy(Range{Loc{20}, 3});
        //   // r.loc.start == 10, r.len == 13  (now covers 10..22)
        void    ExpandBy(const Range& b);
    };

    // Selects whether paths are printed as relative or absolute in
    // diagnostic output. The user toggles this via --path-style.
    enum class PathStyle : uint8_t {
        kRelPath,
        kAbsPath,
    };

    // Holds both the absolute and relative spellings of a source file path.
    // The logger picks which form to display based on the user's PathStyle
    // preference and available terminal width.
    struct PrettyPaths {
        std::string abs;
        std::string rel;

        // Selects the path form matching the requested style.
        //
        //   PrettyPaths{.abs="/src/foo.js", .rel="src/foo.js"}.Select(kRelPath)
        //   -> "src/foo.js"
        //
        //   PrettyPaths{.abs="/src/foo.js", .rel="src/foo.js"}.Select(kAbsPath)
        //   -> "/src/foo.js"
        std::string Select(PathStyle style) const;
    };

    // Source location metadata attached to a log message. Contains the file
    // path, 1-based line and column numbers, the raw source text on that
    // line, and an optional fix suggestion rendered inline with the marker.
    struct MsgLocation {
        PrettyPaths file;
        std::string namespace_;
        std::string line_text;
        std::string suggestion;
        int         line{};
        int         column{};
        int         length{};
    };

    // Controls whether ANSI color escape sequences are emitted. When
    // kColorIfTerminal is selected, the logger checks whether the output
    // file descriptor is a TTY and enables color accordingly.
    enum class UseColor : uint8_t {
        kColorIfTerminal,
        kColorNever,
        kColorAlways,
    };

    // Formatting and filtering options that govern which messages are
    // displayed and how they are rendered. Parsed from command-line flags
    // by OutputOptionsForArgs().
    struct OutputOptions {
        int                                 message_limit{};
        bool                                include_source{};
        UseColor                            color{};
        LogLevel                            log_level{};
        PathStyle                           path_style{};
        std::unordered_map<MsgID, LogLevel> overrides{};
    };

    // The body of a single log message: its display text, an optional source
    // location (absent for messages without file context, e.g. "build
    // complete"), and an opaque pointer for caller-specific metadata carried
    // through the logging pipeline.
    struct MsgData {
        void*                        user_detail{};
        std::shared_ptr<MsgLocation> location{};
        std::string                  text;
        bool                         disable_maximum_width{};
    };

    // Terminal capabilities detected at startup. Width and height are used
    // to wrap long diagnostic lines; color escape support determines
    // whether ANSI sequences should be emitted.
    struct TerminalInfo {
        bool is_tty{};
        bool use_color_escapes{};
        int width{};
        int height{};
    };

    // A fully assembled log message. The primary body lives in `data`.
    // `notes` holds supplementary context (e.g. "the previous declaration
    // was here") rendered below the main message. `plugin_name` is set when
    // the message originates from a plugin and is shown as a prefix.
    struct Msg {
        std::vector<MsgData> notes{};
        std::string plugin_name{};
        MsgData data;
        MsgKind kind{};
        MsgID id{};

        // Formats the message into a human-readable string suitable for
        // terminal display. The output accounts for terminal width, color
        // preferences, and path style.
        //
        //   Msg{.data={.text="unused variable"}, .kind=kWarning}
        //     .String(opts, term)
        //     ->  "\033[33mwarning:\033[0m unused variable"
        std::string String(const OutputOptions& options, const TerminalInfo& terminal_info) const;
    };


    // The logging facade that the rest of Guchho calls into. All logging
    // operations go through function pointers so the concrete behavior can
    // be swapped without touching calling code: NewStderrLog() prints
    // immediately, NewDeferLog() buffers for later replay, and
    // NewStringInJSLog() remaps locations inside template literals.
    //
    // Callers use the convenience methods (AddError, AddID, etc.) which
    // build a Msg from the provided parameters and invoke add_msg.
    struct Log
    {
        std::function<void(const Msg&)>     add_msg;
        std::function<bool()>               has_errors;
        std::function<std::vector<Msg>()>   peek;
        std::function<std::vector<Msg>()>   done;
        LogLevel                            level;
        std::unordered_map<MsgID, LogLevel> overrides;

        // Records an error at the given source range. Errors always use
        // MsgID::kNone because they cannot be overridden by the user.
        //
        //   log.AddError(&tracker, Range{Loc{42}, 3}, "unexpected token")
        void AddError(struct LineColumnTracker* tracker, Range r, const std::string& text);

        // Records a message identified by `id` at the given source range.
        // The actual display level may differ from `kind` if the user has
        // overridden this message ID via command-line options.
        //
        //   log.AddID(kJS_AssignToConstant, kWarning, &tracker,
        //             Range{Loc{10}, 5}, "cannot assign to constant")
        void AddID(MsgID id, MsgKind kind, struct LineColumnTracker* tracker, Range r, const std::string& text);

        // Like AddError, but attaches one or more diagnostic notes that
        // provide additional context (e.g. "did you mean ...?").
        void AddErrorWithNotes(struct LineColumnTracker* tracker, Range r, const std::string& text, const std::vector<MsgData>& notes);

        // Like AddID, but attaches diagnostic notes.
        void AddIDWithNotes(MsgID id, MsgKind kind, struct LineColumnTracker* tracker, Range r, const std::string& text, const std::vector<MsgData>& notes);

        // Low-level insertion: adds a pre-built message, applying the
        // override map to reclassify its kind if necessary.
        void AddMsgID(MsgID id, const Msg& msg);
    };

    // A single key/value pair from an import attribute declaration
    // (e.g. `import foo from "./bar.js" with { type: "json" }`).
    struct ImportAttribute {
        std::string key;
        std::string value;
    };

    // Attributes attached to an import statement, stored in a compact
    // packed string representation for memory efficiency. The raw data is
    // decoded lazily into either an ordered array or a map on demand,
    // depending on which lookup pattern the caller needs.
    struct ImportAttributes {
        std::string                                  packed_data;

        // Decodes the packed data into an ordered vector of key/value pairs.
        //
        //   ImportAttributes{"type:json;loader:ts"}.DecodeIntoArray()
        //   -> [{"type","json"}, {"loader","ts"}]
        std::vector<ImportAttribute> DecodeIntoArray() const;

        // Decodes the packed data into a map. If a key appears more than
        // once, the last value wins.
        std::unordered_map<std::string, std::string> DecodeIntoMap() const;
    };

    // A fully resolved import path, including the module specifier text,
    // an optional namespace prefix (for scoped packages or protocol
    // handlers), an ignored suffix (for side-effect-only imports), and any
    // import attributes.
    struct Path {
        std::string      text;
        std::string      namespace_{};
        std::string      ignored_suffix{};
        ImportAttributes import_attributes{};
        PathFlags        flags{};

        // Returns true when this path is marked as disabled and should
        // not appear in diagnostic output.
        bool IsDisabled() const;

        // Structural equality over every field, used by caches to decide
        // whether a cached entry can be reused.
        bool operator==(const Path& other) const {
            return text == other.text &&
                   namespace_ == other.namespace_ &&
                   ignored_suffix == other.ignored_suffix &&
                   import_attributes.packed_data == other.import_attributes.packed_data &&
                   flags == other.flags;
        }
    };

    // A single source file loaded by Guchho. Contains the file's display
    // paths, its raw text contents, and helper methods for slicing
    // sub-ranges out of that content (used by the parser and formatter
    // to extract the exact text a diagnostic refers to).
    struct Source {
        PrettyPaths pretty_paths;
        std::string identifier_name{};
        std::string contents{};
        Path        key_path;
        uint32_t    index{};

        // Returns the literal text covered by the given range.
        //
        //   source.contents = "hello world";
        //   source.TextForRange(Range{Loc{0}, 5})  ->  "hello"
        std::string TextForRange(Range r) const;

        // Scans backwards from `loc` and returns the location just before
        // any whitespace that precedes it. Useful for finding the start of
        // a token when `loc` points to trailing whitespace.
        Loc         LocBeforeWhitespace(Loc loc) const;

        // Searches backwards from `loc` for the operator string `op` and
        // returns its range if found immediately before the current token.
        // Returns an empty range if no match is found.
        Range       RangeOfOperatorBefore(Loc loc, const std::string& op) const;

        // Searches forwards from `loc` for the operator string `op` and
        // returns its range if found immediately after the current token.
        Range       RangeOfOperatorAfter(Loc loc, const std::string& op) const;

        // Returns the full range of a string literal starting at `loc`,
        // including surrounding quotes and any escape sequences.
        Range       RangeOfString(Loc loc) const;

        // Returns the full range of a numeric literal starting at `loc`,
        // including decimal points, exponents, and unit suffixes.
        Range       RangeOfNumber(Loc loc) const;

        // Returns the range of a legacy octal escape sequence (e.g. \041)
        // at the given location. Used by the escape-sequence diagnostic.
        Range       RangeOfLegacyOctalEscape(Loc loc) const;

        // Returns the comment text with leading indentation stripped,
        // suitable for display in diagnostic output.
        std::string CommentTextWithoutIndent(const Range& r) const;

        // Structural equality over every field, used by caches to decide
        // whether a cached parse entry can be reused.
        bool operator==(const Source& other) const;
    };

    // Hash functor enabling `logger::Path` to be used as a key in
    // `std::unordered_map` containers.
    struct PathHash {
        size_t operator()(const Path& path) const;
    };


    // Converts byte offsets (Loc/Range) into 1-based line and column
    // numbers for diagnostic display. The scanner caches its position so
    // sequential queries on the same source file are O(1) amortized.
    //
    //   LineColumnTracker tracker(&source);
    //   auto loc = tracker.MakeMsgData(Range{Loc{42}, 3}, "error here");
    //   // loc->line == 2, loc->column == 10
    //   //   (if byte 42 falls on line 2, column 10 of the source)
    struct LineColumnTracker {
        LineColumnTracker() = default;
        explicit LineColumnTracker(const Source* source);

        // Builds a MsgData with the source location filled in for the
        // given range. Returns a shared_ptr so callers can store the
        // location alongside the message without copying.
        MsgData MakeMsgData(Range r, const std::string& text);

        // Returns a MsgLocation for the range, or nullptr if the source
        // has not been set. This is the lower-level query used by code
        // that needs the location without the surrounding MsgData wrapper.
        std::shared_ptr<MsgLocation> MsgLocationOrNil(Range r);

    private:
        std::string contents_;
        PrettyPaths pretty_paths_;

        int32_t offset_{0};
        int32_t line_{0};
        int32_t line_start_{0};
        int32_t line_end_{0};

        bool has_line_start_{false};
        bool has_line_end_{false};
        bool has_source_{false};

        // Advances the scanner to the given byte offset, updating the
        // cached line/start/end boundaries as it goes.
        void ScanTo(int32_t offset);

        // Computes the 1-based line number and 1-based column at the
        // given offset, also recording the start and end byte offsets
        // of the line containing that offset.
        void ComputeLineAndColumn(
            int offset,
            int32_t& line_count,
            int32_t& column_count,
            int32_t& line_start,
            int32_t& line_end);
    };

    // Pre-formatted fragments that make up one diagnostic display. These
    // are assembled by DetailStruct() and consumed by the stderr printer
    // to produce the final multi-line output with underlines and markers.
    struct MsgDetail {
        std::string source_before;
        std::string source_marked;
        std::string source_after;
        std::string indent;
        std::string marker;
        std::string suggestion;
        std::string content_after;
        std::string path;
        int         line{};
        int         column{};
    };

    // Internal state for the stderr-backed log. Tracks total and shown
    // error/warning counts, enforces the message limit, and holds
    // deferred warnings that are only emitted if the build ultimately fails.
    struct StderrLogState {
        std::mutex       mutex;
        std::vector<Msg> msgs;
        TerminalInfo     terminal_info;
        OutputOptions    options;
        int              errors = 0;
        int              warnings = 0;
        int              shownErrors = 0;
        int              shownWarnings = 0;
        bool             hasErrors = false;
        int              remainingMessagesBeforeLimit = 0x7FFFFFFF;
        std::vector<Msg> deferredWarnings;
    };

    // Internal state for a deferred log. Buffers all messages in memory
    // without printing, allowing them to be replayed later via the done()
    // callback.
    struct DeferLogState {
        std::mutex       mutex;
        std::vector<Msg> msgs;
        bool             hasErrors = false;
    };


    // One row of the build summary table printed at the end of output.
    // Shows the output file path, its size, and whether it is a source map.
    struct SummaryTableEntry {
        std::string dir;
        std::string base;
        std::string size;
        int         bytes{};
        bool        is_source_map{};
    };

    // Maps a line/column position inside a string literal (e.g. a template
    // literal or JSX text node) back to the corresponding position in the
    // outer source file. This enables diagnostics to point at the correct
    // location even when the parser is operating on inner content that does
    // not correspond directly to the file.
    struct StringInJSTableEntry {
        int32_t inner_line{};
        int32_t inner_column{};
        Loc     inner_loc;
        Loc     outer_loc;
    };


    // ANSI color escape sequences for terminal output. The palette is split
    // into foreground colors (red, green, etc.) and compound background+
    // foreground pairs (red_bg_white) used for highlighted markers in
    // diagnostic output.
    struct Colors {
        std::string_view reset;
        std::string_view bold;
        std::string_view dim;
        std::string_view underline;

        std::string_view red;
        std::string_view green;
        std::string_view blue;

        std::string_view cyan;
        std::string_view magenta;
        std::string_view yellow;

        std::string_view red_bg_red;
        std::string_view red_bg_white;
        std::string_view green_bg_green;
        std::string_view green_bg_white;
        std::string_view blue_bg_blue;
        std::string_view blue_bg_white;

        std::string_view cyan_bg_cyan;
        std::string_view cyan_bg_black;
        std::string_view magenta_bg_magenta;
        std::string_view magenta_bg_black;
        std::string_view yellow_bg_yellow;
        std::string_view yellow_bg_black;
    };

    inline constexpr Colors kTerminalColors{
        .reset = "\033[0m",
        .bold = "\033[1m",
        .dim = "\033[37m",
        .underline = "\033[4m",

        .red = "\033[31m",
        .green = "\033[32m",
        .blue = "\033[34m",

        .cyan = "\033[36m",
        .magenta = "\033[35m",
        .yellow = "\033[33m",

        .red_bg_red = "\033[41;31m",
        .red_bg_white = "\033[41;97m",
        .green_bg_green = "\033[42;32m",
        .green_bg_white = "\033[42;97m",
        .blue_bg_blue = "\033[44;34m",
        .blue_bg_white = "\033[44;97m",

        .cyan_bg_cyan = "\033[46;36m",
        .cyan_bg_black = "\033[46;30m",
        .magenta_bg_magenta = "\033[45;35m",
        .magenta_bg_black = "\033[45;30m",
        .yellow_bg_yellow = "\033[43;33m",
        .yellow_bg_black = "\033[43;30m",
    };


    // Packs a map of import attributes into the compact string format used
    // by ImportAttributes::packed_data. Keys and values are separated by
    // colons; pairs are separated by semicolons.
    //
    //   EncodeImportAttributes({{"type","json"}, {"loader","ts"}})
    //   -> ImportAttributes{.packed_data="type:json;loader:ts"}
    ImportAttributes EncodeImportAttributes(const std::unordered_map<std::string, std::string>& value);

    // Builds a mapping table that relates byte offsets inside a string
    // literal's inner content back to offsets in the outer source file.
    // Used when parsing template literals and JSX text so that diagnostics
    // can point at the correct source location.
    //
    //   GenerateStringInJSTable("`hello\nworld`", Loc{0}, "hello\nworld")
    //   -> [{inner_line:1, inner_column:1, inner_loc:Loc{1}, outer_loc:Loc{1}}, ...]
    std::vector<StringInJSTableEntry> GenerateStringInJSTable(const std::string& outer_contents, Loc outer_string_literal_loc, const std::string& inner_contents);

    // Remaps a location from the inner content of a string literal to the
    // corresponding location in the outer source file using the mapping
    // table produced by GenerateStringInJSTable().
    //
    //   RemapStringInJSLoc(table, Loc{7})  ->  Loc{12}
    //   (adjusted for surrounding quotes and escape sequences)
    Loc RemapStringInJSLoc(const std::vector<StringInJSTableEntry>& table, Loc inner_loc);

    // Wraps an existing Log so that locations inside a string literal are
    // transparently remapped to outer-source coordinates before the
    // message is stored. This lets the parser log diagnostics using inner
    // offsets while the final output shows correct outer positions.
    Log NewStringInJSLog(Log log, LineColumnTracker& outer_tracker, const std::vector<StringInJSTableEntry>& table);

    // Creates a Log that prints messages to stderr with full formatting
    // (color, underlines, source context). The OutputOptions control
    // filtering, path style, and color mode.
    Log NewStderrLog(const OutputOptions& options);

    // Creates a Log that buffers messages in memory without printing.
    // When `kind` is kDeferLogNoVerboseOrDebug, Verbose and Debug
    // messages are still emitted immediately through the original log.
    // Use done() to retrieve all buffered messages.
    Log NewDeferLog(DeferLogKind kind, const std::unordered_map<MsgID, LogLevel>& overrides);

    // Splits a file path into its directory, base name, and extension
    // components. Platform-independent: handles both '/' and '\' separators.
    //
    //   PlatformIndependentPathDirBaseExt("src/utils/index.ts", dir, base, ext)
    //   -> dir="src/utils", base="index", ext=".ts"
    void             PlatformIndependentPathDirBaseExt(const std::string& path, std::string& dir, std::string& base, std::string& ext);

    // Applies the user's message-level overrides to reclassify a message
    // kind. If the ID has no override entry, the original kind is returned
    // unchanged.
    //
    //   AllowOverride(overrides, kJS_BigInt, kWarning)  ->  kError
    //   (if the user specified --warning-level kJS_BigInt=error)
    MsgKind          AllowOverride(const std::unordered_map<MsgID, LogLevel>& overrides, MsgID id, MsgKind kind);

    // Parses command-line arguments to produce OutputOptions. Recognized
    // flags include --color, --log-level, --path-style, and per-message
    // --warning-level overrides.
    OutputOptions    OutputOptionsForArgs(const std::vector<std::string>& os_args);


    // Prints a plain error message to stderr with the standard prefix.
    // The message is always displayed regardless of log level.
    //
    //   PrintErrorToStderr(args, "file not found: foo.js")
    void PrintErrorToStderr(const std::vector<std::string>& os_args, const std::string& text);

    // Prints an error message followed by a note on a separate line.
    // The note provides additional context (e.g. "did you mean ...?").
    void PrintErrorWithNoteToStderr(const std::vector<std::string>& os_args, const std::string& text, const std::string& note);

    // Prints a fully formatted log message to stderr, including color,
    // underlines, and source context when applicable.
    void PrintMessageToStderr(const std::vector<std::string>& os_args, const Msg& msg);

    // Writes arbitrary text to the given file descriptor, applying the
    // appropriate ANSI color sequences based on the log level. The
    // callback receives a Colors struct and returns the text to write.
    void PrintText(int fd, LogLevel level, const std::vector<std::string>& os_args, const std::function<std::string(const Colors&)>& callback);

    // Like PrintText, but with an explicit color policy instead of
    // inferring it from terminal detection.
    void PrintTextWithColor(int fd, UseColor use_color, const std::function<std::string(const Colors&)>& callback);

    // Formats a log message into a single-line string suitable for
    // terminal display. When `include_source` is true, the output
    // includes the file path and line number prefix.
    //
    //   MsgString(true, kRelPath, term, kNone, kWarning,
    //             {.text="unused"}, "")
    //   -> "warning: unused variable"
    std::string              MsgString(bool include_source, PathStyle path_style, const TerminalInfo& terminal_info, MsgID id, MsgKind kind, const MsgData& data, const std::string& plugin_name);

    // Breaks a message into the structured fragments that make up a
    // multi-line diagnostic display: the source snippet, the underline
    // marker, any suggestion text, and the file path footer.
    MsgDetail                DetailStruct(const MsgData& data, PathStyle path_style, const TerminalInfo& terminal_info, int max_margin);

    // Wraps text with ANSI escape sequences that produce a clickable
    // terminal hyperlink (OSC 8) around the underlined portion. On
    // terminals that do not support hyperlinks, this degrades gracefully
    // to plain underlined text.
    std::string              LinkifyText(std::string_view text, std::string_view underline, std::string_view reset);

    // Splits a string into words and wraps them to fit within the given
    // terminal width. Words are never split mid-token; a word that does
    // not fit on the current line is moved to the next.
    //
    //   WrapWordsInString("hello world foo", 10)  ->  {"hello", "world foo"}
    std::vector<std::string> WrapWordsInString(const std::string& text, int width);

    // Estimates the display width of a string in terminal columns,
    // accounting for multi-byte UTF-8 characters that occupy more than
    // one column (e.g. CJK ideographs).
    int                      EstimateWidthInTerminal(const std::string& text);

    // Replaces tab characters in a string with the appropriate number of
    // spaces to align to the next tab stop, given `spaces_per_tab`.
    std::string              RenderTabStops(const std::string& with_tabs, int spaces_per_tab);


    // Detects terminal capabilities (width, height, TTY status, color
    // support) from the given file descriptor. Pass STDOUT_FILENO for
    // standard output or STDERR_FILENO for error output.
    TerminalInfo     GetTerminalInfo(int file_descriptor);

    // Returns true if the named environment variable exists and is set to
    // a non-empty, non-"0", non-"false" value.
    bool             HasEnvironmentVariableValue(std::string_view name);

    // Heuristic check for the Windows Command Prompt, which does not
    // support ANSI color escapes in older versions.
    bool             IsProbablyWindowsCommandPrompt();

    // Writes a string to the given file descriptor, emitting ANSI color
    // escape sequences only when the terminal supports them.
    void             WriteStringWithColor(int fd, const std::string& text);

    // Parses a semicolon-delimited string of message-ID overrides
    // (e.g. "kJS_BigInt=error;kCSS_CSSSyntaxError=warning") and merges
    // them into the provided overrides map.
    void             StringToMsgIDs(std::string_view str, LogLevel logLevel, std::unordered_map<MsgID, LogLevel>& overrides);

    // Returns the human-readable name of a message ID (e.g.
    // MsgID::kJS_BigInt -> "kJS_BigInt"). Used in diagnostic output and
    // in the --warning-level flag.
    std::string_view MsgIDToString(MsgID id);

    // Parses a string into the highest-severity message ID that starts
    // with the given prefix. Returns kNone if no match is found.
    MsgID            StringToMaximumMsgID(std::string_view id);

    // Prints the build summary table to stderr, showing output file sizes
    // and whether source maps were generated. If `elapsed_ms` is non-null,
    // the total build time is included in the summary.
    void             PrintSummary(UseColor use_color, std::vector<SummaryTableEntry>& table, const double* elapsed_ms);


    // Central catalog of every user-facing diagnostic message emitted by
    // Guchho. Each entry maps to a std::format-style template string that
    // callers use through FormatMsg(). Keeping every message in one enum
    // ensures the full set of user-visible strings lives in a single place
    // rather than being scattered across dozens of source files.
    //
    // Entry names are grouped by the subsystem that emits them. The format
    // templates themselves live in "src/core/logger/message.cpp" inside
    // MsgTemplate(). Named MsgCat (not Msg) because logger.hpp already
    // declares struct Msg.
    enum class MsgCat : uint16_t {
        kNone,

        // -------------------------------------------------------------------
        // JavaScript binder (js_bind.cpp)
        // -------------------------------------------------------------------
        kJS_DuplicateCaseNever,
        kJS_DuplicateCaseMay,
        kJS_DuplicateCaseEarlierNote,
        kJS_DuplicateProperty,
        kJS_DuplicatePropertyOriginalNote,
        kJS_PrivateNameNotInEnclosing,
        kJS_NullishCoalescingAlwaysReturns,
        kJS_NullishCoalescingLeftNote,
        kJS_LogicalAlwaysReturns,
        kJS_SuspiciousArrowNote,
        kJS_CommonJSVariableInESM,
        kJS_CommonJSVariableCJSNote,
        kJS_CommonJSVariableTSNote,
        kJS_SuspiciousAssignToDefine,
        kJS_SuspiciousAssignToDefineNote,
        kJS_ImpossibleTypeof,
        kJS_ImpossibleTypeofNullNote,
        kJS_EqualsNegativeZeroOperator,
        kJS_EqualsNegativeZeroCase,
        kJS_EqualsNegativeZeroNote,
        kJS_EqualsNaNOperator,
        kJS_EqualsNaNSwitchCase,
        kJS_EqualsNaNNote,
        kJS_EqualsNewObjectOperator,
        kJS_EqualsNewObjectSwitchCase,
        kJS_EqualsNewObjectNote,
        kJS_AssignToInjectedImport,
        kJS_AssignToInjectedImportNote,
        kJS_CannotEscapeName,
        kJS_SymbolAlreadyDeclared,
        kJS_SymbolOriginalDeclaredNote,
        kJS_DuplicateFunctionNestedBlocks,
        kJS_UnexpectedParenInRegexp,
        kJS_UnsupportedRegexp,
        kJS_UnsupportedRegexpNote,
        kJS_UnsupportedRegexpFlag,
        kJS_UnsupportedRegexpUnicodePropertyEscape,
        kJS_UnsupportedRegexpNamedCaptureGroup,
        kJS_UnsupportedRegexpLookbehind,
        kJS_NonDefaultJSONImportUndefined,
        kJS_UseStrictNonSimpleParamList,
        kJS_CannotAssignToImport,
        kJS_AssignToImportThrowNote,
        kJS_AssignToImportWillThrow,
        kJS_IndirectRequire,
        kJS_TopLevelThisUndefined,
        kJS_DuplicateFnDeclNested,
        kJS_DuplicateFnDeclModule,
        kJS_CannotAccessName,
        kJS_NoContainingLabel,
        kJS_LegacyHTMLCommentInESM,
        kJS_InvalidJSXRuntime,
        kJS_InvalidJSXRuntimeNote,
        kJS_JSXFactoryAutomatic,
        kJS_InvalidJSXFactory,
        kJS_JSXFragmentAutomatic,
        kJS_InvalidJSXFragment,
        kJS_JSXImportSourceAutomatic,
        kJS_JSXImportSourceAutomaticNote,
        kJS_JSXRuntimeInvalid,
        kJS_JSXRuntimeInvalidNote,
        kJS_CannotUseNameIdentifier,
        kJS_DecoratorError,
        kJS_DecoratorErrorCall,
        kJS_DecoratorWrapNote,
        kJS_DecoratorExpressionPosition,
        kJS_ParameterDecoratorsExperimental,
        kJS_ParameterDecoratorsExperimentalNote,
        kJS_ParameterDecoratorsInJS,
        kJS_BoundMultipleTimesInParamList,
        kJS_BoundMultipleTimesOriginalNote,
        kJS_NotDeclaredInThisFile,
        kJS_CannotUseBreak,
        kJS_CannotContinueToLabel,
        kJS_CannotUseContinue,
        kJS_DuplicateLabel,
        kJS_DuplicateLabelOriginalNote,
        kJS_TopLevelReturnInESM,
        kJS_InvalidAssignmentTarget,
        kJS_CannotUseNewTarget,
        kJS_LegacyOctalInTemplate,
        kJS_ImportMetaNotAvailable,
        kJS_ImportMetaNotAvailableNote,
        kJS_ClassNameBeforeInit,
        kJS_AssignToConstant,
        kJS_AssignToConstantThrowNote,
        kJS_AssignToConstantWillThrow,
        kJS_AssignToInjectedImportNote2,
        kJS_AssignToDefineNote,
        kJS_DuplicatePropFound,
        kJS_DuplicatePropFoundNote,
        kJS_KeyShorthandNotAllowedNote,
        kJS_CannotAssignToInjectedImport2,
        kJS_ReadOnlyPrivateMethod,
        kJS_GetterOnlyPrivateProperty,
        kJS_SetterOnlyPrivateProperty,
        kJS_CannotAssignToPropertyOnImport,
        kJS_CannotAssignToPropertyOnImportNote,
        kJS_DeletePropertyOfSuper,
        kJS_UnexpectedCommaAfterRest,
        kJS_ProtoPropertyMoreThanOnce,
        kJS_ProtoPropertyEarlierNote,
        kJS_ImportExpressionNotRecognized,
        kJS_ImportNotBundledNotStringLiteral,
        kJS_DirectEval,
        kJS_DirectEvalNote,
        kJS_ConvertingRequireToESM,
        kJS_RequireNotBundledNotStringLiteral,
        kJS_RequireCallNotBundledNotStringLiteral,
        kJS_RequireNotBundledArgCount,
        kJS_RequireNotBundledArgCountNote,
        kJS_RequireArgCountNote,

        // -------------------------------------------------------------------
        // JavaScript parser (js_parser.cpp)
        // -------------------------------------------------------------------
        kJS_SymbolAlreadyDeclared_2,
        kJS_SymbolOriginalDeclared_2,
        kJS_UnexpectedEquals,
        kJS_UnexpectedToken,
        kJS_InvalidBindingPattern,
        kJS_OperatorsWithoutParens,
        kJS_OperatorsWithoutParensNote,
        kJS_AwaitExpressionHere,
        kJS_YieldExpressionHere,
        kJS_TopLevelAwaitNotSupported,
        kJS_ArbitraryImportSecondArg,
        kJS_TopLevelAwaitNotAvailable,
        kJS_DeferredImportsNotAvailable,
        kJS_SourcePhaseImportsNotAvailable,
        kJS_StringNamespaceIdentifier,
        kJS_BigIntNotAvailable,
        kJS_ImportMetaNotAvailable_2,
        kJS_FeatureNotAvailable,
        kJS_TransformingNotSupported,
        kJS_FeatureCannotBeUsedWhere,
        kJS_FeatureCannotBeUsedWithESM,
        kJS_ForLoopSingleDeclaration,
        kJS_ForLoopNoInitializer,
        kJS_AsyncFnNamedAwait,
        kJS_GeneratorFnNamedYield,
        kJS_AssertKeywordNotSupported,
        kJS_AssertKeywordNote,
        kJS_DeclarationInSingleStatement,
        kJS_UsingInSwitchCase,

        // -------------------------------------------------------------------
        // JavaScript statement parser (js_parser_statement.cpp)
        // -------------------------------------------------------------------
        kJS_NewlineBeforeArrow,
        kJS_NewlineAfterAsync,
        kJS_NewlineAfterType,
        kJS_DecoratorsNotValidHere,
        kJS_MultipleDefaultClauses,
        kJS_AwaitOutsideAsync,
        kJS_LetWrappedInParens,
        kJS_UsingDeclarationsNotAllowed,
        kJS_AwaitUsingDeclarationsNotAllowed,
        kJS_ReturnNotUsableHere,
        kJS_NewlineAfterThrow,
        kJS_UnexpectedInterface,
        kJS_ExpressionNotReturned,
        kJS_DecoratorsOnConstructors,
        kJS_DecoratorsOnlyClassDeclarations,
        kJS_DecoratorsNotClassExpression,
        kJS_EnableExperimentalDecoratorsNote,
        kJS_WrapDecoratorInParensNote,
        kJS_DotNotAllowedAfterDecoratorCall,
        kJS_MultipleConstructors,
        kJS_AwaitAsIdentifier,
        kJS_CannotUseNameIdentifier_2,
        kJS_TSDecoratorsPrivateIdentifier,
        kJS_DeclarePrivateIdentifier,
        kJS_DeclareIndexSignature,
        kJS_InvalidFieldName,
        kJS_DeclareCannotBeUsedWith,
        kJS_ConstructorCannotBeGetter,
        kJS_ConstructorCannotBeSetter,
        kJS_ConstructorCannotBeAsync,
        kJS_ConstructorCannotBeGenerator,
        kJS_InvalidStaticMethodName,
        kJS_GetterZeroArgs,
        kJS_SetterExactlyOneArg,
        kJS_InvalidMethodName,
        kJS_AliasInvalidSurrogate,
        kJS_DuplicateImport,
        kJS_DuplicateImportFirstNote,
        kJS_LetAsIdentifier,
        kJS_ExpectedIdentifierAfterNamespace,
        kJS_UnexpectedDash,
        kJS_DuplicateJSXAttribute,
        kJS_DuplicateJSXAttributeNote,
        kJS_UnexpectedBackslashInJSX,
        kJS_BackslashEscapeNote,
        kJS_StringInsideBraceNote,
        kJS_ClosingTagMismatch,
        kJS_ClosingTagOpeningNote,
        kJS_UnexpectedEOFBeforeClosing,
        kJS_OpeningTagNote,

        // -------------------------------------------------------------------
        // JavaScript expression parser (js_parser_expression.cpp)
        // -------------------------------------------------------------------
        kJS_RestArgDefaultInitializer,
        kJS_UnexpectedColon,
        kJS_UnexpectedEllipsis,
        kJS_ForLoopAsyncOf,
        kJS_NameMustBeInitialized,
        kJS_ThisMustBeInitialized,
        kJS_UnexpectedSuper,
        kJS_CannotUseThis,
        kJS_AwaitCannotBeUsed,
        kJS_AwaitCannotBeEscaped,
        kJS_YieldCannotBeUsed,
        kJS_YieldCannotBeEscaped,
        kJS_YieldWithoutParens,
        kJS_YieldOutsideGenerator,
        kJS_DeletePrivateName,
        kJS_JSXNotEnabled,
        kJS_JSXNotEnabledNote,
        kJS_MTSCtsExtension,
        kJS_ImportWithoutParens,
        kJS_UnparenthesizedOptionalChainNew,
        kJS_TemplateLiteralsOptionalChainTag,
        kJS_SuspiciousBangIn,
        kJS_SuspiciousBangInNote,
        kJS_SuspiciousBangInstanceof,
        kJS_SuspiciousBangInstanceofNote,

        // -------------------------------------------------------------------
        // JavaScript import/export parser (js_parser_import.cpp)
        // -------------------------------------------------------------------
        kJS_MultipleExportsSameName,
        kJS_MultipleExportsOriginalNote,
        kJS_NonDefaultJSONImportWithAssertion,
        kJS_ImportNamespaceCrash,
        kJS_ImportNamespaceDefaultImportNote,
        kJS_ImportNamespaceESModuleInteropNote,

        // -------------------------------------------------------------------
        // TypeScript parser (ts_parser.cpp)
        // -------------------------------------------------------------------
        kTS_UnexpectedConst,
        kTS_UnexpectedToken,
        kTS_ModifierNotValid,
        kTS_ExpectedCommaAfterValueInEnum,
        kTS_ExpectedCommaBeforeNextInEnum,

        // -------------------------------------------------------------------
        // JS legacy lexer (js_lexer.cpp)
        // -------------------------------------------------------------------
        kJS_UnexpectedEOF,
        kJS_SyntaxError,
        kJS_SyntaxErrorHex,
        kJS_SyntaxErrorUnicode,
        kJS_SyntaxErrorQuote,
        kJS_AwaitInAsyncFunction,
        kJS_ConsiderAddingAsyncNote,
        kJS_ExpectedWithSuffix,
        kJS_UnexpectedXF,
        kJS_ExpectedButFound,
        kJS_UnexpectedX,
        kJS_CharNotValidInJSX,
        kJS_TSXArrowDisambiguation,
        kJS_DidYouMeanEscape,
        kJS_ExpectedCommentTerminator,
        kJS_CommentStartsHere,
        kJS_UnterminatedStringLiteral,
        kJS_TreatingAsLegacyHTMLComment,
        kJS_JSONNoComments,
        kJS_JSONStringsDoubleQuotes,
        kJS_InvalidIdentifier,
        kJS_UnterminatedRegexp,
        kJS_DuplicateRegexpFlag,
        kJS_UnicodeEscapeOutOfRange,

        // -------------------------------------------------------------------
        // JSON parser (json_parser.cpp)
        // -------------------------------------------------------------------
        kJSON_NoTrailingCommas,
        kJSON_DuplicateKey,
        kJSON_DuplicateKeyOriginalNote,

        // -------------------------------------------------------------------
        // Source maps (js_sourcemap.cpp)
        // -------------------------------------------------------------------
        kSourceMap_Invalid,
        kSourceMap_ExpectedOffsetObject,
        kSourceMap_ExpectedMapObject,
        kSourceMap_ExpectedSectionsArray,
        kSourceMap_BadMappings,

        // -------------------------------------------------------------------
        // CSS parser (css_parser.cpp)
        // -------------------------------------------------------------------
        kCSS_ExpectedButFound,
        kCSS_Expected,
        kCSS_ExpectedToGoWith,
        kCSS_UnbalancedNote,
        kCSS_Unexpected,
        kCSS_AtCharsetMustBeFirst,
        kCSS_CharsetRuleBeforeNote,
        kCSS_UnsupportedCharsetWillUseUTF8,
        kCSS_AtImportOnlyTopLevel,
        kCSS_AllAtImportsMustComeFirst,
        kCSS_AtImportRuleBeforeNote,
        kCSS_KeyframesNameWithoutQuotes,
        kCSS_KeyframesQuoteNote,
        kCSS_AtNamespaceNotSupported,
        kCSS_LayerNameNotAllowed,
        kCSS_WhitespaceBothSidesOperator,
        kCSS_InfixOnlyOperator,
        kCSS_ExpectedColon,
        kCSS_NotAKnownCSSProperty,
        kCSS_DidYouMeanNote,

        // -------------------------------------------------------------------
        // CSS lexer (css_lexer.cpp)
        // -------------------------------------------------------------------
        kCSS_ExpectedParenEndURLToken,
        kCSS_UnbalancedParenNote,
        kCSS_InvalidEscape,
        kCSS_NonPrintCharURLToken,
        kCSS_UnterminatedStringToken,
        kCSS_ExpectedCommentTerminator_2,
        kCSS_CommentStartsHere_2,
        kCSS_CommentsUseSlashStar,

        // -------------------------------------------------------------------
        // CSS selectors (css_selectors.cpp)
        // -------------------------------------------------------------------
        kCSS_UnexpectedCommaInside,
        kCSS_UnexpectedCommaInsideNote,
        kCSS_TypeSelectorAfterNesting,
        kCSS_TypeSelectorAfterNestingNotes,

        // -------------------------------------------------------------------
        // CSS nesting (css_nesting.cpp)
        // -------------------------------------------------------------------
        kCSS_TooMuchExpansion,
        kCSS_TooMuchExpansionNote,
        kCSS_NestingNotSupportedInTarget,
        kCSS_NestingNotSupportedInTargetNote,

        // -------------------------------------------------------------------
        // CSS helpers (css_helpers.cpp)
        // -------------------------------------------------------------------
        kCSS_ComposesNotValidHere,
        kCSS_ComposesOnlySingleClass,
        kCSS_ComposesOnlySingleClassNote,

        // -------------------------------------------------------------------
        // CSS composes (css_composes.cpp)
        // -------------------------------------------------------------------
        kCSS_ComposesInvalidLocation,
        kCSS_UnexpectedToken,

        // -------------------------------------------------------------------
        // HTML bridge (html_bridge.cpp)
        // -------------------------------------------------------------------
        kHTML_ParseError,
        kHTML_EmptyResourceURL,

        // -------------------------------------------------------------------
        // HTML lexer (html_lexer.cpp)
        // -------------------------------------------------------------------
        kHTML_ControlCharacterInInputStream,
        kHTML_NoncharacterInInputStream,
        kHTML_SurrogateInInputStream,
        kHTML_NonVoidHtmlElementStartTagWithTrailingSolidus,
        kHTML_EndTagWithAttributes,
        kHTML_EndTagWithTrailingSolidus,
        kHTML_UnexpectedSolidusInTag,
        kHTML_UnexpectedNullCharacter,
        kHTML_UnexpectedQuestionMarkInsteadOfTagName,
        kHTML_InvalidFirstCharacterOfTagName,
        kHTML_UnexpectedEqualsSignBeforeAttributeName,
        kHTML_MissingEndTagName,
        kHTML_UnexpectedCharacterInAttributeName,
        kHTML_UnknownNamedCharacterReference,
        kHTML_MissingSemicolonAfterCharacterReference,
        kHTML_UnexpectedCharacterAfterDoctypeSystemIdentifier,
        kHTML_UnexpectedCharacterInUnquotedAttributeValue,
        kHTML_EofBeforeTagName,
        kHTML_EofInTag,
        kHTML_MissingAttributeValue,
        kHTML_MissingWhitespaceBetweenAttributes,
        kHTML_MissingWhitespaceAfterDoctypePublicKeyword,
        kHTML_MissingWhitespaceBetweenDoctypePublicAndSystemIdentifiers,
        kHTML_MissingWhitespaceAfterDoctypeSystemKeyword,
        kHTML_MissingQuoteBeforeDoctypePublicIdentifier,
        kHTML_MissingQuoteBeforeDoctypeSystemIdentifier,
        kHTML_MissingDoctypePublicIdentifier,
        kHTML_MissingDoctypeSystemIdentifier,
        kHTML_AbruptDoctypePublicIdentifier,
        kHTML_AbruptDoctypeSystemIdentifier,
        kHTML_CdataInHtmlContent,
        kHTML_IncorrectlyOpenedComment,
        kHTML_EofInScriptHtmlCommentLikeText,
        kHTML_EofInDoctype,
        kHTML_NestedComment,
        kHTML_AbruptClosingOfEmptyComment,
        kHTML_EofInComment,
        kHTML_IncorrectlyClosedComment,
        kHTML_EofInCdata,
        kHTML_AbsenceOfDigitsInNumericCharacterReference,
        kHTML_NullCharacterReference,
        kHTML_SurrogateCharacterReference,
        kHTML_CharacterReferenceOutsideUnicodeRange,
        kHTML_ControlCharacterReference,
        kHTML_NoncharacterCharacterReference,
        kHTML_MissingWhitespaceBeforeDoctypeName,
        kHTML_MissingDoctypeName,
        kHTML_InvalidCharacterSequenceAfterDoctypeName,
        kHTML_DuplicateAttribute,
        kHTML_NonConformingDoctype,
        kHTML_MissingDoctype,
        kHTML_MisplacedDoctype,
        kHTML_EndTagWithoutMatchingOpenElement,
        kHTML_ClosingOfElementWithOpenChildElements,
        kHTML_DisallowedContentInNoscriptInHead,
        kHTML_OpenElementsLeftAfterEof,
        kHTML_AbandonedHeadElementChild,
        kHTML_MisplacedStartTagForHeadElement,
        kHTML_NestedNoscriptInHead,
        kHTML_EofInElementThatCanContainOnlyText,

        // -------------------------------------------------------------------
        // Resolver (resolver.cpp)
        // -------------------------------------------------------------------
        kResolver_CannotFindTSConfig,
        kResolver_CannotReadFile,
        kResolver_GlobPatternNoMatch,
        kResolver_EmptyGlob,
        kResolver_BaseConfigCycle,
        kResolver_CannotFindBaseConfig,
        kResolver_CannotReadDirectory,
        kResolver_MainFieldIgnoredNeutral,
        kResolver_MainFieldIgnoredList,
        kResolver_PnPForbidsImport,
        kResolver_PnPPeerDependency,

        // -------------------------------------------------------------------
        // package.json (package_json.cpp)
        // -------------------------------------------------------------------
        kPackageJSON_KeysBothStartAndNotWithDot,
        kPackageJSON_IncompatibleKeyNote,
        kPackageJSON_DeadCondition,
        kPackageJSON_DeadConditionMessage,
        kPackageJSON_ValueMustBeStringOrArrayOrNull,
        kPackageJSON_CannotReadFile,
        kPackageJSON_InvalidTypeValue,
        kPackageJSON_TypeFieldMustBeString,
        kPackageJSON_BrowserMappingStringOrBoolean,
        kPackageJSON_ExpectedStringInArray,
        kPackageJSON_SideEffectsBooleanOrArray,
        kPackageJSON_ImportsMustBeObject,
        kPackageJSON_ImportsMapIgnoredInvalidSpecifier,
        kPackageJSON_RemappedPathCouldNotBeResolved,
        kPackageJSON_ModuleSpecifierInvalid,
        kPackageJSON_PackageTargetInvalid,
        kPackageJSON_PathDisabledByPackageAuthor,
        kPackageJSON_PathNotExportedByPackage,
        kPackageJSON_FileExportedAtPath,
        kPackageJSON_ImportFromToGetFile,
        kPackageJSON_PackageImportNotDefined,
        kPackageJSON_ModuleNotFound,
        kPackageJSON_ImportingDirectoryForbidden,
        kPackageJSON_PropertyKeyMakesDirectoryForbidden,
        kPackageJSON_PathNotCurrentlyExportedByPackage,
        kPackageJSON_NoConditionsMatch,
        kPackageJSON_ConsiderEnablingCondition,

        // -------------------------------------------------------------------
        // guchho.json (guchho_json.cpp)
        // -------------------------------------------------------------------
        kGuchhoJSON_UnknownField,
        kGuchhoJSON_InvalidFormat,
        kGuchhoJSON_InvalidPlatform,
        kGuchhoJSON_InvalidSourcemap,
        kGuchhoJSON_UnsupportedDefine,
        kGuchhoJSON_UnsupportedDefineValue,
        kGuchhoJSON_UnsupportedCompoundDefine,
        kGuchhoJSON_UnsupportedCompoundDefineValue,
        kGuchhoJSON_InvalidLogLevel,
        kGuchhoJSON_InvalidLogLevelValue,
        kGuchhoJSON_PluginsNotImplemented,
        kGuchhoJSON_CannotReadFile,

        // -------------------------------------------------------------------
        // tsconfig.json (tsconfig_json.cpp)
        // -------------------------------------------------------------------
        kTSConfig_InvalidJSXMember,
        kTSConfig_InvalidPattern,
        kTSConfig_NonRelativePath,
        kTSConfig_UnrecognizedTarget,
        kTSConfig_InvalidImportsNotUsedAsValues,
        kTSConfig_SubstitutionsShouldBeArray,
        kTSConfig_OptionNotNested,

        // -------------------------------------------------------------------
        // guchho.config.js (guchho_json.cpp)
        // -------------------------------------------------------------------
        kGuchhoConfig_FailedToSpawn,
        kGuchhoConfig_FailedToEvaluate,
        kGuchhoConfig_DidNotProduceConfigObject,
        kGuchhoConfig_NotJSONObject,

        // -------------------------------------------------------------------
        // Yarn PnP (yarnpnp.cpp)
        // -------------------------------------------------------------------
        kResolver_CannotReadFile_2,

        // -------------------------------------------------------------------
        // Bundler (bundler.cpp)
        // -------------------------------------------------------------------
        kBundler_PluginNotAbsolutePath,
        kBundler_PluginNonAbsolutePath,
        kBundler_DifferentPathCase,
        kBundler_CannotReadFile,
        kBundler_CannotReadFileWithError,
        kBundler_CouldNotLoadDataURL,
        kBundler_BundlingPhaseImportsNotSupported,
        kBundler_BundlingPhaseImportsNotSupportedUnlessExternal,
        kBundler_UnsupportedSourceMapCommentWithError,
        kBundler_UnsupportedSourceMapComment,
        kBundler_UnsupportedSourceMapCommentScheme,
        kBundler_UnsupportedSourceMapCommentHost,
        kBundler_UnsupportedSourceMapCommentResolveDir,
        kBundler_CannotReadFile_2,
        kBundler_CannotReadFileWithError_2,
        kBundler_NoLoaderConfigured,
        kBundler_DoNotKnowHowToLoadPath,
        kBundler_CouldNotResolve,
        kBundler_ShouldBeMarkedExternal,
        kBundler_IgnoredDynamicImport,
        kBundler_DynamicImportHandlerNote,
        kBundler_PanicParsing,
        kBundler_CannotInjectCopyLoader,
        kBundler_FailedToParseRuntime,
        kBundler_FailedToReadDirectory,
        kBundler_InjectedPathCannotBeExternal,
        kBundler_CouldNotResolveEntryPoint,
        kBundler_EntryPointCannotBeExternal,
        kBundler_ImportAssertionRequiresJSONLoader,
        kBundler_ImportAssertionRequiresJSONLoaderNote,
        kBundler_ImportingWithAttr,
        kBundler_ImportingWithTypeAttr,
        kBundler_ReconfigureLoaderNote,
        kBundler_CannotUseComposesWith,
        kBundler_ComposesOnlyCSSNote,
        kBundler_CannotImportIntoCSS,
        kBundler_CannotImportIntoCSSNote,
        kBundler_CannotUseAsURL,
        kBundler_CannotUseAsURLNote,
        kBundler_CannotUseAsURLNoURL,
        kBundler_CannotUseAsURLNoURLNote,
        kBundler_CannotImportWithoutOutputPath,
        kBundler_IgnoringImportNoSideEffects,
        kBundler_SideEffectsExcludedFromArray,
        kBundler_SideEffectsFalse,
        kBundler_RequireCallTopLevelAwait,
        kBundler_RequireCallTransitiveTLA,
        kBundler_TopLevelAwaitHere,
        kBundler_ImportsFileHere,
        kBundler_UnexpectedInvalidIndex,
        kBundler_ImportMapReordered,
        kBundler_RefusingToOverwriteInput,
        kBundler_AllowOverwriteHint,
        kBundler_TwoOutputFilesSamePath,

        // -------------------------------------------------------------------
        // Linker (linker.cpp)
        // -------------------------------------------------------------------
        kLinker_ComposesValueUndefined,
        kLinker_ComposesValueFirstNote,
        kLinker_ComposesValueSecondNote,
        kLinker_ComposesValueUndefinedNote,
        kLinker_StringAsNameNotSupported,
        kLinker_ImportAlwaysUndefined,
        kLinker_NoMatchingExport,
        kLinker_CycleWhileResolving,
        kLinker_ImportAlwaysUndefinedMultipleMatches,
        kLinker_OneMatchingExportNote,
        kLinker_AnotherMatchingExportNote,
        kLinker_AmbiguousImportMultipleMatches,
        kLinker_CannotUseGlobalNameWithComposes,
        kLinker_GlobalNameDefinedHereNote,
        kLinker_UseLocalCSSLoaderHint,
        kLinker_UseLocalSelectorHint,
        kLinker_NameNeverAppearsIn,
        kLinker_AmbiguousReexport,
        kLinker_OneDefinitionFromNote,
        kLinker_AnotherDefinitionFromNote,
        kLinker_CannotTraverseDirectoryToChunk,
        kLinker_CircularImport,
        kLinker_PanicPrinting,
        kLinker_PanicWhilePrinting,
        kLinker_UnknownExceptionPrinting,
        kLinker_UnknownException,

        // -------------------------------------------------------------------
        // API (api_impl.cpp)
        // -------------------------------------------------------------------
        kAPI_InvalidVersion,
        kAPI_InvalidVersionNote,
        kAPI_NotValidFeatureName,
        kAPI_NotValidRegexp,
        kAPI_InvalidPath,
        kAPI_MoreThanOneWildcard,
        kAPI_InvalidAliasSubstitution,
        kAPI_InvalidAliasName,
        kAPI_InvalidFileExtension,
        kAPI_InvalidJSX,
        kAPI_DefinedAsIdentifier,
        kAPI_InvalidDefineValue,
        kAPI_InvalidOutputExtension,
        kAPI_InvalidOutputExtensionValid,
        kAPI_InvalidFileType,
        kAPI_KeepNamesCannotBeUsed,
        kAPI_KeepNamesCannotBeUsedNote,
        kAPI_InvalidPluginFilterOnResolve,
        kAPI_InvalidPluginFilterOnLoad,
        kAPI_PluginMissingName,
        kAPI_BuildCanceled,
        kAPI_UnexpectedFileCountWhenWritingToStdout,
        kAPI_FailedToCreateRealFS,
        kAPI_FailedToCreateOutputDirectory,
        kAPI_FailedToWriteOutputFile,
        kAPI_MustUseOutdirMultipleInputFiles,
        kAPI_MustUseOutdirCodeSplitting,
        kAPI_CannotUseBothOutfileAndOutdir,
        kAPI_CannotUseExternalSourceMap,
        kAPI_CannotUseLinkedOrExternalLegalComments,
        kAPI_CannotUseFileLoader,
        kAPI_CannotUseExternalWithoutBundle,
        kAPI_CannotUseAliasWithoutBundle,
        kAPI_SplittingOnlyESM,
        kAPI_CannotProvideTsconfigBoth,
        kAPI_CannotTransformWithLinkedSourceMaps,
        kAPI_MustUseSourcefileWithSourcemap,
        kAPI_CannotTransformWithLinkedLegalComments,
        kAPI_CannotCallResolveBeforeSetup,
        kAPI_MustSpecifyKindWhenResolving,

        // -------------------------------------------------------------------
        // CLI mangle cache (mangle_cache.cpp)
        // -------------------------------------------------------------------
        kCLI_FailedToReadMangleCache,
        kCLI_ExpectedTopLevelObject,
        kCLI_ExpectedKeyStringOrFalse,

        kEND,
    };

    // Returns the std::format-style template string for a catalog entry.
    // The template is looked up at run time from the table in
    // "src/core/logger/message.cpp". Callers should not parse or modify
    // the returned string; it is intended only for passing to FormatMsg().
    //
    //   MsgTemplate(MsgCat::kJS_AssignToConstant)
    //   ->  "cannot assign to variable \"{}\" with const-qualified type \"const {}\""
    std::string_view MsgTemplate(MsgCat id);

    // Formats a catalog message by looking up its template and filling in
    // the placeholders with the provided arguments. Because templates are
    // only known at run time, this goes through std::vformat. The named
    // parameters (which are lvalues) are passed as-is to
    // std::make_format_args rather than being forwarded, because MSVC's
    // implementation only accepts lvalues.
    //
    //   FormatMsg(MsgCat::kJS_AssignToConstant, "x", "number")
    //   ->  "cannot assign to variable \"x\" with const-qualified type \"const number\""
    template <typename... Args>
    std::string FormatMsg(MsgCat id, Args&&... args) {
        return std::vformat(MsgTemplate(id), std::make_format_args(args...));
    }
}
