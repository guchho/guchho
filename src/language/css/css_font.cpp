#include "guchho/css/css_helpers.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/helpers.hpp"


#include <charconv>
#include <unordered_set>

namespace guchho::css {

    namespace {

        // SplitOnSpaces
        // -------------
        // Divides a string into substrings wherever a space character appears.
        // Consecutive spaces produce empty substrings between them. The output
        // vector is cleared before any work is done so callers can safely reuse
        // it across multiple invocations.
        //
        // Input:  text = "Arial  Helvetica"
        // Output: out  = {"Arial", "", "Helvetica"}
        //
        // Edge cases:
        //   - An empty input produces a single empty-string entry.
        //   - A leading or trailing space produces an empty entry at the
        //     corresponding end of the result vector.
        void SplitOnSpaces(std::string_view text, std::vector<std::string>& out) {
            out.clear();
            size_t start = 0;
            for (size_t i = 0; i <= text.size(); ++i) {
                if (i == text.size() || text[i] == ' ') {
                    out.emplace_back(text.substr(start, i - start));
                    start = i + 1;
                }
            }
        }


        // CssWideAndReservedKeywords
        // --------------------------
        // Returns a static set containing every keyword that is reserved by the
        // CSS specification across all properties. These words may never appear
        // as custom identifiers because they carry special meaning at cascade
        // time (e.g. "inherit" pulls the computed value from the parent element).
        // The set is lazily initialised on first call and then reused.
        //
        // The function deliberately includes both the CSS-wide keywords
        // (initial, inherit, unset) and the property-specific reserved word
        // "default", as well as the cascade-dependent keywords "revert" and
        // "revert-layer" that behave differently depending on the origin of
        // the stylesheet.
        const std::unordered_set<std::string>& CssWideAndReservedKeywords() {
            static const std::unordered_set<std::string> keywords = {
                "initial",      // CSS-wide keyword
                "inherit",      // CSS-wide keyword
                "unset",        // CSS-wide keyword
                "default",      // CSS reserved keyword
                "revert",       // Cascade-dependent keyword
                "revert-layer", // Cascade-dependent keyword
            };
            return keywords;
        }


        // GenericFamilyNames
        // ------------------
        // Returns the full list of generic font family names recognised by the
        // CSS Fonts Module Level 4 specification. These families are always
        // available and never need to be downloaded or installed; the browser
        // maps them to an appropriate system font at render time. The set
        // includes the legacy generic families (serif, sans-serif, cursive,
        // fantasy, monospace) as well as the newer additions such as
        // system-ui, emoji, math, fangsong, and the ui-* variants.
        const std::unordered_set<std::string>& GenericFamilyNames() {
            static const std::unordered_set<std::string> keywords = {
                "serif",        "sans-serif", "cursive",    "fantasy",    "monospace",
                "system-ui",    "emoji",      "math",       "fangsong",   "ui-serif",
                "ui-sans-serif", "ui-monospace", "ui-rounded",
            };
            return keywords;
        }

        // IsValidCustomIdent
        // ------------------
        // Determines whether a raw string can legally be used as a
        // <custom-ident> in a CSS declaration. A custom identifier must
        // satisfy several constraints defined by the CSS Values and Units
        // specification (https://drafts.csswg.org/css-values-4/#custom-idents):
        //
        //   1. It must not be a CSS-wide keyword or a property-reserved word
        //      (checked via CssWideAndReservedKeywords).
        //   2. It must not collide with any keyword that is already defined
        //      for the specific property the identifier is used in (passed in
        //      as `predefined_keywords`).
        //   3. It must not be an empty string.
        //   4. The first character must be valid as an identifier start
        //      character (checked by WouldStartIdentifierWithoutEscapes).
        //   5. Every character (decoded as a full Unicode code point) must be
        //      a valid identifier continuation character (IsNameContinue).
        //
        // Input:  text = "serif", predefined_keywords = GenericFamilyNames()
        // Output: false  (because "serif" is a predefined generic family)
        //
        // Input:  text = "My-Cool-Font 2000", predefined_keywords = GenericFamilyNames()
        // Output: false  (space is not a valid identifier character)
        //
        // Input:  text = "Arial", predefined_keywords = GenericFamilyNames()
        // Output: true
        //
        // Edge cases:
        //   - Emoji or other multi-byte code points are accepted as long as
        //     they are valid CSS name characters.
        //   - ASCII uppercase is folded to lowercase before comparison, so
        //     "Arial" and "arial" are treated identically.
        bool IsValidCustomIdent(std::string_view text,
                                const std::unordered_set<std::string>& predefined_keywords) {
            std::string lowered = helpers::ToLowerASCII(text);

            if (predefined_keywords.count(lowered) > 0) {
                return false;
            }
            if (CssWideAndReservedKeywords().count(lowered) > 0) {
                return false;
            }
            if (lowered.empty()) {
                return false;
            }

            // Validate if it contains characters which need to be escaped.
            if (!WouldStartIdentifierWithoutEscapes(text)) {
                return false;
            }
            for (size_t i = 0; i < text.size();) {
                auto [cp, width] = helpers::DecodeWTF8Rune(text.substr(i));
                if (width <= 0 || !IsNameContinue(cp)) {
                    return false;
                }
                i += static_cast<size_t>(width);
            }

            return true;
        }

        // MangleFamilyNameOrGenericName
        // -----------------------------
        // Consumes a single font family entry from the front of a token list.
        // The entry can take one of three forms:
        //
        //   a) A generic family keyword such as "serif" or "monospace".
        //   b) A quoted string like '"Times New Roman"', which may contain
        //      spaces and is validated word-by-word.
        //   c) A sequence of unquoted identifiers like Times New Roman,
        //      where each identifier is individually validated as a legal
        //      <custom-ident>.
        //
        // On success the consumed tokens are appended to `result` and the
        // unconsumed remainder is placed in `rest`. On failure both vectors
        // are cleared and the function returns false.
        //
        // Input:  tokens = ["Arial", "sans-serif"], minify_whitespace = false
        // Output: result = ["Arial", " sans-serif"], rest = [], return true
        //
        // Input:  tokens = ["serif"], minify_whitespace = true
        // Output: result = ["serif"], rest = [], return true
        //
        // Input:  tokens = [], minify_whitespace = false
        // Output: result = [], rest = [], return false
        //
        // Edge cases:
        //   - If a quoted string contains a word that collides with a generic
        //     family name, the entire quoted string is returned as a single
        //     kString token rather than being decomposed, because the spec
        //     says quoted strings are always interpreted literally.
        //   - Unquoted identifiers are greedily consumed: the loop keeps
        //     reading as long as the next token is a valid custom identifier,
        //     which allows multi-word unquoted names like "Times New Roman".
        bool MangleFamilyNameOrGenericName(
            std::vector<Token>& result,
            std::vector<Token>& rest,
            const std::vector<Token>& tokens,
            bool minify_whitespace
        ) {
            if (!tokens.empty()) {
                const Token& t = tokens[0];

                // Handle <generic-family>
                if (t.kind == TokenType::kIdent && GenericFamilyNames().count(t.text) > 0) {
                    result.push_back(t);
                    rest.assign(tokens.begin() + 1, tokens.end());
                    return true;
                }

                // Handle <family-name> (quoted string)
                if (t.kind == TokenType::kString) {
                    // "If a sequence of identifiers is given as a <family-name>, the computed
                    // value is the name converted to a string by joining all the identifiers
                    // in the sequence by single spaces."
                    std::vector<std::string> names;
                    SplitOnSpaces(t.text, names);
                    for (const std::string& name : names) {
                        if (!IsValidCustomIdent(name, GenericFamilyNames())) {
                            result.push_back(t);
                            rest.assign(tokens.begin() + 1, tokens.end());
                            return true;
                        }
                    }
                    for (size_t i = 0; i < names.size(); ++i) {
                        WhitespaceFlags whitespace = WhitespaceFlags::kNone;
                        if (i != 0 || !minify_whitespace) {
                            whitespace = WhitespaceFlags::kWhitespaceBefore;
                        }
                        Token ident;
                        ident.loc = t.loc;
                        ident.kind = TokenType::kIdent;
                        ident.text = names[i];
                        ident.whitespace = whitespace;
                        result.push_back(std::move(ident));
                    }
                    rest.assign(tokens.begin() + 1, tokens.end());
                    return true;
                }

                // Handle <family-name> (unquoted, one or more custom-ident tokens)
                // "Font family names other than generic families must either be given
                // quoted as <string>s, or unquoted as a sequence of one or more
                // <custom-ident>."
                if (t.kind == TokenType::kIdent) {
                    size_t pos = 0;
                    for (;;) {
                        if (!IsValidCustomIdent(tokens[pos].text, GenericFamilyNames())) {
                            result.clear();
                            rest.clear();
                            return false;
                        }
                        result.push_back(tokens[pos]);
                        ++pos;
                        if (pos >= tokens.size() || tokens[pos].kind != TokenType::kIdent) {
                            break;
                        }
                    }
                    rest.assign(tokens.begin() + static_cast<std::ptrdiff_t>(pos), tokens.end());
                    return true;
                }
            }

            // Anything other than the cases listed above causes us to bail.
            result.clear();
            rest.clear();
            return false;
        }

        // ParseFloat
        // ----------
        // Attempts to convert a string view into a double-precision floating
        // point number using the highly efficient std::from_chars routine,
        // which avoids locale dependency and heap allocation. The entire input
        // must be consumed; a partial parse (where the pointer does not reach
        // the end of the string) is treated as failure.
        //
        // Input:  text = "3.14"
        // Output: 3.14
        //
        // Input:  text = "100"
        // Output: 100.0
        //
        // Input:  text = "abc"
        // Output: std::nullopt
        //
        // Input:  text = "42.5deg"  (unit suffix after number)
        // Output: std::nullopt  (partial consumption is failure)
        std::optional<double> ParseFloat(std::string_view text) {
            double value;
            auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec == std::errc{} && ptr == text.data() + text.size()) {
                return value;
            }
            return std::nullopt;
        }

        // SetWhitespaceFlag
        // ------------------
        // Applies a whitespace flag to an existing set of flags using bitwise
        // OR. This is used when a token needs additional whitespace hints
        // added during mangling (for example, ensuring a space before a
        // font-family value when whitespace is not being minified).
        WhitespaceFlags SetWhitespaceFlag(WhitespaceFlags flags, WhitespaceFlags flag) {
            return static_cast<WhitespaceFlags>(static_cast<uint8_t>(flags) | static_cast<uint8_t>(flag));
        }

        // ClearWhitespaceFlag
        // --------------------
        // Removes a whitespace flag from an existing set of flags using
        // bitwise AND-NOT. This is used during minification to strip
        // unnecessary whitespace around tokens like the "/" that separates
        // font-size from line-height.
        WhitespaceFlags ClearWhitespaceFlag(WhitespaceFlags flags, WhitespaceFlags flag) {
            return static_cast<WhitespaceFlags>(static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(flag));
        }

        // FontSizeKeywords
        // ----------------
        // Returns the complete set of absolute and relative font size keywords
        // defined by the CSS Fonts Module. Absolute sizes range from xx-small
        // to xxx-large and map to fixed pixel values at a given base size.
        // Relative sizes ("larger" and "smaller") scale the inherited font size
        // by a browser-defined ratio. This set is used to distinguish
        // legitimate font-size values from other ident tokens in the font
        // shorthand parser.
        const std::unordered_set<std::string>& FontSizeKeywords() {
            static const std::unordered_set<std::string> keywords = {
                "xx-small", "x-small", "small", "medium", "large", "x-large",
                "xx-large", "xxx-large", "larger", "smaller",
            };
            return keywords;
        }

        // IsFontSize
        // -----------
        // Tests whether a single token represents a valid <font-size> value
        // as defined by the CSS Fonts specification. A token qualifies if it
        // is any of the following:
        //
        //   - A dimension token (e.g. 12px, 1.5rem, 2em) — any length unit
        //     is acceptable.
        //   - A percentage token (e.g. 120%) — relative to the parent's font
        //     size.
        //   - An ident token whose case-insensitive text matches one of the
        //     absolute or relative size keywords from FontSizeKeywords.
        //
        // This function does not validate numeric ranges or compute actual
        // pixel sizes; it only checks syntactic eligibility.
        //
        // Input:  token = {kind: kDimension, text: "16px"}
        // Output: true
        //
        // Input:  token = {kind: kIdent, text: "medium"}
        // Output: true
        //
        // Input:  token = {kind: kIdent, text: "bold"}
        // Output: false  ("bold" is a font-weight, not a font-size)
        bool IsFontSize(const Token& token) {
            // <length-percentage>
            if (token.kind == TokenType::kDimension || token.kind == TokenType::kPercentage) {
                return true;
            }

            // <absolute-size> or <relative-size>
            if (token.kind == TokenType::kIdent) {
                return FontSizeKeywords().count(helpers::ToLowerASCII(token.text)) > 0;
            }

            return false;
        }

    }

    // MangleFontFamily
    // ----------------
    // Parses a complete <font-family> value, which is a comma-separated list
    // of individual family names. Each entry is parsed by
    // MangleFamilyNameOrGenericName. On success the result vector contains the
    // normalised token sequence (quoted strings decomposed into ident tokens
    // where possible). On failure the result is cleared and the function
    // returns false, signalling that the original tokens should be kept as-is.
    //
    // Input:  tokens = ["Arial", ",", "sans-serif"], minify_whitespace = false
    // Output: result = ["Arial", ",", " sans-serif"], return true
    //
    // Input:  tokens = ["serif"], minify_whitespace = true
    // Output: result = ["serif"], return true
    //
    // Input:  tokens = ["12px"], minify_whitespace = false
    // Output: result = [], return false  (numeric tokens are not valid family names)
    //
    // Edge cases:
    //   - A trailing comma without a following name causes failure.
    //   - Empty input causes immediate failure.
    //   - If minify_whitespace is true, leading whitespace on the first
    //     family entry is suppressed.
    bool MangleFontFamily(
        std::vector<Token>& result,
        const std::vector<Token>& tokens,
        bool minify_whitespace
    ) {
        result.clear();
        std::vector<Token> rest;
        if (!MangleFamilyNameOrGenericName(result, rest, tokens, minify_whitespace)) {
            result.clear();
            return false;
        }

        while (!rest.empty() && rest[0].kind == TokenType::kComma) {
            result.push_back(rest[0]);
            std::vector<Token> next_tokens(rest.begin() + 1, rest.end());
            if (!MangleFamilyNameOrGenericName(result, rest, next_tokens, minify_whitespace)) {
                result.clear();
                return false;
            }
        }

        if (!rest.empty()) {
            result.clear();
            return false;
        }

        return true;
    }

    // MangleFontWeight
    // ----------------
    // Converts the named font weight keywords "normal" and "bold" into their
    // numeric equivalents (400 and 700 respectively) as specified by the CSS
    // Fonts Module. This produces shorter output in the final CSS because
    // the numeric form requires fewer bytes and avoids any ambiguity with
    // other ident tokens. All other token kinds are returned unchanged.
    //
    // Input:  token = {kind: kIdent, text: "normal"}
    // Output: token = {kind: kNumber, text: "400"}
    //
    // Input:  token = {kind: kIdent, text: "bold"}
    // Output: token = {kind: kNumber, text: "700"}
    //
    // Input:  token = {kind: kNumber, text: "300"}
    // Output: token = {kind: kNumber, text: "300"}  (unchanged)
    //
    // Edge cases:
    //   - Case-insensitive comparison is used so "Bold", "BOLD", and "bold"
    //     are all treated identically.
    //   - "bolder" and "lighter" are intentionally left untouched because
    //     they are relative weight keywords, not absolute values.
    Token MangleFontWeight(Token token) {
        if (token.kind != TokenType::kIdent) {
            return token;
        }

        std::string lowered = helpers::ToLowerASCII(token.text);
        if (lowered == "normal") {
            token.text = "400";
            token.kind = TokenType::kNumber;
        } else if (lowered == "bold") {
            token.text = "700";
            token.kind = TokenType::kNumber;
        }

        return token;
    }

    // MangleFont
    // ----------
    // The main entry point for processing a CSS `font` shorthand declaration.
    // It parses the full shorthand syntax:
    //
    //   [ <font-style> || <font-variant-css2> || <font-weight> ||
    //     <font-stretch-css3> ] <font-size>[ / <line-height> ] <font-family>
    //
    // The function scans tokens from left to right, collecting style,
    // variant, weight, and stretch keywords before the font-size. Once a
    // font-size token is found, it is appended to the result. An optional
    // "/ <line-height>" pair follows immediately, with whitespace around the
    // "/" stripped during minification. The remaining tokens are interpreted
    // as the <font-family> list and delegated to MangleFontFamily.
    //
    // If any token in the preamble is not a recognised font shorthand
    // component, the function bails and returns the original tokens
    // unmodified, preserving the source CSS verbatim.
    //
    // Input:  tokens = ["italic", "bold", "16px", "/", "1.5", "Arial"]
    //         minify_whitespace = false
    // Output: ["italic", "700", "16px", "/", "1.5", " Arial"]
    //
    // Input:  tokens = ["normal", "12px", "serif"]
    //         minify_whitespace = true
    // Output: ["400", "12px", "serif"]
    //
    // Input:  tokens = ["16px", "Arial"]
    //         minify_whitespace = false
    // Output: ["16px", " Arial"]  (no font-style/weight/stretch present)
    //
    // Input:  tokens = ["bold", "16px"]
    //         minify_whitespace = false
    // Output: ["bold", "16px"]  (font-family is mandatory; missing family
    //          causes failure and original tokens are returned)
    //
    // Edge cases:
    //   - A numeric font-weight value must be between 1 and 1000 inclusive;
    //     values outside this range cause the function to bail.
    //   - The "normal" keyword in the preamble is silently consumed because
    //     it represents the default for font-style, font-variant, and
    //     font-weight simultaneously.
    //   - "oblique" and "italic" are followed by an optional angle token
    //     (e.g. "oblique 14deg"). If the angle is present it is included
    //     in the result.
    //   - If the "/" separator for line-height is present but the line-height
    //     token itself is missing, the function bails.
    //   - During minification, the three tokens surrounding "/" (the
    //     font-size, the slash itself, and the line-height) have their
    //     whitespace flags stripped so the output reads "16px/1.5" with no
    //     spaces.
    std::vector<Token> MangleFont(const std::vector<Token>& tokens, bool minify_whitespace) {
        std::vector<Token> result;

        // Scan up to the font size
        size_t pos = 0;
        for (; pos < tokens.size(); ++pos) {
            const Token& token = tokens[pos];
            if (IsFontSize(token)) {
                break;
            }

            switch (token.kind) {
            case TokenType::kIdent: {
                std::string lowered = helpers::ToLowerASCII(token.text);
                if (lowered == "normal") {
                    continue;
                }

                // <font-style>
                if (lowered == "italic" || lowered == "oblique") {
                    if (pos + 1 < tokens.size() && tokens[pos + 1].IsAngle()) {
                        result.push_back(token);
                        result.push_back(tokens[pos + 1]);
                        ++pos;
                        continue;
                    }
                } else if (lowered == "small-caps") {
                    // <font-variant-css2>
                } else if (lowered == "bold" || lowered == "bolder" || lowered == "lighter") {
                    // <font-weight>
                    result.push_back(MangleFontWeight(token));
                    continue;
                } else if (lowered == "ultra-condensed" || lowered == "extra-condensed" ||
                        lowered == "condensed" || lowered == "semi-condensed" ||
                        lowered == "semi-expanded" || lowered == "expanded" ||
                        lowered == "extra-expanded" || lowered == "ultra-expanded") {
                    // <font-stretch-css3>
                } else {
                    // All other tokens are unrecognized, so we bail if we hit one
                    return tokens;
                }
                result.push_back(token);
                break;
            }

            case TokenType::kNumber: {
                // "Only values greater than or equal to 1, and less than or equal to
                // 1000, are valid, and all other values are invalid."
                std::optional<double> value = ParseFloat(token.text);
                if (!value.has_value() || *value < 1 || *value > 1000) {
                    return tokens;
                }
                result.push_back(token);
                break;
            }

            default:
                // All other tokens are unrecognized, so we bail if we hit one
                return tokens;
            }
        }

        // <font-size>
        if (pos == tokens.size()) {
            return tokens;
        }
        result.push_back(tokens[pos]);
        ++pos;

        // / <line-height>
        if (pos < tokens.size() && tokens[pos].kind == TokenType::kDelimSlash) {
            if (pos + 1 == tokens.size()) {
                return tokens;
            }
            result.push_back(tokens[pos]);
            result.push_back(tokens[pos + 1]);
            pos += 2;

            // Remove the whitespace around the "/" character
            if (minify_whitespace) {
                result[result.size() - 3].whitespace =
                    ClearWhitespaceFlag(result[result.size() - 3].whitespace, WhitespaceFlags::kWhitespaceAfter);
                result[result.size() - 2].whitespace = WhitespaceFlags::kNone;
                result[result.size() - 1].whitespace =
                    ClearWhitespaceFlag(result[result.size() - 1].whitespace, WhitespaceFlags::kWhitespaceBefore);
            }
        }

        // <font-family>
        std::vector<Token> family;
        std::vector<Token> family_tokens(tokens.begin() + static_cast<std::ptrdiff_t>(pos), tokens.end());
        if (MangleFontFamily(family, family_tokens, minify_whitespace)) {
            if (!result.empty() && !family.empty() && family[0].kind != TokenType::kString) {
                family[0].whitespace =
                    SetWhitespaceFlag(family[0].whitespace, WhitespaceFlags::kWhitespaceBefore);
            }
            result.insert(result.end(), family.begin(), family.end());
            return result;
        }
        return tokens;
    }



} 
