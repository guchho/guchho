#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <optional>
#include <string>
#include <string_view>

using guchho::helpers::URL;
using guchho::helpers::URLKind;
using guchho::helpers::ParseURL;
using guchho::helpers::ClassifyURL;
using guchho::helpers::IsExternalURL;
using guchho::helpers::IsDataURL;
using guchho::helpers::PathUnescape;
using guchho::helpers::QueryUnescape;

namespace {
    // Parses a raw URL and unwraps the optional, failing the current test
    // loudly (via EXPECT_TRUE) whenever the parser reports failure. Also used
    // to build URL structs for the String/ResolveReference tests.
    URL Parse(std::string_view s)
    {
        std::optional<URL> parsed = ParseURL(s);
        EXPECT_TRUE(parsed.has_value()) << "ParseURL(\"" << s << "\") returned nullopt";
        if (!parsed.has_value()) {
            return URL{};
        }
        return *parsed;
    }

    // Convenience wrapper: resolves `ref` against `base` and returns the
    // serialized result.
    std::string Resolve(std::string_view base, std::string_view ref)
    {
        return Parse(base).ResolveReference(Parse(ref)).String();
    }
}

// ---------------------------------------------------------------------------
// URL::String
// ---------------------------------------------------------------------------

TEST(URLStringTest, EmptyURL)
{
    EXPECT_EQ(URL{}.String(), "");
}

TEST(URLStringTest, FullURL)
{
    const URL url{"https", "example.com", "/a/b", "x=1", "top"};
    EXPECT_EQ(url.String(), "https://example.com/a/b?x=1#top");
}

TEST(URLStringTest, SchemeHostPathOnly)
{
    const URL url{"https", "example.com", "/a/b", "", ""};
    EXPECT_EQ(url.String(), "https://example.com/a/b");
}

TEST(URLStringTest, RelativePath)
{
    const URL url{"", "", "/local/file.js", "", ""};
    EXPECT_EQ(url.String(), "/local/file.js");
}

TEST(URLStringTest, SchemeWithoutHost)
{
    const URL url{"mailto", "", "user@example.com", "", ""};
    EXPECT_EQ(url.String(), "mailto:user@example.com");
}

TEST(URLStringTest, HostWithoutScheme)
{
    const URL url{"", "cdn.example.com", "/app.js", "", ""};
    EXPECT_EQ(url.String(), "//cdn.example.com/app.js");
}

TEST(URLStringTest, EmptyQueryStillEmitsQuestionMark)
{
    const URL url{"", "", "/page", "", "top"};
    EXPECT_EQ(url.String(), "/page?#top");
}

TEST(URLStringTest, PathQueryFragmentPreservedVerbatim)
{
    // Percent-encoded components go out exactly as stored.
    const URL url{"", "", "/a%20b", "x=%2f&y=a+b", ""};
    EXPECT_EQ(url.String(), "/a%20b?x=%2f&y=a+b");
}

// ---------------------------------------------------------------------------
// URL::ResolveReference
// ---------------------------------------------------------------------------

TEST(URLResolveReferenceTest, RelativeReference)
{
    EXPECT_EQ(Resolve("https://example.com/a/b", "c"), "https://example.com/a/c");
}

TEST(URLResolveReferenceTest, TrailingSlashBase)
{
    EXPECT_EQ(Resolve("https://example.com/a/b/", "c"), "https://example.com/a/b/c");
    EXPECT_EQ(Resolve("https://example.com/a/b/", "../x"), "https://example.com/a/x");
}

TEST(URLResolveReferenceTest, RootRelativeReference)
{
    EXPECT_EQ(Resolve("https://example.com/a/b", "/abs/path"), "https://example.com/abs/path");
}

TEST(URLResolveReferenceTest, DotSegmentNormalization)
{
    EXPECT_EQ(Resolve("https://example.com/a/b/c", "d/e/../f"), "https://example.com/a/b/d/f");
    EXPECT_EQ(Resolve("https://example.com/a/b/c", "../../"), "https://example.com/");
}

TEST(URLResolveReferenceTest, EmptyReferenceKeepsBase)
{
    EXPECT_EQ(Resolve("https://example.com/a/b?old=1#old", ""), "https://example.com/a/b?old=1#old");
}

TEST(URLResolveReferenceTest, AbsoluteReferenceWins)
{
    EXPECT_EQ(Resolve("https://example.com/a/b", "https://other.com/y?z#q"),
              "https://other.com/y?z#q");
}

TEST(URLResolveReferenceTest, QueryOnlyReference)
{
    // query="" ref inherits base path but replaces the query.
    EXPECT_EQ(Resolve("https://example.com/a/b", "?x=1"), "https://example.com/a/b?x=1");
}

TEST(URLResolveReferenceTest, FragmentOnlyReference)
{
    // Base has no query, so URL::String emits a '?' before the fragment.
    EXPECT_EQ(Resolve("https://example.com/a/b#old", "#sec"), "https://example.com/a/b?#sec");
    // Base query survives a fragment-only reference.
    EXPECT_EQ(Resolve("https://example.com/a/b?q=1#old", "#new"), "https://example.com/a/b?q=1#new");
}

// ---------------------------------------------------------------------------
// ParseURL
// ---------------------------------------------------------------------------

TEST(ParseURLTest, EmptyString)
{
    const URL url = Parse("");
    EXPECT_EQ(url.scheme, "");
    EXPECT_EQ(url.host, "");
    EXPECT_EQ(url.path, "");
    EXPECT_EQ(url.query, "");
    EXPECT_EQ(url.fragment, "");
}

TEST(ParseURLTest, PlainAbsoluteURL)
{
    const URL url = Parse("https://example.com/foo");
    EXPECT_EQ(url.scheme, "https");
    EXPECT_EQ(url.host, "example.com");
    EXPECT_EQ(url.path, "/foo");
    EXPECT_EQ(url.query, "");
    EXPECT_EQ(url.fragment, "");
}

TEST(ParseURLTest, QueryAndFragment)
{
    const URL url = Parse("https://user@example.com/foo?bar=1#sec");
    EXPECT_EQ(url.scheme, "https");
    EXPECT_EQ(url.host, "example.com"); // userinfo dropped
    EXPECT_EQ(url.path, "/foo");
    EXPECT_EQ(url.query, "bar=1");
    EXPECT_EQ(url.fragment, "sec");
}

TEST(ParseURLTest, SchemeLowercased)
{
    const URL url = Parse("HTTPS://EXAMPLE.COM/Path");
    EXPECT_EQ(url.scheme, "https");
    EXPECT_EQ(url.host, "EXAMPLE.COM"); // host kept as-is
    EXPECT_EQ(url.path, "/Path");
}

TEST(ParseURLTest, FileURL)
{
    const URL url = Parse("file:///tmp/file.js");
    EXPECT_EQ(url.scheme, "file");
    EXPECT_EQ(url.host, "");
    EXPECT_EQ(url.path, "/tmp/file.js");
}

TEST(ParseURLTest, HostWithPort)
{
    const URL url = Parse("https://example.com:8080/x");
    EXPECT_EQ(url.host, "example.com:8080");
    EXPECT_EQ(url.path, "/x");
}

TEST(ParseURLTest, UnderscoreSchemeFails)
{
    // '_' is not a scheme character, so no scheme prefix is recognized and
    // the whole string becomes a relative path.
    const URL url = Parse("http_foo://x");
    EXPECT_EQ(url.scheme, "");
    EXPECT_EQ(url.path, "http_foo://x");
}

TEST(ParseURLTest, SchemeStartingWithDigit)
{
    const URL url = Parse("123abc:foo");
    EXPECT_EQ(url.scheme, "");
    EXPECT_EQ(url.path, "123abc:foo");
}

TEST(ParseURLTest, RelativeKeepEncoded)
{
    const URL url = Parse("a%20b.css?v=1%2f2#frag%2F");
    EXPECT_EQ(url.path, "a%20b.css");
    EXPECT_EQ(url.query, "v=1%2f2");
    EXPECT_EQ(url.fragment, "frag%2F");
}

TEST(ParseURLTest, QueryOnlyString)
{
    const URL url = Parse("?x=1");
    EXPECT_EQ(url.path, "");
    EXPECT_EQ(url.query, "x=1");
    EXPECT_EQ(url.fragment, "");
}

TEST(ParseURLTest, FragmentOnlyString)
{
    const URL url = Parse("#sec");
    EXPECT_EQ(url.path, "");
    EXPECT_EQ(url.fragment, "sec");
}

TEST(ParseURLTest, MailtoSchemeHasNoAuthority)
{
    const URL url = Parse("mailto:user@example.com");
    EXPECT_EQ(url.scheme, "mailto");
    EXPECT_EQ(url.host, "");
    EXPECT_EQ(url.path, "user@example.com");
}

// ---------------------------------------------------------------------------
// ClassifyURL
// ---------------------------------------------------------------------------

TEST(ClassifyURLTest, Empty)
{
    EXPECT_EQ(ClassifyURL(""), URLKind::kEmpty);
}

TEST(ClassifyURLTest, Fragment)
{
    EXPECT_EQ(ClassifyURL("#top"), URLKind::kFragment);
}

TEST(ClassifyURLTest, ProtocolRelative)
{
    EXPECT_EQ(ClassifyURL("//cdn.example.com/app.js"), URLKind::kProtocolRelative);
}

TEST(ClassifyURLTest, RootRelative)
{
    EXPECT_EQ(ClassifyURL("/assets/app.js"), URLKind::kRootRelative);
}

TEST(ClassifyURLTest, Absolute)
{
    EXPECT_EQ(ClassifyURL("https://x.com"), URLKind::kAbsolute);
    EXPECT_EQ(ClassifyURL("data:text/html"), URLKind::kAbsolute);
    EXPECT_EQ(ClassifyURL("mailto:a@b"), URLKind::kAbsolute);
    EXPECT_EQ(ClassifyURL("C:\\foo"), URLKind::kAbsolute);
}

TEST(ClassifyURLTest, Relative)
{
    EXPECT_EQ(ClassifyURL("app.js"), URLKind::kRelative);
    EXPECT_EQ(ClassifyURL("app.js?v=2"), URLKind::kRelative);
    EXPECT_EQ(ClassifyURL("./a/b.css"), URLKind::kRelative);
    EXPECT_EQ(ClassifyURL("../x.css"), URLKind::kRelative);
}

// ---------------------------------------------------------------------------
// IsExternalURL
// ---------------------------------------------------------------------------

TEST(IsExternalURLTest, TrueForAbsoluteAndProtocolRelative)
{
    EXPECT_TRUE(IsExternalURL("https://x.com/a"));
    EXPECT_TRUE(IsExternalURL("data:text/html"));
    EXPECT_TRUE(IsExternalURL("//cdn.example.com/x.js"));
}

TEST(IsExternalURLTest, FalseForLocalReferences)
{
    EXPECT_FALSE(IsExternalURL(""));
    EXPECT_FALSE(IsExternalURL("#top"));
    EXPECT_FALSE(IsExternalURL("/assets/app.js"));
    EXPECT_FALSE(IsExternalURL("app.js?v=2"));
    EXPECT_FALSE(IsExternalURL("./a/b.css"));
}

// ---------------------------------------------------------------------------
// IsDataURL
// ---------------------------------------------------------------------------

TEST(IsDataURLTest, TrueForDataScheme)
{
    EXPECT_TRUE(IsDataURL("data:text/html,<b>x</b>"));
    EXPECT_TRUE(IsDataURL("data:image/png;base64,iVBORw0="));
}

TEST(IsDataURLTest, FalseForOthers)
{
    EXPECT_FALSE(IsDataURL(""));
    EXPECT_FALSE(IsDataURL("https://x.com/data"));
    EXPECT_FALSE(IsDataURL("/data"));
    EXPECT_FALSE(IsDataURL("Data:text/plain")); // case-sensitive prefix
}

// ---------------------------------------------------------------------------
// PathUnescape
// ---------------------------------------------------------------------------

TEST(PathUnescapeTest, Empty)
{
    EXPECT_EQ(PathUnescape(""), std::string{});
}

TEST(PathUnescapeTest, DecodesPercentEscapes)
{
    EXPECT_EQ(PathUnescape("/foo%20bar/baz%2Fqux"), "/foo bar/baz/qux");
    EXPECT_EQ(PathUnescape("%2f"), "/"); // lowercase hex
    EXPECT_EQ(PathUnescape("a%25b"), "a%b");
}

TEST(PathUnescapeTest, PlusStaysLiteral)
{
    EXPECT_EQ(PathUnescape("/a+b/c"), "/a+b/c");
}

TEST(PathUnescapeTest, InvalidEscapeIsError)
{
    EXPECT_FALSE(PathUnescape("/bad%2G").has_value());
    EXPECT_FALSE(PathUnescape("100%").has_value());
    EXPECT_FALSE(PathUnescape("abc%").has_value());
}

// ---------------------------------------------------------------------------
// QueryUnescape
// ---------------------------------------------------------------------------

TEST(QueryUnescapeTest, Empty)
{
    EXPECT_EQ(QueryUnescape(""), std::string{});
}

TEST(QueryUnescapeTest, DecodesPercentEscapes)
{
    EXPECT_EQ(QueryUnescape("a%20b"), "a b");
    EXPECT_EQ(QueryUnescape("hello%21"), "hello!");
}

TEST(QueryUnescapeTest, PlusBecomesSpace)
{
    EXPECT_EQ(QueryUnescape("hello+world%21"), "hello world!");
    EXPECT_EQ(QueryUnescape("a+b+c"), "a b c");
}

TEST(QueryUnescapeTest, InvalidEscapeIsError)
{
    EXPECT_FALSE(QueryUnescape("bad%2G").has_value());
    EXPECT_FALSE(QueryUnescape("a%").has_value());
}