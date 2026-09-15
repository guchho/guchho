#include "test/guchho_test.hpp"
#include "guchho/logger.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace guchho::logger;

// ---------------------------------------------------------------------------
// Loc
// ---------------------------------------------------------------------------

TEST(LocTest, DefaultIsZero)
{
    Loc loc;
    EXPECT_EQ(loc.start, 0);
}

TEST(LocTest, ExplicitValue)
{
    Loc loc{42};
    EXPECT_EQ(loc.start, 42);
}

TEST(LocTest, Equality)
{
    Loc a{10};
    Loc b{10};
    Loc c{20};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ---------------------------------------------------------------------------
// Range
// ---------------------------------------------------------------------------

TEST(RangeTest, DefaultIsZero)
{
    Range r;
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 0);
}

TEST(RangeTest, EndComputesCorrectly)
{
    Range r{Loc{10}, 5};
    EXPECT_EQ(r.End(), 15);
}

TEST(RangeTest, EndZeroLength)
{
    Range r{Loc{7}, 0};
    EXPECT_EQ(r.End(), 7);
}

TEST(RangeTest, ExpandByIncreasesRange)
{
    Range a{Loc{10}, 5};
    Range b{Loc{20}, 3};
    a.ExpandBy(b);
    EXPECT_EQ(a.loc.start, 10);
    EXPECT_EQ(a.len, 13);
}

TEST(RangeTest, ExpandByContainedRangeUnchanged)
{
    Range a{Loc{10}, 10};
    Range b{Loc{12}, 3};
    a.ExpandBy(b);
    EXPECT_EQ(a.loc.start, 10);
    EXPECT_EQ(a.len, 10);
}

TEST(RangeTest, ExpandByOverlapping)
{
    Range a{Loc{5}, 10};
    Range b{Loc{12}, 5};
    a.ExpandBy(b);
    EXPECT_EQ(a.loc.start, 5);
    EXPECT_EQ(a.len, 12);
}

TEST(RangeTest, ExpandByZeroLengthReplaces)
{
    Range a{Loc{0}, 0};
    Range b{Loc{10}, 5};
    a.ExpandBy(b);
    EXPECT_EQ(a.loc.start, 10);
    EXPECT_EQ(a.len, 5);
}

TEST(RangeTest, ExpandByEarlierRange)
{
    Range a{Loc{10}, 5};
    Range b{Loc{5}, 3};
    a.ExpandBy(b);
    EXPECT_EQ(a.loc.start, 5);
    EXPECT_EQ(a.len, 10);
}

// ---------------------------------------------------------------------------
// PrettyPaths::Select
// ---------------------------------------------------------------------------

TEST(PrettyPathsTest, SelectRelativeWhenAvailable)
{
    PrettyPaths pp{.abs = "/home/user/src/index.ts", .rel = "src/index.ts"};
    EXPECT_EQ(pp.Select(PathStyle::kRelPath), "src/index.ts");
}

TEST(PrettyPathsTest, SelectAbsoluteWhenRequested)
{
    PrettyPaths pp{.abs = "/home/user/src/index.ts", .rel = "src/index.ts"};
    EXPECT_EQ(pp.Select(PathStyle::kAbsPath), "/home/user/src/index.ts");
}

TEST(PrettyPathsTest, SelectAbsoluteWhenRelEmpty)
{
    PrettyPaths pp{.abs = "/home/user/src/index.ts", .rel = ""};
    EXPECT_EQ(pp.Select(PathStyle::kRelPath), "/home/user/src/index.ts");
}

// ---------------------------------------------------------------------------
// AllowOverride
// ---------------------------------------------------------------------------

TEST(AllowOverrideTest, NoOverrideReturnsOriginal)
{
    std::unordered_map<MsgID, LogLevel> overrides;
    MsgKind result = AllowOverride(overrides, MsgID::kJS_BigInt, MsgKind::kWarning);
    EXPECT_EQ(result, MsgKind::kWarning);
}

TEST(AllowOverrideTest, OverrideChangesKind)
{
    std::unordered_map<MsgID, LogLevel> overrides;
    overrides[MsgID::kJS_BigInt] = LogLevel::kError;
    MsgKind result = AllowOverride(overrides, MsgID::kJS_BigInt, MsgKind::kWarning);
    EXPECT_EQ(result, MsgKind::kError);
}

TEST(AllowOverrideTest, SilentSuppresses)
{
    std::unordered_map<MsgID, LogLevel> overrides;
    overrides[MsgID::kJS_BigInt] = LogLevel::kSilent;
    MsgKind result = AllowOverride(overrides, MsgID::kJS_BigInt, MsgKind::kWarning);
    EXPECT_EQ(static_cast<uint8_t>(result), 0);
}

TEST(AllowOverrideTest, OverrideOnlyAffectsSpecificID)
{
    std::unordered_map<MsgID, LogLevel> overrides;
    overrides[MsgID::kJS_BigInt] = LogLevel::kError;
    MsgKind result = AllowOverride(overrides, MsgID::kJS_DuplicateCase, MsgKind::kWarning);
    EXPECT_EQ(result, MsgKind::kWarning);
}

TEST(AllowOverrideTest, VerboseOverride)
{
    std::unordered_map<MsgID, LogLevel> overrides;
    overrides[MsgID::kJS_BigInt] = LogLevel::kVerbose;
    MsgKind result = AllowOverride(overrides, MsgID::kJS_BigInt, MsgKind::kWarning);
    EXPECT_EQ(result, MsgKind::kVerbose);
}

TEST(AllowOverrideTest, DebugOverride)
{
    std::unordered_map<MsgID, LogLevel> overrides;
    overrides[MsgID::kJS_BigInt] = LogLevel::kDebug;
    MsgKind result = AllowOverride(overrides, MsgID::kJS_BigInt, MsgKind::kWarning);
    EXPECT_EQ(result, MsgKind::kDebug);
}

TEST(AllowOverrideTest, InfoOverride)
{
    std::unordered_map<MsgID, LogLevel> overrides;
    overrides[MsgID::kJS_BigInt] = LogLevel::kInfo;
    MsgKind result = AllowOverride(overrides, MsgID::kJS_BigInt, MsgKind::kWarning);
    EXPECT_EQ(result, MsgKind::kInfo);
}

// ---------------------------------------------------------------------------
// OutputOptionsForArgs
// ---------------------------------------------------------------------------

TEST(OutputOptionsForArgsTest, DefaultValues)
{
    OutputOptions opts = OutputOptionsForArgs({});
    EXPECT_EQ(opts.log_level, LogLevel::kNone);
}

TEST(OutputOptionsForArgsTest, ColorFalse)
{
    OutputOptions opts = OutputOptionsForArgs({"--color=false"});
    EXPECT_EQ(opts.color, UseColor::kColorNever);
}

TEST(OutputOptionsForArgsTest, ColorTrue)
{
    OutputOptions opts = OutputOptionsForArgs({"--color=true"});
    EXPECT_EQ(opts.color, UseColor::kColorAlways);
}

TEST(OutputOptionsForArgsTest, LogLevelInfo)
{
    OutputOptions opts = OutputOptionsForArgs({"--log-level=info"});
    EXPECT_EQ(opts.log_level, LogLevel::kInfo);
}

TEST(OutputOptionsForArgsTest, LogLevelError)
{
    OutputOptions opts = OutputOptionsForArgs({"--log-level=error"});
    EXPECT_EQ(opts.log_level, LogLevel::kError);
}

TEST(OutputOptionsForArgsTest, LogLevelSilent)
{
    OutputOptions opts = OutputOptionsForArgs({"--log-level=silent"});
    EXPECT_EQ(opts.log_level, LogLevel::kSilent);
}

TEST(OutputOptionsForArgsTest, UnknownArgIgnored)
{
    OutputOptions opts = OutputOptionsForArgs({"--unknown-flag"});
    EXPECT_EQ(opts.log_level, LogLevel::kNone);
}

// ---------------------------------------------------------------------------
// LinkifyText
// ---------------------------------------------------------------------------

TEST(LinkifyTextTest, NoURLsReturnsUnchanged)
{
    EXPECT_EQ(LinkifyText("hello world", "\x1b[4m", "\x1b[0m"), "hello world");
}

TEST(LinkifyTextTest, EmptyUnderlineReturnsCopy)
{
    EXPECT_EQ(LinkifyText("https://example.com", "", ""), "https://example.com");
}

TEST(LinkifyTextTest, SimpleURL)
{
    std::string result = LinkifyText("See https://example.com here", "\x1b[4m", "\x1b[0m");
    EXPECT_EQ(result, "See \x1b[4mhttps://example.com\x1b[0m here");
}

TEST(LinkifyTextTest, URLAtEnd)
{
    std::string result = LinkifyText("Visit https://example.com", "\x1b[4m", "\x1b[0m");
    EXPECT_EQ(result, "Visit \x1b[4mhttps://example.com\x1b[0m");
}

TEST(LinkifyTextTest, URLWithTrailingDot)
{
    std::string result = LinkifyText("See https://example.com.", "\x1b[4m", "\x1b[0m");
    EXPECT_EQ(result, "See \x1b[4mhttps://example.com\x1b[0m.");
}

TEST(LinkifyTextTest, URLWithTrailingComma)
{
    std::string result = LinkifyText("See https://example.com,", "\x1b[4m", "\x1b[0m");
    EXPECT_EQ(result, "See \x1b[4mhttps://example.com\x1b[0m,");
}

TEST(LinkifyTextTest, MultipleURLs)
{
    std::string result = LinkifyText("A https://a.com B https://b.com C", "\x1b[4m", "\x1b[0m");
    EXPECT_EQ(result, "A \x1b[4mhttps://a.com\x1b[0m B \x1b[4mhttps://b.com\x1b[0m C");
}

TEST(LinkifyTextTest, EmptyText)
{
    EXPECT_EQ(LinkifyText("", "\x1b[4m", "\x1b[0m"), "");
}

TEST(LinkifyTextTest, URLWithTrailingQuestion)
{
    std::string result = LinkifyText("See https://example.com?", "\x1b[4m", "\x1b[0m");
    EXPECT_EQ(result, "See \x1b[4mhttps://example.com\x1b[0m?");
}

TEST(LinkifyTextTest, URLWithTrailingParen)
{
    std::string result = LinkifyText("See (https://example.com)", "\x1b[4m", "\x1b[0m");
    EXPECT_EQ(result, "See (\x1b[4mhttps://example.com\x1b[0m)");
}

// ---------------------------------------------------------------------------
// EstimateWidthInTerminal
// ---------------------------------------------------------------------------

TEST(EstimateWidthInTerminalTest, EmptyString)
{
    EXPECT_EQ(EstimateWidthInTerminal(""), 0);
}

TEST(EstimateWidthInTerminalTest, ASCIIString)
{
    EXPECT_EQ(EstimateWidthInTerminal("abc"), 3);
}

TEST(EstimateWidthInTerminalTest, SingleChar)
{
    EXPECT_EQ(EstimateWidthInTerminal("x"), 1);
}

// ---------------------------------------------------------------------------
// RenderTabStops
// ---------------------------------------------------------------------------

TEST(RenderTabStopsTest, NoTabs)
{
    EXPECT_EQ(RenderTabStops("hello", 4), "hello");
}

TEST(RenderTabStopsTest, TabAtStart)
{
    EXPECT_EQ(RenderTabStops("\t", 4), "    ");
}

TEST(RenderTabStopsTest, TabAfterContent)
{
    EXPECT_EQ(RenderTabStops("a\t", 4), "a   ");
}

TEST(RenderTabStopsTest, MultipleTabs)
{
    EXPECT_EQ(RenderTabStops("\t\t", 2), "    ");
}

TEST(RenderTabStopsTest, EmptyString)
{
    EXPECT_EQ(RenderTabStops("", 4), "");
}

// ---------------------------------------------------------------------------
// Source::TextForRange
// ---------------------------------------------------------------------------

TEST(SourceTextForRangeTest, SimpleSubstring)
{
    Source source;
    source.contents = "hello world";
    EXPECT_EQ(source.TextForRange(Range{Loc{0}, 5}), "hello");
}

TEST(SourceTextForRangeTest, MiddleSubstring)
{
    Source source;
    source.contents = "hello world";
    EXPECT_EQ(source.TextForRange(Range{Loc{6}, 5}), "world");
}

TEST(SourceTextForRangeTest, ZeroLength)
{
    Source source;
    source.contents = "hello";
    EXPECT_EQ(source.TextForRange(Range{Loc{2}, 0}), "");
}

// ---------------------------------------------------------------------------
// Source::LocBeforeWhitespace
// ---------------------------------------------------------------------------

TEST(SourceLocBeforeWhitespaceTest, NoWhitespace)
{
    Source source;
    source.contents = "hello";
    Loc result = source.LocBeforeWhitespace(Loc{5});
    EXPECT_EQ(result.start, 5);
}

TEST(SourceLocBeforeWhitespaceTest, TrailingSpaces)
{
    Source source;
    source.contents = "hello   ";
    Loc result = source.LocBeforeWhitespace(Loc{8});
    EXPECT_EQ(result.start, 5);
}

TEST(SourceLocBeforeWhitespaceTest, TrailingTab)
{
    Source source;
    source.contents = "hello\t";
    Loc result = source.LocBeforeWhitespace(Loc{6});
    EXPECT_EQ(result.start, 5);
}

TEST(SourceLocBeforeWhitespaceTest, TrailingNewline)
{
    Source source;
    source.contents = "hello\n";
    Loc result = source.LocBeforeWhitespace(Loc{6});
    EXPECT_EQ(result.start, 5);
}

// ---------------------------------------------------------------------------
// Source::RangeOfString
// ---------------------------------------------------------------------------

TEST(SourceRangeOfStringTest, DoubleQuotedString)
{
    Source source;
    source.contents = R"("hello")";
    Range r = source.RangeOfString(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 7);
}

TEST(SourceRangeOfStringTest, SingleQuotedString)
{
    Source source;
    source.contents = "'hello'";
    Range r = source.RangeOfString(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 7);
}

TEST(SourceRangeOfStringTest, StringWithEscape)
{
    Source source;
    source.contents = R"("hello\"world")";
    Range r = source.RangeOfString(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 14);
}

TEST(SourceRangeOfStringTest, TemplateLiteral)
{
    Source source;
    source.contents = "`hello`";
    Range r = source.RangeOfString(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 7);
}

TEST(SourceRangeOfStringTest, TemplateLiteralStopsAtDollarBrace)
{
    Source source;
    source.contents = "`hello ${name}`";
    Range r = source.RangeOfString(Loc{0});
    EXPECT_EQ(r.len, 0);
}

TEST(SourceRangeOfStringTest, EmptySource)
{
    Source source;
    source.contents = "";
    Range r = source.RangeOfString(Loc{0});
    EXPECT_EQ(r.len, 0);
}

TEST(SourceRangeOfStringTest, NonQuoteCharReturnsEmpty)
{
    Source source;
    source.contents = "abc";
    Range r = source.RangeOfString(Loc{0});
    EXPECT_EQ(r.len, 0);
}

// ---------------------------------------------------------------------------
// Source::RangeOfNumber
// ---------------------------------------------------------------------------

TEST(SourceRangeOfNumberTest, Integer)
{
    Source source;
    source.contents = "123";
    Range r = source.RangeOfNumber(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 3);
}

TEST(SourceRangeOfNumberTest, Float)
{
    Source source;
    source.contents = "3.14";
    Range r = source.RangeOfNumber(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 4);
}

TEST(SourceRangeOfNumberTest, HexNumber)
{
    Source source;
    source.contents = "0xff";
    Range r = source.RangeOfNumber(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 4);
}

TEST(SourceRangeOfNumberTest, UnderscoreSeparator)
{
    Source source;
    source.contents = "1_000";
    Range r = source.RangeOfNumber(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 5);
}

TEST(SourceRangeOfNumberTest, NonDigitStartsEmpty)
{
    Source source;
    source.contents = "abc";
    Range r = source.RangeOfNumber(Loc{0});
    EXPECT_EQ(r.len, 0);
}

TEST(SourceRangeOfNumberTest, TrailingNonDigit)
{
    Source source;
    source.contents = "123abc";
    Range r = source.RangeOfNumber(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 6);
}

// ---------------------------------------------------------------------------
// Source::RangeOfLegacyOctalEscape
// ---------------------------------------------------------------------------

TEST(SourceRangeOfLegacyOctalEscapeTest, ValidOctal)
{
    Source source;
    source.contents = "\\41x";
    Range r = source.RangeOfLegacyOctalEscape(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 3);
}

TEST(SourceRangeOfLegacyOctalEscapeTest, ThreeDigitOctal)
{
    Source source;
    source.contents = "\\377";
    Range r = source.RangeOfLegacyOctalEscape(Loc{0});
    EXPECT_EQ(r.loc.start, 0);
    EXPECT_EQ(r.len, 4);
}

TEST(SourceRangeOfLegacyOctalEscapeTest, NoBackslash)
{
    Source source;
    source.contents = "41x";
    Range r = source.RangeOfLegacyOctalEscape(Loc{0});
    EXPECT_EQ(r.len, 0);
}

TEST(SourceRangeOfLegacyOctalEscapeTest, BackslashOnly)
{
    Source source;
    source.contents = "\\";
    Range r = source.RangeOfLegacyOctalEscape(Loc{0});
    EXPECT_EQ(r.len, 0);
}

TEST(SourceRangeOfLegacyOctalEscapeTest, NonOctalDigit)
{
    Source source;
    source.contents = "\\8";
    Range r = source.RangeOfLegacyOctalEscape(Loc{0});
    EXPECT_EQ(r.len, 2);
}

// ---------------------------------------------------------------------------
// Source::RangeOfOperatorBefore / RangeOfOperatorAfter
// ---------------------------------------------------------------------------

TEST(SourceRangeOfOperatorBeforeTest, Found)
{
    Source source;
    source.contents = "x === y";
    Range r = source.RangeOfOperatorBefore(Loc{7}, "===");
    EXPECT_EQ(r.loc.start, 2);
    EXPECT_EQ(r.len, 3);
}

TEST(SourceRangeOfOperatorBeforeTest, NotFound)
{
    Source source;
    source.contents = "x === y";
    Range r = source.RangeOfOperatorBefore(Loc{7}, ">>>");
    EXPECT_EQ(r.len, 0);
}

TEST(SourceRangeOfOperatorAfterTest, Found)
{
    Source source;
    source.contents = "x => y";
    Range r = source.RangeOfOperatorAfter(Loc{2}, "=>");
    EXPECT_EQ(r.loc.start, 2);
    EXPECT_EQ(r.len, 2);
}

TEST(SourceRangeOfOperatorAfterTest, NotFound)
{
    Source source;
    source.contents = "x => y";
    Range r = source.RangeOfOperatorAfter(Loc{2}, "==");
    EXPECT_EQ(r.len, 0);
}

// ---------------------------------------------------------------------------
// Source::CommentTextWithoutIndent
// ---------------------------------------------------------------------------

TEST(CommentTextWithoutIndentTest, NonBlockCommentReturnedAsIs)
{
    Source source;
    source.contents = "// hello";
    Range r{Loc{0}, 8};
    EXPECT_EQ(source.CommentTextWithoutIndent(r), "// hello");
}

TEST(CommentTextWithoutIndentTest, EmptyComment)
{
    Source source;
    source.contents = "/**/";
    Range r{Loc{0}, 4};
    std::string result = source.CommentTextWithoutIndent(r);
    EXPECT_TRUE(result.find("/*") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Source::operator==
// ---------------------------------------------------------------------------

TEST(SourceEqualityTest, EqualSources)
{
    Source a;
    a.pretty_paths = {"/src/test.js", "src/test.js"};
    a.contents = "hello";
    a.index = 0;

    Source b;
    b.pretty_paths = {"/src/test.js", "src/test.js"};
    b.contents = "hello";
    b.index = 0;

    EXPECT_EQ(a, b);
}

TEST(SourceEqualityTest, DifferentContents)
{
    Source a;
    a.contents = "hello";
    Source b;
    b.contents = "world";
    EXPECT_NE(a, b);
}

TEST(SourceEqualityTest, DifferentPaths)
{
    Source a;
    a.pretty_paths = {"/a.js", "a.js"};
    Source b;
    b.pretty_paths = {"/b.js", "b.js"};
    EXPECT_NE(a, b);
}

// ---------------------------------------------------------------------------
// Path
// ---------------------------------------------------------------------------

TEST(PathTest, DefaultIsNotDisabled)
{
    Path p;
    EXPECT_FALSE(p.IsDisabled());
}

TEST(PathTest, DisabledFlag)
{
    Path p;
    p.flags = PathFlags::kPathDisabled;
    EXPECT_TRUE(p.IsDisabled());
}

TEST(PathTest, Equality)
{
    Path a{"foo.js", "", "", {}, PathFlags{}};
    Path b{"foo.js", "", "", {}, PathFlags{}};
    Path c{"bar.js", "", "", {}, PathFlags{}};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(PathTest, NamespaceAffectsEquality)
{
    Path a{"foo.js", "npm", "", {}, PathFlags{}};
    Path b{"foo.js", "", "", {}, PathFlags{}};
    EXPECT_NE(a, b);
}

TEST(PathTest, IgnoredSuffixAffectsEquality)
{
    Path a{"foo.js", "", "?query", {}, PathFlags{}};
    Path b{"foo.js", "", "", {}, PathFlags{}};
    EXPECT_NE(a, b);
}

TEST(PathTest, FlagsAffectsEquality)
{
    Path a{"foo.js", "", "", {}, PathFlags::kPathDisabled};
    Path b{"foo.js", "", "", {}, PathFlags{}};
    EXPECT_NE(a, b);
}

// ---------------------------------------------------------------------------
// PathHash
// ---------------------------------------------------------------------------

TEST(PathHashTest, SamePathSameHash)
{
    PathHash hasher;
    Path a{"foo.js", "", "", {}, PathFlags{}};
    Path b{"foo.js", "", "", {}, PathFlags{}};
    EXPECT_EQ(hasher(a), hasher(b));
}

TEST(PathHashTest, DifferentPathDifferentHash)
{
    PathHash hasher;
    Path a{"foo.js", "", "", {}, PathFlags{}};
    Path b{"bar.js", "", "", {}, PathFlags{}};
    EXPECT_NE(hasher(a), hasher(b));
}

// ---------------------------------------------------------------------------
// ImportAttributes
// ---------------------------------------------------------------------------

TEST(ImportAttributesTest, EmptyDataDecodesToEmpty)
{
    ImportAttributes attr;
    auto arr = attr.DecodeIntoArray();
    EXPECT_TRUE(arr.empty());
}

TEST(ImportAttributesTest, EmptyDataDecodesToEmptyMap)
{
    ImportAttributes attr;
    auto map = attr.DecodeIntoMap();
    EXPECT_TRUE(map.empty());
}

// ---------------------------------------------------------------------------
// EncodeImportAttributes / DecodeIntoArray roundtrip
// ---------------------------------------------------------------------------

TEST(ImportAttributesTest, EncodeDecodeRoundtrip)
{
    std::unordered_map<std::string, std::string> original;
    original["type"] = "json";
    original["loader"] = "ts";

    ImportAttributes encoded = EncodeImportAttributes(original);
    auto decoded = encoded.DecodeIntoMap();

    EXPECT_EQ(decoded.size(), 2u);
    EXPECT_EQ(decoded["type"], "json");
    EXPECT_EQ(decoded["loader"], "ts");
}

TEST(ImportAttributesTest, EmptyMapEncodesEmpty)
{
    std::unordered_map<std::string, std::string> empty;
    ImportAttributes encoded = EncodeImportAttributes(empty);
    EXPECT_TRUE(encoded.packed_data.empty());
}

TEST(ImportAttributesTest, EncodeDecodeSingleEntry)
{
    std::unordered_map<std::string, std::string> original;
    original["with"] = "json";

    ImportAttributes encoded = EncodeImportAttributes(original);
    auto arr = encoded.DecodeIntoArray();

    EXPECT_EQ(arr.size(), 1u);
    EXPECT_EQ(arr[0].key, "with");
    EXPECT_EQ(arr[0].value, "json");
}

// ---------------------------------------------------------------------------
// LineColumnTracker
// ---------------------------------------------------------------------------

TEST(LineColumnTrackerTest, NoSourceReturnsNull)
{
    LineColumnTracker tracker;
    auto loc = tracker.MsgLocationOrNil(Range{Loc{0}, 1});
    EXPECT_EQ(loc, nullptr);
}

TEST(LineColumnTrackerTest, BasicLineColumn)
{
    Source source;
    source.contents = "hello\nworld\n";
    source.pretty_paths = {"/src/test.js", "src/test.js"};

    LineColumnTracker tracker(&source);
    auto loc = tracker.MsgLocationOrNil(Range{Loc{7}, 3});
    ASSERT_TRUE(loc != nullptr);
    EXPECT_EQ(loc->line, 2);
    EXPECT_EQ(loc->column, 1);
    EXPECT_EQ(loc->length, 3);
}

TEST(LineColumnTrackerTest, FirstLine)
{
    Source source;
    source.contents = "hello world";
    source.pretty_paths = {"/src/test.js", "src/test.js"};

    LineColumnTracker tracker(&source);
    auto loc = tracker.MsgLocationOrNil(Range{Loc{0}, 5});
    ASSERT_TRUE(loc != nullptr);
    EXPECT_EQ(loc->line, 1);
    EXPECT_EQ(loc->column, 0);
    EXPECT_EQ(loc->length, 5);
}

TEST(LineColumnTrackerTest, MakeMsgData)
{
    Source source;
    source.contents = "hello\nworld";
    source.pretty_paths = {"/src/test.js", "src/test.js"};

    LineColumnTracker tracker(&source);
    MsgData data = tracker.MakeMsgData(Range{Loc{7}, 3}, "test message");
    EXPECT_EQ(data.text, "test message");
    ASSERT_TRUE(data.location != nullptr);
    EXPECT_EQ(data.location->line, 2);
    EXPECT_EQ(data.location->column, 1);
}

TEST(LineColumnTrackerTest, MultipleQueries)
{
    Source source;
    source.contents = "line1\nline2\nline3\n";
    source.pretty_paths = {"/src/test.js", "src/test.js"};

    LineColumnTracker tracker(&source);

    auto loc1 = tracker.MsgLocationOrNil(Range{Loc{0}, 5});
    ASSERT_TRUE(loc1 != nullptr);
    EXPECT_EQ(loc1->line, 1);
    EXPECT_EQ(loc1->column, 0);

    auto loc2 = tracker.MsgLocationOrNil(Range{Loc{6}, 5});
    ASSERT_TRUE(loc2 != nullptr);
    EXPECT_EQ(loc2->line, 2);
    EXPECT_EQ(loc2->column, 0);

    auto loc3 = tracker.MsgLocationOrNil(Range{Loc{12}, 5});
    ASSERT_TRUE(loc3 != nullptr);
    EXPECT_EQ(loc3->line, 3);
    EXPECT_EQ(loc3->column, 0);
}

TEST(LineColumnTrackerTest, TracksFilePath)
{
    Source source;
    source.contents = "x";
    source.pretty_paths = {"/abs/path.js", "rel/path.js"};

    LineColumnTracker tracker(&source);
    auto loc = tracker.MsgLocationOrNil(Range{Loc{0}, 1});
    ASSERT_TRUE(loc != nullptr);
    EXPECT_EQ(loc->file.abs, "/abs/path.js");
    EXPECT_EQ(loc->file.rel, "rel/path.js");
}

TEST(LineColumnTrackerTest, WindowsLineEndings)
{
    Source source;
    source.contents = "line1\r\nline2\r\n";
    source.pretty_paths = {"/test.js", "test.js"};

    LineColumnTracker tracker(&source);
    auto loc = tracker.MsgLocationOrNil(Range{Loc{7}, 3});
    ASSERT_TRUE(loc != nullptr);
    EXPECT_EQ(loc->line, 2);
    EXPECT_EQ(loc->column, 0);
}

// ---------------------------------------------------------------------------
// Msg::String
// ---------------------------------------------------------------------------

TEST(MsgStringTest, SimpleErrorWithoutSource)
{
    Msg msg;
    msg.kind = MsgKind::kError;
    msg.data.text = "something went wrong";

    OutputOptions opts;
    opts.include_source = false;
    TerminalInfo term;

    std::string result = msg.String(opts, term);
    EXPECT_TRUE(result.find("ERROR") != std::string::npos);
    EXPECT_TRUE(result.find("something went wrong") != std::string::npos);
}

TEST(MsgStringTest, WarningWithID)
{
    Msg msg;
    msg.kind = MsgKind::kWarning;
    msg.id = MsgID::kJS_BigInt;
    msg.data.text = "BigInt not supported";

    OutputOptions opts;
    opts.include_source = false;
    TerminalInfo term;

    std::string result = msg.String(opts, term);
    EXPECT_TRUE(result.find("WARNING") != std::string::npos);
    EXPECT_TRUE(result.find("BigInt not supported") != std::string::npos);
}

TEST(MsgStringTest, MessageWithNotes)
{
    Msg msg;
    msg.kind = MsgKind::kError;
    msg.data.text = "primary error";

    MsgData note;
    note.text = "additional context";
    msg.notes.push_back(note);

    OutputOptions opts;
    opts.include_source = false;
    TerminalInfo term;

    std::string result = msg.String(opts, term);
    EXPECT_TRUE(result.find("primary error") != std::string::npos);
    EXPECT_TRUE(result.find("additional context") != std::string::npos);
}

TEST(MsgStringTest, InfoMessage)
{
    Msg msg;
    msg.kind = MsgKind::kInfo;
    msg.data.text = "building...";

    OutputOptions opts;
    opts.include_source = false;
    TerminalInfo term;

    std::string result = msg.String(opts, term);
    EXPECT_TRUE(result.find("INFO") != std::string::npos);
    EXPECT_TRUE(result.find("building...") != std::string::npos);
}

TEST(MsgStringTest, PluginName)
{
    Msg msg;
    msg.kind = MsgKind::kWarning;
    msg.data.text = "plugin warning";
    msg.plugin_name = "my-plugin";

    OutputOptions opts;
    opts.include_source = true;
    TerminalInfo term;

    std::string result = msg.String(opts, term);
    EXPECT_TRUE(result.find("plugin warning") != std::string::npos);
    EXPECT_TRUE(result.find("my-plugin") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MsgString (free function)
// ---------------------------------------------------------------------------

TEST(MsgStringFreeTest, WithoutSource)
{
    MsgData data;
    data.text = "undeclared var";

    OutputOptions opts;
    opts.include_source = false;
    TerminalInfo term;

    std::string result = MsgString(false, PathStyle::kRelPath, term, MsgID::kNone, MsgKind::kError, data, "");
    EXPECT_TRUE(result.find("ERROR") != std::string::npos);
    EXPECT_TRUE(result.find("undeclared var") != std::string::npos);
}

TEST(MsgStringFreeTest, WithID)
{
    MsgData data;
    data.text = "duplicate key";

    OutputOptions opts;
    opts.include_source = false;
    TerminalInfo term;

    std::string result = MsgString(true, PathStyle::kRelPath, term, MsgID::kJS_DuplicateObjectKey, MsgKind::kWarning, data, "");
    EXPECT_TRUE(result.find("[duplicate-object-key]") != std::string::npos);
}

// ---------------------------------------------------------------------------
// DeferLog
// ---------------------------------------------------------------------------

TEST(DeferLogTest, CollectsMessages)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogAll, {});

    Msg msg;
    msg.kind = MsgKind::kWarning;
    msg.data.text = "test warning";
    log.add_msg(msg);

    auto msgs = log.peek();
    EXPECT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].data.text, "test warning");
}

TEST(DeferLogTest, HasErrorsReturnsFalseInitially)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    EXPECT_FALSE(log.has_errors());
}

TEST(DeferLogTest, HasErrorsReturnsTrueAfterError)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogAll, {});

    Msg msg;
    msg.kind = MsgKind::kError;
    msg.data.text = "fatal error";
    log.add_msg(msg);

    EXPECT_TRUE(log.has_errors());
}

TEST(DeferLogTest, DoneReturnsSortedMessages)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogAll, {});

    Msg m1;
    m1.kind = MsgKind::kWarning;
    m1.data.text = "second";
    log.add_msg(m1);

    Msg m2;
    m2.kind = MsgKind::kError;
    m2.data.text = "first";
    log.add_msg(m2);

    auto msgs = log.done();
    EXPECT_EQ(msgs.size(), 2u);
}

TEST(DeferLogTest, NoVerboseOrDebugDropsVerbose)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});

    Msg msg;
    msg.kind = MsgKind::kVerbose;
    msg.data.text = "verbose info";
    log.add_msg(msg);

    auto msgs = log.peek();
    EXPECT_TRUE(msgs.empty());
}

TEST(DeferLogTest, NoVerboseOrDebugDropsDebug)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});

    Msg msg;
    msg.kind = MsgKind::kDebug;
    msg.data.text = "debug info";
    log.add_msg(msg);

    auto msgs = log.peek();
    EXPECT_TRUE(msgs.empty());
}

TEST(DeferLogTest, NoVerboseOrDebugKeepsInfo)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});

    Msg msg;
    msg.kind = MsgKind::kInfo;
    msg.data.text = "info message";
    log.add_msg(msg);

    auto msgs = log.peek();
    EXPECT_EQ(msgs.size(), 1u);
}

TEST(DeferLogTest, NoVerboseOrDebugKeepsWarning)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});

    Msg msg;
    msg.kind = MsgKind::kWarning;
    msg.data.text = "warning message";
    log.add_msg(msg);

    auto msgs = log.peek();
    EXPECT_EQ(msgs.size(), 1u);
}

TEST(DeferLogTest, NoVerboseOrDebugKeepsError)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});

    Msg msg;
    msg.kind = MsgKind::kError;
    msg.data.text = "error message";
    log.add_msg(msg);

    auto msgs = log.peek();
    EXPECT_EQ(msgs.size(), 1u);
}

TEST(DeferLogTest, OverrideSuppressesMessage)
{
    std::unordered_map<MsgID, LogLevel> overrides;
    overrides[MsgID::kJS_BigInt] = LogLevel::kSilent;

    Log log = NewDeferLog(DeferLogKind::kDeferLogAll, overrides);
    log.level = LogLevel::kInfo;

    Msg msg;
    msg.kind = MsgKind::kWarning;
    msg.id = MsgID::kJS_BigInt;
    msg.data.text = "BigInt warning";
    log.add_msg(msg);

    auto msgs = log.peek();
    EXPECT_TRUE(msgs.empty());
}

// ---------------------------------------------------------------------------
// Log::AddError / AddID
// ---------------------------------------------------------------------------

TEST(LogAddErrorTest, ErrorRecorded)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    log.AddError(nullptr, Range{Loc{0}, 5}, "syntax error");
    auto msgs = log.peek();
    EXPECT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].data.text, "syntax error");
    EXPECT_EQ(msgs[0].kind, MsgKind::kError);
}

TEST(LogAddIDTest, WarningRecorded)
{
    Log log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    log.AddID(MsgID::kJS_BigInt, MsgKind::kWarning, nullptr, Range{Loc{0}, 3}, "BigInt");
    auto msgs = log.peek();
    EXPECT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].data.text, "BigInt");
    EXPECT_EQ(msgs[0].id, MsgID::kJS_BigInt);
}

TEST(LogAddIDTest, SilentOverrideDrops)
{
    std::unordered_map<MsgID, LogLevel> overrides;
    overrides[MsgID::kJS_BigInt] = LogLevel::kSilent;

    Log log = NewDeferLog(DeferLogKind::kDeferLogAll, overrides);
    log.AddID(MsgID::kJS_BigInt, MsgKind::kWarning, nullptr, Range{Loc{0}, 3}, "BigInt");
    auto msgs = log.peek();
    EXPECT_TRUE(msgs.empty());
}

// ---------------------------------------------------------------------------
// SummaryTableEntry
// ---------------------------------------------------------------------------

TEST(SummaryTableEntryTest, Defaults)
{
    SummaryTableEntry entry;
    EXPECT_TRUE(entry.dir.empty());
    EXPECT_TRUE(entry.base.empty());
    EXPECT_TRUE(entry.size.empty());
    EXPECT_EQ(entry.bytes, 0);
    EXPECT_FALSE(entry.is_source_map);
}

// ---------------------------------------------------------------------------
// MsgLocation
// ---------------------------------------------------------------------------

TEST(MsgLocationTest, Defaults)
{
    MsgLocation loc;
    EXPECT_EQ(loc.line, 0);
    EXPECT_EQ(loc.column, 0);
    EXPECT_EQ(loc.length, 0);
    EXPECT_TRUE(loc.line_text.empty());
    EXPECT_TRUE(loc.suggestion.empty());
}

// ---------------------------------------------------------------------------
// MsgData
// ---------------------------------------------------------------------------

TEST(MsgDataTest, Defaults)
{
    MsgData data;
    EXPECT_EQ(data.user_detail, nullptr);
    EXPECT_TRUE(data.text.empty());
    EXPECT_FALSE(data.disable_maximum_width);
}

// ---------------------------------------------------------------------------
// MsgDetail
// ---------------------------------------------------------------------------

TEST(MsgDetailTest, Defaults)
{
    MsgDetail detail;
    EXPECT_TRUE(detail.source_before.empty());
    EXPECT_TRUE(detail.source_marked.empty());
    EXPECT_TRUE(detail.source_after.empty());
    EXPECT_TRUE(detail.indent.empty());
    EXPECT_TRUE(detail.marker.empty());
    EXPECT_TRUE(detail.suggestion.empty());
    EXPECT_TRUE(detail.content_after.empty());
    EXPECT_TRUE(detail.path.empty());
    EXPECT_EQ(detail.line, 0);
    EXPECT_EQ(detail.column, 0);
}

// ---------------------------------------------------------------------------
// PlatformIndependentPathDirBaseExt
// ---------------------------------------------------------------------------

TEST(PlatformIndependentPathDirBaseExtTest, UnixPath)
{
    std::string dir, base, ext;
    PlatformIndependentPathDirBaseExt("src/utils/index.ts", dir, base, ext);
    EXPECT_EQ(base, "index");
    EXPECT_EQ(ext, ".ts");
}

TEST(PlatformIndependentPathDirBaseExtTest, NoExtension)
{
    std::string dir, base, ext;
    PlatformIndependentPathDirBaseExt("README", dir, base, ext);
    EXPECT_EQ(base, "README");
    EXPECT_TRUE(ext.empty());
}

TEST(PlatformIndependentPathDirBaseExtTest, DotModuleCSS)
{
    std::string dir, base, ext;
    PlatformIndependentPathDirBaseExt("src/Button.module.css", dir, base, ext);
    EXPECT_EQ(base, "Button");
    EXPECT_EQ(ext, ".module.css");
}
