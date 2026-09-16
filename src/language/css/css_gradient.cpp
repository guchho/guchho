#include "guchho/css/css_helpers.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/helpers.hpp"

#include <algorithm>
#include <charconv>

#include <cstdio>

#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace guchho::css {

    namespace {

        using guchho::helpers::F64;
        using guchho::helpers::Lerp;
        using guchho::helpers::Max2;

        // Converts a string to lowercase using the C locale. CSS identifiers are
        // case-insensitive so this is used whenever function names or keywords need
        // to be compared in a case-insensitive manner.
        //
        // Input:  "Linear-Gradient"  =>  "linear-gradient"
        // Input:  "OKLCH"           =>  "oklch"
        std::string ToLower(std::string_view text) {
            std::string result(text);
            std::transform(result.begin(), result.end(), result.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        // Case-insensitive string comparison (ASCII only). Returns true when both
        // strings have the same length and every character pair matches after
        // folding to lowercase.
        //
        // Input:  "HSL", "hsl"  =>  true
        // Input:  "Lab", "LAB"  =>  true
        // Input:  "lab", "lch"  =>  false
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

        // Parses a floating-point number from a string_view. Returns true on
        // success and stores the result in *out. Returns false if the string
        // contains trailing characters that are not part of the number.
        //
        // Input:  "3.14"   =>  true,  *out = 3.14
        // Input:  "100"    =>  true,  *out = 100.0
        // Input:  "12px"   =>  false  (trailing "px")
        // Input:  ""       =>  false
        bool ParseFloat(std::string_view text, double* out) {
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            auto result = std::from_chars(begin, end, *out);
            return result.ec == std::errc() && result.ptr == end;
        }

        // Formats a 32-bit integer as a lowercase hexadecimal string padded to
        // the specified number of digits. Used when emitting hash-color tokens.
        //
        // Input:  v=0xFF00AA, width=6  =>  "ff00aa"
        // Input:  v=0x00FF00AA, width=8  =>  "00ff00aa"
        std::string FormatHex(uint32_t v, int width) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "%0*x", width, v);
            return buffer;
        }

        // Strips the "whitespace before" flag from a token's whitespace bits.
        // Used when a token must not be preceded by whitespace in the output
        // (e.g. the first token in a list).
        WhitespaceFlags ClearWhitespaceBefore(WhitespaceFlags flags) {
            return static_cast<WhitespaceFlags>(static_cast<uint8_t>(flags) &
                                                ~static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore));
        }

        // Strips the "whitespace after" flag from a token's whitespace bits.
        // Used when a token must not be followed by whitespace in the output
        // (e.g. the last token before a closing parenthesis).
        WhitespaceFlags ClearWhitespaceAfter(WhitespaceFlags flags) {
            return static_cast<WhitespaceFlags>(static_cast<uint8_t>(flags) &
                                                ~static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter));
        }

        // Returns a flags value with both "whitespace before" and "whitespace
        // after" bits set. Used for tokens that should be surrounded by spaces
        // when pretty-printed (e.g. the "+" in a calc() expression).
        WhitespaceFlags WhitespaceBeforeAndAfter() {
            return static_cast<WhitespaceFlags>(static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore) |
                                                static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter));
        }

        // Intermediate representation of a parsed gradient color stop. Carries the
        // stop's position (which may be a calc() sum of terms in different units),
        // optional midpoint hint, and the stop's color in multiple representations:
        //   - XYZ (x, y, z) and alpha for device-independent work
        //   - sRGB (r, g, b) for fast similarity checks against browser output
        //   - Premultiplied (v0, v1, v2) in the chosen interpolation color space
        struct ParsedColorStop {
            std::vector<ValueWithUnit> position_terms;
            std::optional<ValueWithUnit> midpoint;
            F64 x{}, y{}, z{}, alpha{};
            F64 r{}, g{}, b{};
            F64 v0{}, v1{}, v2{};
            bool has_color_space{};
        };

        // Formats a floating-point value to a given number of decimal places,
        // then trims trailing zeros and a trailing decimal point for compactness.
        // This is used when emitting numeric tokens for gradient output.
        //
        // Input:  value=50.0, decimals=2   =>  "50"
        // Input:  value=33.33, decimals=2  =>  "33.33"
        // Input:  value=0.5, decimals=3    =>  "0.5"
        std::string FormatFloat(F64 value, int decimals) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%.*f", decimals, value.Value());
            std::string text(buffer);
            while (!text.empty() && text.back() == '0') {
                text.pop_back();
            }
            if (!text.empty() && text.back() == '.') {
                text.pop_back();
            }
            return text;
        }

        // Creates a single CSS token representing a dimension (e.g. "50px") or
        // percentage (e.g. "100%"). The unit string determines which token kind is
        // emitted: "%" produces kPercentage, everything else produces kDimension.
        // The value is formatted to two decimal places with trailing zeros stripped.
        //
        // Input:  loc, 50.0, "%"     =>  Token{kind=kPercentage, text="50%"}
        // Input:  loc, 100.0, "px"   =>  Token{kind=kDimension, text="100px"}
        // Input:  loc, 3.14, "deg"   =>  Token{kind=kDimension, text="3.14deg"}
        Token MakeDimensionOrPercentToken(guchho::logger::Loc loc, F64 value, const std::string& unit) {
            Token token;
            token.loc = loc;
            token.text = FormatFloat(value, 2);
            if (unit == "%") {
                token.kind = TokenType::kPercentage;
            } else {
                token.kind = TokenType::kDimension;
                token.unit_offset = static_cast<uint16_t>(token.text.size());
            }
            token.text += unit;
            return token;
        }

        // Builds a token representing a gradient color stop position. When the
        // position consists of a single term (e.g. "50%" or "200px"), a simple
        // dimension or percentage token is returned. When the position is a sum of
        // multiple terms (e.g. "50% + 10px"), a calc() function token is built
        // with "+" delimiters between the terms, preserving whitespace for
        // readable output.
        //
        // Input:  loc, [{%, 50}]              =>  Token{kind=kPercentage, text="50%"}
        // Input:  loc, [{%, 50}, {px, 10}]    =>  Token{kind=kFunction, text="calc", children=[50%, +, 10px]}
        Token MakePositionToken(guchho::logger::Loc loc, const std::vector<ValueWithUnit>& position_terms) {
            if (position_terms.size() == 1) {
                return MakeDimensionOrPercentToken(loc, position_terms[0].value, position_terms[0].unit);
            }

            std::vector<Token> children;
            children.reserve(1 + 2 * position_terms.size());
            for (size_t i = 0; i < position_terms.size(); i++) {
                if (i > 0) {
                    Token plus;
                    plus.loc = loc;
                    plus.kind = TokenType::kDelimPlus;
                    plus.text = "+";
                    plus.whitespace = WhitespaceBeforeAndAfter();
                    children.push_back(plus);
                }
                children.push_back(MakeDimensionOrPercentToken(loc, position_terms[i].value, position_terms[i].unit));
            }

            Token result;
            result.loc = loc;
            result.kind = TokenType::kFunction;
            result.text = "calc";
            result.children = std::make_shared<std::vector<Token>>(std::move(children));
            return result;
        }

        // Builds a token representing a CSS color value. When the XYZ color can be
        // losslessly converted to sRGB (all channels in [0,1] after gamut mapping),
        // a compact hash token is emitted ("#rrggbb" for opaque, "#rrggbbaa" for
        // translucent). When the color is out of sRGB gamut, a color() function
        // token is emitted using the XYZ color space with explicit numeric channels.
        //
        // Input:  loc, x=0.5, y=0.3, z=0.2, a=1.0  =>  Token{kind=kHash, text="#804d33"}
        // Input:  loc, x=1.5, y=0.3, z=0.2, a=1.0  =>  Token{kind=kFunction, text="color", children=[xyz, 1.5, 0.3, 0.2]}
        //
        // Edge case: when alpha is 1.0 the slash and alpha channel are omitted;
        // when alpha < 1.0 the output includes "/ <alpha>" as a child.
        Token MakeColorToken(guchho::logger::Loc loc, F64 x, F64 y, F64 z, F64 a) {
            Token color;
            color.loc = loc;
            uint32_t alpha = static_cast<uint32_t>(a.MulConst(255).Round().Value());
            if (auto [hex, ok] = TryToConvertToHexWithoutClipping(x, y, z, alpha); ok) {
                color.kind = TokenType::kHash;
                if (alpha == 255) {
                    color.text = FormatHex(hex >> 8, 6);
                } else {
                    color.text = FormatHex(hex, 8);
                }
            } else {
                auto make_number = [&](F64 value, WhitespaceFlags whitespace) {
                    Token t;
                    t.loc = loc;
                    t.kind = TokenType::kNumber;
                    t.text = FormatFloat(value, 3);
                    t.whitespace = whitespace;
                    return t;
                };

                std::vector<Token> children;
                Token ident;
                ident.loc = loc;
                ident.kind = TokenType::kIdent;
                ident.text = "xyz";
                ident.whitespace = WhitespaceFlags::kWhitespaceAfter;
                children.push_back(ident);
                children.push_back(make_number(x, WhitespaceBeforeAndAfter()));
                children.push_back(make_number(y, WhitespaceBeforeAndAfter()));
                children.push_back(make_number(z, WhitespaceFlags::kWhitespaceBefore));

                if (a.Value() < 1) {
                    Token slash;
                    slash.loc = loc;
                    slash.kind = TokenType::kDelimSlash;
                    slash.text = "/";
                    slash.whitespace = WhitespaceBeforeAndAfter();
                    children.push_back(slash);
                    children.push_back(make_number(a, WhitespaceFlags::kWhitespaceBefore));
                }

                color.kind = TokenType::kFunction;
                color.text = "color";
                color.children = std::make_shared<std::vector<Token>>(std::move(children));
            }
            return color;
        }

        // Interpolates two hue values (in degrees, 0-360) using the specified hue
        // interpolation method from the CSS Color Level 4 spec. Hue values are
        // normalized to the [0, 1) range before interpolation and scaled back to
        // degrees afterwards. The method determines how the angular shortest path
        // is chosen:
        //   - kShorter: takes the shorter arc (default in CSS)
        //   - kLonger:  takes the longer arc
        //   - kIncreasing: always goes in the positive direction
        //   - kDecreasing: always goes in the negative direction
        //
        // Input:  a=30, b=330, t=0.5, kShorter   =>  0 (shorter arc through 0°)
        // Input:  a=30, b=330, t=0.5, kLonger    =>  180 (longer arc through 180°)
        // Edge case: when a == b, all methods return the same hue regardless of t.
        F64 InterpolateHues(F64 a, F64 b, F64 t, HueMethod hue_method) {
            a = a.DivConst(360);
            b = b.DivConst(360);
            a = a.Sub(a.Floor());
            b = b.Sub(b.Floor());

            switch (hue_method) {
            case HueMethod::kShorter: {
                F64 delta = b.Sub(a);
                if (delta.Value() > 0.5) {
                    a = a.AddConst(1);
                }
                if (delta.Value() < -0.5) {
                    b = b.AddConst(1);
                }
                break;
            }

            case HueMethod::kLonger: {
                F64 delta = b.Sub(a);
                if (delta.Value() > 0 && delta.Value() < 0.5) {
                    a = a.AddConst(1);
                }
                if (delta.Value() > -0.5 && delta.Value() <= 0) {
                    b = b.AddConst(1);
                }
                break;
            }

            case HueMethod::kIncreasing:
                if (b.Value() < a.Value()) {
                    b = b.AddConst(1);
                }
                break;

            case HueMethod::kDecreasing:
                if (a.Value() < b.Value()) {
                    a = a.AddConst(1);
                }
                break;
            }

            return Lerp(a, b, t).MulConst(360);
        }

        // Interpolates two colors in a given color space by blending each of their
        // three components. For polar color spaces (HSL, HWB, LCH, OKLCH) the hue
        // component is interpolated using InterpolateHues to respect the angular
        // wrapping; all other components use standard linear interpolation.
        //
        // The function knows which component index is the hue for each color space:
        //   - HSL/HWB: hue is the first component (index 0)
        //   - LCH/OKLCH: hue is the third component (index 2)
        //   - All others (Lab, OKLab, sRGB, etc.): no special hue handling
        //
        // Input:  a0=10, a1=0.5, a2=0.6, b0=200, b1=0.5, b2=0.6, kHsl, kShorter, t=0.5
        //         =>  v0=InterpolateHues(10, 200, 0.5, kShorter), v1=0.5, v2=0.6
        F64x3 InterpolateColors(F64 a0, F64 a1, F64 a2, F64 b0, F64 b1, F64 b2, ColorSpace color_space,
                                HueMethod hue_method, F64 t) {
            F64 v1 = Lerp(a1, b1, t);

            F64 v0;
            F64 v2;
            switch (color_space) {
            case ColorSpace::kHsl:
            case ColorSpace::kHwb:
                v2 = Lerp(a2, b2, t);
                v0 = InterpolateHues(a0, b0, t, hue_method);
                break;

            case ColorSpace::kLch:
            case ColorSpace::kOklch:
                v0 = Lerp(a0, b0, t);
                v2 = InterpolateHues(a2, b2, t, hue_method);
                break;

            default:
                v0 = Lerp(a0, b0, t);
                v2 = Lerp(a2, b2, t);
                break;
            }

            return F64x3{v0, v1, v2};
        }

        // Linearly interpolates two position expressions that may each contain
        // multiple terms in different CSS units (e.g. "50%" and "100px + 20%").
        // The interpolation is performed per-unit: each unit's contribution is
        // blended independently using the formula result = a * (1-t) + b * t.
        // When both positions share a unit, their contributions are summed under
        // that unit; when they use different units, the result naturally becomes a
        // multi-term expression (e.g. "calc(50% + 10px)").
        //
        // After interpolation, if the result has more than one term, one zero-valued
        // term is removed for neatness (but a single zero is always retained so
        // that the unit is preserved).
        //
        // Input:  [{%, 50}], [{%, 100}], t=0.5    =>  [{%, 75}]
        // Input:  [{%, 50}], [{px, 100}], t=0.5   =>  [{%, 25}, {px, 50}]
        // Edge case: when t=0 the result equals `a` exactly; when t=1 it equals `b`.
        std::vector<ValueWithUnit> InterpolatePositions(const std::vector<ValueWithUnit>& a,
                                                        const std::vector<ValueWithUnit>& b, F64 t) {
            std::vector<ValueWithUnit> result;
            auto find_unit = [&result](const std::string& unit) -> size_t {
                for (size_t i = 0; i < result.size(); i++) {
                    if (result[i].unit == unit) {
                        return i;
                    }
                }
                result.push_back(ValueWithUnit{unit, F64(0)});
                return result.size() - 1;
            };

            // Accumulate the contribution from position `a`: each term is weighted by (1-t).
            for (const auto& term : a) {
                ValueWithUnit& ptr = result[find_unit(term.unit)];
                ptr.value = t.Neg().AddConst(1).Mul(term.value).Add(ptr.value);
            }

            // Accumulate the contribution from position `b`: each term is weighted by t.
            for (const auto& term : b) {
                ValueWithUnit& ptr = result[find_unit(term.unit)];
                ptr.value = t.Mul(term.value).Add(ptr.value);
            }

            // Remove one zero-valued term for neatness, but keep at least one so the
            // unit information is preserved in the output.
            if (result.size() > 1) {
                for (size_t i = 0; i < result.size(); i++) {
                    if (result[i].value.Value() == 0) {
                        result.erase(result.begin() + static_cast<std::ptrdiff_t>(i));
                        break;
                    }
                }
            }

            return result;
        }

        // Premultiplies color channel values by the alpha component. In premultiplied
        // alpha, each color channel is scaled by alpha so that fully transparent
        // colors contribute zero to the interpolation. The channels that get scaled
        // depend on the color space:
        //   - HSL/HWB: only the saturation/lightness or whiteness/blackness (v2)
        //   - LCH/OKLCH: only the chroma (v0)
        //   - All others: both v0 and v2 are scaled (the non-hue channels)
        //   - v1 (the middle channel) is always scaled
        //
        // When alpha is 1.0 the color is fully opaque and no scaling occurs.
        //
        // Input:  v0=0.5, v1=0.6, v2=0.7, alpha=0.5, kOklab  =>  {0.25, 0.3, 0.35}
        // Input:  v0=0.5, v1=0.6, v2=0.7, alpha=1.0, kOklab  =>  {0.5, 0.6, 0.7} (unchanged)
        F64x3 Premultiply(F64 v0, F64 v1, F64 v2, F64 alpha, ColorSpace color_space) {
            if (alpha.Value() < 1) {
                switch (color_space) {
                case ColorSpace::kHsl:
                case ColorSpace::kHwb:
                    v2 = v2.Mul(alpha);
                    break;
                case ColorSpace::kLch:
                case ColorSpace::kOklch:
                    v0 = v0.Mul(alpha);
                    break;
                default:
                    v0 = v0.Mul(alpha);
                    v2 = v2.Mul(alpha);
                    break;
                }
                v1 = v1.Mul(alpha);
            }
            return F64x3{v0, v1, v2};
        }

        // Reverses premultiplied alpha by dividing color channel values by alpha.
        // This is the inverse of Premultiply and is used after interpolation to
        // restore the original color intensity. Division only occurs when alpha is
        // strictly between 0 and 1; fully opaque (alpha=1) or fully transparent
        // (alpha=0) colors are returned unchanged. The same per-color-space
        // channel selection as Premultiply is used.
        //
        // Input:  v0=0.25, v1=0.3, v2=0.35, alpha=0.5, kOklab  =>  {0.5, 0.6, 0.7}
        // Input:  v0=0.5, v1=0.6, v2=0.7, alpha=0.0, kOklab    =>  {0.5, 0.6, 0.7} (unchanged)
        F64x3 Unpremultiply(F64 v0, F64 v1, F64 v2, F64 alpha, ColorSpace color_space) {
            if (alpha.Value() > 0 && alpha.Value() < 1) {
                switch (color_space) {
                case ColorSpace::kHsl:
                case ColorSpace::kHwb:
                    v2 = v2.Div(alpha);
                    break;
                case ColorSpace::kLch:
                case ColorSpace::kOklch:
                    v0 = v0.Div(alpha);
                    break;
                default:
                    v0 = v0.Div(alpha);
                    v2 = v2.Div(alpha);
                    break;
                }
                v1 = v1.Div(alpha);
            }
            return F64x3{v0, v1, v2};
        }


        // Attempts to expand a gradient into a series of explicit sRGB color stops
        // that approximate the original interpolation. This is used when the target
        // browser does not support features like color-mixing in non-sRGB color spaces,
        // gradient midpoints, or custom color interpolation. The function works by:
        //   1. Converting all parsed color stops into the target interpolation color
        //      space and premultiplying them by alpha.
        //   2. Recursively subdividing each pair of adjacent stops. At each subdivision
        //      the midpoint color is computed by interpolating in the target color space,
        //      then converting back to sRGB. If the result is "close enough" to the
        //      linear midpoint (within 4/255 Euclidean distance in premultiplied sRGB),
        //      the recursion stops — otherwise it splits again, up to depth 4.
        //   3. Replacing gradient.leading_tokens and gradient.color_stops with the
        //      expanded stop list.
        //
        // Returns true on success. Returns false only if the color stops could not be
        // parsed (handled by the caller before reaching this function).
        bool TryToExpandGradient(
            guchho::logger::Loc loc, 
            ParsedGradient* gradient,
            std::vector<ParsedColorStop>& color_stops,
            const std::vector<Token>& remaining, ColorSpace color_space,
            HueMethod hue_method
        ) {
            // Convert each parsed color stop from XYZ into the target interpolation
            // color space and premultiply by alpha for correct blending.
            for (auto& stop : color_stops) {
                F64x3 cs = XyzToColorSpace(stop.x, stop.y, stop.z, color_space);
                F64x3 premul = Premultiply(cs.v0, cs.v1, cs.v2, stop.alpha, color_space);
                stop.v0 = premul.v0;
                stop.v1 = premul.v1;
                stop.v2 = premul.v2;
            }

            std::vector<ColorStop> new_color_stops;
            std::function<void(int, const ParsedColorStop&, const ParsedColorStop&, F64, F64, F64, F64,
                            F64, F64, F64, F64, F64, F64, F64, F64, F64, F64, F64, F64)>
                generate_color_stops;

            generate_color_stops = [&](int depth, const ParsedColorStop& from, const ParsedColorStop& to,
                                    F64 prev_x, F64 prev_y, F64 prev_z, F64 prev_r, F64 prev_g,
                                    F64 prev_b, F64 prev_a, F64 prev_t, F64 next_x, F64 next_y,
                                    F64 next_z, F64 next_r, F64 next_g, F64 next_b, F64 next_a,
                                    F64 next_t) {
                if (depth > 4) {
                    return;
                }

                F64 t = prev_t.Add(next_t).DivConst(2);
                F64 position_t = t;

                // Apply the midpoint (color transition hint). The midpoint shifts the
                // interpolation curve so that the color transitions faster near one end
                // and slower near the other. The formula is: position_t = P^(1/log2(H)),
                // where P is the normalized position and H is the hint value. Edge cases
                // where H <= 0 or H >= 1 clamp to the endpoints to avoid division by zero
                // or degenerate power curves.
                if (from.midpoint.has_value()) {
                    F64 from_pos = from.position_terms[0].value;
                    F64 to_pos = to.position_terms[0].value;
                    F64 stop_pos = Lerp(from_pos, to_pos, t);
                    F64 H = from.midpoint->value.Sub(from_pos).Div(to_pos.Sub(from_pos));
                    F64 P = stop_pos.Sub(from_pos).Div(to_pos.Sub(from_pos));
                    if (H.Value() <= 0) {
                        position_t = F64(1);
                    } else if (H.Value() >= 1) {
                        position_t = F64(0);
                    } else {
                        position_t = P.Pow(F64(-1).Div(H.Log2()));
                    }
                }

                F64x3 interp =
                    InterpolateColors(from.v0, from.v1, from.v2, to.v0, to.v1, to.v2, color_space,
                                    hue_method, position_t);
                F64 v0 = interp.v0;
                F64 v1 = interp.v1;
                F64 v2 = interp.v2;
                F64 a = Lerp(from.alpha, to.alpha, position_t);
                F64x3 unprem = Unpremultiply(v0, v1, v2, a, color_space);
                v0 = unprem.v0;
                v1 = unprem.v1;
                v2 = unprem.v2;
                F64x3 xyz = ColorSpaceToXyz(v0, v1, v2, color_space);
                F64 x = xyz.v0;
                F64 y = xyz.v1;
                F64 z = xyz.v2;

                // Check whether the interpolated color is close enough to the linear
                // midpoint in premultiplied sRGB. If the Euclidean distance is below
                // 4/255 (approximately 1.5 color levels per channel), no further
                // subdivision is needed and this stop is omitted — the endpoints will
                // be sufficient to represent the gradient segment.
                const double epsilon = 4.0 / 255;
                F64x3 lin = XyzToLinSrgb(x, y, z);
                F64x3 gam = GamSrgb(lin.v0, lin.v1, lin.v2);
                F64 r = gam.v0;
                F64 g = gam.v1;
                F64 b = gam.v2;
                F64 dr = r.Mul(a).Sub(prev_r.Mul(prev_a).Add(next_r.Mul(next_a)).DivConst(2));
                F64 dg = g.Mul(a).Sub(prev_g.Mul(prev_a).Add(next_g.Mul(next_a)).DivConst(2));
                F64 db = b.Mul(a).Sub(prev_b.Mul(prev_a).Add(next_b.Mul(next_a)).DivConst(2));
                if (F64 d = dr.Squared().Add(dg.Squared()).Add(db.Squared());
                    d.Value() < epsilon * epsilon) {
                    return;
                }

                // Recursively subdivide the left half (from `from` to the new stop).
                generate_color_stops(depth + 1, from, to, prev_x, prev_y, prev_z, prev_r, prev_g, prev_b,
                                    prev_a, prev_t, x, y, z, r, g, b, a, t);

                // Emit the newly computed color stop at position t between the two
                // endpoints. The color is converted back to sRGB for output and the
                // position is interpolated from the original position terms.
                Token color = MakeColorToken(loc, x, y, z, a);
                std::vector<ValueWithUnit> position_terms =
                    InterpolatePositions(from.position_terms, to.position_terms, t);
                Token position = MakePositionToken(loc, position_terms);
                position.whitespace = WhitespaceFlags::kWhitespaceBefore;
                new_color_stops.push_back(ColorStop{{position}, color, Token{}});

                // Recursively subdivide the right half (from the new stop to `to`).
                generate_color_stops(depth + 1, from, to, x, y, z, r, g, b, a, t, next_x, next_y, next_z,
                                    next_r, next_g, next_b, next_a, next_t);
            };

            for (size_t i = 0; i < color_stops.size(); i++) {
                const auto& stop = color_stops[i];
                Token color = MakeColorToken(loc, stop.x, stop.y, stop.z, stop.alpha);
                Token position = MakePositionToken(loc, stop.position_terms);
                position.whitespace = WhitespaceFlags::kWhitespaceBefore;
                new_color_stops.push_back(ColorStop{{position}, color, Token{}});

                // Recursively insert intermediate color stops between the current
                // stop and the next one. This is what causes the gradient to be
                // expanded into many fine-grained sRGB stops that approximate the
                // original non-sRGB interpolation.
                if (i + 1 < color_stops.size()) {
                    const auto& next = color_stops[i + 1];
                    generate_color_stops(0, stop, next, stop.x, stop.y, stop.z, stop.r, stop.g, stop.b,
                                        stop.alpha, F64(0), next.x, next.y, next.z, next.r, next.g,
                                        next.b, next.alpha, F64(1));
                }
            }

            gradient->leading_tokens = remaining;
            gradient->color_stops = new_color_stops;
            return true;
        }

        // Parses the raw color stop tokens from a ParsedGradient into fully resolved
        // ParsedColorStop values. Each stop's color is converted to XYZ and sRGB
        // representations, positions are parsed into ValueWithUnit terms, and
        // missing positions are automatically filled in according to the CSS spec:
        //   - The first stop defaults to 0% if no position is given.
        //   - The last stop defaults to 100% if no position is given.
        //   - Interior stops with no position are evenly distributed between their
        //     nearest positioned neighbors (or midpoints).
        //   - Double positions (e.g. "red 0% 50%") are expanded into two separate stops.
        //
        // Returns the parsed stops and true on success, or an empty vector and false
        // if any color or position could not be parsed, or if midpoints use mixed units.
        std::pair<std::vector<ParsedColorStop>, bool> TryToParseColorStops(const ParsedGradient& gradient) {
            std::vector<ParsedColorStop> color_stops;

            for (const auto& stop : gradient.color_stops) {
                auto [color, ok] = ParseColor(stop.color);
                if (!ok) {
                    return {};
                }
                F64 r;
                F64 g;
                F64 b;
                if (!color.hasColorSpace) {
                    r = F64(static_cast<double>(HexR(color.hex))).DivConst(255);
                    g = F64(static_cast<double>(HexG(color.hex))).DivConst(255);
                    b = F64(static_cast<double>(HexB(color.hex))).DivConst(255);
                    F64x3 lin = LinSrgb(r, g, b);
                    F64x3 xyz = LinSrgbToXyz(lin.v0, lin.v1, lin.v2);
                    color.xyz = xyz;
                } else {
                    F64x3 lin = XyzToLinSrgb(color.xyz.v0, color.xyz.v1, color.xyz.v2);
                    F64x3 gam = GamSrgb(lin.v0, lin.v1, lin.v2);
                    r = gam.v0;
                    g = gam.v1;
                    b = gam.v2;
                }
                ParsedColorStop parsed_stop;
                parsed_stop.x = color.xyz.v0;
                parsed_stop.y = color.xyz.v1;
                parsed_stop.z = color.xyz.v2;
                parsed_stop.r = r;
                parsed_stop.g = g;
                parsed_stop.b = b;
                parsed_stop.alpha = F64(static_cast<double>(HexA(color.hex))).DivConst(255);
                parsed_stop.has_color_space = color.hasColorSpace;

                for (size_t i = 0; i < stop.positions.size(); i++) {
                    auto [position, position_ok] = TryToParseValue(stop.positions[i], gradient.kind);
                    if (!position_ok) {
                        return {};
                    }
                    parsed_stop.position_terms = std::vector<ValueWithUnit>{position};

                    // When a stop has two positions (double-position stop), emit the
                    // first position as its own stop immediately. The second position
                    // will be handled when the outer loop processes the next iteration.
                    if (i + 1 < stop.positions.size()) {
                        color_stops.push_back(parsed_stop);
                    }
                }

                if (stop.midpoint.kind != TokenType(0)) {
                    auto [midpoint, midpoint_ok] = TryToParseValue(stop.midpoint, gradient.kind);
                    if (!midpoint_ok) {
                        return {};
                    }
                    parsed_stop.midpoint = midpoint;
                }

                color_stops.push_back(parsed_stop);
            }

            // Per the CSS spec, color stops without explicit positions must have their
            // positions filled in automatically. This section handles three passes:
            //   1. Default the first and last stops to 0% and 100% respectively.
            //   2. Ensure every stop's position is >= the previous stop's position
            //      (preventing overlapping or backwards stops).
            //   3. Distribute unstopped positions evenly between their nearest
            //      positioned neighbors.
            if (!color_stops.empty()) {
                // Default the first stop to 0% and the last stop to 100% when no
                // position was explicitly specified.
                if (color_stops[0].position_terms.empty()) {
                    color_stops[0].position_terms = std::vector<ValueWithUnit>{{"%", F64(0)}};
                }
                if (color_stops[color_stops.size() - 1].position_terms.empty()) {
                    color_stops[color_stops.size() - 1].position_terms = std::vector<ValueWithUnit>{{"%", F64(100)}};
                }

                // Walk forward through all stops and clamp each position to be at least
                // as large as the previous stop's position (or the previous midpoint).
                // This prevents overlapping stops which would cause visual artifacts.
                // Only clamps when both positions share the same unit; mixed-unit
                // comparisons are skipped because they cannot be meaningfully ordered.
                for (size_t i = 0; i < color_stops.size(); i++) {
                    auto& stop = color_stops[i];
                    std::optional<ValueWithUnit> prev_pos;
                    for (size_t j = i; j-- > 0;) {
                        const auto& prev = color_stops[j];
                        if (prev.midpoint.has_value()) {
                            prev_pos = *prev.midpoint;
                            break;
                        }
                        if (prev.position_terms.size() == 1) {
                            prev_pos = prev.position_terms[0];
                            break;
                        }
                    }
                    if (stop.position_terms.size() == 1) {
                        if (prev_pos.has_value() && prev_pos->unit == stop.position_terms[0].unit) {
                            stop.position_terms[0].value = Max2(prev_pos->value, stop.position_terms[0].value);
                        }
                        prev_pos = stop.position_terms[0];
                    }
                    if (stop.midpoint.has_value() && prev_pos.has_value() &&
                        prev_pos->unit == stop.midpoint->unit) {
                        stop.midpoint->value = Max2(prev_pos->value, stop.midpoint->value);
                    }
                }

                // For each unstopped color stop, find its nearest positioned neighbors
                // in both directions. The StopInfo struct records the neighbor positions
                // and the number of unstopped stops in each run so that positions can
                // be interpolated proportionally.
                struct StopInfo {
                    ValueWithUnit from_pos;
                    ValueWithUnit to_pos;
                    int32_t from_count{};
                    int32_t to_count{};
                };
                std::vector<StopInfo> infos(color_stops.size());
                for (size_t i = 0; i < color_stops.size(); i++) {
                    const auto& stop = color_stops[i];
                    if (stop.position_terms.size() == 1) {
                        continue;
                    }
                    StopInfo& info = infos[i];

                    // Walk backward from the current stop to find the nearest positioned
                    // neighbor (either a stop with a position or a midpoint).
                    for (size_t from = i; from-- > 0;) {
                        const auto& from_stop = color_stops[from];
                        info.from_count++;
                        if (from_stop.midpoint.has_value()) {
                            info.from_pos = *from_stop.midpoint;
                            break;
                        }
                        if (from_stop.position_terms.size() == 1) {
                            info.from_pos = from_stop.position_terms[0];
                            break;
                        }
                    }

                    // Walk forward from the current stop to find the nearest positioned
                    // neighbor. For double-position stops the second position of the
                    // next stop is used as the upper bound.
                    for (size_t to = i; to < color_stops.size(); to++) {
                        info.to_count++;
                        const auto& to_stop = color_stops[to];
                        if (to_stop.midpoint.has_value()) {
                            info.to_pos = *to_stop.midpoint;
                            break;
                        }
                        if (to + 1 < color_stops.size()) {
                            if (const auto& to_next = color_stops[to + 1];
                                to_next.position_terms.size() == 1) {
                                info.to_pos = to_next.position_terms[0];
                                break;
                            }
                        }
                    }
                }

                // Interpolate the position for each unstopped color stop based on its
                // proportional distance between the two nearest positioned neighbors.
                // When the neighbors share the same unit, a single interpolated value
                // is produced. When they use different units, a two-term expression is
                // created so the output preserves both units (e.g. "calc(30% + 20px)").
                for (size_t i = 0; i < color_stops.size(); i++) {
                    auto& stop = color_stops[i];
                    if (stop.position_terms.size() != 1) {
                        const StopInfo& info = infos[i];
                        F64 t = F64(static_cast<double>(info.from_count))
                                    .DivConst(static_cast<double>(info.from_count + info.to_count));
                        if (info.from_pos.unit == info.to_pos.unit) {
                            stop.position_terms = std::vector<ValueWithUnit>{
                                {info.from_pos.unit, Lerp(info.from_pos.value, info.to_pos.value, t)}};
                        } else {
                            stop.position_terms = std::vector<ValueWithUnit>{
                                {info.from_pos.unit, t.Neg().AddConst(1).Mul(info.from_pos.value)},
                                {info.to_pos.unit, t.Mul(info.to_pos.value)},
                            };
                        }
                    }
                }

                // Midpoints require that both the preceding and following stops have
                // single-valued positions in the same unit as the midpoint itself.
                // Mixed units (e.g. midpoint in % but stop in px) are not supported
                // because the midpoint formula assumes a linear position space.
                // Return false to signal that the gradient cannot be expanded.
                for (size_t i = 0; i < color_stops.size(); i++) {
                    const auto& stop = color_stops[i];
                    if (stop.midpoint.has_value()) {
                        const auto& next = color_stops[i + 1];
                        if (stop.position_terms.size() != 1 ||
                            stop.midpoint->unit != stop.position_terms[0].unit ||
                            next.position_terms.size() != 1 ||
                            stop.midpoint->unit != next.position_terms[0].unit) {
                            return {};
                        }
                    }
                }
            }

            return {color_stops, true};
        }

    }

    // Parses a CSS gradient function token (linear-gradient, radial-gradient,
    // conic-gradient, or their repeating variants) into a ParsedGradient struct.
    //
    // The parsing follows the CSS spec grammar:
    //   linear-gradient(<angle>?, <color-stop> [, <color-stop>]*)
    //   radial-gradient(<ending-shape>?, <size>?, at <position>?, <color-stop> ...)
    //   conic-gradient(from <angle>?, at <position>?, <color-stop> ...)
    //
    // Leading tokens (angle, shape, size, position, "in <colorspace>", etc.) are
    // separated from the color stops by the first comma. Each color stop may have
    // up to two position values and an optional midpoint hint (a numeric value
    // between two commas).
    //
    // Returns {gradient, true} on success. Returns {{}, false} if the token is not
    // a gradient function, contains var() references (which could inject arbitrary
    // commas), or has malformed syntax.
    //
    // Input:  Token{kind=kFunction, text="linear-gradient", children=[kIdent("red"), kComma, kIdent("blue")]}
    //         =>  ParsedGradient{kind=kLinear, color_stops=[{color=red}, {color=blue}]}
    std::pair<ParsedGradient, bool> ParseGradient(const Token& token) {
        if (token.kind != TokenType::kFunction) {
            return {};
        }

        ParsedGradient gradient;
        std::string lower_text = ToLower(token.text);
        if (lower_text == "linear-gradient") {
            gradient.kind = GradientKind::kLinear;
        } else if (lower_text == "radial-gradient") {
            gradient.kind = GradientKind::kRadial;
        } else if (lower_text == "conic-gradient") {
            gradient.kind = GradientKind::kConic;
        } else if (lower_text == "repeating-linear-gradient") {
            gradient.kind = GradientKind::kLinear;
            gradient.repeating = true;
        } else if (lower_text == "repeating-radial-gradient") {
            gradient.kind = GradientKind::kRadial;
            gradient.repeating = true;
        } else if (lower_text == "repeating-conic-gradient") {
            gradient.kind = GradientKind::kConic;
            gradient.repeating = true;
        } else {
            return {};
        }

        // A var() function in the token list could inject commas that would break
        // the stop-parsing logic. Bail out and let the normal error reporter handle it.
        if (!token.children) {
            return {};
        }
        std::vector<Token> tokens = *token.children;
        for (const Token& t : tokens) {
            if (t.kind == TokenType::kFunction && EqualFold(t.text, "var")) {
                return {};
            }
        }

        // If the first token does not look like a color, it must be part of the
        // leading arguments (angle, position, shape, etc.). Consume everything up
        // to the first comma and store it as leading_tokens.
        if (!tokens.empty() && !LooksLikeColor(tokens[0])) {
            size_t i = 0;
            while (i < tokens.size() && tokens[i].kind != TokenType::kComma) {
                i++;
            }
            gradient.leading_tokens.assign(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < tokens.size()) {
                tokens = std::vector<Token>(tokens.begin() + static_cast<std::ptrdiff_t>(i + 1), tokens.end());
            } else {
                tokens.clear();
            }
        }

        // Parse each color stop. Each stop consists of a mandatory color token,
        // zero, one, or two position tokens, and an optional midpoint hint
        // (a numeric token between two commas).
        while (!tokens.empty()) {
            // The first token must be a color. If it isn't, the gradient is malformed.
            Token color = tokens[0];
            if (!LooksLikeColor(color)) {
                return {};
            }
            tokens.erase(tokens.begin());

            // Consume up to two position tokens (percentages, dimensions, or calc()).
            std::vector<Token> positions;
            while (positions.size() < 2 && !tokens.empty()) {
                const Token& position = tokens[0];
                if (IsNumeric(position.kind) ||
                    (position.kind == TokenType::kFunction && EqualFold(position.text, "calc"))) {
                    positions.push_back(position);
                } else {
                    break;
                }
                tokens.erase(tokens.begin());
            }

            // After the positions there should be a comma separating this stop from
            // the next. If there is no comma, the gradient is complete (last stop).
            Token midpoint;
            if (!tokens.empty()) {
                if (tokens[0].kind != TokenType::kComma) {
                    return {};
                }
                tokens.erase(tokens.begin());
                if (tokens.empty()) {
                    return {};
                }

                // An optional midpoint hint can appear after the comma: "red, 0.5, blue"
                // means the transition from red to blue is curved. The midpoint must be
                // followed by another comma before the next color stop.
                if (!tokens.empty() && IsNumeric(tokens[0].kind)) {
                    midpoint = tokens[0];
                    tokens.erase(tokens.begin());

                    // A midpoint must be followed by a comma before the next color stop.
                    if (tokens.empty() || tokens[0].kind != TokenType::kComma) {
                        return {};
                    }
                    tokens.erase(tokens.begin());
                }
            }

            // Store the fully parsed color stop in the gradient.
            gradient.color_stops.push_back(ColorStop{positions, color, midpoint});
        }

        return {gradient, true};
    }

    // Reconstructs a gradient function token from a ParsedGradient. The output
    // token has the same kind (kFunction) and function name as the input, but its
    // children are rebuilt from the leading tokens and color stops. Each stop is
    // separated by a comma token. When a stop has no positions and no midpoint,
    // trailing whitespace after the color is stripped for compactness.
    //
    // Input:  token (original function token), gradient with leading_tokens=[kIdent("to right")]
    //         and color_stops=[{color=red, positions=[]}, {color=blue, positions=[]}]
    //         =>  Token{kind=kFunction, text="linear-gradient",
    //                   children=[kIdent("to right"), comma, red, comma, blue]}
    Token GenerateGradient(Token token, const ParsedGradient& gradient, bool minify_whitespace) {
        std::vector<Token> children;
        Token comma_token = MakeCommaToken(token.loc, minify_whitespace);

        children.insert(children.end(), gradient.leading_tokens.begin(), gradient.leading_tokens.end());
        for (ColorStop stop : gradient.color_stops) {
            if (!children.empty()) {
                children.push_back(comma_token);
            }
            if (stop.positions.empty() && stop.midpoint.kind == TokenType(0)) {
                stop.color.whitespace = ClearWhitespaceAfter(stop.color.whitespace);
            }
            children.push_back(stop.color);
            children.insert(children.end(), stop.positions.begin(), stop.positions.end());
            if (stop.midpoint.kind != TokenType(0)) {
                children.push_back(comma_token);
                children.push_back(stop.midpoint);
            }
        }

        token.children = std::make_shared<std::vector<Token>>(std::move(children));
        return token;
    }

    // Main entry point for lowering and minifying a CSS gradient function token.
    // This function handles several concerns:
    //   1. Parsing the gradient into a structured representation.
    //   2. Expanding the gradient when the target browser lacks support for
    //      gradient midpoints, non-sRGB color spaces, or custom interpolation
    //      (e.g. "in oklch"). Expansion replaces the gradient with many fine-grained
    //      sRGB color stops that approximate the original visual appearance.
    //   3. Lowering individual color values in each stop (e.g. converting oklch()
    //      to hex when the target doesn't support it).
    //   4. Normalizing double-position stops: splitting them when the target doesn't
    //      support the syntax, or merging adjacent same-color stops into double
    //      positions when minifying.
    //   5. Removing color stops whose positions are implied by their neighbors,
    //      which reduces output size.
    //
    // Returns the rewritten gradient token. If parsing fails, the original token
    // is returned unchanged so that the normal error reporter can emit a diagnostic.
    Token LowerAndMinifyGradient(Token token, const ColorDeclOptions& options, bool* would_clip_color) {
        auto [gradient, ok] = ParseGradient(token);
        if (!ok) {
            return token;
        }

        bool lower_midpoints =
            compat::Has(options.unsupported_css_features, compat::CSSFeature::kGradientMidpoints);
        bool lower_color_spaces =
            compat::Has(options.unsupported_css_features, compat::CSSFeature::kColorFunctions);
        bool lower_interpolation =
            compat::Has(options.unsupported_css_features, compat::CSSFeature::kGradientInterpolation);

        // When the target browser lacks gradient interpolation support, it also
        // likely doesn't correctly interpolate non-sRGB colors even without an
        // explicit color space declaration. In this case we must force expansion
        // and replace the original gradient entirely (not just prepend a fallback),
        // because the browser will claim to support the syntax but render it
        // incorrectly.
        if (lower_interpolation) {
            lower_color_spaces = true;
        }

        // When any lowering flags are set, attempt to parse and expand the gradient.
        // Expansion is only performed when the gradient actually contains the feature
        // that needs lowering (midpoints, color spaces, or custom interpolation).
        bool did_expand = false;
        if (lower_midpoints || lower_color_spaces || lower_interpolation) {
            auto [color_stops, stops_ok] = TryToParseColorStops(gradient);
            if (stops_ok) {
                bool has_color_space = false;
                bool has_midpoint = false;
                for (const auto& stop : color_stops) {
                    if (stop.has_color_space) {
                        has_color_space = true;
                    }
                    if (stop.midpoint.has_value()) {
                        has_midpoint = true;
                    }
                }
                ColorInterpolation interpolation = RemoveColorInterpolation(gradient.leading_tokens);
                if ((interpolation.found && lower_interpolation) ||
                    (has_color_space && lower_color_spaces) || (has_midpoint && lower_midpoints)) {
                    if (interpolation.found) {
                        TryToExpandGradient(token.loc, &gradient, color_stops, interpolation.remaining,
                                            interpolation.color_space, interpolation.hue_method);
                    } else {
                        ColorSpace color_space =
                            has_color_space ? ColorSpace::kOklab : ColorSpace::kSrgb;
                        TryToExpandGradient(token.loc, &gradient, color_stops, gradient.leading_tokens,
                                            color_space, HueMethod::kShorter);
                    }
                    did_expand = true;
                }
            }
        }

        // Lower each stop's color value (e.g. convert color() to hex, or clamp
        // to sRGB gamut when needed).
        for (size_t i = 0; i < gradient.color_stops.size(); i++) {
            gradient.color_stops[i].color =
                LowerAndMinifyColor(gradient.color_stops[i].color, options, would_clip_color);
        }

        if (compat::Has(options.unsupported_css_features,
                            compat::CSSFeature::kGradientDoublePosition)) {
            // The target doesn't support double-position stops (e.g. "red 0% 50%").
            // Expand each double-position stop into two separate single-position stops.
            for (const auto& stop : gradient.color_stops) {
                if (stop.positions.size() > 1) {
                    gradient.color_stops = SwitchToSinglePositions(gradient.color_stops);
                    break;
                }
            }
        } else if (options.minify_syntax) {
            // When minifying, merge adjacent same-color stops into a double-position
            // stop to save bytes (e.g. "red 0%, red 50%" becomes "red 0% 50%").
            for (size_t i = 0; i < gradient.color_stops.size(); i++) {
                const auto& stop = gradient.color_stops[i];
                if (i > 0 && stop.positions.size() == 1) {
                    const auto& prev = gradient.color_stops[i - 1];
                    if (prev.positions.size() == 1 && prev.midpoint.kind == TokenType(0) &&
                        TokensEqual(std::vector<Token>{prev.color}, std::vector<Token>{stop.color},
                                    nullptr)) {
                        gradient.color_stops = SwitchToDoublePositions(gradient.color_stops);
                        break;
                    }
                }
            }
        }

        if (options.minify_syntax || did_expand) {
            gradient.color_stops = RemoveImpliedPositions(gradient.kind, gradient.color_stops);
        }

        return GenerateGradient(token, gradient, options.minify_whitespace);
    }

    // Removes color stop positions that are mathematically implied by their
    // neighbors. A position is "implied" when it falls exactly on the linear
    // interpolation between two anchored stops and can therefore be deduced by
    // the browser without being explicitly stated. The algorithm works by:
    //   1. Parsing each stop's position into a numeric value (marking unparseable
    //      or multi-term positions as NaN).
    //   2. Scanning forward from each positioned stop to find the longest run of
    //      stops whose positions are linearly implied between two anchors with the
    //      same unit. Stops with midpoints break the run.
    //   3. Clearing the positions of all implied interior stops.
    //   4. Also clearing the first stop's position when it is 0% or 0px, and the
    //      last stop's position when it is 100%, since these are CSS defaults.
    //
    // The tolerance for "close enough" is 0.01 units, which prevents floating-point
    // drift from keeping unnecessary positions.
    //
    // Input:  [red 0%, blue 50%, green 100%]  =>  [red, blue 50%, green]
    // Input:  [red 0%, blue 25%, green 50%]   =>  [red, blue, green] (25% is implied)
    std::vector<ColorStop> RemoveImpliedPositions(GradientKind kind, std::vector<ColorStop> color_stops) {
        if (color_stops.empty()) {
            return color_stops;
        }

        std::vector<ValueWithUnit> positions(color_stops.size());
        for (size_t i = 0; i < color_stops.size(); i++) {
            const auto& stop = color_stops[i];
            if (stop.positions.size() == 1) {
                if (auto [pos, ok] = TryToParseValue(stop.positions[0], kind); ok) {
                    positions[i] = pos;
                    continue;
                }
            }
            positions[i].value = F64(std::numeric_limits<double>::quiet_NaN());
        }

        size_t start = 0;
        while (start < color_stops.size()) {
            if (!positions[start].value.IsNaN()) {
                ValueWithUnit start_pos = positions[start];
                size_t end = start + 1;
                while (color_stops[end - 1].midpoint.kind == TokenType(0) && end < color_stops.size()) {
                    ValueWithUnit end_pos = positions[end];
                    if (end_pos.value.IsNaN() || end_pos.unit != start_pos.unit) {
                        break;
                    }

                    // Verify that every stop in the run [start+1, end) has a position
                    // that falls on the linear interpolation between start and end.
                    // Using start/end anchors (rather than consecutive pairs) gives a
                    // more accurate check for long runs.
                    bool all_implied = true;
                    for (size_t i = start + 1; i < end; i++) {
                        F64 t = F64(static_cast<double>(i - start)).DivConst(static_cast<double>(end - start));
                        F64 implied_value = Lerp(start_pos.value, end_pos.value, t);
                        if (positions[i].value.Sub(implied_value).Abs().Value() > 0.01) {
                            all_implied = false;
                            break;
                        }
                    }
                    if (!all_implied) {
                        break;
                    }
                    end++;
                }

                // All stops from start+1 to end-1 are implied; clear their positions.
                // Keep start and end (the anchors) intact. Advance start to end-1 so
                // the next iteration begins from the last anchor of this run.
                if (end - start > 1) {
                    for (size_t i = start + 1; i + 1 < end; i++) {
                        color_stops[i].positions.clear();
                    }
                    start = end - 1;
                    continue;
                }
            }
            start++;
        }

        if (const auto& first = color_stops[0].positions;
            first.size() == 1 &&
            ((first[0].kind == TokenType::kPercentage && first[0].PercentageValue() == "0") ||
            (first[0].kind == TokenType::kDimension && first[0].DimensionValue() == "0"))) {
            color_stops[0].positions.clear();
        }

        if (const auto& last = color_stops[color_stops.size() - 1].positions;
            last.size() == 1 && last[0].kind == TokenType::kPercentage && last[0].PercentageValue() == "100") {
            color_stops[color_stops.size() - 1].positions.clear();
        }

        return color_stops;
    }

    // Expands double-position color stops into separate single-position stops.
    // A double-position stop like "red 0% 50%" becomes two stops: "red 0%" and
    // "red 50%". Midpoints are dropped because they are meaningless when the stop
    // is split. This is used when the target browser doesn't support the
    // double-position syntax.
    //
    // Input:  [{color=red, positions=[0%, 50%]}]  =>  [{red, [0%]}, {red, [50%]}]
    std::vector<ColorStop> SwitchToSinglePositions(const std::vector<ColorStop>& double_positions) {
        std::vector<ColorStop> single;
        for (ColorStop stop : double_positions) {
            for (auto& pos : stop.positions) {
                pos.whitespace = WhitespaceFlags::kWhitespaceBefore;
            }
            while (stop.positions.size() > 1) {
                ColorStop clone = stop;
                clone.positions = std::vector<Token>{stop.positions[0]};
                clone.midpoint = Token{};
                single.push_back(clone);
                stop.positions.erase(stop.positions.begin());
            }
            single.push_back(stop);
        }
        return single;
    }

    // Merges adjacent same-color single-position stops into double-position stops.
    // When two consecutive stops share the same color and neither has a midpoint,
    // they are combined into a single stop with two positions. This reduces output
    // size when minifying.
    //
    // Input:  [{red, [0%]}, {red, [50%]}, {blue, [100%]}]
    //         =>  [{red, [0%, 50%]}, {blue, [100%]}]
    std::vector<ColorStop> SwitchToDoublePositions(const std::vector<ColorStop>& single_positions) {
        std::vector<ColorStop> double_positions;
        for (size_t i = 0; i < single_positions.size(); i++) {
            ColorStop stop = single_positions[i];
            if (i + 1 < single_positions.size() && stop.positions.size() == 1 &&
                stop.midpoint.kind == TokenType(0)) {
                const auto& next = single_positions[i + 1];
                if (next.positions.size() == 1 &&
                    TokensEqual(std::vector<Token>{stop.color}, std::vector<Token>{next.color},
                                nullptr)) {
                    double_positions.push_back(ColorStop{
                        {stop.positions[0], next.positions[0]}, stop.color, next.midpoint});
                    i++;
                    continue;
                }
            }
            double_positions.push_back(stop);
        }
        return double_positions;
    }

    // Extracts and removes the color interpolation declaration from a gradient's
    // leading tokens. The syntax is "in <color-space>" with an optional
    // "<hue-method> hue" suffix for polar color spaces.
    //
    // For example, given tokens ["in", "oklch", "longer", "hue", ",", ...], this
    // function returns:
    //   {remaining=[...], color_space=kOklch, hue_method=kLonger, found=true}
    //
    // If no "in <color-space>" is found, returns {found=false} and the tokens
    // are left unchanged. If the color space name is unrecognized, returns
    // {found=false}.
    //
    // The remaining tokens have their leading/trailing whitespace adjusted so that
    // the gradient output remains well-formed after the interpolation tokens are
    // stripped.
    ColorInterpolation RemoveColorInterpolation(const std::vector<Token>& tokens) {
        for (size_t i = 0; i + 1 < tokens.size(); i++) {
            const Token& in = tokens[i];
            if (in.kind == TokenType::kIdent && EqualFold(in.text, "in")) {
                const Token& space = tokens[i + 1];
                if (space.kind == TokenType::kIdent) {
                    ColorSpace color_space{};
                    HueMethod hue_method = HueMethod::kShorter;
                    size_t start = i;
                    size_t end = i + 2;

                    // Match the color space identifier against all known CSS color spaces.
                    std::string lower_space = ToLower(space.text);
                    if (lower_space == "a98-rgb") {
                        color_space = ColorSpace::kA98Rgb;
                    } else if (lower_space == "display-p3") {
                        color_space = ColorSpace::kDisplayP3;
                    } else if (lower_space == "hsl") {
                        color_space = ColorSpace::kHsl;
                    } else if (lower_space == "hwb") {
                        color_space = ColorSpace::kHwb;
                    } else if (lower_space == "lab") {
                        color_space = ColorSpace::kLab;
                    } else if (lower_space == "lch") {
                        color_space = ColorSpace::kLch;
                    } else if (lower_space == "oklab") {
                        color_space = ColorSpace::kOklab;
                    } else if (lower_space == "oklch") {
                        color_space = ColorSpace::kOklch;
                    } else if (lower_space == "prophoto-rgb") {
                        color_space = ColorSpace::kProphotoRgb;
                    } else if (lower_space == "rec2020") {
                        color_space = ColorSpace::kRec2020;
                    } else if (lower_space == "srgb") {
                        color_space = ColorSpace::kSrgb;
                    } else if (lower_space == "srgb-linear") {
                        color_space = ColorSpace::kSrgbLinear;
                    } else if (lower_space == "xyz") {
                        color_space = ColorSpace::kXyz;
                    } else if (lower_space == "xyz-d50") {
                        color_space = ColorSpace::kXyzD50;
                    } else if (lower_space == "xyz-d65") {
                        color_space = ColorSpace::kXyzD65;
                    } else {
                        return {};
                    }

                    // For polar color spaces (HSL, HWB, LCH, OKLCH), an optional
                    // hue interpolation method can follow: "shorter hue", "longer hue",
                    // "increasing hue", or "decreasing hue".
                    if (IsPolar(color_space) && i + 3 < tokens.size()) {
                        if (const Token& hue = tokens[i + 3];
                            hue.kind == TokenType::kIdent && EqualFold(hue.text, "hue")) {
                            if (const Token& method = tokens[i + 2]; method.kind == TokenType::kIdent) {
                                std::string lower_method = ToLower(method.text);
                                if (lower_method == "shorter") {
                                    hue_method = HueMethod::kShorter;
                                } else if (lower_method == "longer") {
                                    hue_method = HueMethod::kLonger;
                                } else if (lower_method == "increasing") {
                                    hue_method = HueMethod::kIncreasing;
                                } else if (lower_method == "decreasing") {
                                    hue_method = HueMethod::kDecreasing;
                                } else {
                                    return {};
                                }
                                end = i + 4;
                            }
                        }
                    }

                    // Rebuild the token list without the "in <space>" (and optional
                    // hue method) tokens. Fix up whitespace on the boundary tokens so
                    // the output remains valid CSS.
                    std::vector<Token> remaining;
                    remaining.reserve(tokens.size() - (end - start));
                    remaining.insert(remaining.end(), tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(start));
                    remaining.insert(remaining.end(), tokens.begin() + static_cast<std::ptrdiff_t>(end), tokens.end());
                    if (!remaining.empty()) {
                        remaining[0].whitespace = ClearWhitespaceBefore(remaining[0].whitespace);
                        remaining[remaining.size() - 1].whitespace =
                            ClearWhitespaceAfter(remaining[remaining.size() - 1].whitespace);
                    }
                    return {remaining, color_space, hue_method, true};
                }
            }
        }

        return {};
    }

    // Attempts to parse a single position token into a ValueWithUnit. The accepted
    // token types depend on the gradient kind:
    //   - Conic gradients accept <angle-percentage>: dimensions like "45deg" or
    //     "100grad" are converted to a percentage of a full turn (0-100%), and
    //     percentage tokens are used directly.
    //   - Linear and radial gradients accept <length-percentage>: a bare number
    //     "0" is treated as 0%, dimensions keep their original unit, and percentages
    //     are used directly.
    //
    // Returns {value, true} on success. Returns {{}, false} if the token is not
    // a valid position type (e.g. an identifier, a negative number, or a non-zero
    // bare number in a non-conic gradient).
    //
    // Input:  Token{kDimension, "45deg"}, kConic    =>  {{"%", 12.5}, true}
    // Input:  Token{kPercentage, "50"}, kLinear     =>  {{"%", 50.0}, true}
    // Input:  Token{kNumber, "0"}, kLinear          =>  {{"%", 0.0}, true}
    // Input:  Token{kIdent, "auto"}, kLinear        =>  {{}, false}
    std::pair<ValueWithUnit, bool> TryToParseValue(const Token& token, GradientKind kind) {
        if (kind == GradientKind::kConic) {
            // Conic gradients use angle-percentage: convert angles to percentage
            // of a full turn, or accept percentages directly.
            switch (token.kind) {
            case TokenType::kDimension: {
                auto [degrees, ok] = DegreesForAngle(token);
                if (!ok) {
                    return {};
                }
                return {ValueWithUnit{"%", F64(degrees).MulConst(100.0 / 360)}, true};
            }

            case TokenType::kPercentage: {
                double percent;
                if (!ParseFloat(token.PercentageValue(), &percent)) {
                    return {};
                }
                return {ValueWithUnit{"%", F64(percent)}, true};
            }

            default:
                return {};
            }
        } else {
            // Linear and radial gradients use length-percentage: a bare "0" is
            // treated as 0%, dimensions keep their unit, and percentages are direct.
            switch (token.kind) {
            case TokenType::kNumber: {
                double zero;
                if (!ParseFloat(token.text, &zero) || zero != 0) {
                    return {};
                }
                return {ValueWithUnit{"%", F64(0)}, true};
            }

            case TokenType::kDimension: {
                double dimension_value;
                if (!ParseFloat(token.DimensionValue(), &dimension_value)) {
                    return {};
                }
                return {ValueWithUnit{token.DimensionUnit(), F64(dimension_value)}, true};
            }

            case TokenType::kPercentage: {
                double percentage_value;
                if (!ParseFloat(token.PercentageValue(), &percentage_value)) {
                    return {};
                }
                return {ValueWithUnit{"%", F64(percentage_value)}, true};
            }

            default:
                return {};
            }
        }
    }

}
