#include "guchho/compat.hpp"

namespace guchho::compat {

    // Maps human-readable CSS feature names (as used in configuration files and
    // command-line flags) to their corresponding CSSFeature enum values. This
    // allows users to reference CSS features by descriptive string names such
    // as "nesting" or "media-range" rather than needing to know the internal
    // enum values.
    //
    // Example:
    //   StringToCSSFeature.at("nesting") -> CSSFeature::kNesting
    //   StringToCSSFeature.at("hwb")     -> CSSFeature::kHWB
    //
    // Edge cases:
    //   - Throws std::out_of_range if the key is not present in the map.
    //   - All keys are lowercase with hyphens as word separators.
    //   - The map is immutable after initialization.
    const std::unordered_map<std::string_view, CSSFeature> StringToCSSFeature = {
        {"color-functions",          CSSFeature::kColorFunctions},
        {"gradient-double-position", CSSFeature::kGradientDoublePosition},
        {"gradient-interpolation",   CSSFeature::kGradientInterpolation},
        {"gradient-midpoints",       CSSFeature::kGradientMidpoints},
        {"hwb",                      CSSFeature::kHWB},
        {"hex-rgba",                 CSSFeature::kHexRGBA},
        {"inline-style",             CSSFeature::kInlineStyle},
        {"inset-property",           CSSFeature::kInsetProperty},
        {"is-pseudo-class",          CSSFeature::kIsPseudoClass},
        {"media-range",              CSSFeature::kMediaRange},
        {"modern-rgb-hsl",           CSSFeature::kModernRGBHSL},
        {"nesting",                  CSSFeature::kNesting},
        {"rebecca-purple",           CSSFeature::kRebeccaPurple},
    };

    namespace {

        using EngineVersions = std::unordered_map<Engine, std::vector<VersionRange>>;

        // Maps each CSSFeature to the minimum browser engine versions that
        // support it without a vendor prefix. Each entry contains a map from
        // Engine (Chrome, Firefox, Safari, etc.) to a vector of VersionRange
        // values representing the supported version ranges. An empty vector
        // for an engine means the feature is not supported by that engine.
        //
        // Example:
        //   kCssTable[CSSFeature::kNesting][Engine::kChrome] -> {{Version{120, 0, 0}}}
        //   This means Chrome 120+ supports CSS nesting natively.
        //
        // Edge cases:
        //   - kInlineStyle has an empty engine map because it's user-specified
        //     and doesn't depend on browser support.
        //   - Some features like kRebeccaPurple have very old minimum versions
        //     (e.g., IE 11) while others like kNesting require modern browsers.
        const std::unordered_map<CSSFeature, EngineVersions> kCssTable = {
            {CSSFeature::kColorFunctions, {
                {Engine::kChrome,  {{Version{111, 0, 0}}}},
                {Engine::kEdge,    {{Version{111, 0, 0}}}},
                {Engine::kFirefox, {{Version{113, 0, 0}}}},
                {Engine::kIOS,     {{Version{15, 4, 0}}}},
                {Engine::kOpera,   {{Version{97, 0, 0}}}},
                {Engine::kSafari,  {{Version{15, 4, 0}}}},
            }},
            {CSSFeature::kGradientDoublePosition, {
                {Engine::kChrome,  {{Version{72, 0, 0}}}},
                {Engine::kEdge,    {{Version{79, 0, 0}}}},
                {Engine::kFirefox, {{Version{83, 0, 0}}}},
                {Engine::kIOS,     {{Version{12, 2, 0}}}},
                {Engine::kOpera,   {{Version{60, 0, 0}}}},
                {Engine::kSafari,  {{Version{12, 1, 0}}}},
            }},
            {CSSFeature::kGradientInterpolation, {
                {Engine::kChrome,  {{Version{111, 0, 0}}}},
                {Engine::kEdge,    {{Version{111, 0, 0}}}},
                {Engine::kFirefox, {{Version{137, 0, 0}}}},
                {Engine::kIOS,     {{Version{16, 2, 0}}}},
                {Engine::kOpera,   {{Version{97, 0, 0}}}},
                {Engine::kSafari,  {{Version{16, 2, 0}}}},
            }},
            {CSSFeature::kGradientMidpoints, {
                {Engine::kChrome,  {{Version{40, 0, 0}}}},
                {Engine::kEdge,    {{Version{79, 0, 0}}}},
                {Engine::kFirefox, {{Version{36, 0, 0}}}},
                {Engine::kIOS,     {{Version{7, 0, 0}}}},
                {Engine::kOpera,   {{Version{27, 0, 0}}}},
                {Engine::kSafari,  {{Version{7, 0, 0}}}},
            }},
            {CSSFeature::kHWB, {
                {Engine::kChrome,  {{Version{101, 0, 0}}}},
                {Engine::kEdge,    {{Version{101, 0, 0}}}},
                {Engine::kFirefox, {{Version{96, 0, 0}}}},
                {Engine::kIOS,     {{Version{15, 0, 0}}}},
                {Engine::kOpera,   {{Version{87, 0, 0}}}},
                {Engine::kSafari,  {{Version{15, 0, 0}}}},
            }},
            {CSSFeature::kHexRGBA, {
                {Engine::kChrome,  {{Version{62, 0, 0}}}},
                {Engine::kEdge,    {{Version{79, 0, 0}}}},
                {Engine::kFirefox, {{Version{49, 0, 0}}}},
                {Engine::kIOS,     {{Version{9, 3, 0}}}},
                {Engine::kOpera,   {{Version{49, 0, 0}}}},
                {Engine::kSafari,  {{Version{10, 0, 0}}}},
            }},
            {CSSFeature::kInlineStyle, {}},
            {CSSFeature::kInsetProperty, {
                {Engine::kChrome,  {{Version{87, 0, 0}}}},
                {Engine::kEdge,    {{Version{87, 0, 0}}}},
                {Engine::kFirefox, {{Version{66, 0, 0}}}},
                {Engine::kIOS,     {{Version{14, 5, 0}}}},
                {Engine::kOpera,   {{Version{73, 0, 0}}}},
                {Engine::kSafari,  {{Version{14, 1, 0}}}},
            }},
            {CSSFeature::kIsPseudoClass, {
                {Engine::kChrome,  {{Version{88, 0, 0}}}},
                {Engine::kEdge,    {{Version{88, 0, 0}}}},
                {Engine::kFirefox, {{Version{78, 0, 0}}}},
                {Engine::kIOS,     {{Version{14, 0, 0}}}},
                {Engine::kOpera,   {{Version{75, 0, 0}}}},
                {Engine::kSafari,  {{Version{14, 0, 0}}}},
            }},
            {CSSFeature::kMediaRange, {
                {Engine::kChrome,  {{Version{104, 0, 0}}}},
                {Engine::kEdge,    {{Version{104, 0, 0}}}},
                {Engine::kFirefox, {{Version{63, 0, 0}}}},
                {Engine::kIOS,     {{Version{16, 4, 0}}}},
                {Engine::kOpera,   {{Version{91, 0, 0}}}},
                {Engine::kSafari,  {{Version{16, 4, 0}}}},
            }},
            {CSSFeature::kModernRGBHSL, {
                {Engine::kChrome,  {{Version{66, 0, 0}}}},
                {Engine::kEdge,    {{Version{79, 0, 0}}}},
                {Engine::kFirefox, {{Version{52, 0, 0}}}},
                {Engine::kIOS,     {{Version{12, 2, 0}}}},
                {Engine::kOpera,   {{Version{53, 0, 0}}}},
                {Engine::kSafari,  {{Version{12, 1, 0}}}},
            }},
            {CSSFeature::kNesting, {
                {Engine::kChrome,  {{Version{120, 0, 0}}}},
                {Engine::kEdge,    {{Version{120, 0, 0}}}},
                {Engine::kFirefox, {{Version{117, 0, 0}}}},
                {Engine::kIOS,     {{Version{17, 2, 0}}}},
                {Engine::kOpera,   {{Version{106, 0, 0}}}},
                {Engine::kSafari,  {{Version{17, 2, 0}}}},
            }},
            {CSSFeature::kRebeccaPurple, {
                {Engine::kChrome,  {{Version{38, 0, 0}}}},
                {Engine::kEdge,    {{Version{12, 0, 0}}}},
                {Engine::kFirefox, {{Version{33, 0, 0}}}},
                {Engine::kIE,      {{Version{11, 0, 0}}}},
                {Engine::kIOS,     {{Version{8, 0, 0}}}},
                {Engine::kOpera,   {{Version{25, 0, 0}}}},
                {Engine::kSafari,  {{Version{9, 0, 0}}}},
            }},
        };

        // Represents a single vendor prefix entry for a CSS property. Each
        // entry specifies which engine requires the prefix, the version at
        // which the unprefixed version became available, and which prefix
        // to use. If without_prefix is Version{}, the property still
        // requires the prefix in all versions of that engine.
        //
        // Example:
        //   PrefixData{Engine::kChrome, Version{84, 0, 0}, CSSPrefix::kWebkitPrefix}
        //   means Chrome needs -webkit- prefix for versions before 84.0.0.
        struct PrefixData {
            Engine engine;
            Version without_prefix;
            CSSPrefix prefix;
        };

        // Maps each CSS declaration to the vendor prefixes required across
        // different browser engines. Each property has a vector of PrefixData
        // entries describing which engines need which prefixes and until which
        // version. During compilation, this table is consulted to determine
        // whether to emit prefixed versions of a property.
        //
        // Example:
        //   kCssPrefixTable[css::kDUserSelect] contains entries for Chrome,
        //   Edge, Firefox, IE, iOS, Opera, and Safari with various prefixes
        //   (webkit, moz, ms, khtml) depending on the engine and version.
        //
        // Edge cases:
        //   - Some properties like kDBoxDecorationBreak have Version{} for
        //     iOS and Safari, meaning they always need the prefix.
        //   - kDUserSelect has two Safari entries: one for khtml (very old)
        //     and one for webkit (newer), showing historical prefix evolution.
        //   - Properties not in this table require no vendor prefixes.
        const std::unordered_map<css::Declarations, std::vector<PrefixData>> kCssPrefixTable = {
            {css::kDAppearance, {
                {Engine::kChrome,  Version{84, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{84, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kFirefox, Version{80, 0, 0},  CSSPrefix::kMozPrefix},
                {Engine::kIOS,     Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{73, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDBackdropFilter, {
                {Engine::kIOS,     Version{18, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{18, 0, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDBackgroundClip, {
                {Engine::kChrome,  Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{15, 0, 0},  CSSPrefix::kMsPrefix},
                {Engine::kEdge,    Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{106, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{5, 0, 0},   CSSPrefix::kWebkitPrefix},
            }},
            {css::kDBoxDecorationBreak, {
                {Engine::kChrome,  Version{130, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{130, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{116, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{},           CSSPrefix::kWebkitPrefix},
            }},
            {css::kDClipPath, {
                {Engine::kChrome,  Version{55, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{13, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{42, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{13, 1, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDFontKerning, {
                {Engine::kChrome,  Version{33, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{12, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{20, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{9, 1, 0},   CSSPrefix::kWebkitPrefix},
            }},
            {css::kDHeight, {
                {Engine::kChrome,  Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kFirefox, Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{122, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{},           CSSPrefix::kWebkitPrefix},
            }},
            {css::kDHyphens, {
                {Engine::kEdge,    Version{79, 0, 0},  CSSPrefix::kMsPrefix},
                {Engine::kFirefox, Version{43, 0, 0},  CSSPrefix::kMozPrefix},
                {Engine::kIE,      Version{},           CSSPrefix::kMsPrefix},
                {Engine::kIOS,     Version{17, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{17, 0, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDInitialLetter, {
                {Engine::kIOS,     Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{},           CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMask, {
                {Engine::kChrome,  Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{106, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMaskComposite, {
                {Engine::kChrome,  Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{106, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMaskImage, {
                {Engine::kChrome,  Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMaskOrigin, {
                {Engine::kChrome,  Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{106, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMaskPosition, {
                {Engine::kChrome,  Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{106, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMaskRepeat, {
                {Engine::kChrome,  Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{106, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMaskSize, {
                {Engine::kChrome,  Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{120, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{106, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMaxHeight, {
                {Engine::kChrome,  Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{122, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{},           CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMaxWidth, {
                {Engine::kChrome,  Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{122, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{},           CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMinHeight, {
                {Engine::kChrome,  Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{122, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{},           CSSPrefix::kWebkitPrefix},
            }},
            {css::kDMinWidth, {
                {Engine::kChrome,  Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{122, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{},           CSSPrefix::kWebkitPrefix},
            }},
            {css::kDPosition, {
                {Engine::kIOS,     Version{13, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{13, 0, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDPrintColorAdjust, {
                {Engine::kChrome,  Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{15, 4, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDTabSize, {
                {Engine::kFirefox, Version{91, 0, 0},  CSSPrefix::kMozPrefix},
                {Engine::kOpera,   Version{15, 0, 0},  CSSPrefix::kOPrefix},
            }},
            {css::kDTextDecorationColor, {
                {Engine::kFirefox, Version{36, 0, 0},  CSSPrefix::kMozPrefix},
                {Engine::kIOS,     Version{12, 2, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{12, 1, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDTextDecorationLine, {
                {Engine::kFirefox, Version{36, 0, 0},  CSSPrefix::kMozPrefix},
                {Engine::kIOS,     Version{12, 2, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{12, 1, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDTextDecorationSkip, {
                {Engine::kIOS,     Version{12, 2, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{12, 1, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDTextEmphasisColor, {
                {Engine::kChrome,  Version{99, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{99, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{85, 0, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDTextEmphasisPosition, {
                {Engine::kChrome,  Version{99, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{99, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{85, 0, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDTextEmphasisStyle, {
                {Engine::kChrome,  Version{99, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{99, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{85, 0, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDTextOrientation, {
                {Engine::kSafari,  Version{14, 0, 0},  CSSPrefix::kWebkitPrefix},
            }},
            {css::kDTextSizeAdjust, {
                {Engine::kEdge,    Version{79, 0, 0},  CSSPrefix::kMsPrefix},
                {Engine::kIOS,     Version{},           CSSPrefix::kWebkitPrefix},
            }},
            {css::kDUserSelect, {
                {Engine::kChrome,  Version{54, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{79, 0, 0},  CSSPrefix::kMsPrefix},
                {Engine::kFirefox, Version{69, 0, 0},  CSSPrefix::kMozPrefix},
                {Engine::kIE,      Version{},           CSSPrefix::kMsPrefix},
                {Engine::kIOS,     Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{41, 0, 0},  CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{3, 0, 0},   CSSPrefix::kKhtmlPrefix},
                {Engine::kSafari,  Version{},           CSSPrefix::kWebkitPrefix},
            }},
            {css::kDWidth, {
                {Engine::kChrome,  Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kEdge,    Version{138, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kFirefox, Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kIOS,     Version{},           CSSPrefix::kWebkitPrefix},
                {Engine::kOpera,   Version{122, 0, 0}, CSSPrefix::kWebkitPrefix},
                {Engine::kSafari,  Version{},           CSSPrefix::kWebkitPrefix},
            }},
        };
    }

    // Determines which CSS features are NOT supported by all target browsers
    // specified in the constraints map. For each CSS feature in kCssTable,
    // this function checks whether every browser engine in the constraints
    // supports that feature at the specified version. If any engine doesn't
    // support the feature (either because it's not in the table or the
    // version is too old), the feature is added to the unsupported bitmask.
    //
    // Example:
    //   constraints = {{Engine::kChrome, Semver{.parts={120}}}}
    //   UnsupportedCSSFeatures(constraints) might return kNesting if Chrome
    //   120+ is required but the feature needs Chrome 121+.
    //
    // Edge cases:
    //   - Non-browser engines (like ES2020 targets) are skipped because
    //     they don't affect CSS support.
    //   - kInlineStyle is always skipped because it's purely user-specified.
    //   - If a browser engine has no entry for a feature, that feature is
    //     considered unsupported for that engine.
    //   - The function uses bitwise OR to accumulate all unsupported features
    //     into a single CSSFeature bitmask.
    CSSFeature UnsupportedCSSFeatures(const std::unordered_map<Engine, Semver>& constraints) {
        CSSFeature unsupported = static_cast<CSSFeature>(0);
        for (const auto& [feature, engines] : kCssTable) {
            if (feature == CSSFeature::kInlineStyle) {
                continue; // Inline style is a user-specified preference, not a browser feature
            }
            for (const auto& [engine, version] : constraints) {
                if (!IsBrowser(engine)) {
                    // Skip non-browser targets (e.g., --target=es2020)
                    continue;
                }
                auto it = engines.find(engine);
                if (it == engines.end() || !IsVersionSupported(it->second, version)) {
                    unsupported |= feature;
                }
            }
        }
        return unsupported;
    }

    // Determines which vendor prefixes are needed for each CSS declaration
    // based on the target browser versions specified in constraints. For each
    // property in kCssPrefixTable, this function checks all browser engines
    // in the constraints. If a browser engine requires a prefix for that
    // property at the specified version, the prefix is added to the result.
    //
    // Example:
    //   constraints = {{Engine::kChrome, Semver{.parts={80}}}}
    //   CSSPrefixData(constraints) might return:
    //   {css::kDAppearance -> CSSPrefix::kWebkitPrefix}
    //   because Chrome 80 needs -webkit-appearance.
    //
    // Edge cases:
    //   - Non-browser engines (like ES2020 targets) are skipped because
    //     they don't affect CSS prefix requirements.
    //   - A property with Version{} for an engine always needs the prefix
    //     (no unprefixed version exists in that engine).
    //   - Multiple engines can contribute different prefixes for the same
    //     property (e.g., -webkit-, -moz-, -ms-).
    //   - If no prefixes are needed for any engine, the property is not
    //     included in the result map.
    std::unordered_map<css::Declarations, CSSPrefix> CSSPrefixData(
        const std::unordered_map<Engine, Semver>& constraints) {
        std::unordered_map<css::Declarations, CSSPrefix> entries;
        for (const auto& [property, items] : kCssPrefixTable) {
            CSSPrefix prefixes = CSSPrefix::kNoPrefix;
            for (const auto& [engine, version] : constraints) {
                if (!IsBrowser(engine)) {
                    // Skip non-browser targets (e.g., --target=es2020)
                    continue;
                }
                for (const auto& item : items) {
                    if (item.engine == engine &&
                        (item.without_prefix == Version{} ||
                         CompareVersions(item.without_prefix, version) > 0)) {
                        prefixes |= item.prefix;
                    }
                }
            }
            if (prefixes != CSSPrefix::kNoPrefix) {
                entries[property] = prefixes;
            }
        }
        return entries;
    }
}
