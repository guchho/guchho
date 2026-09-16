#include "guchho/javascript/js_helpers.hpp"
#include "guchho/helpers.hpp"
#include "guchho/unicode.hpp"


namespace guchho::javascript {

    // Checks whether a UTF-8 encoded string is a valid JavaScript identifier
    // according to the latest ECMAScript specification (ESNext).
    //
    // This delegates to the unicode module's identifier validation, which
    // consults the Unicode Character Database to determine which code points
    // are permitted in identifier names.
    //
    // An identifier must begin with a character satisfying IsIdentifierStart
    // and all subsequent characters must satisfy IsIdentifierContinue.
    //
    //   IsIdentifier("foo")       => true
    //   IsIdentifier("_bar$123")  => true
    //   IsIdentifier("123abc")    => false  (starts with a digit)
    //   IsIdentifier("")          => false  (empty string)
    bool IsIdentifier(std::string_view text) noexcept {
        return guchho::unicode::IsIdentifier(text);
    }

    // Checks whether a UTF-8 encoded string is a valid JavaScript identifier
    // under both ES5 and ESNext rules combined. This is the union of the
    // identifier sets accepted by ES5 and the current spec, useful for
    // contexts where backward compatibility with ES5 is required alongside
    // modern extensions.
    //
    //   IsIdentifierES5AndESNext("let")   => true
    //   IsIdentifierES5AndESNext("class") => true
    bool IsIdentifierES5AndESNext(std::string_view text) noexcept {
        return guchho::unicode::IsIdentifierES5AndESNext(text);
    }

    // Produces a string that is guaranteed to be a valid JavaScript identifier,
    // even if the input text contains characters that are not permitted in
    // identifiers.
    //
    // The result is constructed by concatenating `prefix` with a sanitized
    // version of `text`. Characters that are not valid identifier characters
    // are replaced with underscores. The very first character is checked
    // against IsIdentifierStart (so it cannot be a digit), and all subsequent
    // characters are checked against IsIdentifierContinue.
    //
    //   ForceValidIdentifier("", "foo")          => "foo"
    //   ForceValidIdentifier("", "123abc")       => "_123abc"
    //   ForceValidIdentifier("v_", "my-var")     => "v_my_var"
    //   ForceValidIdentifier("x", "")            => "x"
    //
    // Note: This does not check whether the result collides with JavaScript
    // reserved words. The caller is responsible for additional escaping if
    // needed.
    std::string ForceValidIdentifier(std::string_view prefix, std::string_view text) {
        std::string result;
        result.reserve(prefix.size() + text.size() + 1);
        result += prefix;

        {
            auto [cp, width] = helpers::DecodeRuneInString(text);
            if (IsIdentifierStart(cp)) {
                result.append(text.data(), static_cast<size_t>(width));
            } else {
                result += '_';
            }
            text.remove_prefix(static_cast<size_t>(width));
        }

        while (!text.empty()) {
            auto [cp, width] = helpers::DecodeRuneInString(text);
            if (IsIdentifierContinue(cp)) {
                result.append(text.data(), static_cast<size_t>(width));
            } else {
                result += '_';
            }
            text.remove_prefix(static_cast<size_t>(width));
        }

        return result;
    }

    // Checks whether a UTF-16 code unit sequence forms a valid JavaScript
    // identifier under ESNext rules.
    //
    // UTF-16 encodes code points above U+FFFF as surrogate pairs: a high
    // surrogate (0xD800..0xDBFF) followed by a low surrogate (0xDC00..0xDFFF).
    // This function detects such pairs and recombines them into the original
    // code point before checking identifier validity.
    //
    // The first code point must satisfy IsIdentifierStart (so it cannot be a
    // digit, combining mark, or other character excluded from the start of
    // identifiers). Every subsequent code point must satisfy
    // IsIdentifierContinue.
    //
    //   Input:  { 0x0066, 0x006F, 0x006F }     (UTF-16 for "foo")
    //   Output: true
    //
    //   Input:  { 0x0031, 0x0061 }              (UTF-16 for "1a")
    //   Output: false  (digit at start)
    //
    //   Input:  { 0xD834, 0xDD1E }              (UTF-16 for U+1D11E MUSICAL SYMBOL G CLEF)
    //   Output: false  (not a valid identifier start)
    //
    //   Input:  { }                             (empty span)
    //   Output: false
    bool IsIdentifierUTF16(std::span<const uint16_t> text) noexcept {
        const size_t n = text.size();
        if (n > 0x10000000ULL) {
            fprintf(stderr, "DBG IsIdentifierUTF16 HUGE SIZE %zu\n", n);
        }
        if (n == 0) return false;

        for (size_t i = 0; i < n; ++i) {
            const bool is_start = i == 0;
            char32_t cp = static_cast<char32_t>(text[i]);
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n) {
                const char32_t r2 = static_cast<char32_t>(text[i + 1]);
                if (r2 >= 0xDC00 && r2 <= 0xDFFF) {
                    cp = (cp << 10) + r2 + (0x10000U - (0xD800U << 10) - 0xDC00U);
                    ++i;
                }
            }

            if (is_start) {
                if (!IsIdentifierStart(cp)) return false;
            } else {
                if (!IsIdentifierContinue(cp)) return false;
            }
        }

        return true;
    }

    // Checks whether a UTF-16 code unit sequence forms a valid JavaScript
    // identifier under the combined ES5 and ESNext rules.
    //
    // This function behaves identically to IsIdentifierUTF16 but uses the
    // ES5+ESNext character category tables, which include some code points
    // that are only valid under the union of ES5 and modern ECMAScript.
    //
    // Surrogate pairs (high surrogate in 0xD800..0xDBFF followed by low
    // surrogate in 0xDC00..0xDFFF) are recombined into the full code point
    // before the check. Lone surrogates that do not form valid pairs will
    // be tested as individual code points, which will fail identifier checks
    // since surrogates are never valid identifier characters.
    //
    //   Input:  { 0x0076, 0x0061, 0x0072 }     (UTF-16 for "var")
    //   Output: true
    //
    //   Input:  { 0x0024 }                       (UTF-16 for "$")
    //   Output: true  ($ is a valid identifier start)
    //
    //   Input:  { 0xD800 }                       (lone high surrogate)
    //   Output: false  (surrogates are not valid identifier characters)
    bool IsIdentifierES5AndESNextUTF16(std::span<const uint16_t> text) noexcept {
        const size_t n = text.size();
        if (n > 0x10000000ULL) {
            fprintf(stderr, "DBG IsIdentifierES5AndESNextUTF16 HUGE SIZE %zu\n", n);
        }
        if (n == 0) return false;

        for (size_t i = 0; i < n; ++i) {
            const bool is_start = i == 0;
            char32_t cp = static_cast<char32_t>(text[i]);
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n) {
                const char32_t r2 = static_cast<char32_t>(text[i + 1]);
                if (r2 >= 0xDC00 && r2 <= 0xDFFF) {
                    cp = (cp << 10) + r2 + (0x10000U - (0xD800U << 10) - 0xDC00U);
                    ++i;
                }
            }

            if (is_start) {
                if (!IsIdentifierStartES5AndESNext(cp)) return false;
            } else {
                if (!IsIdentifierContinueES5AndESNext(cp)) return false;
            }
        }

        return true;
    }

    // Determines whether a single Unicode code point is a valid character
    // to begin a JavaScript identifier under ESNext rules. This includes
    // Unicode letters (across all scripts), underscore, dollar sign, and
    // certain Unicode escape sequences that resolve to such characters.
    //
    // Characters that are NOT valid starts include digits (0x0030..0x0039),
    // combining marks, connector punctuation, and surrogates.
    //
    //   IsIdentifierStart('a')   => true
    //   IsIdentifierStart('_')   => true
    //   IsIdentifierStart('$')   => true
    //   IsIdentifierStart('0')   => false
    //   IsIdentifierStart(0x0300) => false  (combining grave accent)
    bool IsIdentifierStart(char32_t code_point) noexcept {
        return guchho::unicode::IsIdentifierStart(code_point);
    }

    // Determines whether a single Unicode code point is a valid character
    // to continue (appear after the first character of) a JavaScript
    // identifier under ESNext rules. This is a superset of the start
    // characters: it additionally allows digits and certain combining marks
    // that cannot appear at the beginning.
    //
    //   IsIdentifierContinue('a')   => true
    //   IsIdentifierContinue('0')   => true
    //   IsIdentifierContinue('_')   => true
    //   IsIdentifierContinue(0x200C) => true  (zero-width non-joiner)
    bool IsIdentifierContinue(char32_t code_point) noexcept {
        return guchho::unicode::IsIdentifierContinue(code_point);
    }

    // Determines whether a single Unicode code point is a valid character
    // to begin a JavaScript identifier under the combined ES5 and ESNext
    // rules. This uses the union of ES5 and ESNext character categories,
    // which may accept a slightly different set than IsIdentifierStart
    // alone.
    //
    //   IsIdentifierStartES5AndESNext('a')  => true
    //   IsIdentifierStartES5AndESNext('α')  => true  (Greek alpha)
    bool IsIdentifierStartES5AndESNext(char32_t code_point) noexcept {
        return guchho::unicode::IsIdentifierStartES5AndESNext(code_point);
    }

    // Determines whether a single Unicode code point is a valid character
    // to continue a JavaScript identifier under the combined ES5 and ESNext
    // rules. Like IsIdentifierContinue, this accepts digits and other
    // characters that are valid only after the first position.
    //
    //   IsIdentifierContinueES5AndESNext('a')  => true
    //   IsIdentifierContinueES5AndESNext('9')  => true
    bool IsIdentifierContinueES5AndESNext(char32_t code_point) noexcept {
        return guchho::unicode::IsIdentifierContinueES5AndESNext(code_point);
    }

    // Determines whether a Unicode code point is classified as whitespace
    // in JavaScript. This covers all ECMAScript whitespace characters as
    // defined in the specification, including:
    //
    //   - Tab (U+0009), vertical tab (U+000B), form feed (U+000C)
    //   - Space (U+0020), no-break space (U+00A0)
    //   - Ogham space mark (U+1680)
    //   - En/em spaces and other Unicode spaces (U+2000..U+200A)
    //   - Narrow no-break space (U+202F), medium mathematical space (U+205F)
    //   - Ideographic space (U+3000)
    //   - BOM / zero-width no-break space (U+FEFF)
    //
    // These characters are ignored by the JavaScript parser outside of
    // string literals and template literals, and serve as token separators.
    //
    //   IsWhitespace(' ')   => true
    //   IsWhitespace('\t')  => true
    //   IsWhitespace('\n')  => false  (newline is a line terminator, not whitespace)
    //   IsWhitespace(0xFEFF) => true  (BOM is whitespace outside strings)
    bool IsWhitespace(char32_t code_point) noexcept {
        switch (code_point) {
        case 0x0009:
        case 0x000B:
        case 0x000C:
        case 0x0020:
        case 0x00A0:
        case 0x1680:
        case 0x2000:
        case 0x2001:
        case 0x2002:
        case 0x2003:
        case 0x2004:
        case 0x2005:
        case 0x2006:
        case 0x2007:
        case 0x2008:
        case 0x2009:
        case 0x200A:
        case 0x202F:
        case 0x205F:
        case 0x3000:
        case 0xFEFF:
            return true;
        default:
            return false;
        }
    }

}
