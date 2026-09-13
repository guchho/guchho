#include "guchho/helpers.hpp"
#include <cstdint>

namespace guchho::helpers {

    namespace {
        // Maximum number of bytes a single WTF-8 code unit can occupy.
        constexpr int      kUTF8Max             = 4;

        // Unicode replacement character, used when decoding encounters
        // an invalid or malformed sequence.
        constexpr char32_t kRuneError           = 0xFFFD;

        // Highest valid Unicode code point (U+10FFFF).
        constexpr char32_t kMaxRune             = 0x10FFFF;

        // Highest code point in the Basic Multilingual Plane (U+FFFF).
        // Code points above this require surrogate pairs in UTF-16.
        constexpr char32_t kMaxBMPCodePoint     = 0xFFFF;

        // UTF-16 surrogate pair range. Surrogates are not valid Unicode
        // code points themselves but are used in UTF-16 to encode code
        // points above U+FFFF.
        constexpr char32_t kSurrogateHighStart  = 0xD800;
        constexpr char32_t kSurrogateHighEnd    = 0xDBFF;
        constexpr char32_t kSurrogateLowStart   = 0xDC00;
        constexpr char32_t kSurrogateLowEnd     = 0xDFFF;

        // Offset added when converting a non-BMP code point to its
        // surrogate pair representation in UTF-16 (0x10000).
        constexpr char32_t kSurrogateOffset     = 0x10000;
    }

    // Decodes a single WTF-8 code point from the beginning of a byte sequence.
    //
    // WTF-8 is a superset of UTF-8 that also allows encoding surrogate code
    // points (U+D800..U+DFFF), which are illegal in strict UTF-8. This makes
    // WTF-8 suitable for representing strings that may contain unpaired
    // surrogates, such as those produced by some JavaScript engines.
    //
    // Parameters:
    //   text  - the byte sequence to decode from
    //
    // Returns a pair of {code_point, byte_length}:
    //   - {code_point, 0}  if text is empty (no error, nothing consumed)
    //   - {code_point, N}  if a valid N-byte sequence was decoded
    //   - {kRuneError, 1}  if the sequence is invalid or truncated
    //
    // Examples:
    //   "A"       -> {'A', 1}             (ASCII)
    //   "\xC3\xA9" -> {0xE9, 2}          (é, U+00E9)
    //   "\xED\xA0\x80" -> {0xD800, 3}    (surrogate, legal in WTF-8)
    //   "\x80"    -> {kRuneError, 1}      (unexpected continuation byte)
    //   ""        -> {kRuneError, 0}      (empty input)
    std::pair<char32_t, int> DecodeWTF8Rune(std::string_view text)
    {
        const size_t n = text.size();

        if (n == 0) {
            return {kRuneError, 0};
        }

        const auto s0 = static_cast<unsigned char>(text[0]);

        // Single-byte ASCII (0xxxxxxx).
        if (s0 < 0x80) {
            return {static_cast<char32_t>(s0), 1};
        }

        // Determine expected byte count from the leading byte pattern.
        int width = 0;

        if ((s0 & 0xE0) == 0xC0) {
            width = 2;
        } else if ((s0 & 0xF0) == 0xE0) {
            width = 3;
        } else if ((s0 & 0xF8) == 0xF0) {
            width = 4;
        } else {
            // Invalid leading byte (e.g. bare continuation byte 10xxxxxx).
            return {kRuneError, 1};
        }

        // Truncated sequence - not enough bytes for the expected width.
        if (n < static_cast<size_t>(width)) {
            return {kRuneError, 0};
        }

        // Validate that the second byte is a continuation byte (10xxxxxx).
        const auto s1 = static_cast<unsigned char>(text[1]);

        if ((s1 & 0xC0) != 0x80) {
            return {kRuneError, 1};
        }

        if (width == 2) {
            const char32_t cp =
                (static_cast<char32_t>(s0 & 0x1F) << 6) |
                static_cast<char32_t>(s1 & 0x3F);

            // Reject overlong encodings (could represent a shorter sequence).
            if (cp < 0x80) {
                return {kRuneError, 1};
            }

            return {cp, 2};
        }

        // Validate third continuation byte for 3- and 4-byte sequences.
        const auto s2 = static_cast<unsigned char>(text[2]);

        if ((s2 & 0xC0) != 0x80) {
            return {kRuneError, 1};
        }

        if (width == 3) {
            const char32_t cp =
                (static_cast<char32_t>(s0 & 0x0F) << 12) |
                (static_cast<char32_t>(s1 & 0x3F) << 6) |
                static_cast<char32_t>(s2 & 0x3F);

            // Reject overlong encodings.
            if (cp < 0x800) {
                return {kRuneError, 1};
            }

            // WTF-8 (unlike UTF-8) permits surrogate code points here.
            return {cp, 3};
        }

        // Validate fourth continuation byte for 4-byte sequences.
        const auto s3 = static_cast<unsigned char>(text[3]);

        if ((s3 & 0xC0) != 0x80) {
            return {kRuneError, 1};
        }

        const char32_t cp =
            (static_cast<char32_t>(s0 & 0x07) << 18) |
            (static_cast<char32_t>(s1 & 0x3F) << 12) |
            (static_cast<char32_t>(s2 & 0x3F) << 6) |
            static_cast<char32_t>(s3 & 0x3F);

        // Reject overlong encodings.
        if (cp < 0x10000) {
            return {kRuneError, 1};
        }

        // Beyond maximum valid code point.
        if (cp > kMaxRune) {
            return {kRuneError, 1};
        }

        return {cp, 4};
    }

    // Encodes a single Unicode code point into its WTF-8 byte representation.
    //
    // The output is written into `buffer`, which must have room for at least
    // kUTF8Max (4) bytes. The function returns the number of bytes written
    // (1, 2, 3, or 4).
    //
    // If code_point exceeds the maximum valid Unicode value (U+10FFFF), it
    // is silently replaced with the Unicode replacement character (U+FFFD).
    // Negative code_point values are treated as U+FFFD as well, since the
    // cast to uint32_t wraps them into a large value that triggers the
    // overflow check.
    //
    // Parameters:
    //   buffer     - destination for the encoded bytes (must be ≥ 4 bytes)
    //   code_point - the Unicode code point to encode
    //
    // Returns the number of bytes written (1–4).
    //
    // Examples:
    //   'A'    (0x41)     -> buffer = "A",         returns 1
    //   0xE9   (é)        -> buffer = "\xC3\xA9",  returns 2
    //   0x4E2D (中)       -> buffer = "\xE4\xB8\xAD", returns 3
    //   0x10348 (𐍈)      -> buffer = "\xF0\x90\x8D\x88", returns 4
    //   0x110000 (invalid) -> buffer = "\xEF\xBF\xBD", returns 3 (U+FFFD)
    int EncodeWTF8Rune(char* buffer, char32_t code_point) 
    {
        // Treating as unsigned avoids undefined behavior for negative values.
        uint32_t value = static_cast<uint32_t>(code_point);

        if (value <= 0x7F) {
            buffer[0] = static_cast<char>(code_point);
            return 1;
        }

        if (value <= 0x7FF) {
            buffer[0] = static_cast<char>(0xC0 | (value >> 6));
            buffer[1] = static_cast<char>(0x80 | (value & 0x3F));
            return 2;
        }

        // Clamp out-of-range values to the replacement character.
        if (value > kMaxRune) {
            code_point = kRuneError;
            value = static_cast<uint32_t>(code_point);
        }

        if (value <= 0xFFFF) {
            buffer[0] = static_cast<char>(0xE0 | (value >> 12));
            buffer[1] = static_cast<char>(0x80 | ((value >> 6) & 0x3F));
            buffer[2] = static_cast<char>(0x80 | (value & 0x3F));
            return 3;
        }

        buffer[0] = static_cast<char>(0xF0 | (value >> 18));
        buffer[1] = static_cast<char>(0x80 | ((value >> 12) & 0x3F));
        buffer[2] = static_cast<char>(0x80 | ((value >> 6) & 0x3F));
        buffer[3] = static_cast<char>(0x80 | (value & 0x3F));
        return 4;
    }

    // Returns true if the given WTF-8 string contains any code point
    // outside the Basic Multilingual Plane (above U+FFFF).
    //
    // Non-BMP code points require 4 bytes in WTF-8 and would need surrogate
    // pairs in UTF-16, so this check is useful for deciding whether a
    // string can be represented in a BMP-only encoding.
    //
    // Examples:
    //   "hello"     -> false  (all ASCII, BMP)
    //   "é"         -> false  (U+00E9, BMP)
    //   "𐍈"        -> true   (U+10348, non-BMP)
    //   "\xED\xA0\x80" -> true  (U+D800, surrogate - non-BMP)
    bool ContainsNonBMPCodePoint(std::string_view text)
    {
        size_t index = 0;

        while (index < text.size()) {
            auto [code_point, width] = DecodeWTF8Rune(text.substr(index));

            if (width == 0) {
                break;
            }

            if (code_point > kMaxBMPCodePoint) {
                return true;
            }

            index += static_cast<size_t>(width);
        }

        return false;
    }

    // Returns true if the given UTF-16 string contains a surrogate pair,
    // which indicates a code point above U+FFFF (non-BMP).
    //
    // A surrogate pair is a high surrogate (U+D800..U+DBFF) immediately
    // followed by a low surrogate (U+DC00..U+DFFF). A lone high surrogate
    // at the end of the string does not count - only valid pairs are
    // considered here.
    //
    // Examples:
    //   u"hello"              -> false
    //   u"\xD800\xDC00"       -> true   (U+10000, valid surrogate pair)
    //   u"\xD800"             -> false  (unpaired high surrogate)
    bool ContainsNonBMPCodePointUTF16(std::span<const char16_t> text)
    {
        const size_t n = text.size();

        if (n > 0) {
            for (size_t i = 0; i + 1 < n; ++i) {

                // Look for a surrogate pair (high followed by low).
                char32_t c = static_cast<char32_t>(text[i]);
                if (c >= kSurrogateHighStart &&
                    c <= kSurrogateHighEnd) {

                    char32_t c2 = static_cast<char32_t>(text[i + 1]);
                    
                    if (c2 >= kSurrogateLowStart &&
                        c2 <= kSurrogateLowEnd) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    // Converts a WTF-8 encoded string to a UTF-16 encoded string.
    //
    // BMP code points (U+0000..U+FFFF) are encoded as a single char16_t.
    // Non-BMP code points (U+10000..U+10FFFF) are encoded as a surrogate
    // pair: a high surrogate followed by a low surrogate.
    //
    // Since WTF-8 allows surrogate code points (U+D800..U+DFFF) as standalone
    // values, they are passed through directly as single char16_t values
    // without being placed in surrogate pairs.
    //
    // Parameters:
    //   text - a valid WTF-8 encoded string
    //
    // Returns the equivalent UTF-16 encoded string.
    //
    // Examples:
    //   "A"          -> u"A"           (1 code unit)
    //   "é"          -> u"\u00E9"      (1 code unit, BMP)
    //   "𐍈"         -> u"\uD800\uDF48" (2 code units, surrogate pair)
    //   "\xED\xA0\x80" -> u"\uD800"   (1 code unit, bare surrogate from WTF-8)
    std::u16string StringToUTF16(std::string_view text)
    {
        std::u16string result;
        result.reserve(text.size());

        size_t i = 0;

        while (i < text.size()) {
            auto [cp, width] = DecodeWTF8Rune(text.substr(i));

            if (width == 0) {
                break;
            }

            if (cp <= 0xFFFF) {
                result.push_back(static_cast<char16_t>(cp));
            } else {
                cp -= kSurrogateOffset;

                result.push_back(static_cast<char16_t>(
                    kSurrogateHighStart + ((cp >> 10) & 0x3FF)));

                result.push_back(static_cast<char16_t>(
                    kSurrogateLowStart + (cp & 0x3FF)));
            }

            i += static_cast<size_t>(width);
        }

        return result;
    }

    // Converts a UTF-16 encoded string to a WTF-8 encoded string.
    //
    // Surrogate pairs (high + low) are combined and encoded as 4-byte
    // WTF-8 sequences. Lone surrogates are encoded directly as 3-byte
    // WTF-8 sequences (U+D800..U+DFFF), which is legal in WTF-8 but
    // not in strict UTF-8.
    //
    // Parameters:
    //   text - a UTF-16 encoded string (may contain unpaired surrogates)
    //
    // Returns the equivalent WTF-8 encoded string.
    //
    // Examples:
    //   u"A"              -> "A"
    //   u"\u00E9"         -> "\xC3\xA9"        (é)
    //   u"\uD800\uDF48"   -> "\xF0\x90\x8D\x88" (U+10348, surrogate pair)
    //   u"\uD800"         -> "\xED\xA0\x80"    (lone surrogate, WTF-8)
    std::string UTF16ToString(std::span<const char16_t> text)
    {
        std::string result;
        result.reserve(text.size());

        const size_t n = text.size();

        for (size_t i = 0; i < n; ++i) {
            char32_t r1 = static_cast<char32_t>(text[i]);

            if (r1 >= kSurrogateHighStart &&
                r1 <= kSurrogateHighEnd &&
                i + 1 < n) {

                char32_t r2 = static_cast<char32_t>(text[i + 1]);

                if (r2 >= kSurrogateLowStart &&
                    r2 <= kSurrogateLowEnd) {

                    r1 = ((r1 - kSurrogateHighStart) << 10) |
                        (r2 - kSurrogateLowStart);

                    r1 += kSurrogateOffset;
                    ++i;
                }
            }

            char buffer[kUTF8Max];
            int width = EncodeWTF8Rune(buffer, r1);

            result.append(buffer, static_cast<size_t>(width));
        }

        return result;
    }

    // Converts a UTF-16 encoded string to WTF-8, rejecting ill-formed
    // surrogate sequences.
    //
    // Unlike UTF16ToString, this function returns an error when it
    // encounters a lone high surrogate not followed by a low surrogate,
    // or a standalone low surrogate. This is useful when strict
    // conformance is required and unpaired surrogates are not acceptable.
    //
    // Parameters:
    //   text - a UTF-16 encoded string to convert
    //
    // Returns a tuple of {result, error_code_point, success}:
    //   - On success:     {wtf8_string, 0, true}
    //   - On error:       {"", offending_code_point, false}
    //
    // Examples:
    //   u"hello"                -> ("hello", 0, true)
    //   u"\uD800\uDF48"         -> ("\xF0\x90\x8D\x88", 0, true)
    //   u"\uD800"               -> ("", 0xD800, false)  (unpaired high surrogate)
    //   u"\uDC00"               -> ("", 0xDC00, false)  (bare low surrogate)
    //   u"\uD800\u0041"         -> ("", 0xD800, false)  (high not followed by low)
    std::tuple<std::string, char16_t, bool> UTF16ToStringWithValidation(std::span<const char16_t> text)
    {
        std::string result;
        const size_t n = text.size();

        for (size_t i = 0; i < n; ++i) {
            char32_t r1 = static_cast<char32_t>(text[i]);

            if (r1 >= kSurrogateHighStart &&
                r1 <= kSurrogateHighEnd) {

                if (i + 1 < n) {
                    char32_t r2 = static_cast<char32_t>(text[i + 1]);

                    if (r2 >= kSurrogateLowStart &&
                        r2 <= kSurrogateLowEnd) {

                        r1 = ((r1 - kSurrogateHighStart) << 10) |
                            (r2 - kSurrogateLowStart);
                        r1 += kSurrogateOffset;

                        ++i;
                    } else {
                        return {"", static_cast<char16_t>(r1), false};
                    }
                } else {
                    return {"", static_cast<char16_t>(r1), false};
                }
            } else if (r1 >= kSurrogateLowStart &&
                    r1 <= kSurrogateLowEnd) {
                return {"", static_cast<char16_t>(r1), false};
            }

            char buffer[kUTF8Max];
            const int width = EncodeWTF8Rune(buffer, r1);
            result.append(buffer, static_cast<size_t>(width));
        }

        return {result, static_cast<char16_t>(0), true};
    }

    // Compares a UTF-16 string with a WTF-8 string for equality.
    //
    // The comparison is done by decoding the UTF-16 string into WTF-8 on
    // the fly and comparing byte-by-byte against the WTF-8 string. This
    // avoids allocating an intermediate WTF-8 string.
    //
    // Surrogate pairs in the UTF-16 string are decoded to their full
    // code point before comparison. Lone surrogates are compared as their
    // 3-byte WTF-8 representation.
    //
    // Parameters:
    //   text - the UTF-16 string to compare
    //   str  - the WTF-8 string to compare against
    //
    // Returns true if both strings represent the same sequence of code points.
    //
    // Examples:
    //   u"hello",  "hello"    -> true
    //   u"\u00E9", "\xC3\xA9" -> true   (é encoded differently)
    //   u"hello",  "world"    -> false
    //   u"A",      "AB"       -> false
    bool UTF16EqualsString(std::span<const char16_t> text, std::string_view str)
    {
        if (text.size() > str.size()) {
            return false;
        }

        const size_t n = text.size();
        size_t j = 0;

        for (size_t i = 0; i < n; ++i) {
            char32_t r1 = static_cast<char32_t>(text[i]);

            if (r1 >= kSurrogateHighStart &&
                r1 <= kSurrogateHighEnd &&
                i + 1 < n) {

                char32_t r2 = static_cast<char32_t>(text[i + 1]);

                if (r2 >= kSurrogateLowStart &&
                    r2 <= kSurrogateLowEnd) {

                    r1 = ((r1 - kSurrogateHighStart) << 10) |
                        (r2 - kSurrogateLowStart);
                    r1 += kSurrogateOffset;

                    ++i;
                }
            }

            char buffer[kUTF8Max];
            const int width = EncodeWTF8Rune(buffer, r1);

            if (j + static_cast<size_t>(width) > str.size()) {
                return false;
            }

            for (int k = 0; k < width; ++k) {
                if (buffer[k] != str[j]) {
                    return false;
                }

                ++j;
            }
        }

        return j == str.size();
    }

    // Compares two UTF-16 strings for byte-for-byte equality.
    //
    // Returns false immediately if the lengths differ. Otherwise checks
    // each element in order.
    //
    // Note: this is a code unit comparison, not a code point comparison.
    // Two strings can represent the same characters but differ in
    // normalization or surrogate ordering. This function only checks
    // whether the raw char16_t sequences are identical.
    //
    // Examples:
    //   u"hello", u"hello" -> true
    //   u"hello", u"world" -> false
    //   u"AB",    u"ABCD"  -> false  (different lengths)
    bool UTF16EqualsUTF16(std::span<const char16_t> a, std::span<const char16_t> b)
    {
        if (a.size() == b.size()) {
            for (size_t i = 0; i < a.size(); ++i) {
                if (a[i] != b[i]) {
                    return false;
                }
            }

            return true;
        }

        return false;
    }

    // Decodes the last code point from a WTF-8 encoded string.
    //
    // Works by scanning backward from the end of the string to locate the
    // leading byte of the last code point (a byte that is not a UTF-8
    // continuation byte), then delegates to DecodeWTF8Rune for the actual
    // decoding and validation.
    //
    // Parameters:
    //   text - a WTF-8 encoded string
    //
    // Returns a pair of {code_point, byte_length}:
    //   - {code_point, N}  for the last valid N-byte code point
    //   - {kRuneError, 1}  if the trailing bytes don't form a valid code point
    //   - {kRuneError, 0}  if the string is empty
    //
    // Examples:
    //   "ABC"       -> {'C', 1}
    //   "éA"        -> {'A', 1}    (last rune is single-byte)
    //   "Aé"        -> {0xE9, 2}   (é, last rune is two bytes)
    //   "\x80"      -> {kRuneError, 1}  (dangling continuation byte)
    std::pair<char32_t, int> DecodeLastRuneInString(std::string_view text)
    {
        if (text.empty()) {
            return {kRuneError, 0};
        }

        size_t i = text.size() - 1;

        // Walk backward over continuation bytes to find the leading byte.
        while (i > 0 &&
            (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) {
            --i;
        }

        auto [rune, width] = DecodeWTF8Rune(text.substr(i));

        // Ensure DecodeWTF8Rune consumed the full rune.
        if (width != static_cast<int>(text.size() - i)) {
            return {kRuneError, 1};
        }

        return {rune, width};
    }

    // Decodes the first code point from a WTF-8 encoded string.
    //
    // This is a stricter variant of DecodeWTF8Rune: it rejects surrogate
    // code points in 3-byte sequences (which WTF-8 normally allows) and
    // treats truncated input as an error rather than returning width 0.
    //
    // Parameters:
    //   text - a WTF-8 encoded string
    //
    // Returns a pair of {code_point, byte_length}:
    //   - {code_point, N}  if a valid N-byte code point was decoded
    //   - {kRuneError, 1}  if the sequence is invalid, truncated, or
    //                       contains a surrogate in a 3-byte sequence
    //   - {kRuneError, 0}  only if the string is empty
    //
    // Examples:
    //   "A"       -> {'A', 1}
    //   "\xC3\xA9" -> {0xE9, 2}
    //   "\xE4\xB8\xAD" -> {0x4E2D, 2}    (中, U+4E2D)
    //   "\xED\xA0\x80" -> {kRuneError, 1} (surrogate rejected)
    //   "\xC3"    -> {kRuneError, 1}       (truncated)
    //   ""        -> {kRuneError, 0}
    std::pair<char32_t, int> DecodeRuneInString(std::string_view text)
    {
        if (text.empty()) {
            return {kRuneError, 0};
        }

        const unsigned char b0 = static_cast<unsigned char>(text[0]);

        if (b0 < 0x80) {
            return {static_cast<char32_t>(b0), 1};
        }

        int width;

        if ((b0 & 0xE0) == 0xC0) {
            width = 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            width = 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            width = 4;
        } else {
            return {kRuneError, 1};
        }

        if (text.size() < static_cast<size_t>(width)) {
            return {kRuneError, 1};
        }

        char32_t rune = 0;

        switch (width) {
            case 2: {
                const unsigned char b1 = static_cast<unsigned char>(text[1]);

                if ((b1 & 0xC0) != 0x80) {
                    return {kRuneError, 1};
                }

                rune = static_cast<char32_t>(
                    ((b0 & 0x1F) << 6) |
                    (b1 & 0x3F));

                if (rune < 0x80) {
                    return {kRuneError, 1};
                }

                break;
            }

            case 3: {
                const unsigned char b1 = static_cast<unsigned char>(text[1]);
                const unsigned char b2 = static_cast<unsigned char>(text[2]);

                if ((b1 & 0xC0) != 0x80 ||
                    (b2 & 0xC0) != 0x80) {
                    return {kRuneError, 1};
                }

                rune = static_cast<char32_t>(
                    ((b0 & 0x0F) << 12) |
                    ((b1 & 0x3F) << 6) |
                    (b2 & 0x3F));

                // Reject overlong encodings and surrogate code points.
                if (rune < 0x800 ||
                    (rune >= 0xD800 && rune <= 0xDFFF)) {
                    return {kRuneError, 1};
                }

                break;
            }

            case 4: {
                const unsigned char b1 = static_cast<unsigned char>(text[1]);
                const unsigned char b2 = static_cast<unsigned char>(text[2]);
                const unsigned char b3 = static_cast<unsigned char>(text[3]);

                if ((b1 & 0xC0) != 0x80 ||
                    (b2 & 0xC0) != 0x80 ||
                    (b3 & 0xC0) != 0x80) {
                    return {kRuneError, 1};
                }

                rune = static_cast<char32_t>(
                    ((b0 & 0x07) << 18) |
                    ((b1 & 0x3F) << 12) |
                    ((b2 & 0x3F) << 6) |
                    (b3 & 0x3F));

                if (rune < 0x10000 ||
                    rune > 0x10FFFF) {
                    return {kRuneError, 1};
                }

                break;
            }
        }

        return {rune, width};
    }

} 
