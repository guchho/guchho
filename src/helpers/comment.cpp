#include "guchho/helpers.hpp"

namespace guchho::helpers {

    // Case-insensitive string comparison.  Returns true when both strings
    // have the same length and every character pair matches after folding
    // to lower case.  Used by EscapeClosingTag to match tag names
    // regardless of capitalisation (e.g. "</Script>" vs "</script>").
    //
    // Example:
    //   EqualFold("abc", "ABC") => true
    //   EqualFold("abc", "abd") => false
    //   EqualFold("ab",  "abc") => false  (different lengths)
    static bool EqualFold(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }

    // Prevents an inline script from being prematurely closed by injecting
    // a backslash before the slash in a matching closing tag sequence.
    //
    // HTML parsers treat `</script>` as the end of a `<script>` block.
    // When a string literal inside the script contains the exact sequence
    // `</script>`, the parser would incorrectly terminate the script.
    // This function inserts a `\` before the `/` to break the match while
    // preserving the visible text - the backslash is ignored inside string
    // literals by the JavaScript engine.
    //
    // `slashTag` is the tag name to protect (e.g. "script", "style").
    // The comparison is case-insensitive, so `</Script>` and `</SCRIPT>`
    // are both caught.
    //
    // Example:
    //   text      = "var s = '</script>';"
    //   slashTag  = "script"
    //   result    = "var s = '<\\/script>';"
    //
    // If `slashTag` is empty the function returns the original text
    // unchanged, since no closing tag can possibly match.
    //
    // Edge cases:
    //   - No `</` present: returned unchanged.
    //   - `</` present but followed by a different tag name: left alone.
    //   - Multiple occurrences: all are escaped independently.
    std::string EscapeClosingTag(std::string_view text, std::string_view slashTag)
    {
        if (slashTag.empty()) {
            return std::string(text);
        }

        std::string result;
        result.reserve(text.size());

        while (true) {
            auto pos = text.find("</");
            if (pos == std::string_view::npos) {
                result.append(text);
                break;
            }

            // text[pos] == '<' and text[pos + 1] == '/'.
            std::string_view name = text.substr(pos + 2);
            if (name.size() >= slashTag.size() && EqualFold(name.substr(0, slashTag.size()), slashTag)) {
                // Insert the backslash between '<' and '/'.
                result.append(text.substr(0, pos + 1));
                result.push_back('\\');
                result.push_back('/');
                text.remove_prefix(pos + 2);
            } else {
                result.append(text.substr(0, pos + 1));
                text.remove_prefix(pos + 1);
            }
        }

        return result;
    }
}