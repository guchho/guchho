#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <string>
#include <string_view>

using guchho::helpers::DetectContentType;

// ---------------------------------------------------------------------------
// DetectContentType — WHATWG MIME sniffing
// ---------------------------------------------------------------------------

// --- HTML ---

TEST(DetectContentTypeTest, DOCTYPEHtml)
{
    EXPECT_EQ(DetectContentType("<!DOCTYPE html>"), "text/html; charset=utf-8");
}

TEST(DetectContentTypeTest, HtmlTagUppercase)
{
    EXPECT_EQ(DetectContentType("<HTML>"), "text/html; charset=utf-8");
}

TEST(DetectContentTypeTest, HtmlTagLowercase)
{
    EXPECT_EQ(DetectContentType("<html>"), "text/html; charset=utf-8");
}

TEST(DetectContentTypeTest, HeadTag)
{
    EXPECT_EQ(DetectContentType("<HEAD>"), "text/html; charset=utf-8");
}

TEST(DetectContentTypeTest, BodyTag)
{
    EXPECT_EQ(DetectContentType("<BODY>"), "text/html; charset=utf-8");
}

TEST(DetectContentTypeTest, DivTag)
{
    EXPECT_EQ(DetectContentType("<DIV>"), "text/html; charset=utf-8");
}

TEST(DetectContentTypeTest, ScriptTag)
{
    EXPECT_EQ(DetectContentType("<SCRIPT>"), "text/html; charset=utf-8");
}

TEST(DetectContentTypeTest, Comment)
{
    EXPECT_EQ(DetectContentType("<!-- comment -->"), "text/html; charset=utf-8");
}

TEST(DetectContentTypeTest, LeadingWhitespace)
{
    EXPECT_EQ(DetectContentType("  \t\n<!DOCTYPE html>"), "text/html; charset=utf-8");
}

TEST(DetectContentTypeTest, PTag)
{
    EXPECT_EQ(DetectContentType("<P>"), "text/html; charset=utf-8");
}

TEST(DetectContentTypeTest, ATagNoTerminator)
{
    // "<A" without a terminating byte should NOT match HTML
    EXPECT_NE(DetectContentType("<A"), "text/html; charset=utf-8");
}

// --- XML ---

TEST(DetectContentTypeTest, XmlDeclaration)
{
    EXPECT_EQ(DetectContentType("<?xml version=\"1.0\"?>"),
              "text/xml; charset=utf-8");
}

// --- PDF ---

TEST(DetectContentTypeTest, Pdf)
{
    EXPECT_EQ(DetectContentType("%PDF-1.4 ..."), "application/pdf");
}

// --- PostScript ---

TEST(DetectContentTypeTest, PostScript)
{
    EXPECT_EQ(DetectContentType("%!PS-Adobe-3.0"), "application/postscript");
}

// --- BOMs ---

TEST(DetectContentTypeTest, Utf8Bom)
{
    char data[] = {'\xEF', '\xBB', '\xBF', 'h', 'e', 'l', 'l', 'o'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "text/plain; charset=utf-8");
}

TEST(DetectContentTypeTest, Utf16LeBom)
{
    char data[] = {'\xFF', '\xFE', 'h', '\0', 'i', '\0'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "text/plain; charset=utf-16le");
}

TEST(DetectContentTypeTest, Utf16BeBom)
{
    char data[] = {'\xFE', '\xFF', '\0', 'h', '\0', 'i'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "text/plain; charset=utf-16be");
}

// --- Images ---

TEST(DetectContentTypeTest, Png)
{
    char data[] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1A', '\n', 0, 0, 0, 0};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "image/png");
}

TEST(DetectContentTypeTest, Jpeg)
{
    char data[] = {'\xFF', '\xD8', '\xFF', '\xE0', 0, 0, 'J', 'F', 'I', 'F'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "image/jpeg");
}

TEST(DetectContentTypeTest, Gif87a)
{
    EXPECT_EQ(DetectContentType("GIF87a"), "image/gif");
}

TEST(DetectContentTypeTest, Gif89a)
{
    EXPECT_EQ(DetectContentType("GIF89a"), "image/gif");
}

TEST(DetectContentTypeTest, Bmp)
{
    EXPECT_EQ(DetectContentType("BM\x00\x00\x00\x00"), "image/bmp");
}

TEST(DetectContentTypeTest, Webp)
{
    char data[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "image/webp");
}

TEST(DetectContentTypeTest, Ico1)
{
    char data[] = {'\x00', '\x00', '\x01', '\x00'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "image/x-icon");
}

TEST(DetectContentTypeTest, Ico2)
{
    char data[] = {'\x00', '\x00', '\x02', '\x00'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "image/x-icon");
}

// --- Audio / Video ---

TEST(DetectContentTypeTest, Mp3WithId3)
{
    char data[] = {'I', 'D', '3', 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "audio/mpeg");
}

TEST(DetectContentTypeTest, Ogg)
{
    char data[] = {'O', 'g', 'g', 'S', '\x00', 0, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "application/ogg");
}

TEST(DetectContentTypeTest, Midi)
{
    char data[] = {'M', 'T', 'h', 'd', '\x00', '\x00', '\x00', '\x06',
                   0, 0, 0, 0, 0, 0};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "audio/midi");
}

TEST(DetectContentTypeTest, Wav)
{
    char data[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "audio/wave");
}

// --- MP4 (special case) ---

TEST(DetectContentTypeTest, Mp4Ftyp)
{
    // box_size=0x20 (32), "ftyp", major brand "isom", version 0, compatible brands include "mp42"
    char data[] = {
        '\x00', '\x00', '\x00', '\x20',  // box size = 32
        'f', 't', 'y', 'p',              // box type
        'i', 's', 'o', 'm',              // major brand
        '\x00', '\x00', '\x00', '\x00',  // version
        'm', 'p', '4', '2',              // compatible brand (starts with "mp4")
        'i', 's', 'o', 'm',
        'a', 'v', 'c', '1',
        'm', 'p', '4', '1',
    };
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "video/mp4");
}

TEST(DetectContentTypeTest, Mp4TooShort)
{
    // Less than 12 bytes — should not match MP4
    char data[] = {'\x00', '\x00', '\x00', '\x0C', 'f', 't', 'y', 'p'};
    EXPECT_NE(DetectContentType(std::string_view(data, sizeof(data))),
              "video/mp4");
}

// --- WebM ---

TEST(DetectContentTypeTest, Webm)
{
    char data[] = {'\x1A', '\x45', '\xDF', '\xA3', 0, 0, 0, 0};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "video/webm");
}

// --- Fonts ---

TEST(DetectContentTypeTest, Ttf)
{
    char data[] = {'\x00', '\x01', '\x00', '\x00', 0, 0, 0, 0};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "font/ttf");
}

TEST(DetectContentTypeTest, Otf)
{
    EXPECT_EQ(DetectContentType("OTTO\x00\x00\x00\x00"), "font/otf");
}

TEST(DetectContentTypeTest, Ttc)
{
    EXPECT_EQ(DetectContentType("ttcf\x00\x00\x00\x00"), "font/collection");
}

TEST(DetectContentTypeTest, Woff)
{
    EXPECT_EQ(DetectContentType("wOFF\x00\x00\x00\x00"), "font/woff");
}

TEST(DetectContentTypeTest, Woff2)
{
    EXPECT_EQ(DetectContentType("wOF2\x00\x00\x00\x00"), "font/woff2");
}

// --- Archives ---

TEST(DetectContentTypeTest, Gzip)
{
    char data[] = {'\x1F', '\x8B', '\x08', 0, 0, 0, 0, 0};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "application/x-gzip");
}

TEST(DetectContentTypeTest, Zip)
{
    EXPECT_EQ(DetectContentType("PK\x03\x04\x00\x00\x00\x00"),
              "application/zip");
}

TEST(DetectContentTypeTest, RarV4)
{
    // RAR v1.5-v4: "Rar!" followed by 0x1A 0x07 0x00
    char data[] = {'R', 'a', 'r', '!', '\x1A', '\x07', '\x00', 0};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data) - 1)),
              "application/x-rar-compressed");
}

TEST(DetectContentTypeTest, RarV5)
{
    // RAR v5+: "Rar!" followed by 0x1A 0x07 0x01 0x00
    char data[] = {'R', 'a', 'r', '!', '\x1A', '\x07', '\x01', '\x00'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "application/x-rar-compressed");
}

// --- WASM ---

TEST(DetectContentTypeTest, Wasm)
{
    char data[] = {'\x00', '\x61', '\x73', '\x6D', 0, 0, 0, 0};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "application/wasm");
}

// --- Fallbacks ---

TEST(DetectContentTypeTest, PlainText)
{
    EXPECT_EQ(DetectContentType("hello world"), "text/plain; charset=utf-8");
}

TEST(DetectContentTypeTest, PlainTextWithNewlines)
{
    EXPECT_EQ(DetectContentType("line1\nline2\nline3"),
              "text/plain; charset=utf-8");
}

TEST(DetectContentTypeTest, OctetStream)
{
    // Null bytes and control chars — not text
    char data[] = {'\x00', '\x01', '\x02', '\x03'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "application/octet-stream");
}

TEST(DetectContentTypeTest, EmptyInput)
{
    EXPECT_EQ(DetectContentType(""), "text/plain; charset=utf-8");
}

TEST(DetectContentTypeTest, SingleNullByte)
{
    char data[] = {'\x00'};
    EXPECT_EQ(DetectContentType(std::string_view(data, sizeof(data))),
              "application/octet-stream");
}

// --- Truncation at 512 bytes ---

TEST(DetectContentTypeTest, LongerThan512)
{
    // First 512 bytes are all null → octet-stream; extra bytes don't matter
    std::string data(600, '\x00');
    EXPECT_EQ(DetectContentType(data), "application/octet-stream");
}
