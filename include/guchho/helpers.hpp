#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>

#include <utility>
#include <tuple>
#include <string_view>
#include <string>
#include <span>
#include <optional>
#include <unordered_map>
#include <vector>


namespace guchho::helpers {

    //---------------------------------------
    // ------------ utf8.cpp ----------------
    //---------------------------------------

    // Decodes one Unicode code point at the start of text.
    // Returns the decoded code point and how many bytes it consumed.
    std::pair<char32_t, int> DecodeWTF8Rune(std::string_view text);

    // Decodes the final Unicode code point of the string.
    // Returns the code point and its byte width.
    std::pair<char32_t, int> DecodeLastRuneInString(std::string_view text);

    // Decodes the first Unicode code point of the string.
    // Returns the code point and its byte width.
    std::pair<char32_t, int> DecodeRuneInString(std::string_view text);

    // Encodes one Unicode code point as WTF-8 into the buffer.
    // Returns the number of bytes written.
    int EncodeWTF8Rune(char* buffer, char32_t code_point);

    // True when the string holds any Unicode code point beyond the BMP.
    bool ContainsNonBMPCodePoint(std::string_view text);

    // True when the UTF-16 text holds any code point beyond the BMP.
    bool ContainsNonBMPCodePointUTF16(
        std::span<const char16_t> text);

    // Converts a UTF-8/WTF-8 string to a UTF-16 string.
    std::u16string StringToUTF16(std::string_view text);

    // Converts UTF-16 text to a UTF-8/WTF-8 string.
    std::string UTF16ToString(
        std::span<const char16_t> text);

    // Converts UTF-16 text and reports validation results.
    // Returns the converted text, the offending UTF-16 value on failure, and
    // whether the text was valid.
    std::tuple<std::string, char16_t, bool> UTF16ToStringWithValidation(
        std::span<const char16_t> text);

    // True when the UTF-16 text and the UTF-8/WTF-8 string represent the same text.
    bool UTF16EqualsString(
        std::span<const char16_t> text,
        std::string_view str);

    // True when two UTF-16 text sequences are identical.
    bool UTF16EqualsUTF16(
        std::span<const char16_t> a,
        std::span<const char16_t> b);

    //---------------------------------------
    // ------------- base64.cpp -------------
    //---------------------------------------
    // Encodes using the standard base64 alphabet (A-Z, a-z, 0-9, +, /).
    std::string Base64StdEncode(std::string_view src);

    // Encodes using the URL-safe base64 alphabet (A-Z, a-z, 0-9, -, _).
    std::string Base64URLEncode(std::string_view src);

    // Encodes using the RFC 4648 base32 alphabet (A-Z, 2-7) with padding.
    std::string Base32StdEncode(std::string_view src);
}