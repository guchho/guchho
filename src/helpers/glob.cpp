#include "guchho/helpers.hpp"

namespace guchho::helpers {

    // Parses a glob pattern string into a sequence of GlobPart segments,
    // each containing a literal prefix and a wildcard type.
    //
    // The parser scans left-to-right for '*' characters and classifies
    // each run of consecutive stars:
    //   - A single '*' becomes kAllExceptSlash - matches any characters
    //     except '/' and '\'.
    //   - Two or more '*' characters surrounded by path separators (or
    //     at the boundaries of the string) becomes kAllIncludingSlash -
    //     matches any characters including directory separators, allowing
    //     the pattern to cross directory boundaries.
    //   - Two or more '*' NOT adjacent to separators degrades to a single
    //     kAllExceptSlash match (the extra stars are consumed but ignored).
    //
    // Everything between wildcard runs is captured verbatim as the prefix
    // of the preceding segment.
    //
    // Example:
    //   "src/**/*.ts"  => [ {"src/",  kAllIncludingSlash},
    //                       {"",      kAllExceptSlash},
    //                       {".ts",   kNone} ]
    //
    //   "docs/*"       => [ {"docs/", kAllExceptSlash},
    //                       {"",      kNone} ]
    //
    // Edge cases:
    //   - Empty string: returns a single segment with kNone.
    //   - Pattern starting with '*': the first segment has an empty prefix.
    //   - Pattern ending with '*': the last segment has an empty prefix
    //     and kAllExceptSlash (or kAllIncludingSlash if preceded by '/').
    std::vector<GlobPart> ParseGlobPattern(std::string_view text)
    {
        std::vector<GlobPart> pattern;

        while (true) {
            // Find the next '*' wildcard in the remaining text.
            auto star = text.find('*');

            // No more wildcards: the rest of the text is a literal suffix.
            if (star == std::string_view::npos) {
                pattern.push_back({std::string(text), GlobWildcard::kNone});
                break;
            }

            // Count how many consecutive '*' characters follow.
            size_t count = 1;
            while (star + count < text.size() && text[star + count] == '*') {
                count++;
            }

            // Default: a single star matches anything except path separators.
            auto wildcard = GlobWildcard::kAllExceptSlash;

            // Multiple stars flanked by path separators (or at string
            // boundaries) are promoted to a directory-spanning wildcard
            // that matches across '/' and '\' boundaries.
            if (count > 1 &&
                (star == 0 || text[star - 1] == '/' || text[star - 1] == '\\') &&
                (star + count == text.size() ||
                text[star + count] == '/' ||
                text[star + count] == '\\'))
            {
                wildcard = GlobWildcard::kAllIncludingSlash;
            }

            // Emit the literal prefix before this wildcard along with
            // the wildcard classification.
            pattern.push_back({
                std::string(text.substr(0, star)),
                wildcard
            });

            // Advance past the consumed wildcard characters.
            text = text.substr(star + count);
        }

        return pattern;
    }

    // Reconstructs a glob pattern string from its parsed representation.
    //
    // Each GlobPart's prefix is appended literally, followed by '*' or
    // '**' depending on the wildcard type.  Segments with kNone
    // wildcard produce only their prefix (no trailing star).
    //
    // The output is not guaranteed to be identical to the original input
    // when the input contained redundant stars (e.g. "***" is
    // normalised to "**").
    //
    // Example:
    //   ParseGlobPattern("src/**/*.ts") then GlobPatternToString
    //   => "src/**/*.ts"
    std::string GlobPatternToString(const std::vector<GlobPart>& pattern)
    {
        std::string result;

        for (auto const& part : pattern) {
            result.append(part.prefix);

            switch (part.wildcard) {
            case GlobWildcard::kAllExceptSlash:
                result.push_back('*');
                break;

            case GlobWildcard::kAllIncludingSlash:
                result.append("**");
                break;

            default:
                break;
            }
        }

        return result;
    }
    
}