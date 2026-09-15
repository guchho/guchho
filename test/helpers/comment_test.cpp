#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <string_view>

using guchho::helpers::EscapeClosingTag;

// ---------------------------------------------------------------------------
// EscapeClosingTag
// ---------------------------------------------------------------------------

TEST(EscapeClosingTagTest, EmptyText)
{
    EXPECT_EQ(EscapeClosingTag("", "script"), "");
}

TEST(EscapeClosingTagTest, EmptySlashTagReturnsUnchanged)
{
    EXPECT_EQ(EscapeClosingTag("var s = '</script>';", ""), "var s = '</script>';");
}

TEST(EscapeClosingTagTest, NoClosingTag)
{
    EXPECT_EQ(EscapeClosingTag("var s = 'hello';", "script"), "var s = 'hello';");
}

TEST(EscapeClosingTagTest, ClosingTagEscaped)
{
    EXPECT_EQ(EscapeClosingTag("var s = '</script>';", "script"), "var s = '<\\/script>';");
}

TEST(EscapeClosingTagTest, CaseInsensitiveMatch)
{
    EXPECT_EQ(EscapeClosingTag("var s = '</Script>';", "script"), "var s = '<\\/Script>';");
}

TEST(EscapeClosingTagTest, CaseInsensitiveUpperCase)
{
    EXPECT_EQ(EscapeClosingTag("var s = '</SCRIPT>';", "script"), "var s = '<\\/SCRIPT>';");
}

TEST(EscapeClosingTagTest, MultipleOccurrences)
{
    EXPECT_EQ(
        EscapeClosingTag("</script> and </script>", "script"),
        "<\\/script> and <\\/script>");
}

TEST(EscapeClosingTagTest, DifferentTagNameNotEscaped)
{
    EXPECT_EQ(EscapeClosingTag("var s = '</style>';", "script"), "var s = '</style>';");
}

TEST(EscapeClosingTagTest, PartialMatchNotEscaped)
{
    EXPECT_EQ(EscapeClosingTag("var s = '</scripts>';", "script"), "var s = '</scripts>';");
}

TEST(EscapeClosingTagTest, NoSlashBeforeTag)
{
    EXPECT_EQ(EscapeClosingTag("<script>", "script"), "<script>");
}

TEST(EscapeClosingTagTest, SlashWithoutClosingBracket)
{
    EXPECT_EQ(EscapeClosingTag("</script", "script"), "</script");
}

TEST(EscapeClosingTagTest, AdjacentToOtherText)
{
    EXPECT_EQ(EscapeClosingTag("a</script>b", "script"), "a<\\/script>b");
}

TEST(EscapeClosingTagTest, StyleTag)
{
    EXPECT_EQ(EscapeClosingTag("var s = '</style>';", "style"), "var s = '<\\/style>';");
}

TEST(EscapeClosingTagTest, StyleTagCaseInsensitive)
{
    EXPECT_EQ(EscapeClosingTag("var s = '</STYLE>';", "style"), "var s = '<\\/STYLE>';");
}

TEST(EscapeClosingTagTest, EmptyTagContent)
{
    EXPECT_EQ(EscapeClosingTag("</>", "script"), "</>");
}

TEST(EscapeClosingTagTest, SlashTagLongerThanRemainder)
{
    EXPECT_EQ(EscapeClosingTag("</scr", "script"), "</scr");
}

TEST(EscapeClosingTagTest, SingleCharTag)
{
    EXPECT_EQ(EscapeClosingTag("</p>", "p"), "<\\/p>");
}

TEST(EscapeClosingTagTest, PreservesSurroundingContent)
{
    EXPECT_EQ(
        EscapeClosingTag("before </script> after", "script"),
        "before <\\/script> after");
}

TEST(EscapeClosingTagTest, BackslashBeforeSlashNotEscaped)
{
    EXPECT_EQ(EscapeClosingTag("\\</script>", "script"), "\\<\\/script>");
}
