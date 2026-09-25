#pragma once

#include "guchho/compiler.hpp"
#include "guchho/css/css_properties.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace guchho::compat {

    // Identifies the target JavaScript engine that Guchho should emit code
    // for. Each engine has its own set of supported features and quirks, so
    // the compiler uses this enum to decide which syntax transformations and
    // polyfills to apply.
    //
    // Browser engines (Chrome, Edge, Firefox, IE, iOS, Opera, Safari) are
    // distinct from runtime engines (Deno, Node, Hermes, Rhino) and the
    // generic ES target. Use IsBrowser() to distinguish them at runtime.
    //
    // Example:
    //   Engine e = Engine::kChrome;
    //   if (IsBrowser(e)) { /* apply browser-specific transformations */ }
    enum class Engine : uint8_t {
        kChrome,   // Google Chrome / Chromium-based browsers
        kDeno,     // Deno JavaScript runtime
        kEdge,     // Microsoft Edge (Chromium-based)
        kES,       // Generic ECMAScript target (no browser/runtime specifics)
        kFirefox,  // Mozilla Firefox
        kHermes,   // Hermes engine (React Native on Android/iOS)
        kIE,       // Microsoft Internet Explorer (legacy)
        kIOS,      // Safari on iOS / iPadOS
        kNode,     // Node.js runtime
        kOpera,    // Opera browser
        kRhino,    // Mozilla Rhino (Java-based JS engine)
        kSafari,   // Apple Safari on macOS
    };

    // Returns true when the given engine is a web browser (as opposed to a
    // JavaScript runtime or the generic ES target). Browser engines may need
    // CSS vendor prefixes, HTML transformations, and browser-specific APIs
    // that runtimes do not require.
    //
    // Example:
    //   IsBrowser(Engine::kChrome)  -> true
    //   IsBrowser(Engine::kNode)    -> false
    //   IsBrowser(Engine::kES)      -> false
    //
    // Edge cases:
    //   - Any engine not explicitly listed in the switch (e.g., a future
    //     engine added to the enum) returns false by default.
    inline bool IsBrowser(Engine engine) {
        switch (engine) {
            case Engine::kChrome:
            case Engine::kEdge:
            case Engine::kFirefox:
            case Engine::kIE:
            case Engine::kIOS:
            case Engine::kOpera:
            case Engine::kSafari:
                return true;
            default:
                return false;
        }
    }

    // A three-component version number (major.minor.patch) used to describe
    // browser and runtime releases. All fields are zero-initialized by default,
    // which represents an unset or unknown version. Two Version values are
    // equal only when all three components match exactly.
    //
    // Example:
    //   Version v{.major = 120, .minor = 0, .patch = 0};
    //   Version w{.major = 120, .minor = 0, .patch = 0};
    //   v == w  -> true
    //
    //   Version x{.major = 120, .minor = 1, .patch = 0};
    //   v == x  -> false
    struct Version {
        uint16_t major{}; // Major release number
        uint8_t minor{};  // Minor feature or update release
        uint8_t patch{};  // Bug fix or small correction

        bool operator==(const Version& other) const {
            return major == other.major &&
                minor == other.minor &&
                patch == other.patch;
        }
    };

    // A semantic version (SemVer) with an arbitrary number of numeric parts
    // and an optional pre-release identifier. This representation is more
    // flexible than Version because it can model versions like "1.2.3-beta.1"
    // or even non-standard version strings with fewer or more than three parts.
    //
    // Example:
    //   Semver s;
    //   s.parts = {1, 2, 3};
    //   s.pre_release = "beta.1";
    //   s.ToString() -> "1.2.3-beta.1"
    //
    // Edge cases:
    //   - An empty 'parts' vector combined with an empty pre_release yields
    //     an empty string from ToString().
    //   - Pre-release is appended verbatim without a '-' separator; callers
    //     must include the leading dash in pre_release if desired.
    struct Semver {
        // The numeric version parts. "1.2.3" yields parts {1, 2, 3}.
        std::vector<uint32_t> parts;

        // The pre-release identifier. "1.2.3-beta.1" yields pre_release "beta.1".
        std::string pre_release {};

        // Reconstructs the full version string from the numeric parts and
        // pre-release identifier. Numeric parts are joined with '.' and the
        // pre-release is appended without any separator.
        //
        // Example:
        //   Semver{.parts={2,0,1}, .pre_release="-rc.1"}.ToString()
        //   -> "2.0.1-rc.1"
        std::string ToString() const;
    };

    // An inclusive-inclusive version range [start, end] used to describe the
    // set of versions that support a particular feature. When 'end' is the
    // default-constructed Version{} (all zeros), the range is treated as
    // having no upper bound, meaning all versions from 'start' onward are
    // included.
    //
    // Example:
    //   VersionRange r{.start={111,0,0}, .end={120,0,0}};
    //   // Represents versions 111.0.0 through 120.0.0 inclusive.
    //
    //   VersionRange open{.start={120,0,0}, .end={}};
    //   // Represents versions 120.0.0 and above (no upper limit).
    struct VersionRange {
        Version start;

        // The upper bound. Using Version{} means there is no upper limit.
        Version end;
    };

    // Compares a structured Version against a Semver. Returns a negative value
    // if 'a' is older than 'b', zero if they are equal, or a positive value
    // if 'a' is newer. The comparison proceeds left-to-right through major,
    // minor, then patch components. If the Semver has fewer than three parts,
    // missing parts are treated as zero.
    //
    // When the numeric parts are equal but the Semver has a non-empty
    // pre-release string, the Version is considered greater (i.e., a release
    // version is newer than the same version with a pre-release tag).
    //
    // Example:
    //   CompareVersions({111,0,0}, Semver{.parts={111,0,0}}) -> 0
    //   CompareVersions({112,0,0}, Semver{.parts={111,0,0}}) -> 1
    //   CompareVersions({111,0,0}, Semver{.parts={112,0,0}}) -> -1
    //
    // Edge cases:
    //   - Semver with empty parts: all numeric parts are treated as zero.
    //   - Pre-release tie-breaking: "1.0.0" > "1.0.0-alpha" (release wins).
    int CompareVersions(Version a, Semver b);

    // Returns true when the given Semver version falls within at least one of
    // the provided version ranges. A version is inside a range when it is
    // >= range.start and <= range.end (or unbounded if range.end is Version{}).
    //
    // Example:
    //   std::vector<VersionRange> ranges = {{{111,0,0}, {120,0,0}}};
    //   IsVersionSupported(ranges, Semver{.parts={115,0,0}}) -> true
    //   IsVersionSupported(ranges, Semver{.parts={121,0,0}}) -> false
    //
    // Edge cases:
    //   - Empty ranges vector: always returns false.
    //   - Version with pre-release: "1.0.0-alpha" is considered less than
    //     1.0.0, so it may not match a range starting at 1.0.0.
    //   - Multiple ranges: returns true on the first match (short-circuits).
    bool IsVersionSupported(
        const std::vector<VersionRange>& ranges,
        const Semver& version);

    // Bitmask of JavaScript language features that Guchho can enable or
    // disable during compilation. Each feature is a single bit so they can
    // be combined with bitwise OR and tested with Has(). The compiler
    // inspects the active feature set to decide which syntax transformations
    // are safe for the target environment.
    //
    // Example:
    //   JSFeature f = JSFeature::kArrow | JSFeature::kAsyncAwait;
    //   Has(f, JSFeature::kArrow) -> true
    //   Has(f, JSFeature::kClass) -> false
    enum class JSFeature : uint64_t {
        kArbitraryModuleNamespaceNames = 1ULL << 0,
        kArraySpread = 1ULL << 1,
        kArrow = 1ULL << 2,
        kAsyncAwait = 1ULL << 3,
        kAsyncGenerator = 1ULL << 4,
        kBigint = 1ULL << 5,
        kClass = 1ULL << 6,
        kClassField = 1ULL << 7,
        kClassPrivateAccessor = 1ULL << 8,
        kClassPrivateBrandCheck = 1ULL << 9,
        kClassPrivateField = 1ULL << 10,
        kClassPrivateMethod = 1ULL << 11,
        kClassPrivateStaticAccessor = 1ULL << 12,
        kClassPrivateStaticField = 1ULL << 13,
        kClassPrivateStaticMethod = 1ULL << 14,
        kClassStaticBlocks = 1ULL << 15,
        kClassStaticField = 1ULL << 16,
        kConstAndLet = 1ULL << 17,
        kDecorators = 1ULL << 18,
        kDefaultArgument = 1ULL << 19,
        kDestructuring = 1ULL << 20,
        kDynamicImport = 1ULL << 21,
        kExponentOperator = 1ULL << 22,
        kExportStarAs = 1ULL << 23,
        kForAwait = 1ULL << 24,
        kForOf = 1ULL << 25,
        kFromBase64 = 1ULL << 26,
        kFunctionNameConfigurable = 1ULL << 27,
        kFunctionOrClassPropertyAccess = 1ULL << 28,
        kGenerator = 1ULL << 29,
        kHashbang = 1ULL << 30,
        kImportAssertions = 1ULL << 31,
        kImportAttributes = 1ULL << 32,
        kImportDefer = 1ULL << 33,
        kImportMeta = 1ULL << 34,
        kImportSource = 1ULL << 35,
        kInlineScript = 1ULL << 36,
        kLogicalAssignment = 1ULL << 37,
        kNestedRestBinding = 1ULL << 38,
        kNewTarget = 1ULL << 39,
        kNodeColonPrefixImport = 1ULL << 40,
        kNodeColonPrefixRequire = 1ULL << 41,
        kNullishCoalescing = 1ULL << 42,
        kObjectAccessors = 1ULL << 43,
        kObjectExtensions = 1ULL << 44,
        kObjectRestSpread = 1ULL << 45,
        kOptionalCatchBinding = 1ULL << 46,
        kOptionalChain = 1ULL << 47,
        kRegexpDotAllFlag = 1ULL << 48,
        kRegexpLookbehindAssertions = 1ULL << 49,
        kRegexpMatchIndices = 1ULL << 50,
        kRegexpNamedCaptureGroups = 1ULL << 51,
        kRegexpSetNotation = 1ULL << 52,
        kRegexpStickyAndUnicodeFlags = 1ULL << 53,
        kRegexpUnicodePropertyEscapes = 1ULL << 54,
        kRestArgument = 1ULL << 55,
        kTemplateLiteral = 1ULL << 56,
        kTopLevelAwait = 1ULL << 57,
        kTypeofExoticObjectIsObject = 1ULL << 58,
        kUnicodeEscapes = 1ULL << 59,
        kUsing = 1ULL << 60,
    };    

    // Combines two JSFeature bitmasks using bitwise OR. This is the primary
    // way to build a feature set that includes multiple features.
    //
    // Example:
    //   JSFeature a = JSFeature::kArrow;
    //   JSFeature b = JSFeature::kAsyncAwait;
    //   JSFeature c = a | b;  // Both arrow functions and async/await enabled
    inline JSFeature operator|(JSFeature a, JSFeature b) {
        return static_cast<JSFeature>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
    }

    // Adds the bits from 'b' into 'a' in place using bitwise OR. Returns
    // a reference to the modified 'a' so calls can be chained.
    //
    // Example:
    //   JSFeature f = JSFeature::kArrow;
    //   f |= JSFeature::kAsyncAwait;  // f now includes both features
    inline JSFeature& operator|=(JSFeature& a, JSFeature b) {
        a = a | b;
        return a;
    }

    // Computes the bitwise AND of two JSFeature bitmasks. Useful for
    // masking out specific bits or testing overlap between feature sets.
    //
    // Example:
    //   JSFeature a = JSFeature::kArrow | JSFeature::kAsyncAwait;
    //   JSFeature b = JSFeature::kArrow;
    //   a & b -> JSFeature::kArrow  (only the common bit remains)
    inline JSFeature operator&(JSFeature a, JSFeature b) {
        return static_cast<JSFeature>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
    }

    // Returns the bitwise NOT of a JSFeature bitmask. Useful for clearing
    // specific bits when combined with AND: features & ~mask clears the
    // bits set in mask.
    //
    // Example:
    //   JSFeature f = JSFeature::kArrow | JSFeature::kAsyncAwait;
    //   JSFeature g = f & ~JSFeature::kArrow;  // Only kAsyncAwait remains
    inline JSFeature operator~(JSFeature a) {
        return static_cast<JSFeature>(~static_cast<uint64_t>(a));
    }

    // Returns true when the feature bitmask 'features' includes the single
    // feature 'feature'. This is the standard way to test for a specific
    // feature in a combined feature set.
    //
    // Example:
    //   JSFeature f = JSFeature::kArrow | JSFeature::kClass;
    //   Has(f, JSFeature::kArrow)    -> true
    //   Has(f, JSFeature::kAsyncAwait) -> false
    //
    // Edge cases:
    //   - If feature has multiple bits set, all those bits must be present
    //     in features for the result to be true (bitwise AND test).
    inline bool Has(JSFeature features, JSFeature feature) {
        return (static_cast<uint64_t>(features) & static_cast<uint64_t>(feature)) != 0;
    }

    // Selectively replaces bits in 'features' using 'overrides', but only
    // for the positions where 'mask' has bits set. Bits not covered by the
    // mask are preserved unchanged. This allows targeted feature overrides
    // without affecting the rest of the feature set.
    //
    // Example:
    //   JSFeature features = JSFeature::kArrow | JSFeature::kClass;
    //   JSFeature overrides = JSFeature::kAsyncAwait | JSFeature::kClass;
    //   JSFeature mask = JSFeature::kClass;
    //   ApplyOverrides(features, overrides, mask)
    //   -> JSFeature::kArrow | JSFeature::kAsyncAwait
    //   (kArrow preserved, kClass replaced with kAsyncAwait via mask)
    //
    // Edge cases:
    //   - If mask is zero, features is returned unchanged.
    //   - If mask covers all bits, the result is overrides & mask.
    inline JSFeature ApplyOverrides(JSFeature features, JSFeature overrides, JSFeature mask) {
        return (features & ~mask) | (overrides & mask);
    }

    // Provides a total ordering on JSFeature values based on their underlying
    // integer representation. This allows JSFeature to be used as a key in
    // ordered containers or with std::sort.
    //
    // Example:
    //   JSFeature::kArrow < JSFeature::kAsyncAwait  -> true (bit 2 < bit 3)
    inline bool operator<(JSFeature a, JSFeature b) {
        return static_cast<uint64_t>(a) < static_cast<uint64_t>(b);
    }

    // Maps a compiler SymbolKind to the JSFeature flag that represents the
    // language feature required to support that symbol kind. Private class
    // members and their static variants each map to a distinct feature flag.
    // Non-private symbol kinds return a zero value (no feature required).
    //
    // Example:
    //   SymbolFeature(compiler::SymbolKind::kPrivateField)
    //   -> JSFeature::kClassPrivateField
    //
    //   SymbolFeature(compiler::SymbolKind::kPrivateStaticMethod)
    //   -> JSFeature::kClassPrivateStaticMethod
    //
    // Edge cases:
    //   - The default case returns static_cast<JSFeature>(0), which Has()
    //     will always report as false for any named feature.
    JSFeature SymbolFeature(compiler::SymbolKind kind);

    // Maps human-readable JavaScript feature names (as used in configuration
    // files and command-line flags) to their corresponding JSFeature enum
    // values. This allows users to reference features by descriptive names
    // like "arrow" or "async-await" rather than internal enum values.
    //
    // Example:
    //   StringToJSFeature.at("arrow")     -> JSFeature::kArrow
    //   StringToJSFeature.at("async-await") -> JSFeature::kAsyncAwait
    //
    // Edge cases:
    //   - Throws std::out_of_range if the key is not present in the map.
    //   - All keys are lowercase with hyphens as word separators.
    extern const std::unordered_map<std::string_view, JSFeature> StringToJSFeature;

    // Computes the bitmask of JavaScript features that are NOT supported by
    // at least one of the target engines specified in 'constraints'. A feature
    // is considered unsupported if any constrained engine either lacks an entry
    // for it or has a minimum version higher than the constrained version.
    //
    // The compiler uses this to decide which syntax transformations are
    // required for the narrowest supported environment.
    //
    // Example:
    //   std::unordered_map<Engine, Semver> constraints = {
    //       {Engine::kChrome, Semver{.parts={120,0,0}}}
    //   };
    //   JSFeature unsup = UnsupportedJSFeatures(constraints);
    //   // unsup may include features that require Chrome 121+
    //
    // Edge cases:
    //   - Non-browser engines (like ES2020 targets) are skipped because
    //     they don't affect JavaScript feature support.
    //   - An empty constraints map returns a zero bitmask (nothing is
    //     unsupported when there are no constraints).
    JSFeature UnsupportedJSFeatures(const std::unordered_map<Engine, Semver>& constraints);

    // Bitmask of CSS features that Guchho can target. Each feature is a
    // single bit so they can be combined with bitwise OR and tested with
    // Has(). The compiler inspects this set to decide which CSS syntax
    // transformations or fallbacks to apply.
    //
    // Example:
    //   CSSFeature f = CSSFeature::kNesting | CSSFeature::kHWB;
    //   Has(f, CSSFeature::kNesting) -> true
    //   Has(f, CSSFeature::kHexRGBA) -> false
    enum class CSSFeature : uint16_t {
        kColorFunctions = 1 << 0,
        kGradientDoublePosition = 1 << 1,
        kGradientInterpolation = 1 << 2,
        kGradientMidpoints = 1 << 3,
        kHWB = 1 << 4,
        kHexRGBA = 1 << 5,
        kInlineStyle = 1 << 6,
        kInsetProperty = 1 << 7,
        kIsPseudoClass = 1 << 8,
        kMediaRange = 1 << 9,
        kModernRGBHSL = 1 << 10,
        kNesting = 1 << 11,
        kRebeccaPurple = 1 << 12,
    };

    // Combines two CSSFeature bitmasks using bitwise OR.
    //
    // Example:
    //   CSSFeature a = CSSFeature::kNesting;
    //   CSSFeature b = CSSFeature::kHWB;
    //   CSSFeature c = a | b;  // Both nesting and HWB enabled
    inline CSSFeature operator|(CSSFeature a, CSSFeature b) {
        return static_cast<CSSFeature>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
    }

    // Computes the bitwise AND of two CSSFeature bitmasks.
    //
    // Example:
    //   CSSFeature a = CSSFeature::kNesting | CSSFeature::kHWB;
    //   CSSFeature b = CSSFeature::kNesting;
    //   a & b -> CSSFeature::kNesting
    inline CSSFeature operator&(CSSFeature a, CSSFeature b) {
        return static_cast<CSSFeature>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
    }

    // Returns the bitwise NOT of a CSSFeature bitmask.
    //
    // Example:
    //   CSSFeature f = CSSFeature::kNesting | CSSFeature::kHWB;
    //   CSSFeature g = f & ~CSSFeature::kNesting;  // Only kHWB remains
    inline CSSFeature operator~(CSSFeature a) {
        return static_cast<CSSFeature>(~static_cast<uint16_t>(a));
    }

    // Adds the bits from 'b' into 'a' in place using bitwise OR.
    //
    // Example:
    //   CSSFeature f = CSSFeature::kNesting;
    //   f |= CSSFeature::kHWB;  // f now includes both features
    inline CSSFeature& operator|=(CSSFeature& a, CSSFeature b) {
        a = a | b;
        return a;
    }

    // Returns true when the CSS feature bitmask 'features' includes the
    // single feature 'feature'.
    //
    // Example:
    //   CSSFeature f = CSSFeature::kNesting | CSSFeature::kHWB;
    //   Has(f, CSSFeature::kNesting)  -> true
    //   Has(f, CSSFeature::kHexRGBA)  -> false
    //
    // Edge cases:
    //   - If feature has multiple bits set, all those bits must be present
    //     in features for the result to be true.
    inline bool Has(CSSFeature features, CSSFeature feature) {
        return (static_cast<uint16_t>(features) & static_cast<uint16_t>(feature)) != 0;
    }

    // Selectively replaces bits in 'features' using 'overrides', but only
    // for the positions where 'mask' has bits set. Bits not covered by the
    // mask are preserved unchanged.
    //
    // Example:
    //   CSSFeature features = CSSFeature::kNesting | CSSFeature::kHWB;
    //   CSSFeature overrides = CSSFeature::kHexRGBA | CSSFeature::kHWB;
    //   CSSFeature mask = CSSFeature::kHWB;
    //   ApplyOverrides(features, overrides, mask)
    //   -> CSSFeature::kNesting | CSSFeature::kHexRGBA
    //
    // Edge cases:
    //   - If mask is zero, features is returned unchanged.
    //   - If mask covers all bits, the result is overrides & mask.
    inline CSSFeature ApplyOverrides(CSSFeature features, CSSFeature overrides, CSSFeature mask) {
        return (features & ~mask) | (overrides & mask);
    }

    // Maps human-readable CSS feature names (as used in configuration files
    // and command-line flags) to their corresponding CSSFeature enum values.
    //
    // Example:
    //   StringToCSSFeature.at("nesting") -> CSSFeature::kNesting
    //   StringToCSSFeature.at("hwb")     -> CSSFeature::kHWB
    //
    // Edge cases:
    //   - Throws std::out_of_range if the key is not present in the map.
    //   - All keys are lowercase with hyphens as word separators.
    extern const std::unordered_map<std::string_view, CSSFeature> StringToCSSFeature;

    // Computes the bitmask of CSS features that are NOT supported by at
    // least one of the target engines specified in 'constraints'. A feature
    // is considered unsupported if any constrained browser engine either
    // lacks an entry for it or has a minimum version higher than the
    // constrained version.
    //
    // Example:
    //   std::unordered_map<Engine, Semver> constraints = {
    //       {Engine::kChrome, Semver{.parts={120,0,0}}}
    //   };
    //   CSSFeature unsup = UnsupportedCSSFeatures(constraints);
    //   // unsup may include kNesting if Chrome 120 doesn't support it
    //
    // Edge cases:
    //   - Non-browser engines are skipped (CSS prefix/feature logic only
    //     applies to browsers).
    //   - kInlineStyle is always skipped because it is user-specified and
    //     does not depend on browser support.
    CSSFeature UnsupportedCSSFeatures(const std::unordered_map<Engine, Semver>& constraints);

    // Bitmask of CSS vendor prefixes that Guchho may need to emit for a
    // given property. Multiple prefixes can be active simultaneously when
    // different target browsers require different vendor prefixes for the
    // same property.
    //
    // Example:
    //   CSSPrefix p = CSSPrefix::kWebkitPrefix | CSSPrefix::kMozPrefix;
    //   Has(p, CSSPrefix::kWebkitPrefix) -> true
    //   Has(p, CSSPrefix::kMsPrefix)     -> false
    enum class CSSPrefix : uint8_t {
        kKhtmlPrefix = 1 << 0,  // -khtml- (legacy WebKit)
        kMozPrefix = 1 << 1,    // -moz- (Mozilla Firefox)
        kMsPrefix = 1 << 2,     // -ms- (Microsoft IE/Edge legacy)
        kOPrefix = 1 << 3,      // -o- (legacy Opera)
        kWebkitPrefix = 1 << 4, // -webkit- (modern WebKit/Blink)
        kNoPrefix = 0,          // No vendor prefix needed
    };

    // Combines two CSSPrefix bitmasks using bitwise OR.
    //
    // Example:
    //   CSSPrefix a = CSSPrefix::kWebkitPrefix;
    //   CSSPrefix b = CSSPrefix::kMozPrefix;
    //   CSSPrefix c = a | b;  // Both -webkit- and -moz- needed
    inline CSSPrefix operator|(CSSPrefix a, CSSPrefix b) {
        return static_cast<CSSPrefix>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    // Computes the bitwise AND of two CSSPrefix bitmasks.
    //
    // Example:
    //   CSSPrefix a = CSSPrefix::kWebkitPrefix | CSSPrefix::kMozPrefix;
    //   CSSPrefix b = CSSPrefix::kWebkitPrefix;
    //   a & b -> CSSPrefix::kWebkitPrefix
    inline CSSPrefix operator&(CSSPrefix a, CSSPrefix b) {
        return static_cast<CSSPrefix>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    // Returns the bitwise NOT of a CSSPrefix bitmask.
    //
    // Example:
    //   CSSPrefix p = CSSPrefix::kWebkitPrefix | CSSPrefix::kMozPrefix;
    //   CSSPrefix q = p & ~CSSPrefix::kWebkitPrefix;  // Only kMozPrefix
    inline CSSPrefix operator~(CSSPrefix a) {
        return static_cast<CSSPrefix>(~static_cast<uint8_t>(a));
    }

    // Adds the bits from 'b' into 'a' in place using bitwise OR.
    //
    // Example:
    //   CSSPrefix p = CSSPrefix::kWebkitPrefix;
    //   p |= CSSPrefix::kMozPrefix;  // p now includes both prefixes
    inline CSSPrefix& operator|=(CSSPrefix& a, CSSPrefix b) {
        a = a | b;
        return a;
    }

    // Returns true when the CSS prefix bitmask 'features' includes the
    // single prefix 'feature'.
    //
    // Example:
    //   CSSPrefix p = CSSPrefix::kWebkitPrefix | CSSPrefix::kMozPrefix;
    //   Has(p, CSSPrefix::kWebkitPrefix) -> true
    //   Has(p, CSSPrefix::kMsPrefix)     -> false
    //
    // Edge cases:
    //   - kNoPrefix (zero) is never "included" in any bitmask via Has(),
    //     because ANDing with zero always yields zero.
    inline bool Has(CSSPrefix features, CSSPrefix feature) {
        return (static_cast<uint8_t>(features) & static_cast<uint8_t>(feature)) != 0;
    }

    // Returns a map from each CSS property declaration to the set of vendor
    // prefixes that must be emitted for the given target browser versions.
    // Only properties that actually require prefixes are included in the
    // result; properties that need no prefix at all are omitted.
    //
    // The function consults an internal table of prefix support data and
    // cross-references it with the constrained browser versions to determine
    // which prefixes are still needed.
    //
    // Example:
    //   std::unordered_map<Engine, Semver> constraints = {
    //       {Engine::kChrome, Semver{.parts={80,0,0}}}
    //   };
    //   auto prefixes = CSSPrefixData(constraints);
    //   // prefixes[css::kDAppearance] may contain CSSPrefix::kWebkitPrefix
    //   // because Chrome 80 still needs -webkit-appearance.
    //
    // Edge cases:
    //   - Non-browser engines in constraints are ignored.
    //   - An empty constraints map returns an empty result (no prefixes
    //     needed when targeting nothing).
    //   - Version{} (all zeros) for a browser means the prefix is always
    //     required regardless of version.
    std::unordered_map<css::Declarations, CSSPrefix> CSSPrefixData(
        const std::unordered_map<Engine, Semver>& constraints);
}
