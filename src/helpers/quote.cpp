#include "guchho/helpers.hpp"
#include "guchho/unicode.hpp"

#include <array>
#include <cstdint>
#include <format>


namespace guchho::helpers {

    namespace {

        constexpr std::string_view kHexChars = "0123456789ABCDEF";
        constexpr char32_t kFirstASCII = 0x20;
        constexpr char32_t kLastASCII = 0x7E;
        constexpr char32_t kFirstHighSurrogate = 0xD800;
        constexpr char32_t kFirstLowSurrogate = 0xDC00;
        constexpr char32_t kLastLowSurrogate = 0xDFFF;
        constexpr char32_t kBOM = 0xFEFF;

        // Determines whether a code point can be emitted as-is inside a
        // quoted string without needing an escape sequence.
        //
        // Printable ASCII (0x20-0x7E) is always safe except for the two
        // characters that have special meaning in string literals: backslash
        // and the quote character itself.  When asciiOnly is false,
        // non-ASCII printable code points are also allowed through - but
        // the BOM and surrogate code points are always escaped.
        //
        // Example:
        //   CanPrintWithoutEscape('A', false) => true
        //   CanPrintWithoutEscape('\\', false) => false
        //   CanPrintWithoutEscape(0xFEFF, false) => false  (BOM)
        bool CanPrintWithoutEscape(char32_t c, bool asciiOnly)
        {
            if (c <= kLastASCII) {
                return c >= kFirstASCII && c != '\\' && c != '"';
            }
            return !asciiOnly && c != kBOM && (c < kFirstHighSurrogate || c > kLastLowSurrogate);
        }

        // Returns the number of UTF-8 bytes needed to encode a code point.
        //
        // Returns -1 for code points outside the valid Unicode range
        // (above U+10FFFF).
        //
        // Example:
        //   RuneLen('A')   => 1
        //   RuneLen(0xE9)  => 2   (é)
        //   RuneLen(0x4E16) => 3  (世)
        //   RuneLen(0x10000) => 4
        int RuneLen(char32_t r)
        {
            if (r <= 0x7F) return 1;
            if (r <= 0x7FF) return 2;
            if (r <= 0xFFFF) return 3;
            if (r <= 0x10FFFF) return 4;
            return -1;
        }

        // Core quoting engine used by both QuoteSingle and QuoteForJSON.
        //
        // Two passes are performed over the input: the first pass
        // estimates the output size so a single allocation can satisfy
        // the resize, and the second pass writes the escaped bytes.
        //
        // Characters that CanPrintWithoutEscape returns true for are
        // copied verbatim (in runs, to avoid per-character overhead).
        // Control characters use short escapes (\n, \t, etc.).  The
        // quote character itself is escaped only when it matches the
        // enclosing quote style.  All other code points are emitted as
        // \uXXXX or \uD800\uDCXX surrogate pairs for code points above
        // U+FFFF.
        //
        // Example:
        //   InternalQuote("hello", false, '"')  => "\"hello\""
        //   InternalQuote("a\tb", false, '"')   => "\"a\\tb\""
        std::string InternalQuote(std::string_view text, bool asciiOnly, char quoteChar)
        {
            size_t lenEstimate = 2;
            {
                size_t i = 0;
                size_t n = text.size();
                while (i < n) {
                    auto [cp, width] = DecodeWTF8Rune(text.substr(i));
                    if (width == 0) break;
                    if (CanPrintWithoutEscape(cp, asciiOnly)) {
                        int rl = RuneLen(cp);
                        if (rl > 0) lenEstimate += static_cast<size_t>(rl);
                    } else {
                        switch (cp) {
                        case '\b': case '\f': case '\n': case '\r': case '\t': case '\\':
                            lenEstimate += 2;
                            break;
                        case '"':
                            if (quoteChar == '"') lenEstimate += 2;
                            break;
                        case '\'':
                            if (quoteChar == '\'') lenEstimate += 2;
                            break;
                        default:
                            lenEstimate += (cp <= 0xFFFF) ? 6 : 12;
                            break;
                        }
                    }
                    i += static_cast<size_t>(width > 0 ? width : 1);
                }
            }

            std::string bytes;
            bytes.reserve(lenEstimate);
            bytes.push_back(quoteChar);

            size_t i = 0;
            size_t n = text.size();

            while (i < n) {
                auto [cp, width] = DecodeWTF8Rune(text.substr(i));

                if (width > 0 && CanPrintWithoutEscape(cp, asciiOnly)) {
                    size_t start = i;
                    i += static_cast<size_t>(width);
                    while (i < n) {
                        auto [cp2, w2] = DecodeWTF8Rune(text.substr(i));
                        if (w2 == 0 || !CanPrintWithoutEscape(cp2, asciiOnly)) break;
                        i += static_cast<size_t>(w2);
                    }
                    bytes.append(text.data() + start, i - start);
                    continue;
                }

                switch (cp) {
                case '\b': bytes.append("\\b");  i += 1; break;
                case '\f': bytes.append("\\f");  i += 1; break;
                case '\n': bytes.append("\\n");  i += 1; break;
                case '\r': bytes.append("\\r");  i += 1; break;
                case '\t': bytes.append("\\t");  i += 1; break;
                case '\\': bytes.append("\\\\"); i += 1; break;
                case '"':
                    if (quoteChar == '"') bytes.append("\\\"");
                    else                  bytes.push_back('"');
                    i += 1;
                    break;
                case '\'':
                    if (quoteChar == '\'') bytes.append("\\'");
                    else                   bytes.push_back('\'');
                    i += 1;
                    break;
                default: {
                    int w = width > 0 ? width : 1;
                    i += static_cast<size_t>(w);
                    if (cp <= 0xFFFF) {
                        std::array<char, 6> buf = {
                            '\\', 'u',
                            kHexChars[(cp >> 12) & 0xF],
                            kHexChars[(cp >> 8) & 0xF],
                            kHexChars[(cp >> 4) & 0xF],
                            kHexChars[cp & 0xF],
                        };
                        bytes.append(buf.data(), buf.size());
                    } else {
                        cp -= 0x10000;
                        char32_t lo = kFirstHighSurrogate + ((cp >> 10) & 0x3FF);
                        char32_t hi = kFirstLowSurrogate + (cp & 0x3FF);
                        std::array<char, 12> buf = {
                            '\\', 'u',
                            kHexChars[(lo >> 12) & 0xF],
                            kHexChars[(lo >> 8) & 0xF],
                            kHexChars[(lo >> 4) & 0xF],
                            kHexChars[lo & 0xF],
                            '\\', 'u',
                            kHexChars[(hi >> 12) & 0xF],
                            kHexChars[(hi >> 8) & 0xF],
                            kHexChars[(hi >> 4) & 0xF],
                            kHexChars[hi & 0xF],
                        };
                        bytes.append(buf.data(), buf.size());
                    }
                    break;
                }
                }
            }

            bytes.push_back(quoteChar);
            return bytes;
        }

    } 

    // Quotes a string with single quotes, escaping special characters.
    //
    // The output is wrapped in '...' and uses backslash escapes for
    // control characters, backslashes, and single quotes.
    //
    // When asciiOnly is true, non-ASCII code points are also escaped
    // as \uXXXX sequences.
    //
    // Example:
    //   QuoteSingle("hello world")  => "'hello world'"
    //   QuoteSingle("it's")         => "'it\\'s'"
    //   QuoteSingle("a\nb")         => "'a\\nb'"
    std::string QuoteSingle(std::string_view text, bool asciiOnly)
    {
        return InternalQuote(text, asciiOnly, '\'');
    }

    // Quotes a string with double quotes for use in JSON string values.
    //
    // The output is wrapped in "..." and uses backslash escapes for
    // control characters, backslashes, and double quotes.
    //
    // When asciiOnly is true, non-ASCII code points are also escaped.
    //
    // Example:
    //   QuoteForJSON("hello")     => "\"hello\""
    //   QuoteForJSON("say \"hi\"") => "\"say \\\"hi\\\"\""
    std::string QuoteForJSON(std::string_view text, bool asciiOnly)
    {
        return InternalQuote(text, asciiOnly, '"');
    }

    // Returns true for code points that should be rendered as visible
    // characters in source text.
    //
    // This covers Unicode categories L (letters), M (marks), N (numbers),
    // P (punctuation), and S (symbols), plus the ASCII space character.
    // The table is generated from Unicode 16.0.0 data.
    //
    // Example:
    //   IsPrint('A') => true
    //   IsPrint(0x4E16) => true  (世, a CJK ideograph)
    //   IsPrint(0x00) => false   (null, a control character)
    bool IsPrint(char32_t c)
    {
        return unicode::IsInRangeTable(unicode::kPrint, c);
    }

    // Quotes a string for use as a Go-style string literal.
    //
    // Control characters use short escapes (\a, \b, \f, \n, \r, \t, \v),
    // while bytes below 0x20 or the DEL character (0x7F) use \xHH hex
    // escapes.  Printable non-ASCII code points are emitted as raw UTF-8.
    // Other code points use \uXXXX for the BMP or \UXXXXXXXX for
    // supplementary planes.
    //
    // The output is wrapped in double quotes.
    //
    // Example:
    //   quoteString("hello")  => "\"hello\""
    //   quoteString("a\tb")   => "\"a\\tb\""
    //   quoteString("\x00")   => "\"\\x00\""
    std::string quoteString(std::string_view s)
    {
        std::string out;
        out += '"';
        for (size_t i = 0; i < s.size();) {
            auto view = s.substr(i);
            auto [c, width] = DecodeWTF8Rune(view);
            if (!view.empty() && width == 0) width = 1;
            if (c == '"') {
                out += "\\\"";
            } else if (c == '\\') {
                out += "\\\\";
            } else {
                switch (c) {
                case '\a': out += "\\a"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\v': out += "\\v"; break;
                default:
                    if (c < 0x20 || c == 0x7F) {
                        out += std::format("\\x{:02x}", static_cast<uint32_t>(c));
                    } else if (c < 0x80 || IsPrint(c)) {
                        char buf[4];
                        auto n = EncodeWTF8Rune(buf, c);
                        out.append(buf, static_cast<size_t>(n));
                    } else if (c < 0x10000) {
                        out += std::format("\\u{:04x}", static_cast<uint32_t>(c));
                    } else {
                        out += std::format("\\U{:08x}", static_cast<uint32_t>(c));
                    }
                    break;
                }
            }
            i += static_cast<size_t>(width);
        }
        out += '"';
        return out;
    }

}