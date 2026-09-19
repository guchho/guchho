#include "test/guchho_test.hpp"

#include "guchho/css/css_helpers.hpp"
#include "guchho/helpers.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace css = guchho::css;
namespace helpers = guchho::helpers;

using css::Token;
using css::TokenType;
using css::WhitespaceFlags;

namespace {

Token MakeNumber(std::string text) {
    Token t;
    t.kind = TokenType::kNumber;
    t.text = std::move(text);
    return t;
}

Token MakeDimension(std::string text, uint16_t unit_offset) {
    Token t;
    t.kind = TokenType::kDimension;
    t.text = std::move(text);
    t.unit_offset = unit_offset;
    return t;
}

Token MakePercentage(std::string text) {
    Token t;
    t.kind = TokenType::kPercentage;
    t.text = std::move(text);
    return t;
}

Token MakeIdent(std::string text) {
    Token t;
    t.kind = TokenType::kIdent;
    t.text = std::move(text);
    return t;
}

Token MakeHash(std::string text) {
    Token t;
    t.kind = TokenType::kHash;
    t.text = std::move(text);
    return t;
}

Token MakeFunction(std::string name) {
    Token t;
    t.kind = TokenType::kFunction;
    t.text = std::move(name);
    t.children = std::make_shared<std::vector<Token>>();
    return t;
}

Token MakeString(std::string text) {
    Token t;
    t.kind = TokenType::kString;
    t.text = std::move(text);
    return t;
}

Token MakeComma() {
    Token t;
    t.kind = TokenType::kComma;
    t.text = ",";
    return t;
}

} // namespace

// ==========================================================================
// ParseHex
// ==========================================================================

TEST(CssHelpersParseHex, ShortForm)
{
    auto [val, ok] = css::ParseHex("fff");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0x00000fffu);
}

TEST(CssHelpersParseHex, SixDigit)
{
    auto [val, ok] = css::ParseHex("ff0000");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0xff0000u);
}

TEST(CssHelpersParseHex, EightDigit)
{
    auto [val, ok] = css::ParseHex("ff000080");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0xff000080u);
}

TEST(CssHelpersParseHex, Lowercase)
{
    auto [val, ok] = css::ParseHex("aabbcc");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0xaabbccu);
}

TEST(CssHelpersParseHex, MixedCase)
{
    auto [val, ok] = css::ParseHex("AaBbCc");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0xaabbccu);
}

TEST(CssHelpersParseHex, AllZeros)
{
    auto [val, ok] = css::ParseHex("000");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0u);
}

TEST(CssHelpersParseHex, InvalidChars)
{
    auto [val, ok] = css::ParseHex("xyz");
    EXPECT_FALSE(ok);
    EXPECT_EQ(val, 0u);
}

TEST(CssHelpersParseHex, Empty)
{
    auto [val, ok] = css::ParseHex("");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0u);
}

// ==========================================================================
// CompactHex / ExpandHex
// ==========================================================================

TEST(CssHelpersCompactHex, Basic)
{
    EXPECT_EQ(css::CompactHex(0xaabbcc), 0x0abcu);
}

TEST(CssHelpersCompactHex, FullWhite)
{
    EXPECT_EQ(css::CompactHex(0xffffff), 0x0fffu);
}

TEST(CssHelpersCompactHex, Black)
{
    EXPECT_EQ(css::CompactHex(0x000000), 0x0000u);
}

TEST(CssHelpersCompactHex, PureRed)
{
    EXPECT_EQ(css::CompactHex(0xff0000), 0x0f00u);
}

TEST(CssHelpersExpandHex, Basic)
{
    EXPECT_EQ(css::ExpandHex(0x0abc), 0xaabbccu);
}

TEST(CssHelpersExpandHex, FullWhite)
{
    EXPECT_EQ(css::ExpandHex(0x0fff), 0xffffffu);
}

TEST(CssHelpersExpandHex, Black)
{
    EXPECT_EQ(css::ExpandHex(0x0000), 0x000000u);
}

TEST(CssHelpersExpandHex, PureRed)
{
    EXPECT_EQ(css::ExpandHex(0x0f00), 0xff0000u);
}

TEST(CssHelpersCompactExpand, RoundTrip)
{
    uint32_t values[] = {0x000, 0xfff, 0xf00, 0x0f0, 0x00f, 0xabc, 0x123, 0xf0f};
    for (uint32_t v : values) {
        EXPECT_EQ(css::CompactHex(css::ExpandHex(v)), v);
    }
}

// ==========================================================================
// HexR / HexG / HexB / HexA
// ==========================================================================

TEST(CssHelpersHexChannels, Red)
{
    EXPECT_EQ(css::HexR(0xff000000), 255);
    EXPECT_EQ(css::HexG(0xff000000), 0);
    EXPECT_EQ(css::HexB(0xff000000), 0);
    EXPECT_EQ(css::HexA(0xff000000), 0);
}

TEST(CssHelpersHexChannels, Green)
{
    EXPECT_EQ(css::HexR(0x00ff0000), 0);
    EXPECT_EQ(css::HexG(0x00ff0000), 255);
    EXPECT_EQ(css::HexB(0x00ff0000), 0);
    EXPECT_EQ(css::HexA(0x00ff0000), 0);
}

TEST(CssHelpersHexChannels, Blue)
{
    EXPECT_EQ(css::HexR(0x0000ff00), 0);
    EXPECT_EQ(css::HexG(0x0000ff00), 0);
    EXPECT_EQ(css::HexB(0x0000ff00), 255);
    EXPECT_EQ(css::HexA(0x0000ff00), 0);
}

TEST(CssHelpersHexChannels, Alpha)
{
    EXPECT_EQ(css::HexR(0x000000ff), 0);
    EXPECT_EQ(css::HexG(0x000000ff), 0);
    EXPECT_EQ(css::HexB(0x000000ff), 0);
    EXPECT_EQ(css::HexA(0x000000ff), 255);
}

TEST(CssHelpersHexChannels, Mixed)
{
    uint32_t v = 0xff804020;
    EXPECT_EQ(css::HexR(v), 255);
    EXPECT_EQ(css::HexG(v), 128);
    EXPECT_EQ(css::HexB(v), 64);
    EXPECT_EQ(css::HexA(v), 32);
}

TEST(CssHelpersHexChannels, AllZero)
{
    EXPECT_EQ(css::HexR(0), 0);
    EXPECT_EQ(css::HexG(0), 0);
    EXPECT_EQ(css::HexB(0), 0);
    EXPECT_EQ(css::HexA(0), 0);
}

// ==========================================================================
// FloatToByte
// ==========================================================================

TEST(CssHelpersFloatToByte, Zero)
{
    EXPECT_EQ(css::FloatToByte(0.0), 0u);
}

TEST(CssHelpersFloatToByte, One)
{
    EXPECT_EQ(css::FloatToByte(1.0), 255u);
}

TEST(CssHelpersFloatToByte, Half)
{
    EXPECT_EQ(css::FloatToByte(0.5), 128u);
}

TEST(CssHelpersFloatToByte, NegativeClamped)
{
    EXPECT_EQ(css::FloatToByte(-0.1), 0u);
}

TEST(CssHelpersFloatToByte, OverOneClamped)
{
    EXPECT_EQ(css::FloatToByte(1.5), 255u);
}

// ==========================================================================
// PackRGBA
// ==========================================================================

TEST(CssHelpersPackRGBA, Red)
{
    uint32_t v = css::PackRGBA(helpers::F64(1.0), helpers::F64(0.0), helpers::F64(0.0), 255);
    EXPECT_EQ(v, 0xff0000ffu);
}

TEST(CssHelpersPackRGBA, Black)
{
    uint32_t v = css::PackRGBA(helpers::F64(0.0), helpers::F64(0.0), helpers::F64(0.0), 255);
    EXPECT_EQ(v, 0x000000ffu);
}

TEST(CssHelpersPackRGBA, SemiTransparentGray)
{
    uint32_t v = css::PackRGBA(helpers::F64(0.5), helpers::F64(0.5), helpers::F64(0.5), 128);
    EXPECT_EQ(v, 0x80808080u);
}

// ==========================================================================
// FloatToStringForColor
// ==========================================================================

TEST(CssHelpersFloatToStringForColor, One)
{
    EXPECT_EQ(css::FloatToStringForColor(1.0), "1");
}

TEST(CssHelpersFloatToStringForColor, Zero)
{
    EXPECT_EQ(css::FloatToStringForColor(0.0), "0");
}

TEST(CssHelpersFloatToStringForColor, Half)
{
    EXPECT_EQ(css::FloatToStringForColor(0.5), "0.5");
}

TEST(CssHelpersFloatToStringForColor, Third)
{
    EXPECT_EQ(css::FloatToStringForColor(0.333), "0.333");
}

TEST(CssHelpersFloatToStringForColor, TrailingZerosStripped)
{
    EXPECT_EQ(css::FloatToStringForColor(0.100), "0.1");
}

TEST(CssHelpersFloatToStringForColor, TwoDecimals)
{
    EXPECT_EQ(css::FloatToStringForColor(0.25), "0.25");
}

// ==========================================================================
// DegreesForAngle
// ==========================================================================

TEST(CssHelpersDegreesForAngle, Degrees)
{
    auto [val, ok] = css::DegreesForAngle(MakeDimension("90deg", 2));
    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(val, 90.0);
}

TEST(CssHelpersDegreesForAngle, Radians)
{
    auto [val, ok] = css::DegreesForAngle(MakeDimension("3.14159265358979323846rad", 22));
    EXPECT_TRUE(ok);
    EXPECT_NEAR(val, 180.0, 0.001);
}

TEST(CssHelpersDegreesForAngle, Gradians)
{
    auto [val, ok] = css::DegreesForAngle(MakeDimension("100grad", 3));
    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(val, 90.0);
}

TEST(CssHelpersDegreesForAngle, Turns)
{
    auto [val, ok] = css::DegreesForAngle(MakeDimension("0.5turn", 3));
    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(val, 180.0);
}

TEST(CssHelpersDegreesForAngle, UnitlessNumber)
{
    auto [val, ok] = css::DegreesForAngle(MakeNumber("100"));
    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(val, 100.0);
}

TEST(CssHelpersDegreesForAngle, InvalidToken)
{
    auto [val, ok] = css::DegreesForAngle(MakeIdent("foo"));
    EXPECT_FALSE(ok);
    EXPECT_DOUBLE_EQ(val, 0.0);
}

// ==========================================================================
// ParseAlphaByte
// ==========================================================================

TEST(CssHelpersParseAlphaByte, MissingToken)
{
    Token empty;
    auto [val, ok] = css::ParseAlphaByte(empty);
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 255u);
}

TEST(CssHelpersParseAlphaByte, FullOpacityNumber)
{
    auto [val, ok] = css::ParseAlphaByte(MakeNumber("1"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 255u);
}

TEST(CssHelpersParseAlphaByte, HalfOpacityNumber)
{
    auto [val, ok] = css::ParseAlphaByte(MakeNumber("0.5"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 128u);
}

TEST(CssHelpersParseAlphaByte, Percentage)
{
    auto [val, ok] = css::ParseAlphaByte(MakePercentage("50%"));
    EXPECT_TRUE(ok);
    EXPECT_TRUE(val == 127u || val == 128u);
}

TEST(CssHelpersParseAlphaByte, FullPercentage)
{
    auto [val, ok] = css::ParseAlphaByte(MakePercentage("100%"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 255u);
}

TEST(CssHelpersParseAlphaByte, ZeroPercentage)
{
    auto [val, ok] = css::ParseAlphaByte(MakePercentage("0%"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0u);
}

TEST(CssHelpersParseAlphaByte, NonNumericToken)
{
    auto [val, ok] = css::ParseAlphaByte(MakeIdent("foo"));
    EXPECT_FALSE(ok);
}

// ==========================================================================
// ParseColorByte
// ==========================================================================

TEST(CssHelpersParseColorByte, NumberScale1)
{
    auto [val, ok] = css::ParseColorByte(MakeNumber("255"), 1.0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 255u);
}

TEST(CssHelpersParseColorByte, NumberScale255)
{
    auto [val, ok] = css::ParseColorByte(MakeNumber("0.5"), 255.0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 128u);
}

TEST(CssHelpersParseColorByte, Percentage)
{
    auto [val, ok] = css::ParseColorByte(MakePercentage("100%"), 1.0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 255u);
}

TEST(CssHelpersParseColorByte, PercentageHalf)
{
    auto [val, ok] = css::ParseColorByte(MakePercentage("50%"), 1.0);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(val == 127u || val == 128u);
}

TEST(CssHelpersParseColorByte, ClampedHigh)
{
    auto [val, ok] = css::ParseColorByte(MakeNumber("999"), 1.0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 255u);
}

TEST(CssHelpersParseColorByte, ClampedLow)
{
    auto [val, ok] = css::ParseColorByte(MakeNumber("-10"), 1.0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0u);
}

TEST(CssHelpersParseColorByte, NonNumericToken)
{
    auto [val, ok] = css::ParseColorByte(MakeIdent("red"), 255.0);
    EXPECT_FALSE(ok);
}

// ==========================================================================
// LowerAlphaPercentageToNumber
// ==========================================================================

TEST(CssHelpersLowerAlphaPercentageToNumber, FiftyPercent)
{
    Token result = css::LowerAlphaPercentageToNumber(MakePercentage("50%"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "0.5");
}

TEST(CssHelpersLowerAlphaPercentageToNumber, HundredPercent)
{
    Token result = css::LowerAlphaPercentageToNumber(MakePercentage("100%"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "1");
}

TEST(CssHelpersLowerAlphaPercentageToNumber, ZeroPercent)
{
    Token result = css::LowerAlphaPercentageToNumber(MakePercentage("0%"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "0");
}

TEST(CssHelpersLowerAlphaPercentageToNumber, AlreadyNumber)
{
    Token input = MakeNumber("0.5");
    Token result = css::LowerAlphaPercentageToNumber(input);
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "0.5");
}

TEST(CssHelpersLowerAlphaPercentageToNumber, IdentUnchanged)
{
    Token input = MakeIdent("red");
    Token result = css::LowerAlphaPercentageToNumber(input);
    EXPECT_EQ(result.kind, TokenType::kIdent);
    EXPECT_EQ(result.text, "red");
}

// ==========================================================================
// LooksLikeColor
// ==========================================================================

TEST(CssHelpersLooksLikeColor, NamedColorRed)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeIdent("red")));
}

TEST(CssHelpersLooksLikeColor, NamedColorBlue)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeIdent("blue")));
}

TEST(CssHelpersLooksLikeColor, NonColorIdent)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeIdent("foo")));
}

TEST(CssHelpersLooksLikeColor, Hash3)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeHash("fff")));
}

TEST(CssHelpersLooksLikeColor, Hash6)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeHash("ff0000")));
}

TEST(CssHelpersLooksLikeColor, Hash8)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeHash("ff000080")));
}

TEST(CssHelpersLooksLikeColor, Hash4)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeHash("f00a")));
}

TEST(CssHelpersLooksLikeColor, HashInvalidLength)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeHash("ff")));
}

TEST(CssHelpersLooksLikeColor, HashInvalidChars)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeHash("xyz")));
}

TEST(CssHelpersLooksLikeColor, FunctionRgb)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeFunction("rgb")));
}

TEST(CssHelpersLooksLikeColor, FunctionRgba)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeFunction("rgba")));
}

TEST(CssHelpersLooksLikeColor, FunctionHsl)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeFunction("hsl")));
}

TEST(CssHelpersLooksLikeColor, FunctionOklch)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeFunction("oklch")));
}

TEST(CssHelpersLooksLikeColor, FunctionLab)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeFunction("lab")));
}

TEST(CssHelpersLooksLikeColor, FunctionColor)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeFunction("color")));
}

TEST(CssHelpersLooksLikeColor, FunctionColorMix)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeFunction("color-mix")));
}

TEST(CssHelpersLooksLikeColor, NonColorFunction)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeFunction("calc")));
}

TEST(CssHelpersLooksLikeColor, NumberToken)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeNumber("10")));
}

TEST(CssHelpersLooksLikeColor, DimensionToken)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeDimension("10px", 2)));
}

// ==========================================================================
// IsPolar
// ==========================================================================

TEST(CssHelpersIsPolar, Hsl)
{
    EXPECT_TRUE(css::IsPolar(css::ColorSpace::kHsl));
}

TEST(CssHelpersIsPolar, Hwb)
{
    EXPECT_TRUE(css::IsPolar(css::ColorSpace::kHwb));
}

TEST(CssHelpersIsPolar, Lch)
{
    EXPECT_TRUE(css::IsPolar(css::ColorSpace::kLch));
}

TEST(CssHelpersIsPolar, Oklch)
{
    EXPECT_TRUE(css::IsPolar(css::ColorSpace::kOklch));
}

TEST(CssHelpersIsPolar, Srgb)
{
    EXPECT_FALSE(css::IsPolar(css::ColorSpace::kSrgb));
}

TEST(CssHelpersIsPolar, Lab)
{
    EXPECT_FALSE(css::IsPolar(css::ColorSpace::kLab));
}

TEST(CssHelpersIsPolar, Oklab)
{
    EXPECT_FALSE(css::IsPolar(css::ColorSpace::kOklab));
}

TEST(CssHelpersIsPolar, DisplayP3)
{
    EXPECT_FALSE(css::IsPolar(css::ColorSpace::kDisplayP3));
}

TEST(CssHelpersIsPolar, Xyz)
{
    EXPECT_FALSE(css::IsPolar(css::ColorSpace::kXyz));
}

// ==========================================================================
// IsInvalidAnimationName
// ==========================================================================

TEST(CssHelpersIsInvalidAnimationName, None)
{
    EXPECT_TRUE(css::IsInvalidAnimationName("none"));
}

TEST(CssHelpersIsInvalidAnimationName, NoneCaseInsensitive)
{
    EXPECT_TRUE(css::IsInvalidAnimationName("None"));
    EXPECT_TRUE(css::IsInvalidAnimationName("NONE"));
}

TEST(CssHelpersIsInvalidAnimationName, Initial)
{
    EXPECT_TRUE(css::IsInvalidAnimationName("initial"));
}

TEST(CssHelpersIsInvalidAnimationName, Inherit)
{
    EXPECT_TRUE(css::IsInvalidAnimationName("inherit"));
}

TEST(CssHelpersIsInvalidAnimationName, Unset)
{
    EXPECT_TRUE(css::IsInvalidAnimationName("unset"));
}

TEST(CssHelpersIsInvalidAnimationName, Revert)
{
    EXPECT_TRUE(css::IsInvalidAnimationName("revert"));
}

TEST(CssHelpersIsInvalidAnimationName, RevertLayer)
{
    EXPECT_TRUE(css::IsInvalidAnimationName("revert-layer"));
}

TEST(CssHelpersIsInvalidAnimationName, Default)
{
    EXPECT_TRUE(css::IsInvalidAnimationName("default"));
}

TEST(CssHelpersIsInvalidAnimationName, ValidName)
{
    EXPECT_FALSE(css::IsInvalidAnimationName("slide-in"));
}

TEST(CssHelpersIsInvalidAnimationName, ValidCamelCase)
{
    EXPECT_FALSE(css::IsInvalidAnimationName("myAnimation"));
}

TEST(CssHelpersIsInvalidAnimationName, ValidSimple)
{
    EXPECT_FALSE(css::IsInvalidAnimationName("foo"));
}

// ==========================================================================
// MangleFontWeight
// ==========================================================================

TEST(CssHelpersMangleFontWeight, Normal)
{
    Token result = css::MangleFontWeight(MakeIdent("normal"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "400");
}

TEST(CssHelpersMangleFontWeight, Bold)
{
    Token result = css::MangleFontWeight(MakeIdent("bold"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "700");
}

TEST(CssHelpersMangleFontWeight, BoldCaseInsensitive)
{
    Token result = css::MangleFontWeight(MakeIdent("Bold"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "700");
}

TEST(CssHelpersMangleFontWeight, NumericUnchanged)
{
    Token input = MakeNumber("300");
    Token result = css::MangleFontWeight(input);
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "300");
}

TEST(CssHelpersMangleFontWeight, RelativeWeightUnchanged)
{
    Token result = css::MangleFontWeight(MakeIdent("bolder"));
    EXPECT_EQ(result.kind, TokenType::kIdent);
    EXPECT_EQ(result.text, "bolder");
}

TEST(CssHelpersMangleFontWeight, LighterUnchanged)
{
    Token result = css::MangleFontWeight(MakeIdent("lighter"));
    EXPECT_EQ(result.kind, TokenType::kIdent);
    EXPECT_EQ(result.text, "lighter");
}

// ==========================================================================
// MangleFontFamily
// ==========================================================================

TEST(CssHelpersMangleFontFamily, SingleGeneric)
{
    std::vector<Token> result;
    std::vector<Token> input = {MakeIdent("serif")};
    EXPECT_TRUE(css::MangleFontFamily(result, input, false));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "serif");
}

TEST(CssHelpersMangleFontFamily, TwoGenerics)
{
    std::vector<Token> result;
    std::vector<Token> input = {MakeIdent("sans-serif"), MakeComma(), MakeIdent("serif")};
    EXPECT_TRUE(css::MangleFontFamily(result, input, false));
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].text, "sans-serif");
    EXPECT_EQ(result[1].kind, TokenType::kComma);
    EXPECT_EQ(result[2].text, "serif");
}

TEST(CssHelpersMangleFontFamily, QuotedSingleWord)
{
    std::vector<Token> result;
    std::vector<Token> input = {MakeString("Arial")};
    EXPECT_TRUE(css::MangleFontFamily(result, input, false));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].kind, TokenType::kIdent);
    EXPECT_EQ(result[0].text, "Arial");
}

TEST(CssHelpersMangleFontFamily, QuotedMultiWord)
{
    std::vector<Token> result;
    std::vector<Token> input = {MakeString("Times New Roman")};
    EXPECT_TRUE(css::MangleFontFamily(result, input, false));
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].text, "Times");
    EXPECT_EQ(result[1].text, "New");
    EXPECT_EQ(result[2].text, "Roman");
}

TEST(CssHelpersMangleFontFamily, NumericTokenFails)
{
    std::vector<Token> result;
    std::vector<Token> input = {MakeNumber("12")};
    EXPECT_FALSE(css::MangleFontFamily(result, input, false));
    EXPECT_TRUE(result.empty());
}

TEST(CssHelpersMangleFontFamily, EmptyFails)
{
    std::vector<Token> result;
    std::vector<Token> input;
    EXPECT_FALSE(css::MangleFontFamily(result, input, false));
}

TEST(CssHelpersMangleFontFamily, SingleUnquotedIdent)
{
    std::vector<Token> result;
    std::vector<Token> input = {MakeIdent("Arial")};
    EXPECT_TRUE(css::MangleFontFamily(result, input, false));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "Arial");
    EXPECT_EQ(result[0].kind, TokenType::kIdent);
}

// ==========================================================================
// ShortColorName / ColorNameToHex
// ==========================================================================

TEST(CssHelpersShortColorName, ContainsRed)
{
    const auto& map = css::ShortColorName();
    auto it = map.find(0xff0000ff);
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, "red");
}

TEST(CssHelpersShortColorName, ContainsPink)
{
    const auto& map = css::ShortColorName();
    auto it = map.find(0xffc0cbff);
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, "pink");
}

TEST(CssHelpersShortColorName, NotEmpty)
{
    EXPECT_FALSE(css::ShortColorName().empty());
}

TEST(CssHelpersColorNameToHex, Red)
{
    const auto& map = css::ColorNameToHex();
    auto it = map.find("red");
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, 0xff0000ff);
}

TEST(CssHelpersColorNameToHex, Blue)
{
    const auto& map = css::ColorNameToHex();
    auto it = map.find("blue");
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, 0x0000ffff);
}

TEST(CssHelpersColorNameToHex, TransparentNotPresent)
{
    const auto& map = css::ColorNameToHex();
    EXPECT_EQ(map.count("transparent"), 0u);
}

TEST(CssHelpersColorNameToHex, NotEmpty)
{
    EXPECT_FALSE(css::ColorNameToHex().empty());
}

// ==========================================================================
// Additional ParseHex edge cases
// ==========================================================================

TEST(CssHelpersParseHex, SingleDigit)
{
    auto [val, ok] = css::ParseHex("a");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0x0000000au);
}

TEST(CssHelpersParseHex, SevenDigitsAccepted)
{
    auto [val, ok] = css::ParseHex("1234567");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0x1234567u);
}

TEST(CssHelpersParseHex, NineDigitsWraps)
{
    auto [val, ok] = css::ParseHex("123456789");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0x23456789u);
}

TEST(CssHelpersParseHex, FiveDigitsAccepted)
{
    auto [val, ok] = css::ParseHex("12345");
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0x12345u);
}

TEST(CssHelpersParseHex, HexDigitG)
{
    auto [val, ok] = css::ParseHex("12g3");
    EXPECT_FALSE(ok);
}

// ==========================================================================
// Additional CompactHex / ExpandHex edge cases
// ==========================================================================

TEST(CssHelpersExpandHex, Zero)
{
    EXPECT_EQ(css::ExpandHex(0x000), 0x000000u);
}

TEST(CssHelpersExpandHex, Max)
{
    EXPECT_EQ(css::ExpandHex(0xfff), 0xffffffu);
}

TEST(CssHelpersCompactHex, MidRange)
{
    EXPECT_EQ(css::CompactHex(0x808080), 0x0808u);
}

TEST(CssHelpersCompactHex, MidNibbles)
{
    EXPECT_EQ(css::CompactHex(0x123456), 0x145u);
}

// ==========================================================================
// Additional HexChannel edge cases
// ==========================================================================

TEST(CssHelpersHexChannels, MidValues)
{
    uint32_t v = 0x8040c080u;
    EXPECT_EQ(css::HexR(v), 0x80);
    EXPECT_EQ(css::HexG(v), 0x40);
    EXPECT_EQ(css::HexB(v), 0xc0);
    EXPECT_EQ(css::HexA(v), 0x80);
}

// ==========================================================================
// Additional FloatToByte edge cases
// ==========================================================================

TEST(CssHelpersFloatToByte, Quarter)
{
    EXPECT_EQ(css::FloatToByte(0.25), 64u);
}

TEST(CssHelpersFloatToByte, ThreeQuarters)
{
    EXPECT_EQ(css::FloatToByte(0.75), 191u);
}

// ==========================================================================
// Additional FloatToStringForColor edge cases
// ==========================================================================

TEST(CssHelpersFloatToStringForColor, LargeInteger)
{
    EXPECT_EQ(css::FloatToStringForColor(255.0), "255");
}

TEST(CssHelpersFloatToStringForColor, SmallDecimal)
{
    EXPECT_EQ(css::FloatToStringForColor(0.33333), "0.333");
}

TEST(CssHelpersFloatToStringForColor, Negative)
{
    EXPECT_EQ(css::FloatToStringForColor(-1.0), "-1");
}

TEST(CssHelpersFloatToStringForColor, ManyTrailingZeros)
{
    EXPECT_EQ(css::FloatToStringForColor(1.10000), "1.1");
}

// ==========================================================================
// Additional DegreesForAngle edge cases
// ==========================================================================

TEST(CssHelpersDegreesForAngle, NegativeDegrees)
{
    auto [val, ok] = css::DegreesForAngle(MakeDimension("-90deg", 3));
    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(val, -90.0);
}

TEST(CssHelpersDegreesForAngle, ZeroDegrees)
{
    auto [val, ok] = css::DegreesForAngle(MakeDimension("0deg", 1));
    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(val, 0.0);
}

TEST(CssHelpersDegreesForAngle, UnknownUnit)
{
    auto [val, ok] = css::DegreesForAngle(MakeDimension("10px", 2));
    EXPECT_FALSE(ok);
}

// ==========================================================================
// Additional ParseAlphaByte edge cases
// ==========================================================================

TEST(CssHelpersParseAlphaByte, QuarterPercentage)
{
    auto [val, ok] = css::ParseAlphaByte(MakePercentage("25%"));
    EXPECT_TRUE(ok);
    EXPECT_TRUE(val == 63u || val == 64u);
}

TEST(CssHelpersParseAlphaByte, ThreeQuarterPercentage)
{
    auto [val, ok] = css::ParseAlphaByte(MakePercentage("75%"));
    EXPECT_TRUE(ok);
    EXPECT_TRUE(val == 191u || val == 192u);
}

TEST(CssHelpersParseAlphaByte, NumberZero)
{
    auto [val, ok] = css::ParseAlphaByte(MakeNumber("0"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0u);
}

// ==========================================================================
// Additional ParseColorByte edge cases
// ==========================================================================

TEST(CssHelpersParseColorByte, PercentageZero)
{
    auto [val, ok] = css::ParseColorByte(MakePercentage("0%"), 1.0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 0u);
}

TEST(CssHelpersParseColorByte, NumberOne)
{
    auto [val, ok] = css::ParseColorByte(MakeNumber("1"), 1.0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 1u);
}

TEST(CssHelpersParseColorByte, Scale100Percent)
{
    auto [val, ok] = css::ParseColorByte(MakePercentage("50%"), 255.0);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(val == 127u || val == 128u);
}

// ==========================================================================
// Additional LowerAlphaPercentageToNumber edge cases
// ==========================================================================

TEST(CssHelpersLowerAlphaPercentageToNumber, TwentyFivePercent)
{
    Token result = css::LowerAlphaPercentageToNumber(MakePercentage("25%"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "0.25");
}

TEST(CssHelpersLowerAlphaPercentageToNumber, SeventyFivePercent)
{
    Token result = css::LowerAlphaPercentageToNumber(MakePercentage("75%"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "0.75");
}

TEST(CssHelpersLowerAlphaPercentageToNumber, WhitespaceString)
{
    Token input;
    input.kind = TokenType::kString;
    input.text = "  ";
    Token result = css::LowerAlphaPercentageToNumber(input);
    EXPECT_EQ(result.kind, TokenType::kString);
    EXPECT_EQ(result.text, "  ");
}

// ==========================================================================
// Additional LooksLikeColor edge cases
// ==========================================================================

TEST(CssHelpersLooksLikeColor, Hash5Invalid)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeHash("12345")));
}

TEST(CssHelpersLooksLikeColor, Hash7Invalid)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeHash("1234567")));
}

TEST(CssHelpersLooksLikeColor, HashEmpty)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeHash("")));
}

TEST(CssHelpersLooksLikeColor, FunctionHwb)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeFunction("hwb")));
}

TEST(CssHelpersLooksLikeColor, FunctionLch)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeFunction("lch")));
}

TEST(CssHelpersLooksLikeColor, FunctionOklab)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeFunction("oklab")));
}

TEST(CssHelpersLooksLikeColor, FunctionVar)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeFunction("var")));
}

TEST(CssHelpersLooksLikeColor, FunctionLinearGradient)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeFunction("linear-gradient")));
}

TEST(CssHelpersLooksLikeColor, IdentCaseInsensitive)
{
    EXPECT_TRUE(css::LooksLikeColor(MakeIdent("Red")));
    EXPECT_TRUE(css::LooksLikeColor(MakeIdent("BLUE")));
}

TEST(CssHelpersLooksLikeColor, StringToken)
{
    EXPECT_FALSE(css::LooksLikeColor(MakeString("red")));
}

// ==========================================================================
// Additional IsPolar edge cases
// ==========================================================================

TEST(CssHelpersIsPolar, AllColorSpaces)
{
    // Polar color spaces
    EXPECT_TRUE(css::IsPolar(css::ColorSpace::kHsl));
    EXPECT_TRUE(css::IsPolar(css::ColorSpace::kHwb));
    EXPECT_TRUE(css::IsPolar(css::ColorSpace::kLch));
    EXPECT_TRUE(css::IsPolar(css::ColorSpace::kOklch));

    // Non-polar color spaces
    EXPECT_FALSE(css::IsPolar(css::ColorSpace::kSrgb));
    EXPECT_FALSE(css::IsPolar(css::ColorSpace::kLab));
    EXPECT_FALSE(css::IsPolar(css::ColorSpace::kOklab));
    EXPECT_FALSE(css::IsPolar(css::ColorSpace::kDisplayP3));
    EXPECT_FALSE(css::IsPolar(css::ColorSpace::kXyz));
}

// ==========================================================================
// Additional IsInvalidAnimationName edge cases
// ==========================================================================

TEST(CssHelpersIsInvalidAnimationName, Empty)
{
    EXPECT_FALSE(css::IsInvalidAnimationName(""));
}

TEST(CssHelpersIsInvalidAnimationName, Whitespace)
{
    EXPECT_FALSE(css::IsInvalidAnimationName(" "));
}

TEST(CssHelpersIsInvalidAnimationName, NumberString)
{
    EXPECT_FALSE(css::IsInvalidAnimationName("123"));
}

TEST(CssHelpersIsInvalidAnimationName, HyphenatedName)
{
    EXPECT_FALSE(css::IsInvalidAnimationName("my-animation"));
}

// ==========================================================================
// Additional MangleFontWeight edge cases
// ==========================================================================

TEST(CssHelpersMangleFontWeight, NormalUpperCase)
{
    Token result = css::MangleFontWeight(MakeIdent("NORMAL"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "400");
}

TEST(CssHelpersMangleFontWeight, BoldMixedCase)
{
    Token result = css::MangleFontWeight(MakeIdent("BoLd"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "700");
}

TEST(CssHelpersMangleFontWeight, EmptyIdent)
{
    Token result = css::MangleFontWeight(MakeIdent(""));
    EXPECT_EQ(result.kind, TokenType::kIdent);
    EXPECT_EQ(result.text, "");
}

TEST(CssHelpersMangleFontWeight, Numeric100)
{
    Token result = css::MangleFontWeight(MakeNumber("100"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "100");
}

TEST(CssHelpersMangleFontWeight, Numeric900)
{
    Token result = css::MangleFontWeight(MakeNumber("900"));
    EXPECT_EQ(result.kind, TokenType::kNumber);
    EXPECT_EQ(result.text, "900");
}

// ==========================================================================
// Additional MangleFontFamily edge cases
// ==========================================================================

TEST(CssHelpersMangleFontFamily, ThreeGenerics)
{
    std::vector<Token> result;
    std::vector<Token> input = {
        MakeIdent("serif"), MakeComma(),
        MakeIdent("sans-serif"), MakeComma(),
        MakeIdent("monospace")
    };
    EXPECT_TRUE(css::MangleFontFamily(result, input, false));
    ASSERT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0].text, "serif");
    EXPECT_EQ(result[2].text, "sans-serif");
    EXPECT_EQ(result[4].text, "monospace");
}

TEST(CssHelpersMangleFontFamily, QuotedWithInvalidCustomIdent)
{
    std::vector<Token> result;
    std::vector<Token> input = {MakeString("Arial, sans")};
    EXPECT_TRUE(css::MangleFontFamily(result, input, false));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].kind, TokenType::kString);
    EXPECT_EQ(result[0].text, "Arial, sans");
}

TEST(CssHelpersMangleFontFamily, SingleCommaFails)
{
    std::vector<Token> result;
    std::vector<Token> input = {MakeComma()};
    EXPECT_FALSE(css::MangleFontFamily(result, input, false));
}

TEST(CssHelpersMangleFontFamily, NumberThenIdentFails)
{
    std::vector<Token> result;
    std::vector<Token> input = {MakeNumber("12"), MakeIdent("Arial")};
    EXPECT_FALSE(css::MangleFontFamily(result, input, false));
}

// ==========================================================================
// MangleFont
// ==========================================================================

static Token MakeDelimSlash() {
    Token t;
    t.kind = TokenType::kDelimSlash;
    t.text = "/";
    return t;
}

TEST(CssHelpersMangleFont, FontSizeOnly)
{
    std::vector<Token> input = {MakeDimension("16px", 2)};
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].kind, TokenType::kDimension);
    EXPECT_EQ(result[0].text, "16px");
}

TEST(CssHelpersMangleFont, NormalConsumed)
{
    std::vector<Token> input = {MakeIdent("normal"), MakeDimension("16px", 2), MakeIdent("Arial")};
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].text, "16px");
    EXPECT_EQ(result[1].text, "Arial");
}

TEST(CssHelpersMangleFont, ItalicFontSize)
{
    std::vector<Token> input = {MakeIdent("italic"), MakeDimension("16px", 2)};
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].text, "italic");
    EXPECT_EQ(result[1].text, "16px");
}

TEST(CssHelpersMangleFont, BoldFontSize)
{
    std::vector<Token> input = {MakeIdent("bold"), MakeDimension("16px", 2), MakeIdent("Arial")};
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].kind, TokenType::kNumber);
    EXPECT_EQ(result[0].text, "700");
    EXPECT_EQ(result[1].text, "16px");
    EXPECT_EQ(result[2].text, "Arial");
}

TEST(CssHelpersMangleFont, FontSizeSlashLineHeight)
{
    std::vector<Token> input = {
        MakeDimension("16px", 2),
        MakeDelimSlash(),
        MakeNumber("1.5")
    };
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].text, "16px");
    EXPECT_EQ(result[1].kind, TokenType::kDelimSlash);
    EXPECT_EQ(result[2].text, "1.5");
}

TEST(CssHelpersMangleFont, FontSizeSlashLineHeightFamily)
{
    std::vector<Token> input = {
        MakeDimension("16px", 2),
        MakeDelimSlash(),
        MakeNumber("1.5"),
        MakeIdent("Arial")
    };
    auto result = css::MangleFont(input, false);
    ASSERT_GE(result.size(), 4u);
    EXPECT_EQ(result[0].text, "16px");
    EXPECT_EQ(result[1].kind, TokenType::kDelimSlash);
    EXPECT_EQ(result[2].text, "1.5");
    EXPECT_EQ(result[3].text, "Arial");
}

TEST(CssHelpersMangleFont, SlashWithNoLineHeightBails)
{
    std::vector<Token> input = {
        MakeDimension("16px", 2),
        MakeDelimSlash()
    };
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].text, "16px");
    EXPECT_EQ(result[1].kind, TokenType::kDelimSlash);
}

TEST(CssHelpersMangleFont, UnrecognizedIdentBails)
{
    std::vector<Token> input = {MakeIdent("foobar"), MakeDimension("16px", 2)};
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].text, "foobar");
    EXPECT_EQ(result[1].text, "16px");
}

TEST(CssHelpersMangleFont, NumericWeightOutOfRange)
{
    std::vector<Token> input = {MakeNumber("0"), MakeDimension("16px", 2)};
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].text, "0");
    EXPECT_EQ(result[1].text, "16px");
}

TEST(CssHelpersMangleFont, NumericWeightValid)
{
    std::vector<Token> input = {MakeNumber("300"), MakeDimension("16px", 2)};
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].text, "300");
    EXPECT_EQ(result[1].text, "16px");
}

TEST(CssHelpersMangleFont, ItalicWithAngle)
{
    std::vector<Token> input = {
        MakeIdent("oblique"),
        MakeDimension("14deg", 2),
        MakeDimension("16px", 2)
    };
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].text, "oblique");
    EXPECT_EQ(result[1].text, "14deg");
    EXPECT_EQ(result[2].text, "16px");
}

TEST(CssHelpersMangleFont, FontStretchPassThrough)
{
    std::vector<Token> input = {
        MakeIdent("condensed"),
        MakeDimension("16px", 2)
    };
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].text, "condensed");
    EXPECT_EQ(result[1].text, "16px");
}

TEST(CssHelpersMangleFont, EmptyInput)
{
    std::vector<Token> input;
    auto result = css::MangleFont(input, false);
    EXPECT_TRUE(result.empty());
}

TEST(CssHelpersMangleFont, NormalBoldItalicFontSize)
{
    std::vector<Token> input = {
        MakeIdent("normal"),
        MakeIdent("bold"),
        MakeIdent("italic"),
        MakeDimension("16px", 2),
        MakeIdent("Arial")
    };
    auto result = css::MangleFont(input, false);
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0].kind, TokenType::kNumber);
    EXPECT_EQ(result[0].text, "700");
    EXPECT_EQ(result[1].text, "italic");
    EXPECT_EQ(result[2].text, "16px");
    EXPECT_EQ(result[3].text, "Arial");
}

// ==========================================================================
// MangleTransforms
// ==========================================================================

TEST(CssHelpersMangleTransforms, MatrixToScale)
{
    // matrix(2, 0, 0, 2, 0, 0) → scale(2)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "matrix";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeNumber("2"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("2"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("0")
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "scale");
    ASSERT_EQ(result[0].children->size(), 1u);
    EXPECT_EQ((*result[0].children)[0].text, "2");
}

TEST(CssHelpersMangleTransforms, MatrixToScaleX)
{
    // matrix(3, 0, 0, 1, 0, 0) → scaleX(3)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "matrix";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeNumber("3"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("1"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("0")
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "scaleX");
    ASSERT_EQ(result[0].children->size(), 1u);
    EXPECT_EQ((*result[0].children)[0].text, "3");
}

TEST(CssHelpersMangleTransforms, MatrixToScaleY)
{
    // matrix(1, 0, 0, 4, 0, 0) → scaleY(4)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "matrix";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeNumber("1"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("4"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("0")
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "scaleY");
    ASSERT_EQ(result[0].children->size(), 1u);
    EXPECT_EQ((*result[0].children)[0].text, "4");
}

TEST(CssHelpersMangleTransforms, MatrixToScaleTwoArgs)
{
    // matrix(2, 0, 0, 3, 0, 0) → scale(2, 3)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "matrix";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeNumber("2"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("3"), MakeComma(),
        MakeNumber("0"), MakeComma(),
        MakeNumber("0")
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "scale");
    ASSERT_EQ(result[0].children->size(), 3u);
    EXPECT_EQ((*result[0].children)[0].text, "2");
    EXPECT_EQ((*result[0].children)[2].text, "3");
}

TEST(CssHelpersMangleTransforms, TranslateDropZeroY)
{
    // translate(10px, 0px) → translate(10px)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "translate";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeDimension("10px", 2), MakeComma(),
        MakeDimension("0px", 1)
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "translate");
    ASSERT_EQ(result[0].children->size(), 1u);
    EXPECT_EQ((*result[0].children)[0].text, "10px");
}

TEST(CssHelpersMangleTransforms, TranslateToTranslateY)
{
    // translate(0px, 20px) → translateY(20px)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "translate";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeDimension("0px", 1), MakeComma(),
        MakeDimension("20px", 2)
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "translateY");
    ASSERT_EQ(result[0].children->size(), 1u);
    EXPECT_EQ((*result[0].children)[0].text, "20px");
}

TEST(CssHelpersMangleTransforms, TranslateXToTranslate)
{
    // translateX(10px) → translate(10px)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "translateX";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeDimension("10px", 2)
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "translate");
    ASSERT_EQ(result[0].children->size(), 1u);
}

TEST(CssHelpersMangleTransforms, ScaleEqualArgs)
{
    // scale(2, 2) → scale(2)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "scale";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeNumber("2"), MakeComma(),
        MakeNumber("2")
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "scale");
    ASSERT_EQ(result[0].children->size(), 1u);
}

TEST(CssHelpersMangleTransforms, ScaleOneY)
{
    // scale(2, 1) → scaleX(2)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "scale";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeNumber("2"), MakeComma(),
        MakeNumber("1")
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "scaleX");
    ASSERT_EQ(result[0].children->size(), 1u);
}

TEST(CssHelpersMangleTransforms, ScaleOneX)
{
    // scale(1, 3) → scaleY(3)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "scale";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeNumber("1"), MakeComma(),
        MakeNumber("3")
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "scaleY");
    ASSERT_EQ(result[0].children->size(), 1u);
}

TEST(CssHelpersMangleTransforms, SkewDropZeroY)
{
    // skew(45deg, 0deg) → skew(45deg)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "skew";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeDimension("45deg", 2), MakeComma(),
        MakeDimension("0deg", 1)
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "skew");
    ASSERT_EQ(result[0].children->size(), 1u);
}

TEST(CssHelpersMangleTransforms, SkewXToSkew)
{
    // skewX(30deg) → skew(30deg)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "skewX";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeDimension("30deg", 2)
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "skew");
    ASSERT_EQ(result[0].children->size(), 1u);
}

TEST(CssHelpersMangleTransforms, RotateZToRotate)
{
    // rotateZ(90deg) → rotate(90deg)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "rotateZ";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeDimension("90deg", 2)
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "rotate");
    ASSERT_EQ(result[0].children->size(), 1u);
}

TEST(CssHelpersMangleTransforms, TranslateZeroZ)
{
    // translateZ(0px) → translateZ(0)
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "translateZ";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeDimension("0px", 1)
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "translateZ");
    ASSERT_EQ(result[0].children->size(), 1u);
    EXPECT_EQ((*result[0].children)[0].kind, TokenType::kNumber);
    EXPECT_EQ((*result[0].children)[0].text, "0");
}

TEST(CssHelpersMangleTransforms, NonCommaSeparatedSkipped)
{
    // Functions without comma-separated args should be left alone
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "calc";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeNumber("1"), MakeNumber("2")
    });

    std::vector<Token> tokens = {fn};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].text, "calc");
}

TEST(CssHelpersMangleTransforms, MultipleTransforms)
{
    Token t1;
    t1.kind = TokenType::kFunction;
    t1.text = "translateX";
    t1.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeDimension("10px", 2)
    });

    Token t2;
    t2.kind = TokenType::kFunction;
    t2.text = "scale";
    t2.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeNumber("2"), MakeComma(),
        MakeNumber("2")
    });

    std::vector<Token> tokens = {t1, t2};
    auto result = css::MangleTransforms(std::move(tokens));
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].text, "translate");
    EXPECT_EQ(result[1].text, "scale");
    ASSERT_EQ(result[1].children->size(), 1u);
}

// ==========================================================================
// TryToReduceCalcExpression
// ==========================================================================

TEST(CssHelpersTryToReduceCalcExpression, SimpleAddition)
{
    // calc(1px + 2px) → 3px
    Token plus;
    plus.kind = TokenType::kDelimPlus;
    plus.text = "+";
    plus.whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;

    Token dim1 = MakeDimension("1px", 1);
    dim1.whitespace = WhitespaceFlags::kWhitespaceAfter;

    Token dim2 = MakeDimension("2px", 1);
    dim2.whitespace = WhitespaceFlags::kWhitespaceBefore;

    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "calc";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        dim1, plus, dim2
    });

    auto result = css::TryToReduceCalcExpression(fn, false);
    EXPECT_EQ(result.kind, TokenType::kDimension);
    EXPECT_EQ(result.text, "3px");
}

TEST(CssHelpersTryToReduceCalcExpression, Subtraction)
{
    // calc(5px - 2px) → 3px
    Token minus;
    minus.kind = TokenType::kDelimMinus;
    minus.text = "-";
    minus.whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;

    Token dim1 = MakeDimension("5px", 1);
    dim1.whitespace = WhitespaceFlags::kWhitespaceAfter;

    Token dim2 = MakeDimension("2px", 1);
    dim2.whitespace = WhitespaceFlags::kWhitespaceBefore;

    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "calc";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        dim1, minus, dim2
    });

    auto result = css::TryToReduceCalcExpression(fn, false);
    EXPECT_EQ(result.kind, TokenType::kDimension);
    EXPECT_EQ(result.text, "3px");
}

TEST(CssHelpersTryToReduceCalcExpression, Multiplication)
{
    // calc(2 * 3px) → 6px
    Token mul;
    mul.kind = TokenType::kDelimAsterisk;
    mul.text = "*";

    Token num = MakeNumber("2");
    Token dim = MakeDimension("3px", 1);

    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "calc";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        num, mul, dim
    });

    auto result = css::TryToReduceCalcExpression(fn, false);
    EXPECT_EQ(result.kind, TokenType::kDimension);
    EXPECT_EQ(result.text, "6px");
}

TEST(CssHelpersTryToReduceCalcExpression, NoChildrenReturnsUnchanged)
{
    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "calc";

    auto result = css::TryToReduceCalcExpression(fn, false);
    EXPECT_EQ(result.kind, TokenType::kFunction);
    EXPECT_EQ(result.text, "calc");
}

TEST(CssHelpersTryToReduceCalcExpression, NonReducibleStaysCalc)
{
    // calc(1px + var(--x)) — var() makes it non-reducible
    Token plus;
    plus.kind = TokenType::kDelimPlus;
    plus.text = "+";
    plus.whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;

    Token dim = MakeDimension("1px", 1);
    dim.whitespace = WhitespaceFlags::kWhitespaceAfter;

    Token var_fn;
    var_fn.kind = TokenType::kFunction;
    var_fn.text = "var";
    var_fn.whitespace = WhitespaceFlags::kWhitespaceBefore;
    var_fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        MakeIdent("--x")
    });

    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "calc";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        dim, plus, var_fn
    });

    auto result = css::TryToReduceCalcExpression(fn, false);
    EXPECT_EQ(result.kind, TokenType::kFunction);
    EXPECT_EQ(result.text, "calc");
}

TEST(CssHelpersTryToReduceCalcExpression, MissingWhitespaceAroundPlusStaysCalc)
{
    // calc(1px+2px) — no whitespace around + is invalid per spec
    Token plus;
    plus.kind = TokenType::kDelimPlus;
    plus.text = "+";

    Token dim1 = MakeDimension("1px", 1);
    Token dim2 = MakeDimension("2px", 1);

    Token fn;
    fn.kind = TokenType::kFunction;
    fn.text = "calc";
    fn.children = std::make_shared<std::vector<Token>>(std::vector<Token>{
        dim1, plus, dim2
    });

    auto result = css::TryToReduceCalcExpression(fn, false);
    EXPECT_EQ(result.kind, TokenType::kFunction);
    EXPECT_EQ(result.text, "calc");
}

// ==========================================================================
// Additional ShortColorName / ColorNameToHex edge cases
// ==========================================================================

TEST(CssHelpersShortColorName, ContainsGray)
{
    const auto& map = css::ShortColorName();
    auto it = map.find(0x808080ff);
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, "gray");
}

TEST(CssHelpersShortColorName, ContainsGreen)
{
    const auto& map = css::ShortColorName();
    auto it = map.find(0x008000ff);
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, "green");
}

TEST(CssHelpersShortColorName, ContainsGold)
{
    const auto& map = css::ShortColorName();
    auto it = map.find(0xffd700ff);
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, "gold");
}

TEST(CssHelpersColorNameToHex, Green)
{
    const auto& map = css::ColorNameToHex();
    auto it = map.find("green");
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, 0x008000ff);
}

TEST(CssHelpersColorNameToHex, Yellow)
{
    const auto& map = css::ColorNameToHex();
    auto it = map.find("yellow");
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, 0xffff00ff);
}

TEST(CssHelpersColorNameToHex, CaseSensitive)
{
    const auto& map = css::ColorNameToHex();
    EXPECT_EQ(map.count("Red"), 0u);
    EXPECT_EQ(map.count("RED"), 0u);
    EXPECT_NE(map.count("red"), 0u);
}
