#include "test/guchho_test.hpp"

#include <cstdint>
#include <string>
#include <string_view>

// Green-only assertion coverage: every EXPECT_* / ASSERT_* macro is
// exercised with operands that satisfy the comparison, so this suite is
// all-passing by construction. It proves each macro compiles and evaluates
// correctly across the type families guchho's tests compare.

TEST(Assertions, TrueAndFalse) {
    EXPECT_TRUE(true);
    EXPECT_FALSE(false);
    EXPECT_TRUE(2 + 2 == 4);
    EXPECT_FALSE(2 + 2 == 5);
    ASSERT_TRUE(true);
    ASSERT_FALSE(false);
}

TEST(Assertions, EqualityInts) {
    EXPECT_EQ(1, 1);
    EXPECT_NE(1, 2);
    EXPECT_EQ(7 * 6, 42);
    ASSERT_EQ(40 + 2, 42);
    ASSERT_NE(7, 8);
}

TEST(Assertions, EqualityStrings) {
    EXPECT_EQ(std::string("abc"), std::string("abc"));
    EXPECT_NE(std::string("a"), std::string("b"));
    EXPECT_EQ(std::string_view("hi"), std::string_view("hi"));
    EXPECT_NE(std::string_view("x"), std::string_view("y"));
    ASSERT_EQ(std::string("same"), std::string("same"));
}

TEST(Assertions, EqualityCStrings) {
    const char* same = "hello";
    const char* other = "world";
    EXPECT_EQ(same, same);
    EXPECT_NE(same, other);
    EXPECT_EQ(nullptr, nullptr);
    ASSERT_EQ(static_cast<const char*>(nullptr), nullptr);
}

TEST(Assertions, RelationalInts) {
    EXPECT_LT(1, 2);
    EXPECT_LE(1, 1);
    EXPECT_GT(2, 1);
    EXPECT_GE(2, 2);
    ASSERT_LT(0, 1);
    ASSERT_LE(5, 5);
    ASSERT_GT(9, 8);
    ASSERT_GE(9, 9);
}

TEST(Assertions, RelationalOrdering) {
    EXPECT_LT('a', 'b');
    EXPECT_GT('Z', 'A');
    EXPECT_EQ(u'a', u'a');
    EXPECT_EQ(std::uint32_t{7}, std::uint32_t{7});
    const std::uint64_t big = std::uint64_t{1} << 62;
    EXPECT_EQ(big, big);
    EXPECT_GT(big, std::uint64_t{100});
    ASSERT_LE(std::uint32_t{1}, std::uint32_t{2});
}

TEST(Assertions, Doubles) {
    EXPECT_DOUBLE_EQ(1.5, 1.5);
    EXPECT_DOUBLE_EQ(0.1 + 0.2, 0.3);
    ASSERT_DOUBLE_EQ(2.0 / 3.0 * 3.0, 2.0);
    EXPECT_NEAR(1.0, 1.01, 0.02);
    ASSERT_NEAR(100.0, 110.0, 10.0);
}

TEST(Assertions, Streamed) {
    const int sum = 2 + 3;
    EXPECT_EQ(sum, 5) << "sum should be 5, got " << sum;
    EXPECT_TRUE(sum == 5) << "sum=" << sum;
    ASSERT_EQ(sum, 5) << "sum should be 5, got " << sum;
}

int main(int argc, char** argv) {
    return ::guchho::test::RunAll(argc, argv);
}