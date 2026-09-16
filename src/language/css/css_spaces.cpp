#include "guchho/css/css_helpers.hpp"
#include "guchho/helpers.hpp"

namespace guchho::css {

    namespace {

        using guchho::helpers::F64;
        using guchho::helpers::Max2;
        using guchho::helpers::Max3;
        using guchho::helpers::Min2;
        using guchho::helpers::Min3;

        // MakeF64x3
        // ---------
        // Convenience wrapper that constructs an F64x3 triple from three
        // individual F64 channel values.  Used throughout this file to
        // avoid repetitive aggregate-initialisation syntax.
        F64x3 MakeF64x3(F64 v0, F64 v1, F64 v2) {
            return F64x3{v0, v1, v2};
        }

        // MultiplyMatrices
        // ----------------
        // Multiplies a 3-element column vector (b0, b1, b2) by a 3x3
        // matrix stored as a flat row-major array of 9 doubles.
        //
        // The result is the standard matrix-vector product:
        //   out[0] = m[0]*b0 + m[1]*b1 + m[2]*b2
        //   out[1] = m[3]*b0 + m[4]*b1 + m[5]*b2
        //   out[2] = m[6]*b0 + m[7]*b1 + m[8]*b2
        //
        // Input:  m = {1,0,0, 0,1,0, 0,0,1}, b0=1, b1=2, b2=3
        // Output: {1, 2, 3}  (identity matrix, vector unchanged)
        //
        // Input:  m = {0,0,1, 0,1,0, 1,0,0}, b0=1, b1=2, b2=3
        // Output: {3, 2, 1}  (swap x and z channels)
        //
        // This is the workhorse for every colour-space conversion in this
        // file.  The F64 type preserves symbolic expression trees so that
        // the matrix multiply can be folded and simplified later by the
        // optimizer.
        F64x3 MultiplyMatrices(const double (&m)[9], F64 b0, F64 b1, F64 b2) {
            return MakeF64x3(
                b0.MulConst(m[0]).Add(b1.MulConst(m[1])).Add(b2.MulConst(m[2])),
                b0.MulConst(m[3]).Add(b1.MulConst(m[4])).Add(b2.MulConst(m[5])),
                b0.MulConst(m[6]).Add(b1.MulConst(m[7])).Add(b2.MulConst(m[8])));
        }

        // D50 white-point chromaticity coordinates, expressed as x/y and
        // (1-x-y)/y ratios.  These constants appear in the CIE Lab
        // reference-white conversion and are used by XyzToLab and LabToXyz.
        constexpr double kD50X = 0.3457 / 0.3585;
        constexpr double kD50Z = (1.0 - 0.3457 - 0.3585) / 0.3585;

        constexpr double kPi = 3.141592653589793238462643383279502884;

    }

    // IsPolar
    // -------
    // Returns true when the given colour space uses polar coordinates
    // (hue angle + chroma) rather than Cartesian axes.  Polar spaces
    // are special-cased in gradient parsing because their hue component
    // needs an interpolation method (shorter, longer, increasing,
    // decreasing) instead of simple linear blending.
    //
    // Input:  ColorSpace::kHsl  => true
    // Input:  ColorSpace::kOklch => true
    // Input:  ColorSpace::kSrgb  => false
    bool IsPolar(ColorSpace color_space) {
        switch (color_space) {
        case ColorSpace::kHsl:
        case ColorSpace::kHwb:
        case ColorSpace::kLch:
        case ColorSpace::kOklch:
            return true;
        default:
            return false;
        }
    }

    // LinSrgb
    // -------
    // Converts a single gamma-encoded sRGB channel value to its linear
    // intensity.  The CSS spec defines sRGB as having a piecewise
    // transfer function: a linear segment near zero and a power-law
    // curve above the threshold.
    //
    //   if |v| < 0.04045:  v / 12.92
    //   else:               sign(v) * ((|v| + 0.055) / 1.055) ^ 2.4
    //
    // Input:  r=0.5   (mid-gray gamma-encoded)
    // Output: ≈0.214  (linear intensity)
    //
    // Input:  r=1.0   (full white)
    // Output: 1.0     (white is its own linear value)
    //
    // Input:  r=0.0   (black)
    // Output: 0.0
    //
    // Edge case: negative inputs are handled correctly; the sign is
    // preserved through the power curve via WithSignFrom.
    F64x3 LinSrgb(F64 r, F64 g, F64 b) {
        auto f = [](F64 val) {
            if (auto abs = val.Abs(); abs.Value() < 0.04045) {
                return val.DivConst(12.92);
            } else {
                return abs.AddConst(0.055).DivConst(1.055).PowConst(2.4).WithSignFrom(val);
            }
        };
        return MakeF64x3(f(r), f(g), f(b));
    }

    // GamSrgb
    // -------
    // Converts a linear-intensity sRGB channel to its gamma-encoded
    // form.  This is the inverse of LinSrgb and applies the sRGB
    // transfer curve:
    //
    //   if |v| > 0.0031308:  sign(v) * (1.055 * |v|^(1/2.4) - 0.055)
    //   else:                 v * 12.92
    //
    // Input:  r=0.214  (linear mid-gray)
    // Output: ≈0.5     (gamma-encoded)
    //
    // Input:  r=1.0
    // Output: 1.0
    //
    // Edge case: negative values are handled by WithSignFrom, which
    // copies the sign bit from the original linear value.
    F64x3 GamSrgb(F64 r, F64 g, F64 b) {
        auto f = [](F64 val) {
            if (auto abs = val.Abs(); abs.Value() > 0.0031308) {
                return abs.PowConst(1 / 2.4).MulConst(1.055).SubConst(0.055).WithSignFrom(val);
            } else {
                return val.MulConst(12.92);
            }
        };
        return MakeF64x3(f(r), f(g), f(b));
    }

    // LinSrgbToXyz
    // -------------
    // Transforms linear sRGB colour values into CIE XYZ (D65) using
    // the sRGB-to-XYZ matrix defined by IEC 61966-2-1.  The matrix
    // coefficients are expressed as exact fractions to avoid rounding
    // error in the constant fold.
    //
    // Input:  r=1, g=0, b=0  (pure red)
    // Output: {0.4124564, 0.2126729, 0.0193339}  (red's XYZ)
    //
    // Input:  r=0, g=0, b=0
    // Output: {0, 0, 0}  (black is black in every space)
    F64x3 LinSrgbToXyz(F64 r, F64 g, F64 b) {
        constexpr double m[9] = {
            506752.0 / 1228815, 87881.0 / 245763, 12673.0 / 70218,
            87098.0 / 409605, 175762.0 / 245763, 12673.0 / 175545,
            7918.0 / 409605, 87881.0 / 737289, 1001167.0 / 1053270,
        };
        return MultiplyMatrices(m, r, g, b);
    }

    // XyzToLinSrgb
    // -------------
    // Inverse of LinSrgbToXyz.  Converts CIE XYZ (D65) back to linear
    // sRGB using the inverse 3x3 matrix.  This is used when mapping
    // colours from other spaces into sRGB for output or gamut testing.
    //
    // Input:  x=0.4124564, y=0.2126729, z=0.0193339
    // Output: ≈{1, 0, 0}  (pure red recovered)
    F64x3 XyzToLinSrgb(F64 x, F64 y, F64 z) {
        constexpr double m[9] = {
            12831.0 / 3959, -329.0 / 214, -1974.0 / 3959,
            -851781.0 / 878810, 1648619.0 / 878810, 36519.0 / 878810,
            705.0 / 12673, -2585.0 / 12673, 705.0 / 667,
        };
        return MultiplyMatrices(m, x, y, z);
    }

    // LinP3
    // -----
    // Display P3 uses the same sRGB transfer function for gamma
    // encoding, so the linearisation step is identical to LinSrgb.
    F64x3 LinP3(F64 r, F64 g, F64 b) {
        return LinSrgb(r, g, b);
    }

    // GamP3
    // -----
    // Gamma encoding for Display P3, identical to GamSrgb.
    F64x3 GamP3(F64 r, F64 g, F64 b) {
        return GamSrgb(r, g, b);
    }

    // LinP3ToXyz
    // -----------
    // Converts linear Display P3 values to CIE XYZ (D65).  The matrix
    // differs from the sRGB version because Display P3 has a wider
    // colour gamut with a different red and green primary.
    //
    // Input:  r=1, g=0, b=0
    // Output: {0.4865709, 0.2656677, 0.0000000}  (P3 red primary)
    F64x3 LinP3ToXyz(F64 r, F64 g, F64 b) {
        constexpr double m[9] = {
            608311.0 / 1250200, 189793.0 / 714400, 198249.0 / 1000160,
            35783.0 / 156275, 247089.0 / 357200, 198249.0 / 2500400,
            0.0 / 1, 32229.0 / 714400, 5220557.0 / 5000800,
        };
        return MultiplyMatrices(m, r, g, b);
    }

    // XyzToLinP3
    // -----------
    // Inverse of LinP3ToXyz.  Maps CIE XYZ (D65) back to the linear
    // Display P3 colour space.
    F64x3 XyzToLinP3(F64 x, F64 y, F64 z) {
        constexpr double m[9] = {
            446124.0 / 178915, -333277.0 / 357830, -72051.0 / 178915,
            -14852.0 / 17905, 63121.0 / 35810, 423.0 / 17905,
            11844.0 / 330415, -50337.0 / 660830, 316169.0 / 330415,
        };
        return MultiplyMatrices(m, x, y, z);
    }

    // LinProphoto
    // -----------
    // Converts ProPhoto RGB values from its gamma-encoded form to
    // linear intensity.  ProPhoto uses a piecewise transfer function
    // with a linear segment near zero (threshold at 16/512) and a 1.8
    // power curve above it.
    //
    //   if |v| <= 16/512:  v / 16
    //   else:               sign(v) * |v|^1.8
    //
    // Input:  r=1.0
    // Output: 1.0
    //
    // Edge case: the threshold value (16/512 = 0.03125) ensures the
    // linear segment matches the power curve at the junction point.
    F64x3 LinProphoto(F64 r, F64 g, F64 b) {
        auto f = [](F64 val) {
            constexpr double et2 = 16.0 / 512;
            if (auto abs = val.Abs(); abs.Value() <= et2) {
                return val.DivConst(16);
            } else {
                return abs.PowConst(1.8).WithSignFrom(val);
            }
        };
        return MakeF64x3(f(r), f(g), f(b));
    }

    // GamProphoto
    // -----------
    // Inverse of LinProphoto.  Applies the ProPhoto gamma curve:
    //
    //   if |v| >= 1/512:  sign(v) * |v|^(1/1.8)
    //   else:              v * 16
    //
    // Input:  r=0.5  (linear)
    // Output: ≈0.747  (gamma-encoded)
    F64x3 GamProphoto(F64 r, F64 g, F64 b) {
        auto f = [](F64 val) {
            constexpr double et = 1.0 / 512;
            if (auto abs = val.Abs(); abs.Value() >= et) {
                return abs.PowConst(1 / 1.8).WithSignFrom(val);
            } else {
                return val.MulConst(16);
            }
        };
        return MakeF64x3(f(r), f(g), f(b));
    }

    // LinProphotoToXyz
    // -----------------
    // Converts linear ProPhoto RGB to CIE XYZ.  ProPhoto has a very
    // wide gamut, so its XYZ matrix has large coefficients.  The
    // ProPhoto working space uses D50 as its reference white, but the
    // output of this function is still D65 XYZ (the caller does not
    // need to worry about white-point adaptation here).
    F64x3 LinProphotoToXyz(F64 r, F64 g, F64 b) {
        constexpr double m[9] = {
            0.7977604896723027, 0.13518583717574031, 0.0313493495815248,
            0.2880711282292934, 0.7118432178101014, 0.00008565396060525902,
            0.0, 0.0, 0.8251046025104601,
        };
        return MultiplyMatrices(m, r, g, b);
    }

    // XyzToLinProphoto
    // -----------------
    // Inverse of LinProphotoToXyz.  Maps CIE XYZ to linear ProPhoto RGB.
    F64x3 XyzToLinProphoto(F64 x, F64 y, F64 z) {
        constexpr double m[9] = {
            1.3457989731028281, -0.25558010007997534, -0.05110628506753401,
            -0.5446224939028347, 1.5082327413132781, 0.02053603239147973,
            0.0, 0.0, 1.2119675456389454,
        };
        return MultiplyMatrices(m, x, y, z);
    }

    // LinA98Rgb
    // ---------
    // Converts Adobe RGB (1998) values to linear intensity.  Adobe RGB
    // uses a pure power curve with exponent 563/256 ≈ 2.19921875.
    //
    //   linear = sign(v) * |v|^(563/256)
    //
    // Input:  r=0.5
    // Output: ≈0.218  (linear)
    F64x3 LinA98Rgb(F64 r, F64 g, F64 b) {
        auto f = [](F64 val) { return val.Abs().PowConst(563.0 / 256).WithSignFrom(val); };
        return MakeF64x3(f(r), f(g), f(b));
    }

    // GamA98Rgb
    // ---------
    // Inverse of LinA98Rgb.  Applies the Adobe RGB gamma curve:
    //
    //   gamma = sign(v) * |v|^(256/563)
    //
    // Input:  r=0.218  (linear)
    // Output: ≈0.5     (gamma-encoded)
    F64x3 GamA98Rgb(F64 r, F64 g, F64 b) {
        auto f = [](F64 val) { return val.Abs().PowConst(256.0 / 563).WithSignFrom(val); };
        return MakeF64x3(f(r), f(g), f(b));
    }

    // LinA98RgbToXyz
    // ---------------
    // Converts linear Adobe RGB (1998) to CIE XYZ.  The matrix
    // coefficients assume the Adobe RGB D65 primaries.
    F64x3 LinA98RgbToXyz(F64 r, F64 g, F64 b) {
        constexpr double m[9] = {
            573536.0 / 994567, 263643.0 / 1420810, 187206.0 / 994567,
            591459.0 / 1989134, 6239551.0 / 9945670, 374412.0 / 4972835,
            53769.0 / 1989134, 351524.0 / 4972835, 4929758.0 / 4972835,
        };
        return MultiplyMatrices(m, r, g, b);
    }

    // XyzToLinA98Rgb
    // ---------------
    // Inverse of LinA98RgbToXyz.  Maps CIE XYZ back to linear
    // Adobe RGB (1998).
    F64x3 XyzToLinA98Rgb(F64 x, F64 y, F64 z) {
        constexpr double m[9] = {
            1829569.0 / 896150, -506331.0 / 896150, -308931.0 / 896150,
            -851781.0 / 878810, 1648619.0 / 878810, 36519.0 / 878810,
            16779.0 / 1248040, -147721.0 / 1248040, 1266979.0 / 1248040,
        };
        return MultiplyMatrices(m, x, y, z);
    }

    // Lin2020
    // -------
    // Converts Rec. 2020 values to linear intensity.  Rec. 2020 uses a
    // piecewise transfer function similar to sRGB but with different
    // constants (alpha = 1.099..., beta = 0.018...):
    //
    //   if |v| < beta * 4.5:  v / 4.5
    //   else:                  sign(v) * (alpha * |v|^0.45 - (alpha - 1))
    //
    // Input:  r=0.5
    // Output: ≈0.238  (linear)
    //
    // Edge case: the Rec. 2020 curve has a wider linear segment near
    // zero than sRGB, which helps with quantisation noise in HDR
    // content.
    F64x3 Lin2020(F64 r, F64 g, F64 b) {
        auto f = [](F64 val) {
            constexpr double alpha = 1.09929682680944;
            constexpr double beta = 0.018053968510807;
            if (auto abs = val.Abs(); abs.Value() < beta * 4.5) {
                return val.DivConst(4.5);
            } else {
                return abs.AddConst(alpha - 1).DivConst(alpha).PowConst(1 / 0.45).WithSignFrom(val);
            }
        };
        return MakeF64x3(f(r), f(g), f(b));
    }

    // Gam2020
    // -------
    // Inverse of Lin2020.  Applies the Rec. 2020 gamma curve:
    //
    //   if |v| > beta:  sign(v) * ((|v| + alpha - 1) / alpha)^(1/0.45)
    //   else:            v * 4.5
    //
    // Input:  r=0.238  (linear)
    // Output: ≈0.5     (gamma-encoded)
    F64x3 Gam2020(F64 r, F64 g, F64 b) {
        auto f = [](F64 val) {
            constexpr double alpha = 1.09929682680944;
            constexpr double beta = 0.018053968510807;
            if (auto abs = val.Abs(); abs.Value() > beta) {
                return abs.PowConst(0.45).MulConst(alpha).SubConst(alpha - 1).WithSignFrom(val);
            } else {
                return val.MulConst(4.5);
            }
        };
        return MakeF64x3(f(r), f(g), f(b));
    }

    // Lin2020ToXyz
    // -------------
    // Converts linear Rec. 2020 to CIE XYZ (D65).  The Rec. 2020
    // primaries are very wide, covering about 75.8% of CIE 1931.
    F64x3 Lin2020ToXyz(F64 r, F64 g, F64 b) {
        constexpr double m[9] = {
            63426534.0 / 99577255, 20160776.0 / 139408157, 47086771.0 / 278816314,
            26158966.0 / 99577255, 472592308.0 / 697040785, 8267143.0 / 139408157,
            0.0 / 1, 19567812.0 / 697040785, 295819943.0 / 278816314,
        };
        return MultiplyMatrices(m, r, g, b);
    }

    // XyzToLin2020
    // -------------
    // Inverse of Lin2020ToXyz.  Maps CIE XYZ to linear Rec. 2020.
    F64x3 XyzToLin2020(F64 x, F64 y, F64 z) {
        constexpr double m[9] = {
            30757411.0 / 17917100, -6372589.0 / 17917100, -4539589.0 / 17917100,
            -19765991.0 / 29648200, 47925759.0 / 29648200, 467509.0 / 29648200,
            792561.0 / 44930125, -1921689.0 / 44930125, 42328811.0 / 44930125,
        };
        return MultiplyMatrices(m, x, y, z);
    }

    // D65ToD50
    // ---------
    // Adapts a colour from the D65 illuminant to the D50 illuminant
    // using the chromatic adaptation transform defined by CIE.  This is
    // required for Lab and LCH, which use D50 as their reference white.
    //
    // Input:  x=0.4124564, y=0.2126729, z=0.0193339  (sRGB red in D65)
    // Output: adapted XYZ values under D50
    F64x3 D65ToD50(F64 x, F64 y, F64 z) {
        constexpr double m[9] = {
            1.0479297925449969, 0.022946870601609652, -0.05019226628920524,
            0.02962780877005599, 0.9904344267538799, -0.017073799063418826,
            -0.009243040646204504, 0.015055191490298152, 0.7518742814281371,
        };
        return MultiplyMatrices(m, x, y, z);
    }

    // D50ToD65
    // ---------
    // Inverse of D65ToD50.  Adapts from D50 back to D65 after Lab/LCH
    // computations are complete.
    F64x3 D50ToD65(F64 x, F64 y, F64 z) {
        constexpr double m[9] = {
            0.955473421488075, -0.02309845494876471, 0.06325924320057072,
            -0.0283697093338637, 1.0099953980813041, 0.021041441191917323,
            0.012314014864481998, -0.020507649298898964, 1.330365926242124,
        };
        return MultiplyMatrices(m, x, y, z);
    }

    // XyzToLab
    // ---------
    // Converts CIE XYZ (D50) to CIE L*a*b*.  The conversion uses the
    // standard cube-root nonlinearity with the CIE constants:
    //
    //   eps = 216/24389   (linear threshold)
    //   kappa = 24389/27  (scaling factor)
    //
    // The D50 white-point ratios (kD50X, kD50Z) are applied to x and z
    // before the nonlinear transform.  The L channel is scaled to the
    // 0-100 range; a* and b* are unbounded.
    //
    // Input:  x=0.9642, y=1.0, z=0.8251  (D50 white)
    // Output: {100, 0, 0}  (L*=100, a*=0, b*=0 for neutral white)
    //
    // Edge case: when a channel falls below the eps threshold the cube
    // root is replaced by a linear expression to avoid numerical issues
    // at zero.
    F64x3 XyzToLab(F64 x, F64 y, F64 z) {
        constexpr double eps = 216.0 / 24389;
        constexpr double kappa = 24389.0 / 27;

        x = x.DivConst(kD50X);
        z = z.DivConst(kD50Z);

        F64 f0, f1, f2;
        if (x.Value() > eps) {
            f0 = x.Cbrt();
        } else {
            f0 = x.MulConst(kappa).AddConst(16).DivConst(116);
        }
        if (y.Value() > eps) {
            f1 = y.Cbrt();
        } else {
            f1 = y.MulConst(kappa).AddConst(16).DivConst(116);
        }
        if (z.Value() > eps) {
            f2 = z.Cbrt();
        } else {
            f2 = z.MulConst(kappa).AddConst(16).DivConst(116);
        }

        return MakeF64x3(
            f1.MulConst(116).SubConst(16),
            f0.Sub(f1).MulConst(500),
            f1.Sub(f2).MulConst(200));
    }

    // LabToXyz
    // ---------
    // Inverse of XyzToLab.  Converts CIE L*a*b* back to CIE XYZ (D50).
    //
    // Input:  L=100, a=0, b=0
    // Output: {0.9642, 1.0, 0.8251}  (D50 white recovered)
    //
    // Edge case: the same eps/kappa threshold logic from XyzToLab is
    // mirrored here to maintain round-trip accuracy near zero.
    F64x3 LabToXyz(F64 l, F64 a, F64 b) {
        constexpr double kappa = 24389.0 / 27;
        constexpr double eps = 216.0 / 24389;

        F64 f1 = l.AddConst(16).DivConst(116);
        F64 f0 = a.DivConst(500).Add(f1);
        F64 f2 = f1.Sub(b.DivConst(200));

        F64 f0_3 = f0.Cubed();
        F64 f2_3 = f2.Cubed();

        F64 x, y, z;
        if (f0_3.Value() > eps) {
            x = f0_3;
        } else {
            x = f0.MulConst(116).SubConst(16).DivConst(kappa);
        }
        if (l.Value() > kappa * eps) {
            y = l.AddConst(16).DivConst(116);
            y = y.Cubed();
        } else {
            y = l.DivConst(kappa);
        }
        if (f2_3.Value() > eps) {
            z = f2_3;
        } else {
            z = f2.MulConst(116).SubConst(16).DivConst(kappa);
        }

        return MakeF64x3(x.MulConst(kD50X), y, z.MulConst(kD50Z));
    }

    // LabToLch
    // ---------
    // Converts CIE L*a*b* to CIE L*C*h (cylindrical Lab).  The hue
    // angle h is computed from a* and b* using atan2, then converted
    // to degrees in the 0-360 range.  Chroma C* is the Euclidean
    // distance from the a*-b* origin.
    //
    // Input:  L=50, a=25, b=50
    // Output: {50, 55.9, 63.4}  (L unchanged, C*=√(25²+50²), h=atan2(50,25))
    F64x3 LabToLch(F64 l, F64 a, F64 b) {
        F64 hue = b.Atan2(a).MulConst(180 / kPi);
        if (hue.Value() < 0) {
            hue = hue.AddConst(360);
        }
        return MakeF64x3(l, a.Squared().Add(b.Squared()).Sqrt(), hue);
    }

    // LchToLab
    // ---------
    // Inverse of LabToLch.  Converts cylindrical L*C*h back to
    // Cartesian L*a*b*.
    //
    // Input:  L=50, C=55.9, h=63.4
    // Output: ≈{50, 25, 50}  (original a* and b* recovered)
    F64x3 LchToLab(F64 l, F64 c, F64 h) {
        return MakeF64x3(l, h.MulConst(kPi / 180).Cos().Mul(c), h.MulConst(kPi / 180).Sin().Mul(c));
    }

    // XyzToOklab
    // -----------
    // Converts CIE XYZ (D65) to Oklab, Björn Ottosson's perceptually
    // uniform colour space.  The transform goes through an intermediate
    // LMS cone-response space:
    //
    //   1. XYZ => LMS  (using xyz_to_lms matrix)
    //   2. Cube-root each LMS component (perceptual nonlinearity)
    //   3. Cube-root LMS => Oklab  (using lms_to_oklab matrix)
    //
    // Input:  x=0.4124564, y=0.2126729, z=0.0193339  (sRGB red)
    // Output: {0.6279, 0.2249, 0.1264}  (red in Oklab)
    //
    // Edge case: Oklab is especially well-suited for colour blending
    // because interpolating in Oklab produces perceptually smooth
    // gradients without the hue shifts that plague HSL.
    F64x3 XyzToOklab(F64 x, F64 y, F64 z) {
        constexpr double xyz_to_lms[9] = {
            0.8190224432164319, 0.3619062562801221, -0.12887378261216414,
            0.0329836671980271, 0.9292868468965546, 0.03614466816999844,
            0.048177199566046255, 0.26423952494422764, 0.6335478258136937,
        };
        constexpr double lms_to_oklab[9] = {
            0.2104542553, 0.7936177850, -0.0040720468,
            1.9779984951, -2.4285922050, 0.4505937099,
            0.0259040371, 0.7827717662, -0.8086757660,
        };
        F64x3 lms = MultiplyMatrices(xyz_to_lms, x, y, z);
        return MultiplyMatrices(lms_to_oklab, lms.v0.Cbrt(), lms.v1.Cbrt(), lms.v2.Cbrt());
    }

    // OklabToXyz
    // -----------
    // Inverse of XyzToOklab.  Converts Oklab back to CIE XYZ (D65).
    // The LMS components are cubed (undoing the cube-root) before the
    // inverse matrix transform.
    //
    // Input:  L=0.6279, a=0.2249, b=0.1264  (sRGB red in Oklab)
    // Output: ≈{0.4124564, 0.2126729, 0.0193339}
    F64x3 OklabToXyz(F64 l, F64 a, F64 b) {
        constexpr double lms_to_xyz[9] = {
            1.2268798733741557, -0.5578149965554813, 0.28139105017721583,
            -0.04057576262431372, 1.1122868293970594, -0.07171106666151701,
            -0.07637294974672142, -0.4214933239627914, 1.5869240244272418,
        };
        constexpr double oklab_to_lms[9] = {
            0.99999999845051981432, 0.39633779217376785678, 0.21580375806075880339,
            1.0000000088817607767, -0.1055613423236563494, -0.063854174771705903402,
            1.0000000546724109177, -0.089484182094965759684, -1.2914855378640917399,
        };
        F64x3 lms = MultiplyMatrices(oklab_to_lms, l, a, b);
        return MultiplyMatrices(lms_to_xyz, lms.v0.Cubed(), lms.v1.Cubed(), lms.v2.Cubed());
    }

    // OklabToOklch
    // -------------
    // Thin wrapper that converts Oklab to its cylindrical form Oklch.
    // Oklch shares the same structure as CIE L*C*h but operates in the
    // Oklab perceptual uniformity, making it the preferred space for
    // gradient hue interpolation in modern CSS.
    //
    // Input:  L=0.6279, a=0.2249, b=0.1264  (sRGB red)
    // Output: {0.6279, 0.2578, 29.23}  (L, chroma, hue in degrees)
    F64x3 OklabToOklch(F64 l, F64 a, F64 b) {
        return LabToLch(l, a, b);
    }

    // OklchToOklab
    // -------------
    // Inverse of OklabToOklch.  Converts cylindrical Oklch back to
    // Cartesian Oklab.
    //
    // Input:  L=0.6279, C=0.2578, h=29.23
    // Output: ≈{0.6279, 0.2249, 0.1264}
    F64x3 OklchToOklab(F64 l, F64 c, F64 h) {
        return LchToLab(l, c, h);
    }

    // DeltaEOk
    // --------
    // Computes the Euclidean distance between two colours in Oklab.
    // This is known as "delta E" in Oklab and serves as a
    // perceptually meaningful colour-difference metric.  A value below
    // approximately 0.02 is generally imperceptible to the human eye.
    //
    // Input:  two identical colours  =>  0
    // Input:  L1=0.5, a1=0.1, b1=0.2, L2=0.6, a2=0.1, b2=0.2
    //         =>  0.1  (only the L channel differs)
    //
    // Edge case: when both colours are identical the function returns
    // exactly 0.0 with no floating-point noise thanks to the F64
    // symbolic representation.
    F64 DeltaEOk(F64 l1, F64 a1, F64 b1, F64 l2, F64 a2, F64 b2) {
        F64 dL_sq = l1.Sub(l2).Squared();
        F64 da_sq = a1.Sub(a2).Squared();
        F64 db_sq = b1.Sub(b2).Squared();
        return dL_sq.Add(da_sq).Add(db_sq).Sqrt();
    }

    // GamutMappingXyzToSrgb
    // ---------------------
    // Maps an arbitrary CIE XYZ colour into the sRGB gamut using
    // CSS Color Level 4 gamut-mapping algorithm.  The approach works
    // in Oklch space and binary-searches for the maximum chroma that
    // still fits inside the sRGB cube:
    //
    //   1. Convert XYZ => Oklab => Oklch.
    //   2. If L is 0 or 1, return grey immediately (edge case).
    //   3. Binary-search chroma from 0 to the original value.
    //   4. At each step, convert back to sRGB and test in-gamut.
    //   5. If out-of-gamut, clip to [0,1] and check if the
    //      perceptual error (delta E in Oklab) is below the
    //      just-noticeable difference threshold (0.02).  If so,
    //      return the clipped colour.
    //   6. Otherwise reduce chroma and repeat.
    //
    // Input:  x=0.7, y=0.2, z=0.1  (wide-gamut colour)
    // Output: sRGB triple clamped to [0,1] for each channel
    //
    // Edge case: the function handles L=0 (black) and L=1 (white)
    // explicitly to avoid division-by-zero in the chroma search.
    // The epsilon of 0.0001 ensures the binary search terminates in
    // a reasonable number of iterations.
    F64x3 GamutMappingXyzToSrgb(F64 x, F64 y, F64 z) {
        auto oklab = XyzToOklab(x, y, z);
        auto oklch = OklabToOklch(oklab.v0, oklab.v1, oklab.v2);
        F64 origin_l = oklch.v0;
        F64 origin_c = oklch.v1;
        F64 origin_h = oklch.v2;

        if (origin_l.Value() >= 1 || origin_l.Value() <= 0) {
            return MakeF64x3(origin_l, origin_l, origin_l);
        }

        auto oklch_to_srgb = [](F64 l, F64 c, F64 h) {
            auto lab = OklchToOklab(l, c, h);
            auto xyz = OklabToXyz(lab.v0, lab.v1, lab.v2);
            auto rgb = XyzToLinSrgb(xyz.v0, xyz.v1, xyz.v2);
            return GamSrgb(rgb.v0, rgb.v1, rgb.v2);
        };

        auto srgb_to_oklab = [](F64 r, F64 g, F64 b) {
            auto rgb = LinSrgb(r, g, b);
            auto xyz = LinSrgbToXyz(rgb.v0, rgb.v1, rgb.v2);
            return XyzToOklab(xyz.v0, xyz.v1, xyz.v2);
        };

        auto in_gamut = [](F64 r, F64 g, F64 b) {
            return r.Value() >= 0 && r.Value() <= 1 && g.Value() >= 0 && g.Value() <= 1 &&
                b.Value() >= 0 && b.Value() <= 1;
        };

        auto rgb = oklch_to_srgb(origin_l, origin_c, origin_h);
        F64 r = rgb.v0, g = rgb.v1, b = rgb.v2;
        if (in_gamut(r, g, b)) {
            return MakeF64x3(r, g, b);
        }

        constexpr double kJnd = 0.02;
        constexpr double kEpsilon = 0.0001;
        F64 min = F64(0.0);
        F64 max = origin_c;

        auto clip = [](F64 v) {
            if (v.Value() < 0) {
                return F64(0);
            }
            if (v.Value() > 1) {
                return F64(1);
            }
            return v;
        };

        while (max.Sub(min).Value() > kEpsilon) {
            F64 chroma = min.Add(max).DivConst(2);
            origin_c = chroma;

            rgb = oklch_to_srgb(origin_l, origin_c, origin_h);
            r = rgb.v0;
            g = rgb.v1;
            b = rgb.v2;
            if (in_gamut(r, g, b)) {
                min = chroma;
                continue;
            }

            F64 clipped_r = clip(r), clipped_g = clip(g), clipped_b = clip(b);
            auto lab1 = srgb_to_oklab(clipped_r, clipped_b, clipped_g);
            auto lab2 = srgb_to_oklab(r, g, b);
            F64 e = DeltaEOk(lab1.v0, lab1.v1, lab1.v2, lab2.v0, lab2.v1, lab2.v2);
            if (e.Value() < kJnd) {
                return MakeF64x3(clipped_r, clipped_g, clipped_b);
            }

            max = chroma;
        }

        return MakeF64x3(r, g, b);
    }

    // HslToRgb
    // ---------
    // Converts HSL (hue in degrees 0-360, saturation and lightness as
    // percentages 0-100) to gamma-encoded sRGB.  The algorithm uses
    // the CSS spec's piecewise formulation based on the hue sector.
    //
    // Input:  h=0, s=100, l=50   =>  {255, 0, 0}  pure red
    // Input:  h=120, s=100, l=50  =>  {0, 255, 0}  pure green
    // Input:  h=0, s=0, l=50     =>  {128, 128, 128}  grey
    //
    // Edge case: when saturation is 0 the hue is irrelevant and the
    // function returns a grey determined solely by lightness.
    F64x3 HslToRgb(F64 hue, F64 sat, F64 light) {
        hue = hue.DivConst(360);
        hue = hue.Sub(hue.Floor());
        hue = hue.MulConst(360);

        sat = sat.DivConst(100);
        light = light.DivConst(100);

        auto f = [&](double n) {
            F64 k = hue.DivConst(30).AddConst(n);
            k = k.DivConst(12);
            k = k.Sub(k.Floor());
            k = k.MulConst(12);
            F64 a = Min2(light, light.Neg().AddConst(1)).Mul(sat);
            return light.Sub(Max2(F64(-1), Min3(k.SubConst(3), k.Neg().AddConst(9), F64(1))).Mul(a));
        };

        return MakeF64x3(f(0), f(8), f(4));
    }

    // RgbToHsl
    // ---------
    // Converts gamma-encoded sRGB to HSL.  Hue is returned in degrees
    // 0-360, saturation and lightness as percentages 0-100.
    //
    // Input:  r=255, g=0, b=0    =>  {0, 100, 50}   pure red
    // Input:  r=128, g=128, b=128  =>  {NaN, 0, 50}  grey (hue undefined)
    //
    // Edge case: when the colour is grey (max == min) the hue is set to
    // NaN and saturation to 0, matching the CSS spec's behaviour.
    F64x3 RgbToHsl(F64 red, F64 green, F64 blue) {
        F64 max = Max3(red, green, blue);
        F64 min = Min3(red, green, blue);
        F64 hue = F64(std::numeric_limits<double>::quiet_NaN());
        F64 sat = F64(0.0);
        F64 light = min.Add(max).DivConst(2);
        F64 d = max.Sub(min);

        if (d.Value() != 0) {
            if (F64 div = Min2(light, light.Neg().AddConst(1)); div.Value() != 0) {
                sat = max.Sub(light).Div(div);
            }

            if (max.Value() == red.Value()) {
                hue = green.Sub(blue).Div(d);
                if (green.Value() < blue.Value()) {
                    hue = hue.AddConst(6);
                }
            } else if (max.Value() == green.Value()) {
                hue = blue.Sub(red).Div(d).AddConst(2);
            } else if (max.Value() == blue.Value()) {
                hue = red.Sub(green).Div(d).AddConst(4);
            }

            hue = hue.MulConst(60);
        }

        return MakeF64x3(hue, sat.MulConst(100), light.MulConst(100));
    }

    // HwbToRgb
    // ---------
    // Converts HWB (hue, whiteness, blackness) to gamma-encoded sRGB.
    // HWB is an alternative to HSL that is more intuitive for humans:
    // you start with a hue and mix in white and black.
    //
    // Input:  h=0, w=0, b=0    =>  {255, 0, 0}   pure red
    // Input:  h=0, w=50, b=50  =>  grey (w+b >= 1 triggers grey path)
    //
    // Edge case: when white + black >= 1 the colour is grey and the hue
    // is ignored.  The grey value is white/(white+black).
    F64x3 HwbToRgb(F64 hue, F64 white, F64 black) {
        white = white.DivConst(100);
        black = black.DivConst(100);
        if (white.Add(black).Value() >= 1) {
            F64 gray = white.Div(white.Add(black));
            return MakeF64x3(gray, gray, gray);
        }
        F64 delta = white.Add(black).Neg().AddConst(1);
        auto rgb = HslToRgb(hue, F64(100), F64(50));
        return MakeF64x3(
            delta.Mul(rgb.v0).Add(white),
            delta.Mul(rgb.v1).Add(white),
            delta.Mul(rgb.v2).Add(white));
    }

    // RgbToHwb
    // ---------
    // Converts gamma-encoded sRGB to HWB.  White is the minimum channel,
    // black is 1 minus the maximum channel, and hue is taken from the
    // HSL conversion.
    //
    // Input:  r=255, g=0, b=0  =>  {0, 0, 0}  (hue=0, no white, no black)
    F64x3 RgbToHwb(F64 red, F64 green, F64 blue) {
        auto hsl = RgbToHsl(red, green, blue);
        F64 white = Min3(red, green, blue);
        F64 black = Max3(red, green, blue).Neg().AddConst(1);
        return MakeF64x3(hsl.v0, white.MulConst(100), black.MulConst(100));
    }

    // XyzToColorSpace
    // ----------------
    // Universal dispatcher that converts CIE XYZ (D65) to any
    // supported CSS colour space.  The `color_space` parameter selects
    // which conversion chain to apply.  Each case applies the
    // appropriate linearisation/gamma and matrix transforms.
    //
    // For spaces that use D50 as their reference white (Lab, LCH,
    // ProPhoto RGB) the D65=>D50 adaptation is applied first.
    //
    // Input:  x=0.4124564, y=0.2126729, z=0.0193339, kSrgb
    //         =>  {1.0, 0.0, 0.0}  (sRGB red)
    //
    // Input:  same XYZ, kHsl
    //         =>  {0, 100, 50}  (HSL red)
    //
    // Edge case: unknown colour spaces return black (0,0,0).
    F64x3 XyzToColorSpace(F64 x, F64 y, F64 z, ColorSpace color_space) {
        switch (color_space) {
        case ColorSpace::kA98Rgb: {
            F64x3 rgb = XyzToLinA98Rgb(x, y, z);
            return GamA98Rgb(rgb.v0, rgb.v1, rgb.v2);
        }

        case ColorSpace::kDisplayP3: {
            F64x3 rgb = XyzToLinP3(x, y, z);
            return GamP3(rgb.v0, rgb.v1, rgb.v2);
        }

        case ColorSpace::kHsl: {
            F64x3 rgb = XyzToLinSrgb(x, y, z);
            F64x3 gam = GamSrgb(rgb.v0, rgb.v1, rgb.v2);
            return RgbToHsl(gam.v0, gam.v1, gam.v2);
        }

        case ColorSpace::kHwb: {
            F64x3 rgb = XyzToLinSrgb(x, y, z);
            F64x3 gam = GamSrgb(rgb.v0, rgb.v1, rgb.v2);
            return RgbToHwb(gam.v0, gam.v1, gam.v2);
        }

        case ColorSpace::kLab: {
            F64x3 d50 = D65ToD50(x, y, z);
            return XyzToLab(d50.v0, d50.v1, d50.v2);
        }

        case ColorSpace::kLch: {
            F64x3 d50 = D65ToD50(x, y, z);
            F64x3 lab = XyzToLab(d50.v0, d50.v1, d50.v2);
            return LabToLch(lab.v0, lab.v1, lab.v2);
        }

        case ColorSpace::kOklab:
            return XyzToOklab(x, y, z);

        case ColorSpace::kOklch: {
            F64x3 oklab = XyzToOklab(x, y, z);
            return OklabToOklch(oklab.v0, oklab.v1, oklab.v2);
        }

        case ColorSpace::kProphotoRgb: {
            F64x3 d50 = D65ToD50(x, y, z);
            F64x3 rgb = XyzToLinProphoto(d50.v0, d50.v1, d50.v2);
            return GamProphoto(rgb.v0, rgb.v1, rgb.v2);
        }

        case ColorSpace::kRec2020: {
            F64x3 rgb = XyzToLin2020(x, y, z);
            return Gam2020(rgb.v0, rgb.v1, rgb.v2);
        }

        case ColorSpace::kSrgb: {
            F64x3 rgb = XyzToLinSrgb(x, y, z);
            return GamSrgb(rgb.v0, rgb.v1, rgb.v2);
        }

        case ColorSpace::kSrgbLinear:
            return XyzToLinSrgb(x, y, z);

        case ColorSpace::kXyz:
        case ColorSpace::kXyzD65:
            return MakeF64x3(x, y, z);

        case ColorSpace::kXyzD50:
            return D65ToD50(x, y, z);

        default:
            return MakeF64x3(F64(0), F64(0), F64(0));
        }
    }

    // ColorSpaceToXyz
    // ----------------
    // Inverse of XyzToColorSpace.  Converts from any supported CSS
    // colour space to CIE XYZ (D65).  Each case applies the inverse
    // chain: gamma decoding => linear-space-to-XYZ matrix, with D50=>D65
    // adaptation where needed.
    //
    // Input:  v0=1, v1=0, v2=0, kSrgb
    //         =>  {0.4124564, 0.2126729, 0.0193339}  (sRGB red XYZ)
    //
    // Input:  v0=0, v1=100, v2=50, kHsl
    //         =>  XYZ of pure green (HSL h=0 s=100 l=50)
    //
    // Edge case: unknown colour spaces return black (0,0,0).
    F64x3 ColorSpaceToXyz(F64 v0, F64 v1, F64 v2, ColorSpace color_space) {
        switch (color_space) {
        case ColorSpace::kA98Rgb: {
            F64x3 rgb = LinA98Rgb(v0, v1, v2);
            return LinA98RgbToXyz(rgb.v0, rgb.v1, rgb.v2);
        }

        case ColorSpace::kDisplayP3: {
            F64x3 rgb = LinP3(v0, v1, v2);
            return LinP3ToXyz(rgb.v0, rgb.v1, rgb.v2);
        }

        case ColorSpace::kHsl: {
            F64x3 rgb = HslToRgb(v0, v1, v2);
            F64x3 lin = LinSrgb(rgb.v0, rgb.v1, rgb.v2);
            return LinSrgbToXyz(lin.v0, lin.v1, lin.v2);
        }

        case ColorSpace::kHwb: {
            F64x3 rgb = HwbToRgb(v0, v1, v2);
            F64x3 lin = LinSrgb(rgb.v0, rgb.v1, rgb.v2);
            return LinSrgbToXyz(lin.v0, lin.v1, lin.v2);
        }

        case ColorSpace::kLab: {
            F64x3 lab = LabToXyz(v0, v1, v2);
            return D50ToD65(lab.v0, lab.v1, lab.v2);
        }

        case ColorSpace::kLch: {
            F64x3 lab = LchToLab(v0, v1, v2);
            F64x3 xyz = LabToXyz(lab.v0, lab.v1, lab.v2);
            return D50ToD65(xyz.v0, xyz.v1, xyz.v2);
        }

        case ColorSpace::kOklab:
            return OklabToXyz(v0, v1, v2);

        case ColorSpace::kOklch: {
            F64x3 oklab = OklchToOklab(v0, v1, v2);
            return OklabToXyz(oklab.v0, oklab.v1, oklab.v2);
        }

        case ColorSpace::kProphotoRgb: {
            F64x3 rgb = LinProphoto(v0, v1, v2);
            F64x3 xyz = LinProphotoToXyz(rgb.v0, rgb.v1, rgb.v2);
            return D50ToD65(xyz.v0, xyz.v1, xyz.v2);
        }

        case ColorSpace::kRec2020: {
            F64x3 rgb = Lin2020(v0, v1, v2);
            return Lin2020ToXyz(rgb.v0, rgb.v1, rgb.v2);
        }

        case ColorSpace::kSrgb: {
            F64x3 rgb = LinSrgb(v0, v1, v2);
            return LinSrgbToXyz(rgb.v0, rgb.v1, rgb.v2);
        }

        case ColorSpace::kSrgbLinear:
            return LinSrgbToXyz(v0, v1, v2);

        case ColorSpace::kXyz:
        case ColorSpace::kXyzD65:
            return MakeF64x3(v0, v1, v2);

        case ColorSpace::kXyzD50:
            return D50ToD65(v0, v1, v2);

        default:
            return MakeF64x3(F64(0), F64(0), F64(0));
        }
    }
}
