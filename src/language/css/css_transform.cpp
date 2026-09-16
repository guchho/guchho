#include "guchho/css/css_helpers.hpp"
#include "guchho/helpers.hpp"


namespace guchho::css {

    namespace {

        // ClearWhitespaceBefore
        // ---------------------
        // Strips the kWhitespaceBefore flag from a WhitespaceFlags bitmask
        // without affecting any other flags.  Used when the first token of
        // a function's argument list should have no leading space, and
        // again on the last token to strip trailing space.
        //
        // Input:  kWhitespaceBefore | kWhitespaceAfter
        // Output: kWhitespaceAfter
        //
        // Input:  kNone
        // Output: kNone  (no-op when the flag is already absent)
        WhitespaceFlags ClearWhitespaceBefore(WhitespaceFlags flags) {
            return static_cast<WhitespaceFlags>(static_cast<uint8_t>(flags) &
                                                ~static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore));
        }

        // ClearWhitespaceAfter
        // --------------------
        // Strips the kWhitespaceAfter flag from a WhitespaceFlags bitmask.
        // Symmetric to ClearWhitespaceBefore; used to remove the trailing
        // space after the last argument of a function call.
        WhitespaceFlags ClearWhitespaceAfter(WhitespaceFlags flags) {
            return static_cast<WhitespaceFlags>(static_cast<uint8_t>(flags) &
                                                ~static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter));
        }


        // ShiftDot
        // --------
        // Moves the decimal point in a numeric string by `dot_offset`
        // positions and returns the resulting string together with a
        // success flag.  A positive offset moves the dot to the right
        // (making the number larger), a negative offset moves it left.
        //
        // The function is used to convert percentage tokens into plain
        // numbers when doing so produces a shorter string.  For example,
        // "50.00%" can become "0.5" by shifting the dot two places left.
        //
        // Input:  text = "12.5",  dot_offset = -2  =>  "0.125"
        // Input:  text = "0.5",   dot_offset = -2  =>  "0.005"
        // Input:  text = "100",   dot_offset = 2   =>  "10000"
        // Input:  text = "1.0",   dot_offset = -1  =>  "10"  (fractional part dropped)
        //
        // Returns {"", false} when the input contains an exponent (e/E)
        // because the function does not handle scientific notation.
        //
        // Edge cases:
        //   - Leading zeros before the dot are stripped (e.g. "00.5" => "0.5").
        //   - Trailing zeros after the dot are stripped (e.g. "1.50" => "1.5").
        //   - When the dot moves past the end of the digit string, trailing
        //     zeros are appended to form an integer (e.g. "1.5", offset=2 => "150").
        //   - When the dot moves before the first digit, leading zeros are
        //     prepended (e.g. "5", offset=-2 => "0.05").
        std::pair<std::string, bool> ShiftDot(std::string_view text, int dot_offset) {
            // This doesn't handle numbers with exponents
            if (text.find_first_of("eE") != std::string_view::npos) {
                return {};
            }

            std::string remaining(text);
            std::string sign;

            // Handle a leading sign
            if (!remaining.empty() && (remaining[0] == '-' || remaining[0] == '+')) {
                sign = remaining.substr(0, 1);
                remaining = remaining.substr(1);
            }

            // Remove the dot
            ptrdiff_t dot = static_cast<ptrdiff_t>(remaining.find('.'));
            if (dot == static_cast<ptrdiff_t>(std::string::npos)) {
                dot = static_cast<ptrdiff_t>(remaining.size());
            } else {
                remaining.erase(static_cast<size_t>(dot), 1);
            }

            // Move the dot
            dot += dot_offset;

            // Remove any leading zeros before the dot
            while (!remaining.empty() && dot > 0 && remaining[0] == '0') {
                remaining = remaining.substr(1);
                --dot;
            }

            // Remove any trailing zeros after the dot
            while (!remaining.empty() && static_cast<ptrdiff_t>(remaining.size()) > dot &&
                remaining.back() == '0') {
                remaining.pop_back();
            }

            // Does this number have no fractional component?
            if (dot >= static_cast<ptrdiff_t>(remaining.size())) {
                return {sign + remaining + std::string(static_cast<size_t>(dot - static_cast<ptrdiff_t>(remaining.size())), '0'),
                        true};
            }

            // Potentially add leading zeros
            if (dot < 0) {
                remaining = std::string(static_cast<size_t>(-dot), '0') + remaining;
                dot = 0;
            }

            // Insert the dot again
            return {sign + remaining.substr(0, static_cast<size_t>(dot)) + "." +
                        remaining.substr(static_cast<size_t>(dot)),
                    true};
        }

        // TurnPercentIntoNumberIfShorter
        // ------------------------------
        // Attempts to convert a percentage token into a plain number token
        // when the resulting string would be shorter.  This is only valid
        // for scale-related CSS transform functions where "50%" and the
        // number "0.5" are equivalent.
        //
        // The conversion delegates to ShiftDot with an offset of -2, which
        // divides the percentage value by 100.  If the shifted string is
        // not shorter than the original text the token is left unchanged.
        //
        // Input:  token = {kind: kPercentage, text: "50%"}
        // Output: token = {kind: kNumber, text: "0.5"}  (2 bytes saved)
        //
        // Input:  token = {kind: kPercentage, text: "0.5%"}
        // Output: token unchanged  ("0.005" is longer than "0.5%")
        //
        // Input:  token = {kind: kNumber, text: "42"}
        // Output: token unchanged  (not a percentage)
        void TurnPercentIntoNumberIfShorter(Token* t) {
            if (t->kind == TokenType::kPercentage) {
                auto [shifted, ok] = ShiftDot(t->PercentageValue(), -2);
                if (ok && shifted.size() < t->text.size()) {
                    t->kind = TokenType::kNumber;
                    t->text = std::move(shifted);
                }
            }
        }

    }

    // MangleTransforms
    // ----------------
    // Simplifies CSS `transform` function tokens in-place.  The function
    // walks every token in the vector; when it finds a function token whose
    // arguments are comma-separated, it attempts to simplify it using the
    // rules below.  Non-function tokens and functions with non-comma-separated
    // arguments (e.g. containing `var()`) are left untouched.
    //
    // Simplification categories:
    //
    //   2D transforms — matrix, translate(x/y), scale(x/y), rotate,
    //                    rotateZ, skew(x/y)
    //   3D transforms — matrix3d, translate3d/z, scale3d/z, rotate3d/x/y,
    //                    perspective
    //
    // Each transform type has its own set of algebraic reductions:
    //
    //   • Redundant arguments are dropped (e.g. translate(tx, 0) => translate(tx)).
    //   • Axis-specific functions are emitted when one component is identity
    //     (e.g. translate(0, ty) => translateY(ty)).
    //   • Identical scale components collapse (e.g. scale(s, s) => scale(s)).
    //   • matrix/matrix3d are detected as pure scale operations and rewritten
    //     into the simpler scale/scale3d/scaleZ forms.
    //   • rotateZ is rewritten as rotate (2D equivalent) because it does not
    //     trigger Safari's 3D rendering bug.
    //   • Percentage arguments in scale functions are converted to plain
    //     numbers when the numeric form is shorter.
    //
    // After simplification, leading and trailing whitespace flags are
    // stripped from the function's argument list for cleaner output.
    //
    // Safari note: 3D transforms are never collapsed into 2D transforms
    // because Safari renders 3D and 2D transforms differently, which would
    // cause a visual regression.
    //
    // Input:  [kFunction("translate", [kDimension("10px"), kComma, kNumber("0")])]
    // Output: [kFunction("translate", [kDimension("10px")])]
    //         — the redundant zero Y component is removed
    //
    // Input:  [kFunction("matrix", [kNumber("2"), kComma, kNumber("0"), ...])]
    //         where b=c=e=f=0 and a=d=2
    // Output: [kFunction("scale", [kNumber("2")])]
    //         — a uniform-scale matrix becomes scale()
    //
    // Input:  [kFunction("scale", [kPercentage("50%")])]
    // Output: [kFunction("scale", [kNumber("0.5")])]
    //         — percentage converted to shorter number form
    //
    // Edge cases:
    //   - Functions containing `var()` or other non-comma-separated args
    //     are skipped entirely to avoid breaking custom-property substitution.
    //   - matrix3d is only simplified to scale3d/scaleZ; it is never
    //     converted to translate3d because translate3d requires units.
    //   - matrix is never converted to translate for the same reason.
    //   - The function returns the input vector unmodified when no
    //     simplifications apply.
    std::vector<Token> MangleTransforms(std::vector<Token> tokens) {
        for (Token& token : tokens) {
            if (token.kind == TokenType::kFunction) {
                if (!token.children || !TokensAreCommaSeparated(*token.children)) {
                    continue;
                }

                std::vector<Token>& args = *token.children;
                size_t n = args.size();
                std::string lower_text = helpers::ToLowerASCII(token.text);

                ////////////////////////////////////////////////////////////////////////////////
                // 2D transforms

                if (lower_text == "matrix") {
                    // specifies a 2D transformation in the form of a transformation
                    // matrix of the six values a, b, c, d, e, f.
                    if (n == 11) {
                        // | a c 0 e |
                        // | b d 0 f |
                        // | 0 0 1 0 |
                        // | 0 0 0 1 |
                        Token& a = args[0];
                        Token& b = args[2];
                        Token& c = args[4];
                        Token& d = args[6];
                        Token& e = args[8];
                        Token& f = args[10];
                        if (b.IsZero() && c.IsZero() && e.IsZero() && f.IsZero()) {
                            // | a 0 0 0 |
                            // | 0 d 0 0 |
                            // | 0 0 1 0 |
                            // | 0 0 0 1 |
                            if (a.EqualIgnoringWhitespace(d)) {
                                // "matrix(a, 0, 0, a, 0, 0)" => "scale(a)"
                                token.text = "scale";
                                *token.children = std::vector<Token>(args.begin(), args.begin() + 1);
                            } else if (d.IsOne()) {
                                // "matrix(a, 0, 0, 1, 0, 0)" => "scaleX(a)"
                                token.text = "scaleX";
                                *token.children = std::vector<Token>(args.begin(), args.begin() + 1);
                            } else if (a.IsOne()) {
                                // "matrix(1, 0, 0, d, 0, 0)" => "scaleY(d)"
                                token.text = "scaleY";
                                *token.children = std::vector<Token>{args[6]};
                            } else {
                                // "matrix(a, 0, 0, d, 0, 0)" => "scale(a, d)"
                                token.text = "scale";
                                std::vector<Token> new_children(args.begin(), args.begin() + 2);
                                new_children.push_back(d);
                                *token.children = std::move(new_children);
                            }

                            // Note: A "matrix" cannot be directly converted into a "translate"
                            // because "translate" requires units while "matrix" requires no
                            // units. I'm not sure exactly what the semantics are so I'm not
                            // sure if you can just add "px" or not. Even if that did work,
                            // you still couldn't substitute values containing "var()" since
                            // units would still not be substituted in that case.
                        }
                    }
                } else if (lower_text == "translate") {
                    // specifies a 2D translation by the vector [tx, ty], where tx is the
                    // first translation-value parameter and ty is the optional second
                    // translation-value parameter. If <ty> is not provided, ty has zero
                    // as a value.
                    if (n == 1) {
                        args[0].TurnLengthOrPercentageIntoNumberIfZero();
                    } else if (n == 3) {
                        Token& tx = args[0];
                        Token& ty = args[2];
                        tx.TurnLengthOrPercentageIntoNumberIfZero();
                        ty.TurnLengthOrPercentageIntoNumberIfZero();
                        if (ty.IsZero()) {
                            // "translate(tx, 0)" => "translate(tx)"
                            *token.children = std::vector<Token>(args.begin(), args.begin() + 1);
                        } else if (tx.IsZero()) {
                            // "translate(0, ty)" => "translateY(ty)"
                            token.text = "translateY";
                            *token.children = std::vector<Token>(args.begin() + 2, args.end());
                        }
                    }
                } else if (lower_text == "translatex") {
                    // specifies a translation by the given amount in the X direction.
                    if (n == 1) {
                        // "translateX(tx)" => "translate(tx)"
                        token.text = "translate";
                        args[0].TurnLengthOrPercentageIntoNumberIfZero();
                    }
                } else if (lower_text == "translatey") {
                    // specifies a translation by the given amount in the Y direction.
                    if (n == 1) {
                        args[0].TurnLengthOrPercentageIntoNumberIfZero();
                    }
                } else if (lower_text == "scale") {
                    // specifies a 2D scale operation by the [sx,sy] scaling vector
                    // described by the 2 parameters. If the second parameter is not
                    // provided, it takes a value equal to the first. For example,
                    // scale(1, 1) would leave an element unchanged, while scale(2, 2)
                    // would cause it to appear twice as long in both the X and Y axes,
                    // or four times its typical geometric size.
                    if (n == 1) {
                        TurnPercentIntoNumberIfShorter(&args[0]);
                    } else if (n == 3) {
                        Token& sx = args[0];
                        Token& sy = args[2];
                        TurnPercentIntoNumberIfShorter(&sx);
                        TurnPercentIntoNumberIfShorter(&sy);
                        if (sx.EqualIgnoringWhitespace(sy)) {
                            // "scale(s, s)" => "scale(s)"
                            *token.children = std::vector<Token>(args.begin(), args.begin() + 1);
                        } else if (sy.IsOne()) {
                            // "scale(s, 1)" => "scaleX(s)"
                            token.text = "scaleX";
                            *token.children = std::vector<Token>(args.begin(), args.begin() + 1);
                        } else if (sx.IsOne()) {
                            // "scale(1, s)" => "scaleY(s)"
                            token.text = "scaleY";
                            *token.children = std::vector<Token>(args.begin() + 2, args.end());
                        }
                    }
                } else if (lower_text == "scalex") {
                    // specifies a 2D scale operation using the [sx,1] scaling vector,
                    // where sx is given as the parameter.
                    if (n == 1) {
                        TurnPercentIntoNumberIfShorter(&args[0]);
                    }
                } else if (lower_text == "scaley") {
                    // specifies a 2D scale operation using the [1,sy] scaling vector,
                    // where sy is given as the parameter.
                    if (n == 1) {
                        TurnPercentIntoNumberIfShorter(&args[0]);
                    }
                } else if (lower_text == "rotate") {
                    // specifies a 2D rotation by the angle specified in the parameter
                    // about the origin of the element, as defined by the
                    // transform-origin property. For example, rotate(90deg) would
                    // cause elements to appear rotated one-quarter of a turn in the
                    // clockwise direction.
                    if (n == 1) {
                        args[0].TurnLengthIntoNumberIfZero();
                    }
                }
                // Note: This is considered a 2D transform even though it's specified
                // in terms of a 3D transform because it doesn't trigger Safari's 3D
                // transform bugs.
                else if (lower_text == "rotatez") {
                    // same as rotate3d(0, 0, 1, <angle>), which is a 3d transform
                    // equivalent to the 2d transform rotate(<angle>).
                    if (n == 1) {
                        // "rotateZ(angle)" => "rotate(angle)"
                        token.text = "rotate";
                        args[0].TurnLengthIntoNumberIfZero();
                    }
                } else if (lower_text == "skew") {
                    // specifies a 2D skew by [ax,ay] for X and Y. If the second
                    // parameter is not provided, it has a zero value.
                    if (n == 1) {
                        args[0].TurnLengthIntoNumberIfZero();
                    } else if (n == 3) {
                        Token& ax = args[0];
                        Token& ay = args[2];
                        ax.TurnLengthIntoNumberIfZero();
                        ay.TurnLengthIntoNumberIfZero();
                        if (ay.IsZero()) {
                            // "skew(ax, 0)" => "skew(ax)"
                            *token.children = std::vector<Token>(args.begin(), args.begin() + 1);
                        }
                    }
                } else if (lower_text == "skewx") {
                    // specifies a 2D skew transformation along the X axis by the given
                    // angle.
                    if (n == 1) {
                        // "skewX(ax)" => "skew(ax)"
                        token.text = "skew";
                        args[0].TurnLengthIntoNumberIfZero();
                    }
                } else if (lower_text == "skewy") {
                    // specifies a 2D skew transformation along the Y axis by the given
                    // angle.
                    if (n == 1) {
                        args[0].TurnLengthIntoNumberIfZero();
                    }
                }

                ////////////////////////////////////////////////////////////////////////////////
                // 3D transforms

                // Note: Safari has a bug where 3D transforms render differently than
                // other transforms. This means we should not minify a 3D transform
                // into a 2D transform or it will cause a rendering difference in
                // Safari.
                else if (lower_text == "matrix3d") {
                    // specifies a 3D transformation as a 4x4 homogeneous matrix of 16
                    // values in column-major order.
                    if (n == 31) {
                        // | m0 m4 m8  m12 |
                        // | m1 m5 m9  m13 |
                        // | m2 m6 m10 m14 |
                        // | m3 m7 m11 m15 |
                        uint32_t mask = 0;
                        for (int i = 0; i < 16; i++) {
                            const Token& arg = args[static_cast<size_t>(i) * 2];
                            if (arg.IsZero()) {
                                mask |= 1u << i;
                            } else if (arg.IsOne()) {
                                mask |= (1u << 16) << i;
                            }
                        }
                        const uint32_t only_scale = 0b1000'0000'0000'0000'0111'1011'1101'1110;
                        if ((mask & only_scale) == only_scale) {
                            // | m0 0  0   0 |
                            // | 0  m5 0   0 |
                            // | 0  0  m10 0 |
                            // | 0  0  0   1 |
                            Token& sx = args[0];
                            Token& sy = args[10];
                            if (sx.IsOne() && sy.IsOne()) {
                                token.text = "scaleZ";
                                *token.children = std::vector<Token>{args[20]};
                            } else {
                                token.text = "scale3d";
                                std::vector<Token> new_children;
                                new_children.reserve(5);
                                new_children.push_back(args[0]);
                                new_children.push_back(args[1]);
                                new_children.push_back(args[10]);
                                new_children.push_back(args[11]);
                                new_children.push_back(args[20]);
                                *token.children = std::move(new_children);
                            }
                        }

                        // Note: A "matrix3d" cannot be directly converted into a "translate3d"
                        // because "translate3d" requires units while "matrix3d" requires no
                        // units. I'm not sure exactly what the semantics are so I'm not
                        // sure if you can just add "px" or not. Even if that did work,
                        // you still couldn't substitute values containing "var()" since
                        // units would still not be substituted in that case.
                    }
                } else if (lower_text == "translate3d") {
                    // specifies a 3D translation by the vector [tx,ty,tz], with tx,
                    // ty and tz being the first, second and third translation-value
                    // parameters respectively.
                    if (n == 5) {
                        Token& tx = args[0];
                        Token& ty = args[2];
                        Token& tz = args[4];
                        tx.TurnLengthOrPercentageIntoNumberIfZero();
                        ty.TurnLengthOrPercentageIntoNumberIfZero();
                        tz.TurnLengthIntoNumberIfZero();
                        if (tx.IsZero() && ty.IsZero()) {
                            // "translate3d(0, 0, tz)" => "translateZ(tz)"
                            token.text = "translateZ";
                            *token.children = std::vector<Token>(args.begin() + 4, args.end());
                        }
                    }
                } else if (lower_text == "translatez") {
                    // specifies a 3D translation by the vector [0,0,tz] with the given
                    // amount in the Z direction.
                    if (n == 1) {
                        args[0].TurnLengthIntoNumberIfZero();
                    }
                } else if (lower_text == "scale3d") {
                    // specifies a 3D scale operation by the [sx,sy,sz] scaling vector
                    // described by the 3 parameters.
                    if (n == 5) {
                        Token& sx = args[0];
                        Token& sy = args[2];
                        Token& sz = args[4];
                        TurnPercentIntoNumberIfShorter(&sx);
                        TurnPercentIntoNumberIfShorter(&sy);
                        TurnPercentIntoNumberIfShorter(&sz);
                        if (sx.IsOne() && sy.IsOne()) {
                            // "scale3d(1, 1, sz)" => "scaleZ(sz)"
                            token.text = "scaleZ";
                            *token.children = std::vector<Token>(args.begin() + 4, args.end());
                        }
                    }
                } else if (lower_text == "scalez") {
                    // specifies a 3D scale operation using the [1,1,sz] scaling vector,
                    // where sz is given as the parameter.
                    if (n == 1) {
                        TurnPercentIntoNumberIfShorter(&args[0]);
                    }
                } else if (lower_text == "rotate3d") {
                    // specifies a 3D rotation by the angle specified in last parameter
                    // about the [x,y,z] direction vector described by the first three
                    // parameters. A direction vector that cannot be normalized, such as
                    // [0,0,0], will cause the rotation to not be applied.
                    if (n == 7) {
                        Token& x = args[0];
                        Token& y = args[2];
                        Token& z = args[4];
                        Token& angle = args[6];
                        angle.TurnLengthIntoNumberIfZero();
                        if (x.IsOne() && y.IsZero() && z.IsZero()) {
                            // "rotate3d(1, 0, 0, angle)" => "rotateX(angle)"
                            token.text = "rotateX";
                            *token.children = std::vector<Token>(args.begin() + 6, args.end());
                        } else if (x.IsZero() && y.IsOne() && z.IsZero()) {
                            // "rotate3d(0, 1, 0, angle)" => "rotateY(angle)"
                            token.text = "rotateY";
                            *token.children = std::vector<Token>(args.begin() + 6, args.end());
                        }
                    }
                } else if (lower_text == "rotatex") {
                    // same as rotate3d(1, 0, 0, <angle>).
                    if (n == 1) {
                        args[0].TurnLengthIntoNumberIfZero();
                    }
                } else if (lower_text == "rotatey") {
                    // same as rotate3d(0, 1, 0, <angle>).
                    if (n == 1) {
                        args[0].TurnLengthIntoNumberIfZero();
                    }
                } else if (lower_text == "perspective") {
                    // specifies a perspective projection matrix. This matrix scales
                    // points in X and Y based on their Z value, scaling points with
                    // positive Z values away from the origin, and those with negative Z
                    // values towards the origin. Points on the z=0 plane are unchanged.
                    // The parameter represents the distance of the z=0 plane from the
                    // viewer.
                    if (n == 1) {
                        args[0].TurnLengthIntoNumberIfZero();
                    }
                }

                // Trim whitespace at the ends
                std::vector<Token>& trimmed = *token.children;
                if (!trimmed.empty()) {
                    trimmed[0].whitespace = ClearWhitespaceBefore(trimmed[0].whitespace);
                    trimmed[trimmed.size() - 1].whitespace =
                        ClearWhitespaceAfter(trimmed[trimmed.size() - 1].whitespace);
                }
            }
        }

        return tokens;
    }

}
