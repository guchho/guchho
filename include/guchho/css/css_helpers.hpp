#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "guchho/helpers.hpp"
#include "css_properties.hpp"
#include "guchho/logger.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/css/css_ast.hpp"

namespace guchho::css {

    // Indices for the four corners of a border-radius shorthand value.
    // Order follows CSS specification: top-left, top-right, bottom-right, bottom-left.
    inline constexpr int kBorderRadiusTopLeft = 0;
    inline constexpr int kBorderRadiusTopRight = 1;
    inline constexpr int kBorderRadiusBottomRight = 2;
    inline constexpr int kBorderRadiusBottomLeft = 3;

    // Indices for the four sides of a box model property (margin, padding, inset).
    // Order follows CSS shorthand convention: top, right, bottom, left.
    inline constexpr int kBoxTop = 0;
    inline constexpr int kBoxRight = 1;
    inline constexpr int kBoxBottom = 2;
    inline constexpr int kBoxLeft = 3;


    // Holds the context needed to create CSS symbols for animation names.
    // When Guchho encounters animation-name values (or names embedded in the
    // animation shorthand), it creates symbol references so the names can be
    // renamed during minification without breaking references.
    //
    // Example:
    //   .box { animation: slide-in 1s; }
    //   // The "slide-in" token becomes a symbol that can be renamed to "a"
    //   // in the output, with all references updated accordingly.
    struct AnimationSymbolContext {
        // List of all symbols in the current source. Each Ref's inner index
        // serves as the PayloadIndex for TSymbol tokens.
        std::vector<compiler::Symbol>* symbols{};

        // List of locally-scoped symbols, used for tracking which symbols
        // were created in this specific source file.
        std::vector<compiler::LocRef>* local_symbols{};

        // Scope maps for name-based symbol lookup and reuse. local_scope
        // covers the current file, global_scope covers all files.
        std::unordered_map<std::string, compiler::LocRef>* local_scope{};
        std::unordered_map<std::string, compiler::LocRef>* global_scope{};

        // Index of the source file these symbols belong to.
        uint32_t source_index{};

        // Whether to create local (file-scoped) symbols rather than global ones.
        bool make_local_symbols{};
    };

    // Scans the tokens of an "animation" shorthand property to find and
    // register the animation name. The animation shorthand can contain the
    // name in various positions among other sub-values (duration, timing
    // function, iteration count, etc.).
    //
    // Example:
    //   animation: slide-in 1s ease-in-out
    //   // Identifies "slide-in" as the animation name and creates a symbol.
    void ProcessAnimationShorthand(
        std::vector<Token>& tokens,
        AnimationSymbolContext& context);

    // Scans the tokens of an "animation-name" property to find and register
    // the animation name. Unlike the shorthand, this property contains only
    // the name(s).
    //
    // Example:
    //   animation-name: slide-in;
    //   // Identifies "slide-in" and creates a symbol for it.
    void ProcessAnimationName(
        std::vector<Token>& tokens,
        AnimationSymbolContext& context);

    // Returns true if the given text is not a valid animation name. This
    // includes CSS reserved keywords like "none", "initial", "inherit",
    // and "unset" which cannot be used as animation names.
    //
    // Example:
    //   IsInvalidAnimationName("none")     -> true
    //   IsInvalidAnimationName("slide-in") -> false
    bool IsInvalidAnimationName(std::string_view text);


    // Tracks whether units used across multiple longhand declarations are
    // safe to compact into a shorthand. When units like vw, vh, em, or %
    // are mixed in ways that depend on the element's dimensions, compacting
    // into a shorthand could change the computed values.
    //
    // Example of unsafe compaction:
    //   margin-top: 1vw; margin-bottom: 2vw;  -> margin: 1vw 0 2vw;
    //   // Safe: same unit on both sides.
    //
    //   margin-top: 1vw; margin-bottom: 2vh;  -> cannot compact
    //   // Unsafe: vw and vh resolve differently depending on viewport.
    struct UnitSafetyTracker {
        enum class Status : uint8_t {
            // Unit is safe to use in shorthand compaction.
            // Example: "border-radius: 0 1px 2cm 3%;"
            kUnitSafe,

            // The same type of unsafe unit appears multiple times.
            // Example: "border-radius: 0 1vw 2vw 3vw;"
            kUnitUnsafeSingle,

            // Different types of unsafe units are mixed together.
            // Example: "border-radius: 0 1vw 2vh 3ch;"
            kUnitUnsafeMixed,
        };

        // The unit string encountered (e.g., "px", "vw", "%").
        std::string unit;

        // Current safety status, starts as safe.
        Status status{Status::kUnitSafe};

        // Checks whether this tracker's unit is compatible with another
        // tracker's unit for safe shorthand compaction.
        bool IsSafeWith(const UnitSafetyTracker& b) const;

        // Incorporates the unit from a token into this tracker's state,
        // updating the safety status if needed.
        void IncludeUnitOf(const Token& token);
    };

    // Tracks per-corner state for border-radius shorthand compaction.
    // Each corner can have one or two radius values (horizontal and vertical
    // radii for elliptical corners). This tracker decides whether multiple
    // border-radius declarations can be merged into a single shorthand.
    struct BorderRadiusTracker {
        struct Corner {
            // The first and second radius tokens (for circular vs elliptical corners).
            Token first_token;
            Token second_token;

            // Tracks whether the units used in this corner are safe for compaction.
            UnitSafetyTracker unit_safety;

            // Index of the rule that provided this corner's value.
            uint32_t rule_index{};

            // Whether this corner came from a single-property rule (e.g.,
            // border-top-left-radius) vs a shorthand rule.
            bool was_single_rule{};
        };

        // State for each of the four corners, indexed by kBorderRadius* constants.
        std::array<Corner, 4> corners;

        // Whether all active rules used !important.
        bool important{};

        // Updates a specific corner's state and potentially compacts rules.
        void UpdateCorner(
            std::vector<Rule>& rules,
            int corner,
            const Corner& new_corner);

        // Attempts to merge multiple border-radius declarations into shorthands.
        // Called when encountering a border-radius shorthand declaration.
        void MangleCorners(
            std::vector<Rule>& rules,
            RDeclaration* decl,
            bool minify_whitespace);

        // Attempts to compact a single border-radius corner declaration.
        // Called when encountering a longhand like border-top-left-radius.
        void MangleCorner(
            std::vector<Rule>& rules,
            RDeclaration* decl,
            bool minify_whitespace,
            int corner);

        // Compacts accumulated corner rules into shorthand declarations.
        void CompactRules(
            std::vector<Rule>& rules,
            logger::Range key_range,
            bool minify_whitespace);
    };

    // Holds the token and metadata for one side of a box model property
    // (margin, padding, or inset). Used by BoxTracker to decide whether
    // longhand declarations can be compacted into a shorthand.
    struct BoxSide {
        // The CSS value token for this side (e.g., "10px", "auto").
        Token token;

        // Tracks unit safety for shorthand compaction.
        UnitSafetyTracker unit_safety;

        // Index of the rule that provided this side's value.
        uint32_t rule_index{};

        // Whether this side came from a single-property rule vs a shorthand.
        bool was_single_rule{};
    };

    // Tracks state for margin, padding, and inset properties to determine
    // when longhand declarations (margin-top, margin-right, etc.) can be
    // compacted into a shorthand (margin). The compaction is only performed
    // when it is safe and would reduce output size.
    //
    // Example:
    //   margin-top: 10px; margin-right: 20px; margin-bottom: 10px; margin-left: 20px;
    //   // Compacted to: margin: 10px 20px;
    struct BoxTracker {
        // The CSS property name (e.g., "margin", "padding", "inset").
        std::string key_text;

        // State for each of the four sides, indexed by kBox* constants.
        std::array<BoxSide, 4> sides;

        // Whether the "auto" keyword is allowed for this property.
        // Margin allows auto; padding does not.
        bool allow_auto{};

        // Whether all active rules used !important.
        bool important{};

        // The shorthand declaration type to generate (kDMargin, kDPadding, kDInset).
        Declarations key{};

        // Updates a specific side's state and potentially compacts rules.
        void UpdateSide(
            std::vector<Rule>& rules,
            int side,
            const BoxSide& new_side);

        // Attempts to merge multiple longhand declarations into a shorthand.
        // Called when encountering a shorthand declaration (e.g., margin: ...).
        void MangleSides(
            std::vector<Rule>& rules,
            RDeclaration* decl,
            bool minify_whitespace);

        // Attempts to compact a single longhand declaration.
        // Called when encountering a side-specific declaration (e.g., margin-top).
        void MangleSide(
            std::vector<Rule>& rules,
            RDeclaration* decl,
            bool minify_whitespace,
            int side);

        // Compacts accumulated side rules into shorthand declarations.
        void CompactRules(
            std::vector<Rule>& rules,
            logger::Range key_range,
            bool minify_whitespace);
    };

    // A group of three deterministic floating-point values used in color
    // space conversions. Most color conversions operate on triplets of values
    // (e.g., RGB → XYZ produces three coordinates).
    struct F64x3 {
        helpers::F64 v0;
        helpers::F64 v1;
        helpers::F64 v2;
    };

    // Identifies which color space a color value is represented in.
    // Different color spaces have different gamuts and perceptual properties.
    enum class ColorSpace : uint8_t {
        kA98Rgb,       // Adobe RGB (1998) - wide-gamut RGB
        kDisplayP3,    // Display-P3 - Apple's wide-gamut color space
        kHsl,          // HSL - Hue, Saturation, Lightness
        kHwb,          // HWB - Hue, Whiteness, Blackness
        kLab,          // CIE Lab - perceptually uniform color space
        kLch,          // CIE LCH - cylindrical Lab
        kOklab,        // OKLab - modern perceptual color space
        kOklch,        // OKLCH - cylindrical OKLab
        kProphotoRgb,  // ProPhoto RGB - very wide-gamut RGB
        kRec2020,      // Rec. 2020 - UHDTV color space
        kSrgb,         // Standard RGB - the web standard
        kSrgbLinear,   // Linear sRGB - gamma-corrected sRGB
        kXyz,          // CIE XYZ - device-independent color space
        kXyzD50,       // CIE XYZ with D50 white point (used in ICC profiles)
        kXyzD65,       // CIE XYZ with D65 white point (sRGB reference white)
    };

    // Returns true if the color space uses polar coordinates for its
    // second and third components (e.g., HSL, OKLCH, LCH use hue angles).
    bool IsPolar(ColorSpace color_space);

    // Specifies the direction of hue interpolation when blending two colors
    // in a polar color space (HSL, HWB, OKLCH, LCH).
    enum class HueMethod : uint8_t {
        // Take the shorter arc between the two hues (0-180 degrees).
        kShorter,

        // Take the longer arc between the two hues (180-360 degrees).
        kLonger,

        // Always interpolate in the increasing hue direction.
        kIncreasing,

        // Always interpolate in the decreasing hue direction.
        kDecreasing,
    };

    // Color space conversion functions. These implement the mathematical
    // transformations between different color representations. All functions
    // use D65 white point unless otherwise noted (D50 variants exist for
    // ICC profile compatibility).
    //
    // Naming convention:
    //   Lin*  - Linear version of a gamut (gamma removed)
    //   Gam*  - Gamma-encoded version of a gamut
    //   *To*  - Conversion from one space to another
    //
    // Example:
    //   auto [x, y, z] = LinSrgbToXyz(1.0, 0.0, 0.0);
    //   // Converts pure red in linear sRGB to XYZ coordinates.
    F64x3 LinSrgb(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 GamSrgb(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 LinP3(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 GamP3(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 LinProphoto(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 GamProphoto(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 LinA98Rgb(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 GamA98Rgb(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 Lin2020(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 Gam2020(helpers::F64 r, helpers::F64 g, helpers::F64 b);

    // Converts between linear RGB variants and CIE XYZ (D65).
    F64x3 LinSrgbToXyz(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 XyzToLinSrgb(helpers::F64 x, helpers::F64 y, helpers::F64 z);
    F64x3 LinP3ToXyz(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 XyzToLinP3(helpers::F64 x, helpers::F64 y, helpers::F64 z);
    F64x3 LinProphotoToXyz(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 XyzToLinProphoto(helpers::F64 x, helpers::F64 y, helpers::F64 z);
    F64x3 LinA98RgbToXyz(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 XyzToLinA98Rgb(helpers::F64 x, helpers::F64 y, helpers::F64 z);
    F64x3 Lin2020ToXyz(helpers::F64 r, helpers::F64 g, helpers::F64 b);
    F64x3 XyzToLin2020(helpers::F64 x, helpers::F64 y, helpers::F64 z);

    // Converts between D65 and D50 adapted XYZ. D50 is used in ICC profiles
    // and some color management workflows.
    F64x3 D65ToD50(helpers::F64 x, helpers::F64 y, helpers::F64 z);
    F64x3 D50ToD65(helpers::F64 x, helpers::F64 y, helpers::F64 z);

    // Converts between XYZ and CIE Lab/LCH. Lab is perceptually uniform,
    // making it useful for color difference calculations.
    F64x3 XyzToLab(helpers::F64 x, helpers::F64 y, helpers::F64 z);
    F64x3 LabToXyz(helpers::F64 l, helpers::F64 a, helpers::F64 b);
    F64x3 LabToLch(helpers::F64 l, helpers::F64 a, helpers::F64 b);
    F64x3 LchToLab(helpers::F64 l, helpers::F64 c, helpers::F64 h);

    // Converts between XYZ and OKLab/OKLCH. OKLab is a modern perceptual
    // color space designed for better uniformity than CIE Lab.
    F64x3 XyzToOklab(helpers::F64 x, helpers::F64 y, helpers::F64 z);
    F64x3 OklabToXyz(helpers::F64 l, helpers::F64 a, helpers::F64 b);
    F64x3 OklabToOklch(helpers::F64 l, helpers::F64 a, helpers::F64 b);
    F64x3 OklchToOklab(helpers::F64 l, helpers::F64 c, helpers::F64 h);

    // Computes the Delta-E distance in OKLab space between two colors.
    // Lower values indicate more similar colors. A Delta-E of ~1 is the
    // just-noticeable difference for most observers.
    helpers::F64 DeltaEOk(helpers::F64 l1, helpers::F64 a1, helpers::F64 b1, helpers::F64 l2, helpers::F64 a2, helpers::F64 b2);

    // Maps an XYZ color into the sRGB gamut, clamping values that fall
    // outside the sRGB range. Returns the closest in-gamut sRGB color.
    F64x3 GamutMappingXyzToSrgb(helpers::F64 x, helpers::F64 y, helpers::F64 z);

    // Converts between HSL/HWB and RGB. These are the traditional CSS color
    // spaces that most developers are familiar with.
    F64x3 HslToRgb(helpers::F64 hue, helpers::F64 sat, helpers::F64 light);
    F64x3 RgbToHsl(helpers::F64 red, helpers::F64 green, helpers::F64 blue);
    F64x3 HwbToRgb(helpers::F64 hue, helpers::F64 white, helpers::F64 black);
    F64x3 RgbToHwb(helpers::F64 red, helpers::F64 green, helpers::F64 blue);

    // Generic conversion from any color space to XYZ and back.
    // The color_space parameter selects which conversion path to use.
    F64x3 XyzToColorSpace(helpers::F64 x, helpers::F64 y, helpers::F64 z, ColorSpace color_space);
    F64x3 ColorSpaceToXyz(helpers::F64 v0, helpers::F64 v1, helpers::F64 v2, ColorSpace color_space);

    // Returns a map from hex color values to the shortest CSS named color
    // that represents that exact color. Used when minifying colors to their
    // shortest representation.
    //
    // Example:
    //   ShortColorName()[0xFF0000] -> "red"  (3 chars vs 7 for "#ff0000")
    const std::unordered_map<uint32_t, std::string>& ShortColorName();

    // Returns a map from CSS color names to their hex values. Used for
    // converting named colors to hex during minification when the hex
    // representation is shorter.
    //
    // Example:
    //   ColorNameToHex().at("red") -> 0xFF0000
    const std::unordered_map<std::string, uint32_t>& ColorNameToHex();

    // Internal representation of a parsed CSS color. Colors can be stored
    // either as hex+alpha values or as XYZ coordinates with a separate
    // alpha channel, depending on whether a modern color space was used.
    struct ParsedColor {
        F64x3 xyz;          // XYZ coordinates when hasColorSpace is true.
        uint32_t hex{};     // Packed RGBA when hasColorSpace is false;
                            // alpha-only when hasColorSpace is true.
        bool hasColorSpace{}; // True if the color uses XYZ/color-space representation.
    };

    // Options controlling how CSS color declarations are processed.
    struct ColorDeclOptions {
        // Bitmask of CSS features not supported by the target browsers.
        guchho::compat::CSSFeature unsupported_css_features{};

        // Whether to aggressively minify CSS syntax (shorter output).
        bool minify_syntax{};

        // Whether to remove unnecessary whitespace.
        bool minify_whitespace{};
    };

    // Parses a hex color string (e.g., "#fff", "#ff000080", "abc") into
    // a packed 32-bit RGBA value. Supports 3, 4, 6, and 8 digit forms.
    //
    // Example:
    //   ParseHex("#ff0000")   -> (0xFF0000FF, true)
    //   ParseHex("#ff000080") -> (0xFF000080, true)
    //   ParseHex("invalid")   -> (0, false)
    std::pair<uint32_t, bool> ParseHex(std::string_view text);

    // Compacts an 8-digit hex color to 4-digit form when possible.
    // Each pair of identical hex digits becomes a single digit.
    //
    // Example:
    //   CompactHex(0xAABBCCDD) -> 0xABCD
    //   CompactHex(0xFF0000FF) -> 0xF00F  (not compactable, returns original)
    uint32_t CompactHex(uint32_t v);

    // Expands a 4-digit hex color to 8-digit form by duplicating each digit.
    //
    // Example:
    //   ExpandHex(0xABCD) -> 0xAABBCCDD
    uint32_t ExpandHex(uint32_t v);

    // Extracts individual color channels from a packed hex color value.
    //
    // Example:
    //   int r = HexR(0xFF804020);  // r = 0xFF = 255
    //   int g = HexG(0xFF804020);  // g = 0x80 = 128
    //   int b = HexB(0xFF804020);  // b = 0x40 = 64
    //   int a = HexA(0xFF804020);  // a = 0x20 = 32
    int HexR(uint32_t v);
    int HexG(uint32_t v);
    int HexB(uint32_t v);
    int HexA(uint32_t v);


    // Returns a map from hex color values to the shortest CSS named color
    // that represents that exact color.
    const std::unordered_map<uint32_t, std::string>& ShortColorName();

    // Returns a map from CSS color names to their hex values.
    const std::unordered_map<std::string, uint32_t>& ColorNameToHex();

    // Parses a hex color string into a packed RGBA value.
    std::pair<uint32_t, bool> ParseHex(std::string_view text);

    // Compacts 0xAABBCCDD to 0xABCD.
    uint32_t CompactHex(uint32_t v);

    // Expands 0xABCD to 0xAABBCCDD.
    uint32_t ExpandHex(uint32_t v);

    // Extracts color channels from packed hex values.
    int HexR(uint32_t v);
    int HexG(uint32_t v);
    int HexB(uint32_t v);
    int HexA(uint32_t v);

    // Converts a floating-point number to a string optimized for CSS color
    // values. Uses the shortest representation that round-trips correctly.
    //
    // Example:
    //   FloatToStringForColor(0.5)  -> "0.5"
    //   FloatToStringForColor(1.0)  -> "1"
    //   FloatToStringForColor(0.0)  -> "0"
    std::string FloatToStringForColor(double a);

    // Extracts the degree value from an angle token, handling various units
    // (deg, rad, grad, turn).
    //
    // Example:
    //   DegreesForAngle(Token("90deg"))  -> (90.0, true)
    //   DegreesForAngle(Token("0.5turn")) -> (180.0, true)
    //   DegreesForAngle(Token("invalid")) -> (0.0, false)
    std::pair<double, bool> DegreesForAngle(const Token& token);

    // Lowers an alpha percentage value to a plain number. In CSS Color Level 4,
    // alpha can be specified as a percentage (e.g., "50%") which gets converted
    // to a number between 0 and 1.
    //
    // Example:
    //   LowerAlphaPercentageToNumber(Token("50%")) -> Token("0.5")
    //   LowerAlphaPercentageToNumber(Token("100%")) -> Token("1")
    Token LowerAlphaPercentageToNumber(Token token);

    // Quick heuristic check whether a token looks like it could be a color
    // value. This is used to decide whether to apply color-specific lowering.
    //
    // Example:
    //   LooksLikeColor(Token("#ff0000"))  -> true
    //   LooksLikeColor(Token("rgb("))     -> true
    //   LooksLikeColor(Token("10px"))     -> false
    bool LooksLikeColor(const Token& token);

    // Parses a CSS color token into a ParsedColor. Handles hex colors,
    // named colors, rgb(), hsl(), oklch(), and other CSS color functions.
    //
    // Example:
    //   auto [color, ok] = ParseColor(Token("#ff0000"));
    //   // ok = true, color.hex = 0xFF0000FF
    std::pair<ParsedColor, bool> ParseColor(const Token& token);

    // Packs floating-point RGB values and an integer alpha into a single
    // 32-bit RGBA value. Each channel is clamped to [0, 255].
    //
    // Example:
    //   PackRGBA(1.0, 0.0, 0.0, 255) -> 0xFF0000FF
    uint32_t PackRGBA(helpers::F64 rf, helpers::F64 gf, helpers::F64 bf, uint32_t a);

    // Converts a floating-point value in [0.0, 1.0] to an integer byte [0, 255].
    //
    // Example:
    //   FloatToByte(0.5)  -> 128
    //   FloatToByte(1.0)  -> 255
    //   FloatToByte(0.0)  -> 0
    uint32_t FloatToByte(double f);

    // Parses an alpha value token into a byte [0, 255]. Handles both
    // number format (0-1) and percentage format (0%-100%).
    //
    // Example:
    //   ParseAlphaByte(Token("0.5"))  -> (128, true)
    //   ParseAlphaByte(Token("100%")) -> (255, true)
    std::pair<uint32_t, bool> ParseAlphaByte(const Token& token);

    // Parses a color component token into a byte value, scaled by the
    // given factor (e.g., 255 for 0-1 range, 1 for 0-255 range).
    //
    // Example:
    //   ParseColorByte(Token("1"), 255.0)   -> (255, true)
    //   ParseColorByte(Token("0.5"), 255.0) -> (128, true)
    std::pair<uint32_t, bool> ParseColorByte(const Token& token, double scale);

    // Attempts to convert an XYZ color to a hex sRGB value without any
    // gamut clipping. Returns the hex value and true if the color is
    // already within the sRGB gamut, or false if clipping would be needed.
    //
    // Example:
    //   TryToConvertToHexWithoutClipping(0.5, 0.2, 0.1, 255)
    //   // Returns (hex_value, true) if within sRGB gamut
    std::pair<uint32_t, bool> TryToConvertToHexWithoutClipping(
        helpers::F64 x, helpers::F64 y,
        helpers::F64 z, uint32_t a);

    // Creates a comma separator token for use in CSS color functions.
    //
    // Example:
    //   MakeCommaToken(loc, false) -> Token with text "," and trailing space
    //   MakeCommaToken(loc, true)  -> Token with text "," and no trailing space
    Token MakeCommaToken(logger::Loc loc, bool minify_whitespace = false);

    // Generates a new color token from a ParsedColor, choosing the shortest
    // valid CSS representation. May use named colors, hex, rgb(), oklch(),
    // or other formats depending on what's shortest.
    //
    // Example:
    //   TryToGenerateColor(token, color, options, &would_clip)
    //   // May produce: Token("red"), Token("#f00"), Token("rgb(255 0 0)")
    Token TryToGenerateColor(Token token, const ParsedColor& color,
                            const ColorDeclOptions& options,
                            bool* would_clip_color);

    // Lowers modern CSS color syntax (oklch(), oklab(), etc.) to older
    // syntax when needed for browser compatibility, and minifies the result.
    // Also handles color gamut clipping for colors outside sRGB.
    //
    // Example:
    //   LowerAndMinifyColor(Token("oklch(50% 0.1 200)"), options, &clip)
    //   // May produce: Token("rgb(128 48 180)") for older browsers
    Token LowerAndMinifyColor(Token token, const ColorDeclOptions& options,
                            bool* would_clip_color);


    // Processes and minifies box-shadow values. Each shadow in a comma-separated
    // list is individually processed: colors are lowered, whitespace is minified,
    // and redundant values are removed.
    //
    // Example:
    //   Input:  "0 0 5px rgba(0, 0, 0, 0.5)"
    //   Output: "0 0 5px #00000080" (if hex is shorter)
    std::vector<Token> LowerAndMangleBoxShadows(
        std::vector<Token> tokens,
        const ColorDeclOptions& options,
        bool minify_whitespace,
        bool* would_clip_color
    );

    // Lookup table for alpha values 0-255 as shortened decimal strings.
    // Used to efficiently convert alpha bytes to their shortest string
    // representation without floating-point formatting.
    constexpr std::string_view kAlphaFractionTable =
        "0   .004.008.01 .016.02 .024.027.03 .035.04 .043.047.05 .055.06 "
        ".063.067.07 .075.08 .082.086.09 .094.098.1  .106.11 .114.118.12 "
        ".125.13 .133.137.14 .145.15 .153.157.16 .165.17 .173.176.18 .184"
        ".19 .192.196.2  .204.208.21 .216.22 .224.227.23 .235.24 .243.247"
        ".25 .255.26 .263.267.27 .275.28 .282.286.29 .294.298.3  .306.31 "
        ".314.318.32 .325.33 .333.337.34 .345.35 .353.357.36 .365.37 .373"
        ".376.38 .384.39 .392.396.4  .404.408.41 .416.42 .424.427.43 .435"
        ".44 .443.447.45 .455.46 .463.467.47 .475.48 .482.486.49 .494.498"
        ".5  .506.51 .514.518.52 .525.53 .533.537.54 .545.55 .553.557.56 "
        ".565.57 .573.576.58 .584.59 .592.596.6  .604.608.61 .616.62 .624"
        ".627.63 .635.64 .643.647.65 .655.66 .663.667.67 .675.68 .682.686"
        ".69 .694.698.7  .706.71 .714.718.72 .725.73 .733.737.74 .745.75 "
        ".753.757.76 .765.77 .773.776.78 .784.79 .792.796.8  .804.808.81 "
        ".816.82 .824.827.83 .835.84 .843.847.85 .855.86 .863.867.87 .875"
        ".88 .882.886.89 .894.898.9  .906.91 .914.918.92 .925.93 .933.937"
        ".94 .945.95 .953.957.96 .965.97 .973.976.98 .984.99 .992.9961   ";

    
    // Minifies CSS transform functions by removing redundant operations,
    // simplifying identity transforms, and shortening function names.
    //
    // Example:
    //   MangleTransforms({translateX(0), translateY(0)})
    //   // May produce: {} (empty, since both are no-ops)
    std::vector<Token> MangleTransforms(std::vector<Token> tokens);

    // Attempts to simplify a calc() expression by evaluating constant
    // subexpressions and removing unnecessary parentheses. Only applies
    // to calc() function tokens, not other math functions.
    //
    // Example:
    //   TryToReduceCalcExpression(Token("calc(10px + 20px)"), false)
    //   // -> Token("30px")
    //
    //   TryToReduceCalcExpression(Token("calc(100% - 20px)"), true)
    //   // -> Token("calc(100%-20px)") (whitespace removed)
    Token TryToReduceCalcExpression(Token token, bool minify_whitespace);


    // Holds the context for processing a "composes" directive. When a CSS
    // class selector contains composes (CSS Modules), this context tracks
    // which parent class the composes applies to.
    struct ComposesContext {
        // References to the parent class selectors this composes applies to.
        std::vector<compiler::Ref> parent_refs;
        // Source range of the parent selector.
        logger::Range parent_range;
        // Source range of any problematic part of the composes value.
        logger::Range problem_range;
    };

    // Holds the state needed to process composes directives and create
    // the corresponding CSS symbols.
    struct ComposesSymbolContext {
        // Map from class selectors to their composes entries.
        std::unordered_map<compiler::Ref, std::shared_ptr<Composes>, RefHash>* composes{};

        // Import records for resolving cross-file references.
        std::vector<compiler::ImportRecord>* import_records{};

        // All symbols in the current source.
        std::vector<compiler::Symbol>* symbols{};

        // Locally-scoped symbols.
        std::vector<compiler::LocRef>* local_symbols{};

        // Scope maps for name lookup.
        std::unordered_map<std::string, compiler::LocRef>* local_scope{};
        std::unordered_map<std::string, compiler::LocRef>* global_scope{};

        uint32_t source_index{};
        bool make_local_symbols{};

        // Source metadata for error reporting.
        const logger::Source* source{};
        logger::Log* log{};
        logger::LineColumnTracker* tracker{};

        // Location of the previous composes error, used to avoid duplicate warnings.
        logger::Loc* prev_error{};
    };

    // Processes a "composes" declaration from a CSS Modules file. This
    // extracts the composed class names and records the relationships
    // so that the final class names can be resolved during bundling.
    //
    // Example:
    //   .button { composes: primary; }
    //   // Records that .button includes the classes from .primary
    void HandleComposesPragma(const ComposesContext& context,
                            const std::vector<Token>& tokens,
                            ComposesSymbolContext& symbol_context);



    // Holds the context needed to create CSS symbols for container query
    // names. When Guchho encounters container-name values (or names in the
    // container shorthand), it creates symbol references so the names can
    // be renamed during minification.
    struct ContainerSymbolContext {
        // All symbols in the current source.
        std::vector<compiler::Symbol>* symbols{};

        // Locally-scoped symbols.
        std::vector<compiler::LocRef>* local_symbols{};

        // Scope maps for name lookup.
        std::unordered_map<std::string, compiler::LocRef>* local_scope{};
        std::unordered_map<std::string, compiler::LocRef>* global_scope{};

        uint32_t source_index{};
        bool make_local_symbols{};
    };

    // Processes the "container" shorthand property to find and register
    // container names. The shorthand can contain the name among other
    // sub-values (type, name).
    //
    // Example:
    //   container: sidebar / inline-size;
    //   // Registers "sidebar" as a container name symbol.
    void ProcessContainerShorthand(std::vector<Token>& tokens,
                                ContainerSymbolContext& context);

    // Processes the "container-name" property to find and register
    // container names.
    //
    // Example:
    //   container-name: sidebar;
    //   // Registers "sidebar" as a container name symbol.
    void ProcessContainerName(std::vector<Token>& tokens,
                            ContainerSymbolContext& context);


    // Identifies the type of CSS gradient function.
    enum class GradientKind : uint8_t {
        kLinear,  // linear-gradient()
        kRadial, // radial-gradient()
        kConic,  // conic-gradient()
    };

    // Represents a single color stop in a gradient, including its color,
    // optional midpoint, and position(s).
    struct ColorStop {
        // Position tokens (one for regular stops, two for double-position stops).
        std::vector<Token> positions;
        // The color token for this stop.
        Token color;
        // Midpoint between this stop and the next (empty if not specified).
        Token midpoint;
    };

    // Represents a fully parsed CSS gradient function, with its type,
    // leading tokens (angle, shape keywords), and color stops.
    struct ParsedGradient {
        // Tokens before the color stops (e.g., "to right", "45deg", "circle").
        std::vector<Token> leading_tokens;
        // The list of color stops with their positions.
        std::vector<ColorStop> color_stops;
        // The type of gradient (linear, radial, conic).
        GradientKind kind{};
        // Whether this is a repeating gradient.
        bool repeating{};
    };

    // A numeric value paired with its CSS unit (e.g., "10px", "50%").
    struct ValueWithUnit {
        std::string unit;
        helpers::F64 value;
    };

    // Result of extracting "in <color space>" from a gradient's tokens.
    // Modern CSS gradients allow specifying the interpolation color space.
    struct ColorInterpolation {
        // Remaining tokens after removing the color interpolation directive.
        std::vector<Token> remaining;
        // The specified color space for interpolation.
        ColorSpace color_space{};
        // The hue interpolation method.
        HueMethod hue_method{};
        // Whether an "in <color space>" directive was found and removed.
        bool found{};
    };

    // Parses a CSS gradient function token into its structured representation.
    // Handles linear-gradient(), radial-gradient(), conic-gradient(), and their
    // repeating variants.
    //
    // Example:
    //   auto [gradient, ok] = ParseGradient(Token("linear-gradient(red, blue)"));
    //   // ok = true, gradient.kind = kLinear, gradient.color_stops.size() = 2
    std::pair<ParsedGradient, bool> ParseGradient(const Token& token);

    // Generates a gradient token from a ParsedGradient structure. Produces
    // the shortest valid CSS representation, potentially using modern syntax
    // when the target browsers support it.
    //
    // Example:
    //   GenerateGradient(token, gradient, false)
    //   // -> Token("linear-gradient(red,blue)")
    Token GenerateGradient(Token token, const ParsedGradient& gradient,
                            bool minify_whitespace = false);

    // Lowers modern gradient syntax to older forms when needed for browser
    // compatibility, and minifies the result. Handles color space conversion,
    // double-position stops, and color function lowering.
    //
    // Example:
    //   LowerAndMinifyGradient(Token("linear-gradient(in oklch, red, blue)"), opts, &clip)
    //   // May produce: Token("linear-gradient(red,blue)") for older browsers
    Token LowerAndMinifyGradient(Token token, const ColorDeclOptions& options,
                                bool* would_clip_color);

    // Removes implied positions from gradient color stops. When positions
    // are evenly spaced, they can be omitted since the browser will infer
    // them automatically.
    //
    // Example:
    //   Input:  [red 0%, blue 100%]
    //   Output: [red, blue]  (positions are implied)
    std::vector<ColorStop> RemoveImpliedPositions(
        GradientKind kind, std::vector<ColorStop> color_stops);

    // Converts double-position color stops to single positions. A double-position
    // stop like "red 0% 50%" means the color is solid from 0% to 50%. This
    // may be shorter to express as two separate stops.
    //
    // Example:
    //   Input:  [red 0% 50%, blue 100%]
    //   Output: [red 0%, red 50%, blue 100%]
    std::vector<ColorStop> SwitchToSinglePositions(
        const std::vector<ColorStop>& double_positions);

    // Converts single-position color stops to double positions when it
    // would result in shorter output. Two consecutive stops with the
    // same color can be merged into one double-position stop.
    //
    // Example:
    //   Input:  [red 0%, red 50%, blue 100%]
    //   Output: [red 0% 50%, blue 100%]
    std::vector<ColorStop> SwitchToDoublePositions(
        const std::vector<ColorStop>& single_positions);

    // Extracts and removes the "in <color space>" interpolation directive
    // from a list of gradient tokens. Returns the remaining tokens and
    // the parsed color space information.
    //
    // Example:
    //   RemoveColorInterpolation({"in", "oklch", ",", "red", ",", "blue"})
    //   // Returns remaining: {",", "red", ",", "blue"}, color_space: kOklch, found: true
    ColorInterpolation RemoveColorInterpolation(
        const std::vector<Token>& tokens);

    // Attempts to parse a token as a numeric value with a CSS unit.
    // The kind parameter determines which units are valid (e.g., angles
    // for linear-gradient, lengths for radial-gradient).
    //
    // Example:
    //   TryToParseValue(Token("10px"), kRadial) -> ({"px", 10.0}, true)
    //   TryToParseValue(Token("45deg"), kLinear) -> ({"deg", 45.0}, true)
    std::pair<ValueWithUnit, bool> TryToParseValue(
        const Token& token, GradientKind kind);

    // Minifies a font-family declaration by removing unnecessary quotes
    // from font family names that are valid CSS identifiers. Returns false
    // if the declaration cannot be safely minified.
    //
    // Example:
    //   MangleFontFamily(result, {Token("\"Arial\"")}, false)
    //   // result = {Token("Arial")}  (quotes removed)
    //
    //   MangleFontFamily(result, {Token("\"New Times\"")}, false)
    //   // result unchanged (needs quotes because of space)
    bool MangleFontFamily(std::vector<Token>& result,
                        const std::vector<Token>& tokens,
                        bool minify_whitespace);

    // Converts font-weight keyword values to their numeric equivalents.
    // "normal" becomes 400, "bold" becomes 700. Other tokens pass through
    // unchanged.
    //
    // Example:
    //   MangleFontWeight(Token("normal")) -> Token("400")
    //   MangleFontWeight(Token("bold"))   -> Token("700")
    //   MangleFontWeight(Token("500"))    -> Token("500") (unchanged)
    Token MangleFontWeight(Token token);

    // Minifies a font shorthand declaration by removing unnecessary "normal"
    // values and applying font-weight and font-family minification. Returns
    // the original tokens if the declaration cannot be safely minified.
    //
    // Example:
    //   MangleFont({Token("normal"), Token("16px"), Token("normal"), Token("Arial")}, false)
    //   // -> {Token("16px"), Token("Arial")}  (redundant "normal" removed)
    std::vector<Token> MangleFont(const std::vector<Token>& tokens, bool minify_whitespace);

    // Holds the context needed to create CSS symbols for custom list-style-type
    // names. When Guchho encounters custom list style values, it creates symbol
    // references so the names can be renamed during minification.
    struct ListStyleSymbolContext {
        // All symbols in the current source.
        std::vector<guchho::compiler::Symbol>* symbols{};

        // Locally-scoped symbols.
        std::vector<guchho::compiler::LocRef>* local_symbols{};

        // Scope maps for name lookup.
        std::unordered_map<std::string, guchho::compiler::LocRef>* local_scope{};
        std::unordered_map<std::string, guchho::compiler::LocRef>* global_scope{};

        uint32_t source_index{};
        bool make_local_symbols{};
    };

    // Processes the "list-style" shorthand property to find and register
    // custom list-style-type names.
    //
    // Example:
    //   list-style: square outside my-custom-type;
    //   // Registers "my-custom-type" as a symbol.
    void ProcessListStyleShorthand(std::vector<Token>& tokens,
                                ListStyleSymbolContext& context);

    // Processes a "list-style-type" value. If it's a custom ident (not a
    // standard keyword like "disc" or "circle"), creates a symbol for it.
    //
    // Example:
    //   ProcessListStyleType(&Token("my-custom-type"), context)
    //   // Creates a symbol for "my-custom-type" that can be renamed.
    void ProcessListStyleType(Token* token, ListStyleSymbolContext& context);


    // Appends a media query term to an inner list, optionally simplifying
    // the result when minifying. When the operator matches (both AND or
    // both OR), nested terms are flattened.
    //
    // Example:
    //   AppendMediaTerm({(a)}, (b), AND, true)
    //   // -> {a AND b}  (flattened from (a) AND b)
    //
    //   AppendMediaTerm({(a AND b)}, (c), AND, true)
    //   // -> {a AND b AND c}  (nested AND flattened)
    std::vector<MediaQuery> AppendMediaTerm(
        std::vector<MediaQuery> inner,
        const MediaQuery& term,
        MQBinaryOp op,
        bool minify_syntax);

    // Lowers a range-style media feature comparison to the older "min-*"/"max-*"
    // form when possible. This improves compatibility with older browsers.
    //
    // Example:
    //   LowerMediaRange(loc, "width", CMP_GTE, {Token("500px")})
    //   // -> "(min-width: 500px)"
    MediaQuery LowerMediaRange(
        guchho::logger::Loc loc,
        const std::string& name,
        MQCmp cmp,
        std::vector<Token> value);

    // Attempts to parse a span of tokens as a plain or boolean media feature.
    // Plain features have a name and optional value (e.g., "color", "width: 100px").
    // Boolean features are just a name (e.g., "color", "hover").
    //
    // Example:
    //   ParsePlainOrBooleanMediaFeature({"color"})
    //   // -> MQPlainOrBoolean{name: "color", value: nullopt}
    //
    //   ParsePlainOrBooleanMediaFeature({"width", ":", "100px"})
    //   // -> MQPlainOrBoolean{name: "width", value: "100px"}
    std::shared_ptr<MQPlainOrBoolean> ParsePlainOrBooleanMediaFeature(
        std::span<Token> tokens);

    // Attempts to parse a span of tokens as a range-style media feature.
    // Range features use comparison operators (e.g., "width >= 500px",
    // "500px <= width <= 700px").
    //
    // Example:
    //   ParseRangeMediaFeature({"width", ">=", "500px"})
    //   // -> MQRange{name: "width", cmp: CMP_GTE, value: "500px"}
    std::shared_ptr<MQRange> ParseRangeMediaFeature(
        std::span<Token> tokens);

    // Simplifies media queries during minification. Handles double negation
    // ("not (not a)" → "a") and applies De Morgan's law to binary expressions
    // to reduce nesting.
    //
    // Example:
    //   MaybeSimplifyMediaNot(loc, MQNot(MQNot(a)), true)
    //   // -> a  (double negation removed)
    MediaQuery MaybeSimplifyMediaNot(
        guchho::logger::Loc loc,
        MediaQuery inner,
        bool minify_syntax);

    // Checks whether a token span contains exactly one identifier, and if so,
    // returns its name and source location.
    //
    // Example:
    //   IsSingleIdent({Token("hover")}) -> ("hover", loc, true)
    //   IsSingleIdent({Token("hover"), Token("(")}) -> ("", loc, false)
    std::tuple<std::string, guchho::logger::Loc, bool> IsSingleIdent(
        std::span<Token> tokens);

    // Scans and consumes a media comparison operator (=, <, <=, >, >=)
    // from the beginning of a token span.
    //
    // Example:
    //   ScanMediaComparison({">=", Token("500px")})
    //   // -> (CMP_GTE, remaining: {Token("500px")})
    std::pair<MQCmp, std::span<Token>> ScanMediaComparison(
        std::span<Token> tokens);

    // Scans and consumes a media value from the beginning of a token span.
    // Supports numbers, identifiers, dimensions (e.g., "500px"), and
    // ratios (e.g., "16/9").
    //
    // Example:
    //   ScanMediaValue({Token("500px"), Token(">="), Token("width")})
    //   // -> (value: {Token("500px")}, remaining: {Token(">="), Token("width")})
    std::pair<std::span<Token>, std::span<Token>> ScanMediaValue(
        std::span<Token> tokens);


    // Options controlling how CSS declarations are processed.
    struct DeclarationsOptions {
        // Bitmask of CSS features not supported by target browsers.
        guchho::compat::CSSFeature unsupported_css_features{};

        // Map from CSS properties to their required vendor prefixes.
        std::unordered_map<Declarations, guchho::compat::CSSPrefix>* css_prefix_data{};

        // Whether to aggressively minify CSS syntax.
        bool minify_syntax{};

        // Whether to remove unnecessary whitespace.
        bool minify_whitespace{};

        // Whether CSS Modules symbol mode is enabled (for composes processing).
        bool symbol_mode_enabled{};
    };

    // Holds the state needed to create CSS symbols during declaration processing.
    struct DeclarationsSymbolContext {
        // All symbols in the current source.
        std::vector<guchho::compiler::Symbol>* symbols{};

        // Locally-scoped symbols.
        std::vector<guchho::compiler::LocRef>* local_symbols{};

        // Scope maps for name lookup.
        std::unordered_map<std::string, guchho::compiler::LocRef>* local_scope{};
        std::unordered_map<std::string, guchho::compiler::LocRef>* global_scope{};

        // Map from class selectors to their composes entries.
        std::unordered_map<guchho::compiler::Ref, std::shared_ptr<Composes>, RefHash>* composes{};

        // Import records for resolving cross-file references.
        std::vector<guchho::compiler::ImportRecord>* import_records{};

        uint32_t source_index{};
        bool make_local_symbols{};
    };

    // Holds the state needed for error reporting during declaration processing.
    struct DeclarationsLogContext {
        guchho::logger::Log* log{};
        guchho::logger::LineColumnTracker* tracker{};
        guchho::logger::Loc* prev_error{};
        const guchho::logger::Source* source{};
    };

    // Main entry point for processing a list of CSS declarations. This function
    // applies all lowering, minification, and symbol creation transformations
    // to the declarations. It handles:
    //
    //   - Color lowering and minification (oklch → rgb, named colors, hex)
    //   - Gradient lowering and minification
    //   - Box-shadow processing
    //   - Transform minification
    //   - Font shorthand minification
    //   - Box model shorthand compaction (margin, padding, inset)
    //   - Border-radius compaction
    //   - Vendor prefix insertion
    //   - CSS Modules composes processing
    //   - Container query name symbol creation
    //   - Animation name symbol creation
    //   - List-style type symbol creation
    //
    // Example:
    //   auto result = ProcessDeclarations(rules, options, nullptr, sym_ctx, log_ctx);
    //   // result contains the transformed declarations
    std::vector<Rule> ProcessDeclarations(
        std::vector<Rule> rules,
        const DeclarationsOptions& options,
        const ComposesContext* composes_context,
        DeclarationsSymbolContext& symbol_context,
        const DeclarationsLogContext& log_context);


}
