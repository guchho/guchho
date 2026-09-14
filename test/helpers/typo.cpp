#include "guchho/helpers.hpp"

namespace guchho::helpers {
    
    // TypoDetector — builds an index that maps a mistyped word back to the
    // correct one, so Guchho can suggest a fix when an unrecognized name is
    // only off by a single character.
    TypoDetector::TypoDetector(const std::vector<std::string>& valid)
    {
        // From each valid word, drop one Unicode character at a time to form a
        // candidate typo and remember which word it came from.
        for (const auto& correct : valid) {
            // Tiny words are skipped — a typo on them is impractical to detect.
            if (correct.size() <= 3) continue;

            size_t i = 0;
            size_t n = correct.size();

            // Walk the string one Unicode character at a time (UTF-8/WTF-8).
            while (i < n) {
                auto [ch, width] =
                    guchho::helpers::DecodeWTF8Rune(
                        std::string_view(correct).substr(i));

                // An invalid or truncated rune means this word is done.
                if (width == 0) break;

                // Build the word without the current character.
                std::string key =
                    correct.substr(0, i) +
                    correct.substr(i + static_cast<size_t>(width));

                // Map that typo to its correct original word.
                oneCharTypos_[key] = correct;

                // Move on to the next Unicode character.
                i += static_cast<size_t>(width);
            }
        }
    }

    // MaybeCorrectTypo — look up a suspect word and return the corrected form if
    // it differs from a known word by a single missing character.
    // First an exact match against the stored one-char-deleted typos is tried;
    // if none hits, each possible one-character deletion of `typo` is probed.
    // Example: valid {"bundle"}; typo "bundl" -> "bundle"
    //          typo "bundle" (already correct) -> std::nullopt
    std::optional<std::string> TypoDetector::MaybeCorrectTypo(std::string_view typo) const
    {
        // First, check whether this word is itself a one-char-deleted typo.
        {
            auto it = oneCharTypos_.find(std::string(typo));
            if (it != oneCharTypos_.end()) {
                return it->second;
            }
        }

        // Next, consider that a character may be sitting in the wrong place.
        size_t i = 0;
        size_t n = typo.size();

        // Probe one Unicode character at a time (UTF-8/WTF-8).
        while (i < n) {
            auto [ch, width] = DecodeWTF8Rune(typo.substr(i));

            // A bad or incomplete rune stops the search.
            if (width == 0) break;

            // Delete the current character to build a candidate key.
            std::string key =
                std::string(typo.substr(0, i)) +
                std::string(typo.substr(i + static_cast<size_t>(width)));

            // If this deletion matches a known typo, return its correction.
            auto it = oneCharTypos_.find(key);
            if (it != oneCharTypos_.end()) {
                return it->second;
            }

            // Advance to the next Unicode character.
            i += static_cast<size_t>(width > 0 ? width : 1);
        }

        // No plausible correction was found.
        return std::nullopt;
    }
}
