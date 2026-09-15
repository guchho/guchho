#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <string>
#include <string_view>

using guchho::helpers::EncodeStringAsPercentEscapedDataURL;
using guchho::helpers::EncodeStringAsShortestDataURL;
using guchho::helpers::IsDataURL;
using guchho::helpers::MimeTypeByExtension;

// ---------------------------------------------------------------------------
// IsDataURL
// ---------------------------------------------------------------------------

TEST(IsDataURLTest, SimpleDataURL)
{
    EXPECT_TRUE(IsDataURL("data:text/plain,hello"));
}

TEST(IsDataURLTest, Base64DataURL)
{
    EXPECT_TRUE(IsDataURL("data:text/plain;base64,aGVsbG8="));
}

TEST(IsDataURLTest, EmptyDataURL)
{
    EXPECT_TRUE(IsDataURL("data:,"));
}

TEST(IsDataURLTest, HTTPNotDataURL)
{
    EXPECT_FALSE(IsDataURL("http://example.com"));
}

TEST(IsDataURLTest, HTTPSNotDataURL)
{
    EXPECT_FALSE(IsDataURL("https://example.com"));
}

TEST(IsDataURLTest, FileNotDataURL)
{
    EXPECT_FALSE(IsDataURL("file:///path/to/file"));
}

TEST(IsDataURLTest, EmptyString)
{
    EXPECT_FALSE(IsDataURL(""));
}

TEST(IsDataURLTest, DataWithCapitalNotDataURL)
{
    EXPECT_FALSE(IsDataURL("Data:text/plain,hello"));
}

TEST(IsDataURLTest, DataColonSpace)
{
    EXPECT_TRUE(IsDataURL("data: text/plain,hello"));
}

TEST(IsDataURLTest, DATAUppercase)
{
    EXPECT_FALSE(IsDataURL("DATA:text/plain,hello"));
}

TEST(IsDataURLTest, JustData)
{
    EXPECT_FALSE(IsDataURL("data"));
}

TEST(IsDataURLTest, DataSemicolon)
{
    EXPECT_TRUE(IsDataURL("data:;base64,"));
}

TEST(IsDataURLTest, DataColonedAtEnd)
{
    EXPECT_TRUE(IsDataURL("data:"));
}

TEST(IsDataURLTest, PrefixSimilarNotMatch)
{
    EXPECT_FALSE(IsDataURL("edata:text/plain,hello"));
}

// ---------------------------------------------------------------------------
// MimeTypeByExtension
// ---------------------------------------------------------------------------

TEST(MimeTypeByExtensionTest, JavaScript)
{
    EXPECT_EQ(MimeTypeByExtension(".js"), "text/javascript; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, JavaScriptMJS)
{
    EXPECT_EQ(MimeTypeByExtension(".mjs"), "text/javascript; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, CSS)
{
    EXPECT_EQ(MimeTypeByExtension(".css"), "text/css; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, HTML)
{
    EXPECT_EQ(MimeTypeByExtension(".html"), "text/html; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, HTM)
{
    EXPECT_EQ(MimeTypeByExtension(".htm"), "text/html; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, JSON)
{
    EXPECT_EQ(MimeTypeByExtension(".json"), "application/json; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, Markdown)
{
    EXPECT_EQ(MimeTypeByExtension(".md"), "text/markdown; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, MarkdownLong)
{
    EXPECT_EQ(MimeTypeByExtension(".markdown"), "text/markdown; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, XML)
{
    EXPECT_EQ(MimeTypeByExtension(".xml"), "text/xml; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, XHTML)
{
    EXPECT_EQ(MimeTypeByExtension(".xhtml"), "application/xhtml+xml; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, PNG)
{
    EXPECT_EQ(MimeTypeByExtension(".png"), "image/png");
}

TEST(MimeTypeByExtensionTest, JPEG)
{
    EXPECT_EQ(MimeTypeByExtension(".jpeg"), "image/jpeg");
}

TEST(MimeTypeByExtensionTest, JPG)
{
    EXPECT_EQ(MimeTypeByExtension(".jpg"), "image/jpeg");
}

TEST(MimeTypeByExtensionTest, GIF)
{
    EXPECT_EQ(MimeTypeByExtension(".gif"), "image/gif");
}

TEST(MimeTypeByExtensionTest, SVG)
{
    EXPECT_EQ(MimeTypeByExtension(".svg"), "image/svg+xml");
}

TEST(MimeTypeByExtensionTest, AVIF)
{
    EXPECT_EQ(MimeTypeByExtension(".avif"), "image/avif");
}

TEST(MimeTypeByExtensionTest, WebP)
{
    EXPECT_EQ(MimeTypeByExtension(".webp"), "image/webp");
}

TEST(MimeTypeByExtensionTest, MP3)
{
    EXPECT_EQ(MimeTypeByExtension(".mp3"), "audio/mpeg");
}

TEST(MimeTypeByExtensionTest, WOFF)
{
    EXPECT_EQ(MimeTypeByExtension(".woff"), "font/woff");
}

TEST(MimeTypeByExtensionTest, WOFF2)
{
    EXPECT_EQ(MimeTypeByExtension(".woff2"), "font/woff2");
}

TEST(MimeTypeByExtensionTest, TTF)
{
    EXPECT_EQ(MimeTypeByExtension(".ttf"), "font/ttf");
}

TEST(MimeTypeByExtensionTest, OTF)
{
    EXPECT_EQ(MimeTypeByExtension(".otf"), "font/otf");
}

TEST(MimeTypeByExtensionTest, EOT)
{
    EXPECT_EQ(MimeTypeByExtension(".eot"), "application/vnd.ms-fontobject");
}

TEST(MimeTypeByExtensionTest, PDF)
{
    EXPECT_EQ(MimeTypeByExtension(".pdf"), "application/pdf");
}

TEST(MimeTypeByExtensionTest, WASM)
{
    EXPECT_EQ(MimeTypeByExtension(".wasm"), "application/wasm");
}

TEST(MimeTypeByExtensionTest, WebManifest)
{
    EXPECT_EQ(MimeTypeByExtension(".webmanifest"), "application/manifest+json");
}

TEST(MimeTypeByExtensionTest, CaseInsensitivePNG)
{
    EXPECT_EQ(MimeTypeByExtension(".PNG"), "image/png");
}

TEST(MimeTypeByExtensionTest, CaseInsensitiveJS)
{
    EXPECT_EQ(MimeTypeByExtension(".JS"), "text/javascript; charset=utf-8");
}

TEST(MimeTypeByExtensionTest, UnknownExtension)
{
    EXPECT_EQ(MimeTypeByExtension(".xyz"), "");
}

TEST(MimeTypeByExtensionTest, NoDot)
{
    EXPECT_EQ(MimeTypeByExtension("js"), "");
}

TEST(MimeTypeByExtensionTest, EmptyExtension)
{
    EXPECT_EQ(MimeTypeByExtension(""), "");
}

// ---------------------------------------------------------------------------
// EncodeStringAsPercentEscapedDataURL
// ---------------------------------------------------------------------------

TEST(EncodeStringAsPercentEscapedDataURLTest, SimpleASCII)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "hello world");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,hello world");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, EmptyText)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, NewlineEscaped)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "line1\nline2");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,line1%0Aline2");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, TabEscaped)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "a\tb");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,a%09b");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, CarriageReturnEscaped)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "a\rb");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,a%0Db");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, HashEscaped)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "a#b");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,a%23b");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, TrailingSpaceEscaped)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "hello ");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,hello%20");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, TrailingNewlineEscaped)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "hello\n");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,hello%0A");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, PercentSignFollowedByHexEscaped)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "a%20b");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,a%2520b");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, PercentSignAlone)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "a%bc");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,a%25bc");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, UnicodePassThrough)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "caf\xC3\xA9");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,caf\xC3\xA9");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, JavaScriptMimeType)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/javascript", "var x = 1;");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/javascript,var x = 1;");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, InvalidUTF8)
{
    std::string bad = "hello\xFFworld";
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", bad);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(url.empty());
}

TEST(EncodeStringAsPercentEscapedDataURLTest, MultipleNewlines)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "a\n\nb");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,a%0A%0Ab");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, LeadingSpacePreserved)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", " hello");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain, hello");
}

TEST(EncodeStringAsPercentEscapedDataURLTest, SpaceBetweenCharsPreserved)
{
    auto [url, ok] = EncodeStringAsPercentEscapedDataURL("text/plain", "a b c");
    EXPECT_TRUE(ok);
    EXPECT_EQ(url, "data:text/plain,a b c");
}

// ---------------------------------------------------------------------------
// EncodeStringAsShortestDataURL
// ---------------------------------------------------------------------------

TEST(EncodeStringAsShortestDataURLTest, SimpleASCII)
{
    std::string url = EncodeStringAsShortestDataURL("text/plain", "hello world");
    EXPECT_TRUE(url.find("data:text/plain,") == 0);
}

TEST(EncodeStringAsShortestDataURLTest, EmptyText)
{
    std::string url = EncodeStringAsShortestDataURL("text/plain", "");
    EXPECT_TRUE(url.find("data:text/plain,") == 0);
}

TEST(EncodeStringAsShortestDataURLTest, HighByteContent)
{
    std::string binary(256, '\xFF');
    std::string url = EncodeStringAsShortestDataURL("application/octet-stream", binary);
    EXPECT_TRUE(url.find("data:application/octet-stream;base64,") == 0);
}

TEST(EncodeStringAsShortestDataURLTest, ShortTextPrefersPercent)
{
    std::string url = EncodeStringAsShortestDataURL("text/plain", "hello");
    EXPECT_TRUE(url.find("data:text/plain,") == 0);
    EXPECT_TRUE(url.find("base64") == std::string::npos);
}

TEST(EncodeStringAsShortestDataURLTest, UnicodeText)
{
    std::string url = EncodeStringAsShortestDataURL("text/plain", "caf\xC3\xA9");
    EXPECT_TRUE(url.find("data:text/plain,") == 0);
}
