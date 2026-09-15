#include "guchho/unicode.hpp"
#include "guchho/helpers.hpp"

#include <cstddef>
#include <string_view>

namespace guchho::unicode {

    // Checks whether the given text is a valid identifier according to the
    // latest Unicode rules (ESNext). An identifier must be non-empty, start
    // with a valid identifier start character (letters, '_', '$', or
    // designated Unicode code points), and every subsequent character must be
    // a valid identifier continue character (the above plus digits and certain
    // extra code points like zero-width joiner/non-joiner).
    //
    // The function processes the string by decoding UTF-8 sequences one at a
    // time using helpers::DecodeRuneInString, which returns the decoded code
    // point and the number of bytes consumed. Each code point is validated
    // against the appropriate Unicode range tables for the current spec.
    //
    // Example:
    //   IsIdentifier("abc123")  => true
    //   IsIdentifier("123abc")  => false  (starts with digit)
    //   IsIdentifier("_foo")    => true
    //   IsIdentifier("$bar")    => true
    //   IsIdentifier("")        => false  (empty string)
    //
    // Edge cases:
    //   - Returns false for empty strings immediately.
    //   - Non-ASCII code points are checked against dynamically generated
    //     Unicode range tables (kIDStartESNext, kIDContinueESNext).
    //   - Zero-width joiner (U+200D) and zero-width non-joiner (U+200C) are
    //     valid continue characters.
    //   - The function is noexcept and will not throw or allocate.
    bool IsIdentifier(std::string_view text) noexcept {
        if (text.empty()) {
            return false;
        }

        bool first = true;

        while (!text.empty()) {
            auto [cp, width] = helpers::DecodeRuneInString(text);

            text.remove_prefix(static_cast<size_t>(width));

            if (first) {
                if (!IsIdentifierStart(cp)) {
                    return false;
                }
                first = false;
            } else {
                if (!IsIdentifierContinue(cp)) {
                    return false;
                }
            }
        }

        return true;
    }


    // Checks whether the given text is simultaneously a valid identifier
    // under both the ES5 and ESNext Unicode rules. This stricter check
    // ensures compatibility across both specification versions. A valid
    // identifier must be non-empty, start with a character that is valid
    // as an identifier start in both ES5 and ESNext, and all subsequent
    // characters must be valid continue characters in both specifications.
    //
    // The key difference from IsIdentifier is that this function uses
    // conjunction (both ES5 AND ESNext must accept each character) rather
    // than the disjunction (ES5 OR ESNext) used by the standard check.
    // Additionally, code points above U+FFFF are rejected outright because
    // ES5 does not support supplementary plane characters.
    //
    // Example:
    //   IsIdentifierES5AndESNext("abc")      => true
    //   IsIdentifierES5AndESNext("café")     => true  (if é is in both ES5 & ESNext)
    //   IsIdentifierES5AndESNext("foo🎉")   => false  (emoji above U+FFFF)
    //   IsIdentifierES5AndESNext("")          => false  (empty string)
    //
    // Edge cases:
    //   - Returns false for empty strings immediately.
    //   - Code points above U+FFFF are always rejected (ES5 limitation).
    //   - Zero-width joiner (U+200D) and zero-width non-joiner (U+200C) are
    //     still valid continue characters when they appear after the first
    //     character.
    //   - The function is noexcept and will not throw or allocate.
    bool IsIdentifierES5AndESNext(std::string_view text) noexcept {
        if (text.empty()) {
            return false;
        }

        bool first = true;

        while (!text.empty()) {
            auto [cp, width] = helpers::DecodeRuneInString(text);

            text.remove_prefix(static_cast<size_t>(width));

            if (first) {
                if (!IsIdentifierStartES5AndESNext(cp)) {
                    return false;
                }
                first = false;
            } else {
                if (!IsIdentifierContinueES5AndESNext(cp)) {
                    return false;
                }
            }
        }

        return true;
    }

}