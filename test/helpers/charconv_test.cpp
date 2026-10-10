#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <cmath>

using guchho::helpers::ParseDouble;

// ---------------------------------------------------------------------------
// ParseDouble
// ---------------------------------------------------------------------------

TEST(ParseDoubleTest, AcceptsPlainDecimals)
{
    double value = -1;
    EXPECT_TRUE(ParseDouble("0", &value));
    EXPECT_DOUBLE_EQ(value, 0.0);

    EXPECT_TRUE(ParseDouble("3.14", &value));
    EXPECT_DOUBLE_EQ(value, 3.14);

    EXPECT_TRUE(ParseDouble("100", &value));
    EXPECT_DOUBLE_EQ(value, 100.0);

    EXPECT_TRUE(ParseDouble("-50", &value));
    EXPECT_DOUBLE_EQ(value, -50.0);

    EXPECT_TRUE(ParseDouble("+2.5", &value));
    EXPECT_DOUBLE_EQ(value, 2.5);

    EXPECT_TRUE(ParseDouble(".5", &value));
    EXPECT_DOUBLE_EQ(value, 0.5);
}

TEST(ParseDoubleTest, AcceptsExponents)
{
    double value = -1;
    EXPECT_TRUE(ParseDouble("1e3", &value));
    EXPECT_DOUBLE_EQ(value, 1000.0);

    EXPECT_TRUE(ParseDouble("1E3", &value));
    EXPECT_DOUBLE_EQ(value, 1000.0);

    EXPECT_TRUE(ParseDouble("1.5e-3", &value));
    EXPECT_DOUBLE_EQ(value, 0.0015);

    EXPECT_TRUE(ParseDouble("1e20", &value));
    EXPECT_DOUBLE_EQ(value, 1e20);
}

TEST(ParseDoubleTest, AcceptsRangeBoundaries)
{
    double value = -1;
    EXPECT_TRUE(ParseDouble("1.7976931348623157e308", &value));
    EXPECT_DOUBLE_EQ(value, 1.7976931348623157e308);

    // The smallest positive subnormal is still representable, so it must be
    // accepted even though strtod reports it through ERANGE.
    EXPECT_TRUE(ParseDouble("5e-324", &value));
    EXPECT_EQ(value, 5e-324);

    EXPECT_TRUE(ParseDouble("-0.0", &value));
    EXPECT_TRUE(std::signbit(value));
}

TEST(ParseDoubleTest, AcceptsLongInput)
{
    // Longer than the on-stack buffer in ParseDouble, exercising the heap path.
    double value = -1;
    EXPECT_TRUE(ParseDouble("3.1415926535897932384626433832795", &value));
    EXPECT_DOUBLE_EQ(value, 3.141592653589793);
}

TEST(ParseDoubleTest, AcceptsInfinityAndNotANumber)
{
    double value = -1;
    EXPECT_TRUE(ParseDouble("inf", &value));
    EXPECT_TRUE(std::isinf(value));

    EXPECT_TRUE(ParseDouble("-inf", &value));
    EXPECT_TRUE(std::isinf(value) && value < 0);

    EXPECT_TRUE(ParseDouble("nan", &value));
    EXPECT_TRUE(std::isnan(value));
}

TEST(ParseDoubleTest, RejectsEmptyAndWhitespace)
{
    double value = -1;
    EXPECT_FALSE(ParseDouble("", &value));
    EXPECT_FALSE(ParseDouble(" ", &value));
    EXPECT_FALSE(ParseDouble(" 1.5", &value));
    EXPECT_FALSE(ParseDouble("1.5 ", &value));
    EXPECT_EQ(value, -1);
}

TEST(ParseDoubleTest, RejectsPartialMatches)
{
    double value = -1;
    EXPECT_FALSE(ParseDouble("1.5px", &value));
    EXPECT_FALSE(ParseDouble("42.5deg", &value));
    EXPECT_FALSE(ParseDouble("abc", &value));
    EXPECT_FALSE(ParseDouble(".", &value));
    EXPECT_FALSE(ParseDouble("-", &value));
    EXPECT_FALSE(ParseDouble("1e", &value));
    EXPECT_FALSE(ParseDouble("1e+", &value));
    EXPECT_EQ(value, -1);
}

TEST(ParseDoubleTest, RejectsOutOfRangeValues)
{
    double value = -1;
    // Overflow to infinity.
    EXPECT_FALSE(ParseDouble("1e400", &value));
    // Underflow all the way to zero.
    EXPECT_FALSE(ParseDouble("1e-400", &value));
    EXPECT_EQ(value, -1);
}

TEST(ParseDoubleTest, RejectsHexFloats)
{
    // std::from_chars has no hex-float support, so these must keep failing
    // even though strtod would accept them.
    double value = -1;
    EXPECT_FALSE(ParseDouble("0x10", &value));
    EXPECT_FALSE(ParseDouble("-0x10", &value));
    EXPECT_FALSE(ParseDouble("0x", &value));
    EXPECT_EQ(value, -1);
}

TEST(ParseDoubleTest, HandlesNullOutputPointer)
{
    EXPECT_FALSE(ParseDouble("1.5", nullptr));
}
