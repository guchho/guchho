#include "guchho/css/css_helpers.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/helpers.hpp"

#include <string>
#include <string_view>


#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace guchho::css {

    namespace {

        using guchho::helpers::F64;
        using guchho::helpers::Lerp;

        // Converts a string to lowercase using ASCII rules.
        //
        // Input:  text = "RED"
        // Output: "red"
        //
        // Input:  text = "Hello World"
        // Output: "hello world"
        //
        std::string ToLower(std::string_view text) {
            std::string result(text);
            std::transform(result.begin(), result.end(), result.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        // Case-insensitive string comparison. Returns true if both strings
        // are equal when compared character-by-character in lowercase.
        //
        // Input:  a = "RGB", b = "rgb"
        // Output: true
        //
        // Input:  a = "red", b = "blue"
        // Output: false
        //
        // Input:  a = "abc", b = "abcd"
        // Output: false (different lengths)
        //
        bool EqualFold(std::string_view a, std::string_view b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); i++) {
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        // Parses a string view into a double value using std::from_chars.
        // Returns true if the entire string was consumed and parsing succeeded.
        //
        // Input:  text = "3.14", out = &d
        // Output: true, d = 3.14
        //
        // Input:  text = "100%", out = &d
        // Output: false (trailing '%' not consumed)
        //
        // Input:  text = "var(--x)", out = &d
        // Output: false (not a number)
        //
        bool ParseFloat(std::string_view text, double* out) {
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            auto result = std::from_chars(begin, end, *out);
            return result.ec == std::errc() && result.ptr == end;
        }

        // Formats a 32-bit unsigned integer as a hexadecimal string with
        // a minimum width (zero-padded).
        //
        // Input:  v = 0xFF, width = 6
        // Output: "0000ff"
        //
        // Input:  v = 0x123456, width = 6
        // Output: "123456"
        //
        // Input:  v = 0xA, width = 3
        // Output: "00a"
        //
        std::string FormatHex(uint32_t v, int width) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "%0*x", width, v);
            return buffer;
        }


        // Converts a hue value to an RGB fraction (0.0 to 1.0) using the
        // HSL-to-RGB algorithm. This is a helper for the HslToRgbFraction function.
        //
        // The algorithm uses the "六" (six) sectors of the color wheel:
        //
        // Input:  t1=0.25, t2=0.75, hue=0.0 (red sector)
        // Output: value interpolated between t1 and t2 based on hue
        //
        // Input:  t1=0.25, t2=0.75, hue=0.5 (green sector)
        // Output: t2 (0.75)
        //
        F64 HueToRgbFraction(F64 t1, F64 t2, F64 hue) {
            hue = hue.Sub(hue.Floor());
            hue = hue.MulConst(6);
            F64 f;
            if (hue.Value() < 1) {
                f = Lerp(t1, t2, hue);
            } else if (hue.Value() < 3) {
                f = t2;
            } else if (hue.Value() < 4) {
                f = Lerp(t1, t2, hue.Neg().AddConst(4));
            } else {
                f = t1;
            }
            return f;
        }

        // Converts HSL (Hue, Saturation, Lightness) to RGB fractional values.
        // All inputs are in the range [0, 1] (hue is normalized from degrees).
        // Output is three F64 values in [0, 1] representing R, G, B.
        //
        // The algorithm:
        //   1. Calculate t2 based on lightness and saturation.
        //   2. Calculate t1 from t2 and lightness.
        //   3. Use HueToRgbFraction for each color channel with 120-degree offsets.
        //
        // Input:  hue=0.0 (red), sat=1.0, light=0.5
        // Output: {1.0, 0.0, 0.0} (pure red)
        //
        // Input:  hue=0.333 (green), sat=1.0, light=0.5
        // Output: {0.0, 1.0, 0.0} (pure green)
        //
        // Input:  hue=0.0, sat=0.0, light=0.5
        // Output: {0.5, 0.5, 0.5} (gray)
        //
        F64x3 HslToRgbFraction(F64 hue, F64 sat, F64 light) {
            hue = hue.DivConst(360.0);
            F64 t2;
            if (light.Value() <= 0.5) {
                t2 = sat.AddConst(1).Mul(light);
            } else {
                t2 = light.Add(sat).Sub(light.Mul(sat));
            }
            F64 t1 = light.MulConst(2).Sub(t2);
            F64 r = HueToRgbFraction(t1, t2, hue.AddConst(1.0 / 3.0));
            F64 g = HueToRgbFraction(t1, t2, hue);
            F64 b = HueToRgbFraction(t1, t2, hue.SubConst(1.0 / 3.0));
            return F64x3{r, g, b};
        }


        // Converts HWB (Hue, Whiteness, Blackness) to RGB fractional values.
        // All inputs are in the range [0, 1] (hue is normalized from degrees).
        // Output is three F64 values in [0, 1] representing R, G, B.
        //
        // The algorithm:
        //   1. If whiteness + blackness >= 1, return a gray value.
        //   2. Otherwise, calculate a full-saturation RGB color from hue.
        //   3. Mix in whiteness and blackness to produce the final color.
        //
        // Input:  hue=0.0 (red), white=0.0, black=0.0
        // Output: {1.0, 0.0, 0.0} (pure red)
        //
        // Input:  hue=0.0, white=0.5, black=0.0
        // Output: {1.0, 0.5, 0.5} (pink/red tinted with white)
        //
        // Input:  hue=0.0, white=0.0, black=0.5
        // Output: {0.5, 0.0, 0.0} (dark red)
        //
        // Input:  hue=0.0, white=0.3, black=0.7
        // Output: {0.3, 0.3, 0.3} (gray, since w+b >= 1)
        //
        //     proportional to whiteness / (whiteness + blackness).
        F64x3 HwbToRgbFraction(F64 hue, F64 white, F64 black) {
            if (white.Add(black).Value() >= 1) {
                F64 gray = white.Div(white.Add(black));
                return F64x3{gray, gray, gray};
            }
            F64 delta = white.Add(black).Neg().AddConst(1);
            F64x3 rgb = HslToRgbFraction(hue, F64(1), F64(0.5));
            return F64x3{
                delta.Mul(rgb.v0).Add(white),
                delta.Mul(rgb.v1).Add(white),
                delta.Mul(rgb.v2).Add(white),
            };
        }

    }

    // Returns a static map of short hex color codes to their named equivalents.
    // This map contains only the most common/simplistic color names that
    // fit in a short hex format.
    //
    // The map is keyed by RGBA uint32 values (e.g., 0xFF0000FF for red).
    //
    // Input:  (none - accessor function)
    // Output: Reference to static map with 30 entries
    //
    //   - Returns a const reference; modifications are not possible.
    const std::unordered_map<uint32_t, std::string>& ShortColorName() {
        static const std::unordered_map<uint32_t, std::string> map = {
            {0x000080ff, "navy"},  {0x008000ff, "green"},  {0x008080ff, "teal"},
            {0x4b0082ff, "indigo"}, {0x800000ff, "maroon"}, {0x800080ff, "purple"},
            {0x808000ff, "olive"},  {0x808080ff, "gray"},   {0xa0522dff, "sienna"},
            {0xa52a2aff, "brown"},  {0xc0c0c0ff, "silver"}, {0xcd853fff, "peru"},
            {0xd2b48cff, "tan"},    {0xda70d6ff, "orchid"}, {0xdda0ddff, "plum"},
            {0xee82eeff, "violet"}, {0xf0e68cff, "khaki"},  {0xf0ffffff, "azure"},
            {0xf5deb3ff, "wheat"},  {0xf5f5dcff, "beige"},  {0xfa8072ff, "salmon"},
            {0xfaf0e6ff, "linen"},  {0xff0000ff, "red"},    {0xff6347ff, "tomato"},
            {0xff7f50ff, "coral"},  {0xffa500ff, "orange"}, {0xffc0cbff, "pink"},
            {0xffd700ff, "gold"},   {0xffe4c4ff, "bisque"}, {0xfffafaff, "snow"},
            {0xfffff0ff, "ivory"},
        };
        return map;
    }

    // Returns a static map of CSS color names to their hex RGBA values.
    // This contains all standard CSS named colors (148 entries).
    //
    // The map is keyed by lowercase color name strings.
    // Values are RGBA uint32 values with alpha in the lowest byte.
    //
    // Input:  (none - accessor function)
    // Output: Reference to static map with 148 entries
    //
    //   - Color names are case-sensitive in the map (use ToLower() before lookup).
    //   - "grey" and "gray" are both present (mapped to same value).
    const std::unordered_map<std::string, uint32_t>& ColorNameToHex() {
        static const std::unordered_map<std::string, uint32_t> map = {
            {"black", 0x000000ff},          {"silver", 0xc0c0c0ff},
            {"gray", 0x808080ff},           {"white", 0xffffffff},
            {"maroon", 0x800000ff},         {"red", 0xff0000ff},
            {"purple", 0x800080ff},         {"fuchsia", 0xff00ffff},
            {"green", 0x008000ff},          {"lime", 0x00ff00ff},
            {"olive", 0x808000ff},          {"yellow", 0xffff00ff},
            {"navy", 0x000080ff},           {"blue", 0x0000ffff},
            {"teal", 0x008080ff},           {"aqua", 0x00ffffff},
            {"orange", 0xffa500ff},         {"aliceblue", 0xf0f8ffff},
            {"antiquewhite", 0xfaebd7ff},   {"aquamarine", 0x7fffd4ff},
            {"azure", 0xf0ffffff},          {"beige", 0xf5f5dcff},
            {"bisque", 0xffe4c4ff},         {"blanchedalmond", 0xffebcdff},
            {"blueviolet", 0x8a2be2ff},     {"brown", 0xa52a2aff},
            {"burlywood", 0xdeb887ff},      {"cadetblue", 0x5f9ea0ff},
            {"chartreuse", 0x7fff00ff},     {"chocolate", 0xd2691eff},
            {"coral", 0xff7f50ff},          {"cornflowerblue", 0x6495edff},
            {"cornsilk", 0xfff8dcff},       {"crimson", 0xdc143cff},
            {"cyan", 0x00ffffff},           {"darkblue", 0x00008bff},
            {"darkcyan", 0x008b8bff},       {"darkgoldenrod", 0xb8860bff},
            {"darkgray", 0xa9a9a9ff},       {"darkgreen", 0x006400ff},
            {"darkgrey", 0xa9a9a9ff},       {"darkkhaki", 0xbdb76bff},
            {"darkmagenta", 0x8b008bff},    {"darkolivegreen", 0x556b2fff},
            {"darkorange", 0xff8c00ff},     {"darkorchid", 0x9932ccff},
            {"darkred", 0x8b0000ff},        {"darksalmon", 0xe9967aff},
            {"darkseagreen", 0x8fbc8fff},   {"darkslateblue", 0x483d8bff},
            {"darkslategray", 0x2f4f4fff},  {"darkslategrey", 0x2f4f4fff},
            {"darkturquoise", 0x00ced1ff},  {"darkviolet", 0x9400d3ff},
            {"deeppink", 0xff1493ff},       {"deepskyblue", 0x00bfffff},
            {"dimgray", 0x696969ff},        {"dimgrey", 0x696969ff},
            {"dodgerblue", 0x1e90ffff},     {"firebrick", 0xb22222ff},
            {"floralwhite", 0xfffaf0ff},    {"forestgreen", 0x228b22ff},
            {"gainsboro", 0xdcdcdcff},      {"ghostwhite", 0xf8f8ffff},
            {"gold", 0xffd700ff},           {"goldenrod", 0xdaa520ff},
            {"greenyellow", 0xadff2fff},    {"grey", 0x808080ff},
            {"honeydew", 0xf0fff0ff},       {"hotpink", 0xff69b4ff},
            {"indianred", 0xcd5c5cff},      {"indigo", 0x4b0082ff},
            {"ivory", 0xfffff0ff},          {"khaki", 0xf0e68cff},
            {"lavender", 0xe6e6faff},       {"lavenderblush", 0xfff0f5ff},
            {"lawngreen", 0x7cfc00ff},      {"lemonchiffon", 0xfffacdff},
            {"lightblue", 0xadd8e6ff},      {"lightcoral", 0xf08080ff},
            {"lightcyan", 0xe0ffffff},      {"lightgoldenrodyellow", 0xfafad2ff},
            {"lightgray", 0xd3d3d3ff},      {"lightgreen", 0x90ee90ff},
            {"lightgrey", 0xd3d3d3ff},      {"lightpink", 0xffb6c1ff},
            {"lightsalmon", 0xffa07aff},    {"lightseagreen", 0x20b2aaff},
            {"lightskyblue", 0x87cefaff},   {"lightslategray", 0x778899ff},
            {"lightslategrey", 0x778899ff}, {"lightsteelblue", 0xb0c4deff},
            {"lightyellow", 0xffffe0ff},    {"limegreen", 0x32cd32ff},
            {"linen", 0xfaf0e6ff},          {"magenta", 0xff00ffff},
            {"mediumaquamarine", 0x66cdaaff}, {"mediumblue", 0x0000cdff},
            {"mediumorchid", 0xba55d3ff},   {"mediumpurple", 0x9370dbff},
            {"mediumseagreen", 0x3cb371ff}, {"mediumslateblue", 0x7b68eeff},
            {"mediumspringgreen", 0x00fa9aff}, {"mediumturquoise", 0x48d1ccff},
            {"mediumvioletred", 0xc71585ff}, {"midnightblue", 0x191970ff},
            {"mintcream", 0xf5fffaff},      {"mistyrose", 0xffe4e1ff},
            {"moccasin", 0xffe4b5ff},       {"navajowhite", 0xffdeadff},
            {"oldlace", 0xfdf5e6ff},        {"olivedrab", 0x6b8e23ff},
            {"orangered", 0xff4500ff},      {"orchid", 0xda70d6ff},
            {"palegoldenrod", 0xeee8aaff},  {"palegreen", 0x98fb98ff},
            {"paleturquoise", 0xafeeeeff},  {"palevioletred", 0xdb7093ff},
            {"papayawhip", 0xffefd5ff},     {"peachpuff", 0xffdab9ff},
            {"peru", 0xcd853fff},           {"pink", 0xffc0cbff},
            {"plum", 0xdda0ddff},           {"powderblue", 0xb0e0e6ff},
            {"rosybrown", 0xbc8f8fff},      {"royalblue", 0x4169e1ff},
            {"saddlebrown", 0x8b4513ff},    {"salmon", 0xfa8072ff},
            {"sandybrown", 0xf4a460ff},     {"seagreen", 0x2e8b57ff},
            {"seashell", 0xfff5eeff},       {"sienna", 0xa0522dff},
            {"skyblue", 0x87ceebff},        {"slateblue", 0x6a5acdff},
            {"slategray", 0x708090ff},      {"slategrey", 0x708090ff},
            {"snow", 0xfffafaff},           {"springgreen", 0x00ff7fff},
            {"steelblue", 0x4682b4ff},      {"tan", 0xd2b48cff},
            {"thistle", 0xd8bfd8ff},        {"tomato", 0xff6347ff},
            {"turquoise", 0x40e0d0ff},      {"violet", 0xee82eeff},
            {"wheat", 0xf5deb3ff},          {"whitesmoke", 0xf5f5f5ff},
            {"yellowgreen", 0x9acd32ff},    {"rebeccapurple", 0x663399ff},
        };
        return map;
    }

    // Parses a hexadecimal color string into a 32-bit RGBA value.
    // Supports 3, 4, 6, and 8 character hex strings (without the '#' prefix).
    //
    // The output format is 0xRRGGBBAA (red in highest byte, alpha in lowest).
    //
    // Input:  text = "ff0000"
    // Output: {0xff0000ff, true} (red with full alpha)
    //
    // Input:  text = "80ff"
    // Output: {0x8800ffff, true} (expanded from #80ff to #8800ffff)
    //
    // Input:  text = "abc"
    // Output: {0xaabbccff, true} (expanded from #abc to #aabbcc)
    //
    // Input:  text = "xyz"
    // Output: {0, false} (invalid hex characters)
    //
    //   - Returns false for strings with invalid hex characters.
    //   - Returns false for empty strings.
    //   - The '#' prefix is not included; the caller must strip it.
    std::pair<uint32_t, bool> ParseHex(std::string_view text) {
        uint32_t hex = 0;
        for (char ch : text) {
            auto c = static_cast<unsigned char>(ch);
            hex <<= 4;
            if (c >= '0' && c <= '9') {
                hex |= static_cast<uint32_t>(c) - '0';
            } else if (c >= 'a' && c <= 'f') {
                hex |= static_cast<uint32_t>(c) - ('a' - 10);
            } else if (c >= 'A' && c <= 'F') {
                hex |= static_cast<uint32_t>(c) - ('A' - 10);
            } else {
                return {0, false};
            }
        }
        return {hex, true};
    }

    // Compacts a 24-bit RGB hex value (0xRRGGBB) into a 12-bit representation
    // (0x0RGB) where each nibble is doubled. This is used for shortening
    // 6-digit hex colors to 3-digit when possible.
    //
    // The compaction works by taking the high nibble of each byte:
    //   Input:  0xRRGGBB
    //   Output: 0x0RGB (each channel's high nibble)
    //
    // Input:  v = 0xAABBCC
    // Output: 0x0ABC
    //
    // Input:  v = 0xFF0000
    // Output: 0x0F00
    //
    //   - Use ExpandHex to verify the round-trip is lossless.
    uint32_t CompactHex(uint32_t v) {
        return ((v & 0x0FF00000) >> 12) | ((v & 0x00000FF0) >> 4);
    }

    // Expands a 12-bit compact hex value (0x0RGB) into a 24-bit full
    // hex value (0xRRGGBB) by doubling each nibble. This is the inverse
    // of CompactHex.
    //
    // Input:  v = 0x0ABC
    // Output: 0xAABBCC
    //
    // Input:  v = 0x0F00
    // Output: 0xFF0000
    //
    //   - The round-trip CompactHex(ExpandHex(v)) == v is always true.
    uint32_t ExpandHex(uint32_t v) {
        return ((v & 0xF000) << 16) | ((v & 0xFF00) << 12) | ((v & 0x0FF0) << 8) | ((v & 0x00FF) << 4) |
            (v & 0x000F);
    }

    // Extracts the red channel from an RGBA uint32 value.
    // The red channel is in bits 24-31 (highest byte).
    //
    // Input:  v = 0xFF804020
    // Output: 0xFF (255)
    //
    // Input:  v = 0x00000000
    // Output: 0 (0)
    int HexR(uint32_t v) {
        return static_cast<int>(v >> 24);
    }

    // Extracts the green channel from an RGBA uint32 value.
    // The green channel is in bits 16-23.
    //
    // Input:  v = 0xFF804020
    // Output: 0x80 (128)
    int HexG(uint32_t v) {
        return static_cast<int>((v >> 16) & 255);
    }

    // Extracts the blue channel from an RGBA uint32 value.
    // The blue channel is in bits 8-15.
    //
    // Input:  v = 0xFF804020
    // Output: 0x40 (64)
    int HexB(uint32_t v) {
        return static_cast<int>((v >> 8) & 255);
    }

    // Extracts the alpha channel from an RGBA uint32 value.
    // The alpha channel is in bits 0-7 (lowest byte).
    //
    // Input:  v = 0xFF804020
    // Output: 0x20 (32)
    int HexA(uint32_t v) {
        return static_cast<int>(v & 255);
    }

    // Converts a floating-point number to a string suitable for CSS color
    // output. The result is formatted with up to 3 decimal places, with
    // trailing zeros and the decimal point stripped when not needed.
    //
    // Input:  a = 1.0
    // Output: "1"
    //
    // Input:  a = 0.5
    // Output: "0.5"
    //
    // Input:  a = 0.333333
    // Output: "0.333"
    //
    // Input:  a = 0.100
    // Output: "0.1"
    //
    //   - The result never has a trailing decimal point.
    std::string FloatToStringForColor(double a) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.03f", a);
        std::string text(buffer);
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
        return text;
    }

    // Converts a CSS angle token to degrees. Supports all CSS angle units:
    //   - deg: degrees (no conversion)
    //   - grad: gradians (400 grad = 360 deg)
    //   - rad: radians (π rad = 180 deg)
    //   - turn: turns (1 turn = 360 deg)
    //
    // Input:  token = "90deg"
    // Output: {90.0, true}
    //
    // Input:  token = "100grad"
    // Output: {90.0, true}
    //
    // Input:  token = "πrad" (approximately)
    // Output: {180.0, true}
    //
    // Input:  token = "0.5turn"
    // Output: {180.0, true}
    //
    // Input:  token = "100" (unitless number)
    // Output: {100.0, true} (treated as degrees)
    //
    //   - Returns {0, false} for tokens that cannot be parsed as numbers.
    //   - Negative angles are supported.
    std::pair<double, bool> DegreesForAngle(const Token& token) {
        switch (token.kind) {
        case TokenType::kNumber: {
            double value;
            if (ParseFloat(token.text, &value)) {
                return {value, true};
            }
            break;
        }

        case TokenType::kDimension: {
            double value;
            if (ParseFloat(token.DimensionValue(), &value)) {
                const std::string& unit = token.DimensionUnit();
                if (unit == "deg") {
                    return {value, true};
                } else if (unit == "grad") {
                    return {value * (360.0 / 400.0), true};
                } else if (unit == "rad") {
                    return {value * (180.0 / 3.14159265358979323846), true};
                } else if (unit == "turn") {
                    return {value * 360.0, true};
                }
            }
            break;
        }

        default:
            break;
        }
        return {0, false};
    }

    // Converts an alpha percentage token to a number token. If the input
    // is a percentage, it divides by 100 to get the fractional value.
    // Non-percentage tokens are returned unchanged.
    //
    // Input:  token = "50%"
    // Output: Token{kind=kNumber, text="0.5"}
    //
    // Input:  token = "100%"
    // Output: Token{kind=kNumber, text="1"}
    //
    // Input:  token = "0.5"
    // Output: Token{kind=kNumber, text="0.5"} (unchanged)
    //
    //   - The original token's source location is preserved.
    Token LowerAlphaPercentageToNumber(Token token) {
        if (token.kind == TokenType::kPercentage) {
            double value;
            if (ParseFloat(std::string_view(token.text).substr(0, token.text.size() - 1), &value)) {
                token.kind = TokenType::kNumber;
                token.text = FloatToStringForColor(value / 100.0);
            }
        }
        return token;
    }

    // Heuristically determines whether a token looks like it could be
    // a CSS color value. This is used for optimization hints, not for
    // actual parsing (ParseColor does the real work).
    //
    // Returns true for:
    //   - Ident tokens that match a CSS color name (case-insensitive)
    //   - Hash tokens with 3, 4, 6, or 8 hex digits
    //   - Function tokens for known color functions: rgb(), rgba(),
    //     hsl(), hsla(), hwb(), color(), color-mix(), lab(), lch(),
    //     oklab(), oklch()
    //
    // Input:  token = "red"
    // Output: true
    //
    // Input:  token = "#ff0000"
    // Output: true
    //
    // Input:  token = "rgb(255, 0, 0)"
    // Output: true
    //
    // Input:  token = "var(--x)"
    // Output: false
    //
    // Input:  token = "10px"
    // Output: false
    //
    //   - Hash tokens must have exactly 3, 4, 6, or 8 hex digits.
    bool LooksLikeColor(const Token& token) {
        switch (token.kind) {
        case TokenType::kIdent:
            if (ColorNameToHex().count(ToLower(token.text)) != 0) {
                return true;
            }
            break;

        case TokenType::kHash:
            switch (token.text.size()) {
            case 3:
            case 4:
            case 6:
            case 8:
                if (ParseHex(token.text).second) {
                    return true;
                }
            }
            break;

        case TokenType::kFunction:
            switch (ToLower(token.text)[0]) {
            case 'c':
                if (EqualFold(token.text, "color-mix") || EqualFold(token.text, "color")) {
                    return true;
                }
                break;
            case 'h':
                if (EqualFold(token.text, "hsl") || EqualFold(token.text, "hsla") ||
                    EqualFold(token.text, "hwb")) {
                    return true;
                }
                break;
            case 'l':
                if (EqualFold(token.text, "lab") || EqualFold(token.text, "lch")) {
                    return true;
                }
                break;
            case 'o':
                if (EqualFold(token.text, "oklab") || EqualFold(token.text, "oklch")) {
                    return true;
                }
                break;
            case 'r':
                if (EqualFold(token.text, "rgb") || EqualFold(token.text, "rgba")) {
                    return true;
                }
                break;
            }
            break;
        default:
            break;
        }
        return false;
    }

    // Parses a single color component token (red, green, blue, or alpha)
    // into a byte value (0-255). Supports both number and percentage tokens.
    //
    // The scale parameter controls how number values are interpreted:
    //   - scale=1: numbers are treated as 0-255 range
    //   - scale=255: numbers are treated as 0-1 range (fractional)
    //
    // Input:  token = "255", scale = 1
    // Output: {255, true}
    //
    // Input:  token = "100%", scale = 1
    // Output: {255, true}
    //
    // Input:  token = "0.5", scale = 255
    // Output: {128, true}
    //
    // Input:  token = "var(--x)", scale = 1
    // Output: {0, false}
    //
    //   - Negative values become 0, values > 255 become 255.
    //   - Percentages are converted to 0-255 range (100% = 255).
    std::pair<uint32_t, bool> ParseColorByte(const Token& token, double scale) {
        int i = 0;
        bool ok = false;

        switch (token.kind) {
        case TokenType::kNumber: {
            double f;
            if (ParseFloat(token.text, &f)) {
                i = static_cast<int>(std::round(f * scale));
                ok = true;
            }
            break;
        }

        case TokenType::kPercentage: {
            double f;
            if (ParseFloat(token.PercentageValue(), &f)) {
                i = static_cast<int>(std::round(f * (255.0 / 100.0)));
                ok = true;
            }
            break;
        }

        default:
            break;
        }

        if (i < 0) {
            i = 0;
        } else if (i > 255) {
            i = 255;
        }
        return {static_cast<uint32_t>(i), ok};
    }

    // Parses an alpha channel token. If the token is empty (TokenType(0)),
    // returns 255 (fully opaque). Otherwise, delegates to ParseColorByte
    // with scale=255 to handle both number and percentage values.
    //
    // Input:  token = Token() (empty/missing)
    // Output: {255, true}
    //
    // Input:  token = "50%"
    // Output: {128, true}
    //
    // Input:  token = "1"
    // Output: {255, true}
    //
    //   - Values are clamped to [0, 255] range.
    std::pair<uint32_t, bool> ParseAlphaByte(const Token& token) {
        if (token.kind == TokenType(0)) {
            return {255, true};
        }
        return ParseColorByte(token, 255);
    }

    // Converts a floating-point color component (0.0 to 1.0) to a byte
    // value (0 to 255) with proper rounding and clamping.
    //
    // Input:  f = 0.0
    // Output: 0
    //
    // Input:  f = 1.0
    // Output: 255
    //
    // Input:  f = 0.5
    // Output: 128
    //
    // Input:  f = -0.1
    // Output: 0 (clamped)
    //
    // Input:  f = 1.5
    // Output: 255 (clamped)
    //
    //   - Values outside [0, 1] are clamped to [0, 255].
    uint32_t FloatToByte(double f) {
        int i = static_cast<int>(std::round(f * 255));
        if (i < 0) {
            i = 0;
        } else if (i > 255) {
            i = 255;
        }
        return static_cast<uint32_t>(i);
    }

    // Packs three floating-point RGB values (0.0 to 1.0) and an alpha
    // byte into a single RGBA uint32 value.
    //
    // The output format is 0xRRGGBBAA.
    //
    // Input:  rf=1.0, gf=0.0, bf=0.0, a=255
    // Output: 0xFF0000FF (red, fully opaque)
    //
    // Input:  rf=0.5, gf=0.5, bf=0.5, a=128
    // Output: 0x80808080 (gray, semi-transparent)
    //
    //   - The alpha is used directly without conversion.
    uint32_t PackRGBA(F64 rf, F64 gf, F64 bf, uint32_t a) {
        uint32_t r = FloatToByte(rf.Value());
        uint32_t g = FloatToByte(gf.Value());
        uint32_t b = FloatToByte(bf.Value());
        return (r << 24) | (g << 16) | (b << 8) | a;
    }

    // Attempts to convert a CIE XYZ color to sRGB hex without gamut clipping.
    // Returns the packed RGBA value and true if the conversion succeeds
    // without clipping. Returns {0, false} if the color is outside the
    // sRGB gamut.
    //
    // The function applies a tolerance of ±0.5/255 to allow colors that
    // are just barely out of gamut (due to floating-point precision).
    //
    // Input:  x=0.4124, y=0.2126, z=0.0193, a=255 (approximately red)
    // Output: {0xFF0000FF, true}
    //
    // Input:  x=0.5, y=0.5, z=0.5 (out of sRGB gamut)
    // Output: {0, false}
    //
    //   - The tolerance allows colors that round to valid sRGB values.
    std::pair<uint32_t, bool> TryToConvertToHexWithoutClipping(F64 x, F64 y, F64 z, uint32_t a) {
        F64x3 lin = XyzToLinSrgb(x, y, z);
        F64x3 rgb = GamSrgb(lin.v0, lin.v1, lin.v2);
        if (rgb.v0.Value() < -0.5 / 255 || rgb.v0.Value() > 255.5 / 255 ||
            rgb.v1.Value() < -0.5 / 255 || rgb.v1.Value() > 255.5 / 255 ||
            rgb.v2.Value() < -0.5 / 255 || rgb.v2.Value() > 255.5 / 255) {
            return {0, false};
        }
        return {PackRGBA(rgb.v0, rgb.v1, rgb.v2, a), true};
    }

    // Creates a comma token with appropriate whitespace flags. Used when
    // generating comma-separated color function arguments.
    //
    // Input:  loc = {line 1, col 5}, minify_whitespace = false
    // Output: Token{kComma, ",", whitespace = kWhitespaceAfter}
    //
    // Input:  loc = {line 1, col 5}, minify_whitespace = true
    // Output: Token{kComma, ",", whitespace = kNone}
    //
    //   - When minifying, no whitespace is added after the comma.
    Token MakeCommaToken(guchho::logger::Loc loc, bool minify_whitespace) {
        Token token;
        token.kind = TokenType::kComma;
        token.loc = loc;
        token.text = ",";
        if (!minify_whitespace) {
            token.whitespace = WhitespaceFlags::kWhitespaceAfter;
        }
        return token;
    }

    // Parses a CSS color token into a ParsedColor structure. This is the
    // main color parsing function that handles all CSS color syntaxes:
    //
    //   - Named colors: "red", "blue", etc.
    //   - Hex colors: "#rgb", "#rgba", "#rrggbb", "#rrggbbaa"
    //   - RGB/RGBA: rgb(), rgba() with modern and legacy syntax
    //   - HSL/HSLA: hsl(), hsla() with modern and legacy syntax
    //   - HWB: hwb()
    //   - color(): color(srgb ...), color(display-p3 ...), etc.
    //   - Lab/LCH: lab(), lch()
    //   - OKLab/OKLCH: oklab(), oklch()
    //
    // Input:  token = "red"
    // Output: {ParsedColor{hex=0xFF0000FF}, true}
    //
    // Input:  token = "#ff0000"
    // Output: {ParsedColor{hex=0xFF0000FF}, true}
    //
    // Input:  token = "rgb(255, 0, 0)"
    // Output: {ParsedColor{hex=0xFF0000FF}, true}
    //
    // Input:  token = "hsl(0, 100%, 50%)"
    // Output: {ParsedColor{hex=0xFF0000FF}, true}
    //
    // Input:  token = "color(display-p3 1 0 0)"
    // Output: {ParsedColor{xyz={...}, hasColorSpace=true}, true}
    //
    //   - Modern syntax uses space-separated values: rgb(1 2 3).
    //   - Legacy syntax uses commas: rgb(1, 2, 3).
    //   - Alpha can be specified with "/": rgb(1 2 3 / 0.5).
    //   - Named colors are looked up case-insensitively.
    //   - Hex colors are expanded (3-digit → 6-digit, 4-digit → 8-digit).
    std::pair<ParsedColor, bool> ParseColor(const Token& token) {
        const std::string& text = token.text;

        switch (token.kind) {
        case TokenType::kIdent: {
            auto it = ColorNameToHex().find(ToLower(text));
            if (it != ColorNameToHex().end()) {
                return {ParsedColor{{}, it->second, false}, true};
            }
            break;
        }

        case TokenType::kHash:
            switch (text.size()) {
            case 3: {
                auto [hex, ok] = ParseHex(text);
                if (ok) {
                    return {ParsedColor{{}, (ExpandHex(hex) << 8) | 0xFF, false}, true};
                }
                break;
            }

            case 4: {
                auto [hex, ok] = ParseHex(text);
                if (ok) {
                    return {ParsedColor{{}, ExpandHex(hex), false}, true};
                }
                break;
            }

            case 6: {
                auto [hex, ok] = ParseHex(text);
                if (ok) {
                    return {ParsedColor{{}, (hex << 8) | 0xFF, false}, true};
                }
                break;
            }

            case 8: {
                auto [hex, ok] = ParseHex(text);
                if (ok) {
                    return {ParsedColor{{}, hex, false}, true};
                }
                break;
            }
            }
            break;

        case TokenType::kFunction: {
            std::string lower_text = ToLower(text);
            if (lower_text == "rgb" || lower_text == "rgba") {
                std::vector<Token>& args = *token.children;
                Token r, g, b, a;

                switch (args.size()) {
                case 3:
                    r = args[0];
                    g = args[1];
                    b = args[2];
                    break;

                case 5:
                    if (args[1].kind == TokenType::kComma && args[3].kind == TokenType::kComma) {
                        r = args[0];
                        g = args[2];
                        b = args[4];
                        break;
                    }

                    if (args[3].kind == TokenType::kDelimSlash) {
                        r = args[0];
                        g = args[1];
                        b = args[2];
                        a = args[4];
                    }
                    break;

                case 7:
                    if (args[1].kind == TokenType::kComma && args[3].kind == TokenType::kComma &&
                        args[5].kind == TokenType::kComma) {
                        r = args[0];
                        g = args[2];
                        b = args[4];
                        a = args[6];
                    }
                    break;
                }

                auto [r_byte, r_ok] = ParseColorByte(r, 1);
                if (r_ok) {
                    auto [g_byte, g_ok] = ParseColorByte(g, 1);
                    if (g_ok) {
                        auto [b_byte, b_ok] = ParseColorByte(b, 1);
                        if (b_ok) {
                            auto [a_byte, a_ok] = ParseAlphaByte(a);
                            if (a_ok) {
                                return {ParsedColor{{}, (r_byte << 24) | (g_byte << 16) | (b_byte << 8) | a_byte, false}, true};
                            }
                        }
                    }
                }
            }

            if (lower_text == "hsl" || lower_text == "hsla") {
                std::vector<Token>& args = *token.children;
                Token h, s, l, a;

                switch (args.size()) {
                case 3:
                    h = args[0];
                    s = args[1];
                    l = args[2];
                    break;

                case 5:
                    if (args[1].kind == TokenType::kComma && args[3].kind == TokenType::kComma) {
                        h = args[0];
                        s = args[2];
                        l = args[4];
                        break;
                    }

                    if (args[3].kind == TokenType::kDelimSlash) {
                        h = args[0];
                        s = args[1];
                        l = args[2];
                        a = args[4];
                    }
                    break;

                case 7:
                    if (args[1].kind == TokenType::kComma && args[3].kind == TokenType::kComma &&
                        args[5].kind == TokenType::kComma) {
                        h = args[0];
                        s = args[2];
                        l = args[4];
                        a = args[6];
                    }
                    break;
                }

                auto [h_deg, h_ok] = DegreesForAngle(h);
                if (h_ok) {
                    auto [s_frac, s_ok] = s.ClampedFractionForPercentage();
                    if (s_ok) {
                        auto [l_frac, l_ok] = l.ClampedFractionForPercentage();
                        if (l_ok) {
                            auto [a_byte, a_ok] = ParseAlphaByte(a);
                            if (a_ok) {
                                F64x3 rgb = HslToRgbFraction(F64(h_deg), F64(s_frac), F64(l_frac));
                                return {ParsedColor{{}, PackRGBA(rgb.v0, rgb.v1, rgb.v2, a_byte), false}, true};
                            }
                        }
                    }
                }
            }

            if (lower_text == "hwb") {
                std::vector<Token>& args = *token.children;
                Token h, s, l, a;

                switch (args.size()) {
                case 3:
                    h = args[0];
                    s = args[1];
                    l = args[2];
                    break;

                case 5:
                    if (args[3].kind == TokenType::kDelimSlash) {
                        h = args[0];
                        s = args[1];
                        l = args[2];
                        a = args[4];
                    }
                    break;
                }

                auto [h_deg, h_ok] = DegreesForAngle(h);
                if (h_ok) {
                    auto [white, white_ok] = s.ClampedFractionForPercentage();
                    if (white_ok) {
                        auto [black, black_ok] = l.ClampedFractionForPercentage();
                        if (black_ok) {
                            auto [a_byte, a_ok] = ParseAlphaByte(a);
                            if (a_ok) {
                                F64x3 rgb = HwbToRgbFraction(F64(h_deg), F64(white), F64(black));
                                return {ParsedColor{{}, PackRGBA(rgb.v0, rgb.v1, rgb.v2, a_byte), false}, true};
                            }
                        }
                    }
                }
            }

            if (lower_text == "color") {
                std::vector<Token>& args = *token.children;
                Token color_space, alpha;

                switch (args.size()) {
                case 4:
                    color_space = args[0];
                    break;

                case 6:
                    if (args[4].kind == TokenType::kDelimSlash) {
                        color_space = args[0];
                        alpha = args[5];
                    }
                    break;
                }

                if (color_space.kind == TokenType::kIdent) {
                    auto [v0, v0_ok] = args[1].NumberOrFractionForPercentage(1, PercentageFlags::kNone);
                    if (v0_ok) {
                        auto [v1, v1_ok] = args[2].NumberOrFractionForPercentage(1, PercentageFlags::kNone);
                        if (v1_ok) {
                            auto [v2, v2_ok] = args[3].NumberOrFractionForPercentage(1, PercentageFlags::kNone);
                            if (v2_ok) {
                                auto [a, a_ok] = ParseAlphaByte(alpha);
                                if (a_ok) {
                                    F64 x = F64(v0), y = F64(v1), z = F64(v2);
                                    std::string space = ToLower(color_space.text);
                                    if (space == "a98-rgb") {
                                        F64x3 rgb = LinA98Rgb(x, y, z);
                                        F64x3 xyz = LinA98RgbToXyz(rgb.v0, rgb.v1, rgb.v2);
                                        return {ParsedColor{xyz, a, true}, true};
                                    } else if (space == "display-p3") {
                                        F64x3 rgb = LinP3(x, y, z);
                                        F64x3 xyz = LinP3ToXyz(rgb.v0, rgb.v1, rgb.v2);
                                        return {ParsedColor{xyz, a, true}, true};
                                    } else if (space == "prophoto-rgb") {
                                        F64x3 rgb = LinProphoto(x, y, z);
                                        F64x3 xyz = LinProphotoToXyz(rgb.v0, rgb.v1, rgb.v2);
                                        xyz = D50ToD65(xyz.v0, xyz.v1, xyz.v2);
                                        return {ParsedColor{xyz, a, true}, true};
                                    } else if (space == "rec2020") {
                                        F64x3 rgb = Lin2020(x, y, z);
                                        F64x3 xyz = Lin2020ToXyz(rgb.v0, rgb.v1, rgb.v2);
                                        return {ParsedColor{xyz, a, true}, true};
                                    } else if (space == "srgb") {
                                        F64x3 rgb = LinSrgb(x, y, z);
                                        F64x3 xyz = LinSrgbToXyz(rgb.v0, rgb.v1, rgb.v2);
                                        return {ParsedColor{xyz, a, true}, true};
                                    } else if (space == "srgb-linear") {
                                        F64x3 xyz = LinSrgbToXyz(x, y, z);
                                        return {ParsedColor{xyz, a, true}, true};
                                    } else if (space == "xyz" || space == "xyz-d65") {
                                        return {ParsedColor{F64x3{x, y, z}, a, true}, true};
                                    } else if (space == "xyz-d50") {
                                        F64x3 xyz = D50ToD65(x, y, z);
                                        return {ParsedColor{xyz, a, true}, true};
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (lower_text == "lab" || lower_text == "lch" || lower_text == "oklab" ||
                lower_text == "oklch") {
                std::vector<Token>& args = *token.children;
                Token v0, v1, v2, alpha;

                switch (args.size()) {
                case 3:
                    v0 = args[0];
                    v1 = args[1];
                    v2 = args[2];
                    break;

                case 5:
                    if (args[3].kind == TokenType::kDelimSlash) {
                        v0 = args[0];
                        v1 = args[1];
                        v2 = args[2];
                        alpha = args[4];
                    }
                    break;
                }

                if (v0.kind != TokenType(0)) {
                    auto [a_byte, a_ok] = ParseAlphaByte(alpha);
                    if (a_ok) {
                        if (lower_text == "lab") {
                            auto [l, l_ok] = v0.NumberOrFractionForPercentage(100, PercentageFlags::kNone);
                            if (l_ok) {
                                auto [a_val, a2_ok] = v1.NumberOrFractionForPercentage(
                                    125, PercentageFlags::kAllowAnyPercentage);
                                if (a2_ok) {
                                    auto [b_val, b_ok] = v2.NumberOrFractionForPercentage(
                                        125, PercentageFlags::kAllowAnyPercentage);
                                    if (b_ok) {
                                        F64x3 xyz = LabToXyz(F64(l), F64(a_val), F64(b_val));
                                        xyz = D50ToD65(xyz.v0, xyz.v1, xyz.v2);
                                        return {ParsedColor{xyz, a_byte, true}, true};
                                    }
                                }
                            }
                        } else if (lower_text == "lch") {
                            auto [l, l_ok] = v0.NumberOrFractionForPercentage(100, PercentageFlags::kNone);
                            if (l_ok) {
                                auto [c, c_ok] = v1.NumberOrFractionForPercentage(
                                    125, PercentageFlags::kAllowPercentageAbove100);
                                if (c_ok) {
                                    auto [h_deg, h_ok] = DegreesForAngle(v2);
                                    if (h_ok) {
                                        F64x3 lab = LchToLab(F64(l), F64(c), F64(h_deg));
                                        F64x3 xyz = LabToXyz(lab.v0, lab.v1, lab.v2);
                                        xyz = D50ToD65(xyz.v0, xyz.v1, xyz.v2);
                                        return {ParsedColor{xyz, a_byte, true}, true};
                                    }
                                }
                            }
                        } else if (lower_text == "oklab") {
                            auto [l, l_ok] = v0.NumberOrFractionForPercentage(1, PercentageFlags::kNone);
                            if (l_ok) {
                                auto [a_val, a2_ok] = v1.NumberOrFractionForPercentage(
                                    0.4, PercentageFlags::kAllowAnyPercentage);
                                if (a2_ok) {
                                    auto [b_val, b_ok] = v2.NumberOrFractionForPercentage(
                                        0.4, PercentageFlags::kAllowAnyPercentage);
                                    if (b_ok) {
                                        F64x3 xyz = OklabToXyz(F64(l), F64(a_val), F64(b_val));
                                        return {ParsedColor{xyz, a_byte, true}, true};
                                    }
                                }
                            }
                        } else if (lower_text == "oklch") {
                            auto [l, l_ok] = v0.NumberOrFractionForPercentage(1, PercentageFlags::kNone);
                            if (l_ok) {
                                auto [c, c_ok] = v1.NumberOrFractionForPercentage(
                                    0.4, PercentageFlags::kAllowPercentageAbove100);
                                if (c_ok) {
                                    auto [h_deg, h_ok] = DegreesForAngle(v2);
                                    if (h_ok) {
                                        F64x3 oklab = OklchToOklab(F64(l), F64(c), F64(h_deg));
                                        F64x3 xyz = OklabToXyz(oklab.v0, oklab.v1, oklab.v2);
                                        return {ParsedColor{xyz, a_byte, true}, true};
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

        return {ParsedColor{}, false};
    }

    // Generates the optimal token representation for a parsed color. This
    // function handles gamut mapping, color name lookup, hex shortening,
    // and fallback to rgba() syntax when needed.
    //
    // The output format depends on the color and options:
    //   1. Short color name (if minifying and color has a short name)
    //   2. Short hex (3 or 4 digits, if lossless)
    //   3. Full hex (6 or 8 digits)
    //   4. rgba() function (if alpha < 255 and hex-rgba not supported)
    //
    // Input:  color = ParsedColor{hex=0xFF0000FF}, options.minify_syntax=true
    // Output: Token{kIdent, "red"}
    //
    // Input:  color = ParsedColor{hex=0xFF0000FF}, options.minify_syntax=false
    // Output: Token{kHash, "ff0000"}
    //
    // Input:  color = ParsedColor{hex=0xFF000080}, options with kHexRGBA unsupported
    // Output: Token{kFunction, "rgba", children=[255, 0, 0, 0.502]}
    //
    //     returns the original token unchanged and sets *would_clip_color=true.
    //   - If would_clip_color is null, performs gamut mapping instead.
    //   - Gamut mapping uses perceptual lightness preservation.
    Token TryToGenerateColor(
        Token token, const ParsedColor& color, 
        const ColorDeclOptions& options,
        bool* would_clip_color
    ) {
        uint32_t hex;
        if (!color.hasColorSpace) {
            hex = color.hex;
        } else if (auto [result, ok] = TryToConvertToHexWithoutClipping(color.xyz.v0, color.xyz.v1,
                                                                        color.xyz.v2, color.hex);
                ok) {
            hex = result;
        } else if (would_clip_color != nullptr) {
            *would_clip_color = true;
            return token;
        } else {
            F64x3 rgb = GamutMappingXyzToSrgb(color.xyz.v0, color.xyz.v1, color.xyz.v2);
            hex = PackRGBA(rgb.v0, rgb.v1, rgb.v2, color.hex);
        }

        if (HexA(hex) == 255) {
            token.children.reset();
            auto it = ShortColorName().find(hex);
            if (it != ShortColorName().end() && options.minify_syntax) {
                token.kind = TokenType::kIdent;
                token.text = it->second;
            } else {
                token.kind = TokenType::kHash;
                hex >>= 8;
                uint32_t compact = CompactHex(hex);
                if (options.minify_syntax && hex == ExpandHex(compact)) {
                    token.text = FormatHex(compact, 3);
                } else {
                    token.text = FormatHex(hex, 6);
                }
            }
        } else if (!guchho::compat::Has(options.unsupported_css_features,
                                    guchho::compat::CSSFeature::kHexRGBA)) {
            token.children.reset();
            token.kind = TokenType::kHash;
            uint32_t compact = CompactHex(hex);
            if (options.minify_syntax && hex == ExpandHex(compact)) {
                token.text = FormatHex(compact, 4);
            } else {
                token.text = FormatHex(hex, 8);
            }
        } else {
            token.kind = TokenType::kFunction;
            token.text = "rgba";
            Token comma_token = MakeCommaToken(token.loc, options.minify_whitespace);
            size_t index = static_cast<size_t>(HexA(hex)) * 4;
            std::string_view alpha = kAlphaFractionTable.substr(index, 4);
            if (size_t space = alpha.find(' '); space != std::string_view::npos) {
                alpha = alpha.substr(0, space);
            }
            std::vector<Token> children;
            children.reserve(7);
            children.push_back({nullptr, std::to_string(HexR(hex)), token.loc, 0, 0, TokenType::kNumber});
            children.push_back(comma_token);
            children.push_back({nullptr, std::to_string(HexG(hex)), token.loc, 0, 0, TokenType::kNumber});
            children.push_back(comma_token);
            children.push_back({nullptr, std::to_string(HexB(hex)), token.loc, 0, 0, TokenType::kNumber});
            children.push_back(comma_token);
            children.push_back({nullptr, std::string(alpha), token.loc, 0, 0, TokenType::kNumber});
            token.children = std::make_shared<std::vector<Token>>(std::move(children));
        }

        return token;
    }

    // Lowers and minifies a CSS color token based on browser compatibility
    // constraints. This is the main entry point for color optimization.
    //
    // The function handles:
    //   - Converting modern syntax to legacy when unsupported
    //   - Converting unsupported color functions to hex or rgba()
    //   - Shortening hex colors when minifying
    //   - Converting named colors to hex when unsupported
    //
    // Input:  token = "#1234", options with kHexRGBA unsupported
    // Output: Token{kFunction, "rgba", children=[18, 52, 86, 0.004]}
    //
    // Input:  token = "rebeccapurple", options with kRebeccaPurple unsupported
    // Output: Token{kHash, "663399"}
    //
    // Input:  token = "rgb(1 2 3)", options with kModernRGBHSL unsupported
    // Output: Token{kFunction, "rgb", children=[1, ",", 2, ",", 3]}
    //
    // Input:  token = "hwb(0 0% 0%)", options with kHWB unsupported
    // Output: Token{kHash, "ff0000"}
    //
    //     returns the original token unchanged.
    //   - When minifying, colors are re-serialized using shortest encoding.
    //   - Modern syntax (space-separated) is converted to legacy (comma-separated)
    //     when kModernRGBHSL is unsupported.
    Token LowerAndMinifyColor(Token token, const ColorDeclOptions& options, bool* would_clip_color) {
        const std::string& text = token.text;

        switch (token.kind) {
        case TokenType::kHash:
            if (guchho::compat::Has(options.unsupported_css_features, guchho::compat::CSSFeature::kHexRGBA)) {
                switch (text.size()) {
                case 4: {
                    auto [hex, ok] = ParseHex(text);
                    if (ok) {
                        hex = ExpandHex(hex);
                        return TryToGenerateColor(token, ParsedColor{{}, hex, false}, options,
                                                would_clip_color);
                    }
                    break;
                }

                case 8: {
                    auto [hex, ok] = ParseHex(text);
                    if (ok) {
                        return TryToGenerateColor(token, ParsedColor{{}, hex, false}, options,
                                                would_clip_color);
                    }
                    break;
                }
                }
            }
            break;

        case TokenType::kIdent:
            if (guchho::compat::Has(options.unsupported_css_features,
                                guchho::compat::CSSFeature::kRebeccaPurple) &&
                EqualFold(text, "rebeccapurple")) {
                Token result = token;
                result.kind = TokenType::kHash;
                result.text = "663399";
                return result;
            }
            break;

        case TokenType::kFunction: {
            std::string lower_text = ToLower(text);
            if (lower_text == "rgb" || lower_text == "rgba" || lower_text == "hsl" ||
                lower_text == "hsla") {
                if (guchho::compat::Has(
                    options.unsupported_css_features,
                    guchho::compat::CSSFeature::kModernRGBHSL)) {
                    std::vector<Token>& args = *token.children;
                    bool remove_alpha = false;
                    bool add_alpha = false;

                    if ((text == "hsl" || text == "hsla") && !args.empty()) {
                        if (auto [degrees, ok] = DegreesForAngle(args[0]); ok) {
                            args[0].kind = TokenType::kNumber;
                            args[0].text = FloatToStringForColor(degrees);
                        }
                    }

                    switch (args.size()) {
                    case 3:
                        if (IsNumeric(args[0].kind) && IsNumeric(args[1].kind) && IsNumeric(args[2].kind)) {
                            remove_alpha = true;
                            args[0].whitespace = static_cast<WhitespaceFlags>(0);
                            args[1].whitespace = static_cast<WhitespaceFlags>(0);
                            Token comma_token = MakeCommaToken(token.loc, options.minify_whitespace);
                            std::vector<Token> children;
                            children.push_back(args[0]);
                            children.push_back(comma_token);
                            children.push_back(args[1]);
                            children.push_back(comma_token);
                            children.push_back(args[2]);
                            token.children = std::make_shared<std::vector<Token>>(std::move(children));
                        }
                        break;

                    case 5:
                        if (IsNumeric(args[0].kind) && args[1].kind == TokenType::kComma && IsNumeric(args[2].kind) &&
                            args[3].kind == TokenType::kComma && IsNumeric(args[4].kind)) {
                            remove_alpha = true;
                            break;
                        }

                        if (IsNumeric(args[0].kind) && IsNumeric(args[1].kind) && IsNumeric(args[2].kind) &&
                            args[3].kind == TokenType::kDelimSlash && IsNumeric(args[4].kind)) {
                            add_alpha = true;
                            args[0].whitespace = static_cast<WhitespaceFlags>(0);
                            args[1].whitespace = static_cast<WhitespaceFlags>(0);
                            args[2].whitespace = static_cast<WhitespaceFlags>(0);
                            Token comma_token = MakeCommaToken(token.loc, options.minify_whitespace);
                            std::vector<Token> children;
                            children.push_back(args[0]);
                            children.push_back(comma_token);
                            children.push_back(args[1]);
                            children.push_back(comma_token);
                            children.push_back(args[2]);
                            children.push_back(comma_token);
                            children.push_back(LowerAlphaPercentageToNumber(args[4]));
                            token.children = std::make_shared<std::vector<Token>>(std::move(children));
                        }
                        break;

                    case 7:
                        if (IsNumeric(args[0].kind) && args[1].kind == TokenType::kComma && IsNumeric(args[2].kind) &&
                            args[3].kind == TokenType::kComma && IsNumeric(args[4].kind) &&
                            args[5].kind == TokenType::kComma && IsNumeric(args[6].kind)) {
                            add_alpha = true;
                            args[6] = LowerAlphaPercentageToNumber(args[6]);
                        }
                        break;
                    }

                    if (remove_alpha) {
                        if (EqualFold(text, "rgba")) {
                            token.text = "rgb";
                        } else if (EqualFold(text, "hsla")) {
                            token.text = "hsl";
                        }
                    } else if (add_alpha) {
                        if (EqualFold(text, "rgb")) {
                            token.text = "rgba";
                        } else if (EqualFold(text, "hsl")) {
                            token.text = "hsla";
                        }
                    }
                }
            } else if (lower_text == "hwb") {
                if (guchho::compat::Has(options.unsupported_css_features, guchho::compat::CSSFeature::kHWB)) {
                    if (auto [color, ok] = ParseColor(token); ok) {
                        return TryToGenerateColor(token, color, options, would_clip_color);
                    }
                }
            } else if (lower_text == "color" || lower_text == "lab" || lower_text == "lch" ||
                    lower_text == "oklab" || lower_text == "oklch") {
                if (guchho::compat::Has(options.unsupported_css_features,
                                    guchho::compat::CSSFeature::kColorFunctions)) {
                    if (auto [color, ok] = ParseColor(token); ok) {
                        return TryToGenerateColor(token, color, options, would_clip_color);
                    }
                }
            }
            break;
        }

        default:
            break;
        }

        // When minifying, try to parse the color and print it back out. This minifies
        // the color because we always print it out using the shortest encoding.
        Token result = token;
        if (options.minify_syntax) {
            if (auto [color, ok] = ParseColor(token); ok) {
                result = TryToGenerateColor(token, color, options, would_clip_color);
            }
        }

        return result;
    }

}
