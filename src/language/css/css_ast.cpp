#include "guchho/helpers.hpp"
#include "guchho/css/css_ast.hpp"
#include "guchho/css/css_properties.hpp"

#include <charconv>

// =============================================================================
// CSS Abstract Syntax Tree (AST) Operations
//
// This file implements the core AST data structures for Guchho's CSS parser.
// It provides equality testing, hashing, cloning, and utility functions for
// all CSS rule types, tokens, media queries, and selectors.
//
// Design Principles:
//   - Every AST node type implements Equal() and Hash() for efficient
//     deduplication and comparison.
//   - Cross-file equality checks allow comparing nodes from different source
//     files by resolving symbol references rather than comparing indices.
//   - Clone functions deep-copy AST nodes while properly remapping import
//     records when nodes are moved between files.
// =============================================================================

namespace guchho::css {
    using guchho::helpers::HashCombine;
    using guchho::helpers::HashCombineString;

    namespace {

        // -------------------------------------------------------------------------
        // EqualFoldASCII
        //
        // Case-insensitive ASCII string comparison.  This is used when comparing
        // at-keyword names (e.g. "@media" vs "@MEDIA") and other CSS identifiers
        // where the spec defines case-insensitive matching.
        //
        // Behavior:
        //   - Returns false if the strings have different lengths.
        //   - Compares characters one by one, folding uppercase to lowercase.
        //   - Only handles ASCII letters; non-ASCII characters are compared
        //     literally.
        //
        // Example:
        //   EqualFoldASCII("@media", "@MEDIA") == true
        //   EqualFoldASCII("red", "blue") == false
        //   EqualFoldASCII("10px", "10px") == true
        // -------------------------------------------------------------------------
        bool EqualFoldASCII(std::string_view a, std::string_view b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); i++) {
                auto ca = a[i];
                auto cb = b[i];
                if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
                if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
                if (ca != cb) {
                    return false;
                }
            }
            return true;
        }

        // -------------------------------------------------------------------------
        // ParseFloat
        //
        // Parses a string view as a floating-point number.  Returns
        // std::nullopt if the entire string is not a valid number (partial
        // matches are rejected).
        //
        // Uses std::from_chars for efficient, locale-independent parsing.
        // This is used when evaluating numeric CSS values such as percentages
        // and dimensions.
        //
        // Example:
        //   ParseFloat("3.14") == 3.14
        //   ParseFloat("-50") == -50.0
        //   ParseFloat("10px") == std::nullopt (not a pure number)
        //   ParseFloat("") == std::nullopt
        // -------------------------------------------------------------------------
        std::optional<double> ParseFloat(std::string_view text) {
            double value;
            auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec == std::errc{} && ptr == text.data() + text.size()) {
                return value;
            }
            return std::nullopt;
        }

    }

    // -------------------------------------------------------------------------
    // Token::Equal
    //
    // Compares two tokens for structural equality.  This function handles the
    // special comparison rules for URL and Symbol tokens that cannot rely on
    // simple text comparison.
    //
    // Comparison Rules:
    //   1. Basic fields (kind, text, whitespace) must match first.
    //   2. URL tokens: Compare import record paths instead of raw text, since
    //      the same URL may have different text representations.  When
    //      comparing within the same file (check == nullptr), only the payload
    //      index is compared.
    //   3. Symbol tokens: Compare symbol references for equivalence rather
    //      than text.  When comparing across files, creates Ref objects and
    //      uses the CrossFileEqualityCheck to verify they refer to the same
    //      symbol.
    //   4. Child tokens: If both tokens have children, recursively compares
    //      them using TokensEqual.
    //
    // Example:
    //   Token a("url('img.png')", TokenType::kUrl);
    //   Token b("url(\"img.png\")", TokenType::kUrl);
    //   // a == b because they reference the same import record
    //
    // Edge Cases:
    //   - Returns false if one token has children and the other does not.
    //   - Cross-file comparison requires non-null check parameter.
    // -------------------------------------------------------------------------
    bool Token::Equal(const Token& b, const CrossFileEqualityCheck* check) const
    {
        // Check basic token fields first
        if (kind == b.kind && text == b.text && whitespace == b.whitespace) {

            // For URL tokens, compare import record paths instead of raw text
            if (kind == TokenType::kUrl) {
                if (check == nullptr) {
                    // (comment)
                    if (payload_index != b.payload_index) {
                        return false;
                    }
                } else {
                    // (comment)
                    if (check->import_records_a->at(payload_index).path.text !=
                        check->import_records_b->at(b.payload_index).path.text) {
                        return false;
                    }
                }
            }

            // For this token type, compare by reference
            // (comment)
            if (kind == TokenType::kSymbol) {
                if (check == nullptr) {
                    // (comment)
                    if (payload_index != b.payload_index) {
                        return false;
                    }
                } else {
                    // Different files: create symbol references
                    // (comment)
                    guchho::compiler::Ref ref_a{
                        check->source_index_a, payload_index
                    };
                    guchho::compiler::Ref ref_b{
                        check->source_index_b, b.payload_index
                    };

                    if (!check->RefsAreEquivalent(ref_a, ref_b)) {
                        return false;
                    }
                }
            }

            // Both tokens have no children: they are equal
            if (children == nullptr && b.children == nullptr) {
                return true;
            }

            // (comment)
            if (children != nullptr &&
                b.children != nullptr &&
                TokensEqual(*children, *b.children, check)) {
                return true;
            }
        }

        // (comment)
        return false;
    }

    // -------------------------------------------------------------------------
    // TokensEqual
    //
    // Compares two token vectors for structural equality.  Each token is
    // compared using Token::Equal, which handles the special rules for URL
    // and Symbol tokens.
    //
    // Example:
    //   std::vector<Token> a = {Token("red"), Token("blue")};
    //   std::vector<Token> b = {Token("red"), Token("blue")};
    //   TokensEqual(a, b, nullptr) == true
    //
    // Edge Cases:
    //   - Returns false immediately if vectors have different sizes.
    //   - Empty vectors are considered equal.
    // -------------------------------------------------------------------------
    bool TokensEqual(const std::vector<Token>& a, const std::vector<Token>& b, const CrossFileEqualityCheck* check) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++) {
            if (!a[i].Equal(b[i], check)) {
                return false;
            }
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // HashTokens
    //
    // Computes a hash value for a vector of tokens.  This hash is used for
    // efficient deduplication of token sequences.  The hash incorporates:
    //   - The number of tokens in the vector.
    //   - Each token's kind (as a uint32).
    //   - Each token's text (except URL tokens, which are hashed by kind only).
    //   - Recursively hashes any child tokens.
    //
    // Note: URL tokens intentionally exclude their text from the hash because
    // the same URL may have different text representations.
    //
    // Example:
    //   std::vector<Token> tokens = {Token("10px"), Token("20px")};
    //   uint32_t h = HashTokens(0, tokens);
    //   // h is a deterministic hash value for this token sequence
    //
    // Edge Cases:
    //   - Empty tokens vector produces a hash based only on the size (0).
    //   - The initial hash value affects the result (seeding).
    // -------------------------------------------------------------------------
    uint32_t HashTokens(uint32_t hash, const std::vector<Token>& tokens) {
        hash = HashCombine(hash, static_cast<uint32_t>(tokens.size()));

        for (const auto& t : tokens) {
            hash = HashCombine(hash, static_cast<uint32_t>(t.kind));
            if (t.kind != TokenType::kUrl) {
                hash = HashCombineString(hash, t.text);
            }
            if (t.children != nullptr) {
                hash = HashTokens(hash, *t.children);
            }
        }

        return hash;
    }

    // -------------------------------------------------------------------------
    // Token::EqualIgnoringWhitespace
    //
    // Compares two tokens for equality while ignoring whitespace differences.
    // This is useful when comparing CSS values that may have different
    // whitespace formatting but are semantically equivalent.
    //
    // Unlike Token::Equal, this function:
    //   - Ignores the whitespace field entirely.
    //   - Compares payload_index instead of text for URL/Symbol tokens.
    //   - Recursively compares children using TokensEqualIgnoringWhitespace.
    //
    // Example:
    //   Token a("10px", TokenType::kDimension);
    //   Token b("10px", TokenType::kDimension);
    //   // a.EqualIgnoringWhitespace(b) == true regardless of whitespace
    //
    // Edge Cases:
    //   - Returns false if one token has children and the other does not.
    //   - Does not perform cross-file comparison (no check parameter).
    // -------------------------------------------------------------------------
    bool Token::EqualIgnoringWhitespace(const Token& b) const {
        if (kind == b.kind && text == b.text && payload_index == b.payload_index) {
            if (children == nullptr && b.children == nullptr) {
                return true;
            }

            if (children != nullptr && b.children != nullptr && TokensEqualIgnoringWhitespace(*children, *b.children)) {
                return true;
            }
        }

        return false;
    }

    // -------------------------------------------------------------------------
    // TokensEqualIgnoringWhitespace
    //
    // Compares two token vectors for equality while ignoring whitespace
    // differences.  This is the vector version of Token::EqualIgnoringWhitespace.
    //
    // Example:
    //   std::vector<Token> a = {Token("10"), Token("px")};
    //   std::vector<Token> b = {Token("10"), Token("px")};
    //   TokensEqualIgnoringWhitespace(a, b) == true
    //
    // Edge Cases:
    //   - Returns false immediately if vectors have different sizes.
    //   - Empty vectors are considered equal.
    // -------------------------------------------------------------------------
    bool TokensEqualIgnoringWhitespace(const std::vector<Token>& a, const std::vector<Token>& b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++) {
            if (!a[i].EqualIgnoringWhitespace(b[i])) {
                return false;
            }
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // TokensAreCommaSeparated
    //
    // Checks if a token vector represents a comma-separated list.  The
    // pattern is: value, comma, value, comma, ..., value.  This means:
    //   - The vector must have an odd number of tokens.
    //   - All even-indexed tokens (1, 3, 5, ...) must be commas.
    //
    // This is used to validate CSS values like "red, blue, green" which
    // should have the pattern [ident, comma, ident, comma, ident].
    //
    // Example:
    //   {Token("red"), Token(","), Token("blue")} → true
    //   {Token("red"), Token(","), Token("blue"), Token(",")} → false (even count)
    //   {Token("red"), Token("blue")} → false (no commas)
    //
    // Edge Cases:
    //   - Empty vector returns false.
    //   - Single token (odd count 1) returns true (no commas needed).
    // -------------------------------------------------------------------------
    bool TokensAreCommaSeparated(const std::vector<Token>& tokens) {
        size_t n = tokens.size();
        if ((n & 1) != 0) {
            for (size_t i = 1; i < n; i += 2) {
                if (tokens[i].kind != TokenType::kComma) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Token::NumberOrFractionForPercentage
    //
    // Converts a number or percentage token to an absolute value based on a
    // reference range.  This is used when evaluating CSS values that can be
    // either absolute numbers or percentages.
    //
    // Behavior:
    //   - For kNumber tokens: Returns the parsed number directly.
    //   - For kPercentage tokens: Converts the percentage to an absolute value
    //     by multiplying with percent_reference_range (e.g., 50% of 200 = 100).
    //   - Applies clamping based on PercentageFlags:
    //     - kAllowPercentageBelow0: If not set, negative percentages are clamped to 0.
    //     - kAllowPercentageAbove100: If not set, percentages > 100 are clamped to
    //       percent_reference_range.
    //
    // Returns:
    //   - A pair of (value, success).  If success is false, the token could not
    //     be parsed as a number or percentage.
    //
    // Example:
    //   Token t("50%", TokenType::kPercentage);
    //   auto [value, ok] = t.NumberOrFractionForPercentage(200.0);
    //   // value == 100.0, ok == true
    //
    // Edge Cases:
    //   - Returns {0, false} for non-numeric tokens.
    //   - Returns {0, false} if the text cannot be parsed as a float.
    // -------------------------------------------------------------------------
    std::pair<double, bool> Token::NumberOrFractionForPercentage(double percent_reference_range, PercentageFlags flags) const {
        switch (kind) {
        case TokenType::kNumber:
            if (auto f = ParseFloat(text)) {
                return {*f, true};
            }
            break;

        case TokenType::kPercentage:
            if (auto f = ParseFloat(PercentageValue())) {
                if (!Has(flags, PercentageFlags::kAllowPercentageBelow0) && *f < 0) {
                    return {0, true};
                }
                if (!Has(flags, PercentageFlags::kAllowPercentageAbove100) && *f > 100) {
                    return {percent_reference_range, true};
                }
                return {*f / 100 * percent_reference_range, true};
            }
            break;

        default:
            break;
        }

        return {0, false};
    }

    // -------------------------------------------------------------------------
    // Token::ClampedFractionForPercentage
    //
    // Converts a percentage token to a fraction between 0.0 and 1.0, clamped
    // to that range.  This is useful for CSS properties like opacity that
    // expect a value between 0 and 1.
    //
    // Behavior:
    //   - Only processes kPercentage tokens.
    //   - Returns the percentage divided by 100 (e.g., 50% → 0.5).
    //   - Clamps negative values to 0.0 and values above 100% to 1.0.
    //
    // Returns:
    //   - A pair of (fraction, success).  If success is false, the token is
    //     not a percentage or could not be parsed.
    //
    // Example:
    //   Token t("75%", TokenType::kPercentage);
    //   auto [fraction, ok] = t.ClampedFractionForPercentage();
    //   // fraction == 0.75, ok == true
    //
    // Edge Cases:
    //   - Token "-10%" → returns {0.0, true} (clamped to 0).
    //   - Token "150%" → returns {1.0, true} (clamped to 1).
    //   - Token "10px" → returns {0.0, false} (not a percentage).
    // -------------------------------------------------------------------------
    std::pair<double, bool> Token::ClampedFractionForPercentage() const {
        if (kind == TokenType::kPercentage) {
            if (auto f = ParseFloat(PercentageValue())) {
                if (*f < 0) {
                    return {0, true};
                }
                if (*f > 100) {
                    return {1, true};
                }
                return {*f / 100, true};
            }
        }

        return {0, false};
    }

    // -------------------------------------------------------------------------
    // Token::TurnLengthIntoNumberIfZero
    //
    // Converts a zero-length dimension token (e.g., "0px", "0em") to a plain
    // number token ("0").  This is an optimization because zero is the same
    // regardless of the unit.
    //
    // Behavior:
    //   - Only processes kDimension tokens.
    //   - Checks if the numeric value is exactly "0".
    //   - If so, changes the token kind to kNumber and sets text to "0".
    //
    // Returns:
    //   - true if the conversion was performed, false otherwise.
    //
    // Example:
    //   Token t("0px", TokenType::kDimension);
    //   bool changed = t.TurnLengthIntoNumberIfZero();
    //   // changed == true, t.text == "0", t.kind == kNumber
    //
    // Edge Cases:
    //   - Token "0em" → converts to "0" (true).
    //   - Token "10px" → no change (false).
    //   - Token "0.0px" → no change because DimensionValue() returns "0.0" (false).
    // -------------------------------------------------------------------------
    bool Token::TurnLengthIntoNumberIfZero()
    {
        if (kind == TokenType::kDimension && DimensionValue() == "0") {
            kind = TokenType::kNumber;
            text = "0";
            return true;
        }

        return false;
    }

    // -------------------------------------------------------------------------
    // Token::TurnLengthOrPercentageIntoNumberIfZero
    //
    // Converts a zero-length dimension or zero percentage token to a plain
    // number token ("0").  This combines the logic of TurnLengthIntoNumberIfZero
    // with percentage handling.
    //
    // Behavior:
    //   - For kPercentage tokens with value "0": Converts to kNumber "0".
    //   - For kDimension tokens: Delegates to TurnLengthIntoNumberIfZero.
    //
    // Returns:
    //   - true if the conversion was performed, false otherwise.
    //
    // Example:
    //   Token t("0%", TokenType::kPercentage);
    //   bool changed = t.TurnLengthOrPercentageIntoNumberIfZero();
    //   // changed == true, t.text == "0", t.kind == kNumber
    //
    // Edge Cases:
    //   - Token "0px" → converts to "0" (true).
    //   - Token "50%" → no change (false).
    // -------------------------------------------------------------------------
    bool Token::TurnLengthOrPercentageIntoNumberIfZero() {
        if (kind == TokenType::kPercentage && PercentageValue() == "0") {
            kind = TokenType::kNumber;
            text = "0";
            return true;
        }
        return TurnLengthIntoNumberIfZero();
    }

    // -------------------------------------------------------------------------
    // Token::PercentageValue
    //
    // Extracts the numeric value from a percentage token by removing the
    // trailing '%' character.
    //
    // Returns:
    //   - The text of the token without the last character (the '%').
    //
    // Example:
    //   Token t("50%", TokenType::kPercentage);
    //   t.PercentageValue() == "50"
    //
    // Edge Cases:
    //   - Token "0%" → returns "0".
    //   - Token "100.5%" → returns "100.5".
    //   - Assumes the token is a percentage; no validation is performed.
    // -------------------------------------------------------------------------
    std::string Token::PercentageValue() const {
        return text.substr(0, text.size() - 1);
    }

    // -------------------------------------------------------------------------
    // Token::DimensionValue
    //
    // Extracts the numeric value from a dimension token (e.g., "10px" → "10").
    // Uses the unit_offset field to determine where the number ends and the
    // unit begins.
    //
    // Returns:
    //   - The text of the token up to (but not including) the unit.
    //
    // Example:
    //   Token t("3.5em", TokenType::kDimension, .unit_offset=3);
    //   t.DimensionValue() == "3.5"
    //
    // Edge Cases:
    //   - Token "0" with unit_offset=1 → returns "" (no value, only unit).
    //   - Assumes the token is a dimension; no validation is performed.
    // -------------------------------------------------------------------------
    std::string Token::DimensionValue() const {
        return text.substr(0, unit_offset);
    }

    // -------------------------------------------------------------------------
    // Token::DimensionUnit
    //
    // Extracts the unit suffix from a dimension token (e.g., "10px" → "px").
    // Uses the unit_offset field to determine where the unit starts.
    //
    // Returns:
    //   - The text of the token from unit_offset to the end.
    //
    // Example:
    //   Token t("3.5em", TokenType::kDimension, .unit_offset=3);
    //   t.DimensionUnit() == "em"
    //
    // Edge Cases:
    //   - Token "10" with unit_offset=2 → returns "" (no unit).
    //   - Assumes the token is a dimension; no validation is performed.
    // -------------------------------------------------------------------------
    std::string Token::DimensionUnit() const {
        return text.substr(unit_offset);
    }

    // -------------------------------------------------------------------------
    // Token::DimensionUnitIsSafeLength
    //
    // Checks if the dimension's unit is a standard CSS length unit that is
    // universally supported across browsers.  These units can be safely used
    // without vendor prefixes or fallbacks.
    //
    // Supported units:
    //   - cm (centimeters)
    //   - em (em units, relative to font-size)
    //   - in (inches)
    //   - mm (millimeters)
    //   - pc (picas)
    //   - pt (points)
    //   - px (pixels)
    //
    // Returns:
    //   - true if the unit is a safe length, false otherwise.
    //
    // Example:
    //   Token t("10px", TokenType::kDimension);
    //   t.DimensionUnitIsSafeLength() == true
    //
    //   Token t("10rem", TokenType::kDimension);
    //   t.DimensionUnitIsSafeLength() == false (rem not in safe list)
    //
    // Edge Cases:
    //   - Comparison is case-insensitive ("PX" == "px").
    //   - Returns false for non-length units like "deg", "s", "Hz".
    // -------------------------------------------------------------------------
    bool Token::DimensionUnitIsSafeLength() const {
        auto unit = helpers::ToLowerASCII(DimensionUnit());
        // These units can be reasonably expected to be supported everywhere.
        if (unit == "cm" || unit == "em" || unit == "in" || unit == "mm" || unit == "pc" || unit == "pt" || unit == "px") {
            return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Token::IsZero
    //
    // Checks if the token is the number zero (kNumber with text "0").
    //
    // Example:
    //   Token t("0", TokenType::kNumber);
    //   t.IsZero() == true
    //
    //   Token t("0px", TokenType::kDimension);
    //   t.IsZero() == false (it's a dimension, not a number)
    // -------------------------------------------------------------------------
    bool Token::IsZero() const {
        return kind == TokenType::kNumber && text == "0";
    }

    // -------------------------------------------------------------------------
    // Token::IsOne
    //
    // Checks if the token is the number one (kNumber with text "1").
    //
    // Example:
    //   Token t("1", TokenType::kNumber);
    //   t.IsOne() == true
    //
    //   Token t("1.0", TokenType::kNumber);
    //   t.IsOne() == false (text is "1.0", not "1")
    // -------------------------------------------------------------------------
    bool Token::IsOne() const {
        return kind == TokenType::kNumber && text == "1";
    }

    // -------------------------------------------------------------------------
    // Token::IsAngle
    //
    // Checks if the token is an angle dimension (e.g., "45deg", "3.14rad").
    // Only kDimension tokens with angle units are considered.
    //
    // Supported angle units (case-insensitive):
    //   - deg (degrees, 0-360)
    //   - grad (grads, 0-400)
    //   - rad (radians, 0-2π)
    //   - turn (turns, 0-1)
    //
    // Example:
    //   Token t("90deg", TokenType::kDimension);
    //   t.IsAngle() == true
    //
    //   Token t("10px", TokenType::kDimension);
    //   t.IsAngle() == false (px is a length, not an angle)
    //
    //   Token t("45", TokenType::kNumber);
    //   t.IsAngle() == false (plain number, not a dimension)
    // -------------------------------------------------------------------------
    bool Token::IsAngle() const {
        if (kind == TokenType::kDimension) {
            auto unit = helpers::ToLowerASCII(DimensionUnit());
            return unit == "deg" || unit == "grad" || unit == "rad" || unit == "turn";
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // CloneTokensWithoutImportRecords
    //
    // Deep-copies a vector of tokens without remapping import records.  This
    // is used when cloning tokens that stay within the same file.
    //
    // Behavior:
    //   - Each token is copied by value.
    //   - If a token has children, they are recursively cloned.
    //   - Import records are NOT cloned (they remain shared).
    //
    // Returns:
    //   - A new vector containing independent copies of all tokens.
    //
    // Example:
    //   std::vector<Token> original = {Token("red"), Token("blue")};
    //   auto clone = CloneTokensWithoutImportRecords(original);
    //   // clone is a deep copy; modifying it does not affect original
    //
    // Edge Cases:
    //   - Empty input produces an empty output vector.
    //   - Preserves all token fields (kind, text, flags, etc.).
    // -------------------------------------------------------------------------
    std::vector<Token> CloneTokensWithoutImportRecords(const std::vector<Token>& tokens_in) {
        std::vector<Token> tokens_out;
        tokens_out.reserve(tokens_in.size());
        for (const auto& t : tokens_in) {
            Token clone = t;
            if (t.children != nullptr) {
                clone.children = std::make_shared<std::vector<Token>>(CloneTokensWithoutImportRecords(*t.children));
            }
            tokens_out.push_back(std::move(clone));
        }
        return tokens_out;
    }

    // -------------------------------------------------------------------------
    // CloneTokensWithImportRecords
    //
    // Deep-copies a vector of tokens while remapping import records from one
    // file to another.  This is used when moving tokens between files during
    // bundling or concatenation.
    //
    // Behavior:
    //   - Each token is copied by value.
    //   - Source location (loc.start) is reset to 0 for all cloned tokens.
    //   - For URL tokens: The import record index is remapped to a new index
    //     in the output import records vector.
    //   - If a token has children, they are recursively cloned with the same
    //     import record remapping.
    //
    // Parameters:
    //   - tokens_in: Source tokens to clone.
    //   - import_records_in: Source file's import records.
    //   - import_records_out: Output import records (appended to).
    //
    // Returns:
    //   - A new vector containing independent copies with remapped import records.
    //
    // Example:
    //   std::vector<Token> src = {Token("url('img.png')", TokenType::kUrl)};
    //   std::vector<ImportRecord> records_in = {{.path="img.png"}};
    //   std::vector<ImportRecord> records_out;
    //   auto clone = CloneTokensWithImportRecords(src, records_in, records_out);
    //   // clone[0].payload_index == 0 (index in records_out)
    //   // records_out[0].path == "img.png"
    //
    // Edge Cases:
    //   - Empty input produces an empty output vector.
    //   - Non-URL tokens are cloned but their payload_index is unchanged.
    // -------------------------------------------------------------------------
    std::vector<Token> CloneTokensWithImportRecords(
        const std::vector<Token>& tokens_in,
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out) {
        std::vector<Token> tokens_out;
        tokens_out.reserve(tokens_in.size());

        for (const auto& source : tokens_in) {
            Token t = source;

            // Clear the source mapping if this token is being used in another file
            t.loc.start = 0;

            // If this is a URL token, also clone the import record
            if (t.kind == TokenType::kUrl) {
                uint32_t import_record_index = static_cast<uint32_t>(import_records_out.size());
                import_records_out.push_back(import_records_in[t.payload_index]);
                t.payload_index = import_record_index;
            }

            // Also search for URL tokens in this token's children
            if (t.children != nullptr) {
                auto children = CloneTokensWithImportRecords(*t.children, import_records_in, import_records_out);
                t.children = std::make_shared<std::vector<Token>>(std::move(children));
            }

            tokens_out.push_back(std::move(t));
        }

        return tokens_out;
    }

    // -------------------------------------------------------------------------
    // CloneMediaQueriesWithImportRecords
    //
    // Deep-copies a vector of media queries while remapping import records.
    // Each media query's internal data is cloned using its own
    // CloneWithImportRecords method.
    //
    // Parameters:
    //   - queries_in: Source media queries to clone.
    //   - import_records_in: Source file's import records.
    //   - import_records_out: Output import records (appended to).
    //
    // Returns:
    //   - A new vector containing independent copies of all media queries.
    //
    // Example:
    //   std::vector<MediaQuery> src = {MediaQuery(...)};
    //   auto clone = CloneMediaQueriesWithImportRecords(src, records_in, records_out);
    //   // clone is a deep copy with remapped import records
    //
    // Edge Cases:
    //   - Empty input produces an empty output vector.
    //   - Each query's data is cloned polymorphically (virtual CloneWithImportRecords).
    // -------------------------------------------------------------------------
    std::vector<MediaQuery> CloneMediaQueriesWithImportRecords(
        const std::vector<MediaQuery>& queries_in,
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out) {
        std::vector<MediaQuery> queries_out;
        queries_out.reserve(queries_in.size());

        // Recursively clone each query
        for (const auto& query : queries_in) {
            MediaQuery clone;
            clone.data = query.data->CloneWithImportRecords(import_records_in, import_records_out);
            queries_out.push_back(std::move(clone));
        }

        return queries_out;
    }


    // -------------------------------------------------------------------------
    // CrossFileEqualityCheck::RefsAreEquivalent
    //
    // Determines if two symbol references (from potentially different files)
    // refer to the same CSS symbol.  This is used when comparing AST nodes
    // from different source files.
    //
    // Algorithm:
    //   1. If the references are identical (same file + same index), return true.
    //   2. If no symbol table is available, return false.
    //   3. Follow both references to their canonical definitions using FollowSymbols.
    //   4. If the canonical references are identical, return true.
    //   5. Check if both symbols are global CSS symbols with the same original name.
    //
    // Returns:
    //   - true if the references are equivalent, false otherwise.
    //
    // Example:
    //   // File A defines: .foo { color: red; }
    //   // File B defines: .foo { color: blue; }
    //   // RefsAreEquivalent can determine these are the same class name
    //   // even though they come from different files.
    //
    // Edge Cases:
    //   - Returns false if symbols pointer is null.
    //   - Two different local symbols with the same name are NOT equivalent.
    //   - Two global CSS symbols with the same name ARE equivalent.
    // -------------------------------------------------------------------------
    bool CrossFileEqualityCheck::RefsAreEquivalent(guchho::compiler::Ref a, guchho::compiler::Ref b) const {
        if (a == b) {
            return true;
        }
        if (symbols == nullptr) {
            return false;
        }
        a = FollowSymbols(*symbols, a);
        b = FollowSymbols(*symbols, b);
        if (a == b) {
            return true;
        }
        const auto& symbol_a = *symbols->Get(a);
        const auto& symbol_b = *symbols->Get(b);
        return symbol_a.kind == guchho::compiler::SymbolKind::kGlobalCSS &&
            symbol_b.kind == guchho::compiler::SymbolKind::kGlobalCSS &&
            symbol_a.original_name == symbol_b.original_name;
    }


    // -------------------------------------------------------------------------
    // RulesEqual
    //
    // Compares two vectors of CSS rules for structural equality.  Each rule
    // is compared using its virtual Equal method, which handles the specific
    // comparison logic for each rule type.
    //
    // Parameters:
    //   - a, b: The rule vectors to compare.
    //   - check: Optional cross-file equality check (nullptr for same-file).
    //
    // Returns:
    //   - true if the vectors have the same size and all rules are equal.
    //
    // Example:
    //   std::vector<Rule> rules_a = {Rule(RSelector(...)), Rule(RDeclaration(...))};
    //   std::vector<Rule> rules_b = {Rule(RSelector(...)), Rule(RDeclaration(...))};
    //   RulesEqual(rules_a, rules_b, nullptr) == true
    //
    // Edge Cases:
    //   - Returns false immediately if vectors have different sizes.
    //   - Empty vectors are considered equal.
    // -------------------------------------------------------------------------
    bool RulesEqual(const std::vector<Rule>& a, const std::vector<Rule>& b, const CrossFileEqualityCheck* check) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++) {
            if (!a[i].data->Equal(b[i].data.get(), check)) {
                return false;
            }
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // HashRules
    //
    // Computes a hash value for a vector of CSS rules.  This hash is used
    // for efficient deduplication of rule sequences.  The hash incorporates:
    //   - The number of rules in the vector.
    //   - Each rule's hash (via virtual Hash method).
    //
    // Parameters:
    //   - hash: Initial hash value (seed).
    //   - rules: Vector of rules to hash.
    //
    // Returns:
    //   - The combined hash value.
    //
    // Example:
    //   std::vector<Rule> rules = {Rule(RDeclaration(...))};
    //   uint32_t h = HashRules(0, rules);
    //   // h is a deterministic hash for this rule sequence
    //
    // Edge Cases:
    //   - Empty rules vector produces a hash based only on the size (0).
    //   - If a rule's Hash() returns nullopt, it contributes 0 to the hash.
    // -------------------------------------------------------------------------
    uint32_t HashRules(uint32_t hash, const std::vector<Rule>& rules) {
        hash = HashCombine(hash, static_cast<uint32_t>(rules.size()));
        for (const auto& child : rules) {
            if (auto child_hash = child.data->Hash()) {
                hash = HashCombine(hash, *child_hash);
            } else {
                hash = HashCombine(hash, 0);
            }
        }
        return hash;
    }

    // -------------------------------------------------------------------------
    // RAtCharset::Equal
    //
    // Compares two @charset rules for equality.  The encoding string is
    // compared case-sensitively.
    //
    // Example:
    //   RAtCharset a("utf-8");
    //   RAtCharset b("utf-8");
    //   a.Equal(&b) == true
    //
    // Edge Cases:
    //   - Returns false if the other rule is not an RAtCharset.
    //   - Cross-file check is ignored (charset rules are file-specific).
    // -------------------------------------------------------------------------
    bool RAtCharset::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        (void)check;
        auto b = dynamic_cast<const RAtCharset*>(rule);
        return b != nullptr && encoding == b->encoding;
    }

    std::optional<uint32_t> RAtCharset::Hash() const {
        uint32_t hash = 1;
        hash = HashCombineString(hash, encoding);
        return hash;
    }

    // -------------------------------------------------------------------------
    // ImportConditions::CloneWithImportRecords
    //
    // Deep-copies import conditions (layers, supports, and media queries)
    // while remapping import records.  This is used when cloning @import
    // rules between files.
    //
    // Behavior:
    //   - Clones layers tokens using CloneTokensWithImportRecords.
    //   - Clones supports tokens using CloneTokensWithImportRecords.
    //   - Clones media queries using CloneMediaQueriesWithImportRecords.
    //
    // Returns:
    //   - A new ImportConditions with remapped import records.
    //
    // Example:
    //   ImportConditions original = ...;
    //   auto clone = original.CloneWithImportRecords(records_in, records_out);
    //   // clone is a deep copy with remapped import records
    //
    // Edge Cases:
    //   - Handles empty vectors correctly.
    //   - All components are cloned independently.
    // -------------------------------------------------------------------------
    ImportConditions ImportConditions::CloneWithImportRecords(
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out) const {
        ImportConditions result;
        result.layers = CloneTokensWithImportRecords(layers, import_records_in, import_records_out);
        result.supports = CloneTokensWithImportRecords(supports, import_records_in, import_records_out);
        result.queries = CloneMediaQueriesWithImportRecords(queries, import_records_in, import_records_out);
        return result;
    }

    bool RAtImport::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        (void)rule;
        (void)check;
        return false;
    }

    std::optional<uint32_t> RAtImport::Hash() const {
        return std::nullopt;
    }

    bool RAtKeyframes::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const RAtKeyframes*>(rule);
        if (b != nullptr && EqualFoldASCII(at_token, b->at_token) &&
            (check == nullptr ? name.ref == b->name.ref : check->RefsAreEquivalent(name.ref, b->name.ref)) &&
            blocks.size() == b->blocks.size()) {
            for (size_t i = 0; i < blocks.size(); i++) {
                auto& ai = blocks[i];
                auto& bi = b->blocks[i];
                if (ai.selectors.size() != bi.selectors.size()) {
                    return false;
                }
                for (size_t j = 0; j < ai.selectors.size(); j++) {
                    if (ai.selectors[j] != bi.selectors[j]) {
                        return false;
                    }
                }
                if (!RulesEqual(ai.rules, bi.rules, check)) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    std::optional<uint32_t> RAtKeyframes::Hash() const {
        uint32_t hash = 2;
        hash = HashCombineString(hash, at_token);
        hash = HashCombine(hash, static_cast<uint32_t>(blocks.size()));
        for (const auto& block : blocks) {
            hash = HashCombine(hash, static_cast<uint32_t>(block.selectors.size()));
            for (const auto& sel : block.selectors) {
                hash = HashCombineString(hash, sel);
            }
            hash = HashRules(hash, block.rules);
        }
        return hash;
    }

    bool RKnownAt::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const RKnownAt*>(rule);
        return b != nullptr && EqualFoldASCII(at_token, b->at_token) &&
            TokensEqual(prelude, b->prelude, check) && RulesEqual(rules, b->rules, check);
    }

    std::optional<uint32_t> RKnownAt::Hash() const {
        uint32_t hash = 3;
        hash = HashCombineString(hash, at_token);
        hash = HashTokens(hash, prelude);
        hash = HashRules(hash, rules);
        return hash;
    }

    bool RUnknownAt::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const RUnknownAt*>(rule);
        return b != nullptr && EqualFoldASCII(at_token, b->at_token) &&
            TokensEqual(prelude, b->prelude, check) && TokensEqual(block, b->block, check);
    }

    std::optional<uint32_t> RUnknownAt::Hash() const {
        uint32_t hash = 4;
        hash = HashCombineString(hash, at_token);
        hash = HashTokens(hash, prelude);
        hash = HashTokens(hash, block);
        return hash;
    }

    bool RSelector::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const RSelector*>(rule);
        return b != nullptr && ComplexSelectorsEqual(selectors, b->selectors, check) && RulesEqual(rules, b->rules, check);
    }

    std::optional<uint32_t> RSelector::Hash() const {
        uint32_t hash = 5;
        hash = HashCombine(hash, static_cast<uint32_t>(selectors.size()));
        hash = HashComplexSelectors(hash, selectors);
        hash = HashRules(hash, rules);
        return hash;
    }

    bool RQualified::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const RQualified*>(rule);
        return b != nullptr && TokensEqual(prelude, b->prelude, check) && RulesEqual(rules, b->rules, check);
    }

    std::optional<uint32_t> RQualified::Hash() const {
        uint32_t hash = 6;
        hash = HashTokens(hash, prelude);
        hash = HashRules(hash, rules);
        return hash;
    }

    bool RDeclaration::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const RDeclaration*>(rule);
        return b != nullptr && key_text == b->key_text && TokensEqual(value, b->value, check) && important == b->important;
    }

    std::optional<uint32_t> RDeclaration::Hash() const {
        uint32_t hash;
        if (key == kDUnknown) {
            if (important) {
                hash = 7;
            } else {
                hash = 8;
            }
            hash = HashCombineString(hash, key_text);
        } else {
            if (important) {
                hash = 9;
            } else {
                hash = 10;
            }
            hash = HashCombine(hash, static_cast<uint32_t>(key));
        }
        hash = HashTokens(hash, value);
        return hash;
    }

    bool RBadDeclaration::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const RBadDeclaration*>(rule);
        return b != nullptr && TokensEqual(tokens, b->tokens, check);
    }

    std::optional<uint32_t> RBadDeclaration::Hash() const {
        uint32_t hash = 7;
        hash = HashTokens(hash, tokens);
        return hash;
    }

    bool RComment::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        (void)check;
        auto b = dynamic_cast<const RComment*>(rule);
        return b != nullptr && text == b->text;
    }

    std::optional<uint32_t> RComment::Hash() const {
        uint32_t hash = 8;
        hash = HashCombineString(hash, text);
        return hash;
    }

    bool RAtLayer::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const RAtLayer*>(rule);
        if (b != nullptr && names.size() == b->names.size() && rules.size() == b->rules.size()) {
            for (size_t i = 0; i < names.size(); i++) {
                auto& ai = names[i];
                auto& bi = b->names[i];
                if (ai.size() != bi.size()) {
                    return false;
                }
                for (size_t j = 0; j < ai.size(); j++) {
                    if (ai[j] != bi[j]) {
                        return false;
                    }
                }
            }
            if (!RulesEqual(rules, b->rules, check)) {
                return false;
            }
        }
        return false;
    }

    std::optional<uint32_t> RAtLayer::Hash() const {
        uint32_t hash = 9;
        hash = HashCombine(hash, static_cast<uint32_t>(names.size()));
        for (const auto& parts : names) {
            hash = HashCombine(hash, static_cast<uint32_t>(parts.size()));
            for (const auto& part : parts) {
                hash = HashCombineString(hash, part);
            }
        }
        hash = HashRules(hash, rules);
        return hash;
    }

    bool RAtMedia::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const RAtMedia*>(rule);
        return b != nullptr && MediaQueriesEqual(queries, b->queries, check) && RulesEqual(rules, b->rules, check);
    }

    std::optional<uint32_t> RAtMedia::Hash() const {
        uint32_t hash = 10;
        hash = HashMediaQueries(hash, queries);
        hash = HashRules(hash, rules);
        return hash;
    }

    bool RAtScope::Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const RAtScope*>(rule);
        return b != nullptr && ComplexSelectorsEqual(start, b->start, check) &&
            ComplexSelectorsEqual(end, b->end, check) && RulesEqual(rules, b->rules, check);
    }

    std::optional<uint32_t> RAtScope::Hash() const {
        uint32_t hash = 11;
        hash = HashComplexSelectors(hash, start);
        hash = HashComplexSelectors(hash, end);
        hash = HashRules(hash, rules);
        return hash;
    }


    // -------------------------------------------------------------------------
    // MediaQueriesEqual
    //
    // Compares two vectors of media queries for structural equality.  Each
    // media query is compared using its virtual Equal method.
    //
    // Parameters:
    //   - a, b: The media query vectors to compare.
    //   - check: Optional cross-file equality check (nullptr for same-file).
    //
    // Returns:
    //   - true if the vectors have the same size and all queries are equal.
    //
    // Example:
    //   std::vector<MediaQuery> q1 = {MediaQuery(MQType(...))};
    //   std::vector<MediaQuery> q2 = {MediaQuery(MQType(...))};
    //   MediaQueriesEqual(q1, q2, nullptr) == true
    //
    // Edge Cases:
    //   - Returns false immediately if vectors have different sizes.
    //   - Empty vectors are considered equal.
    // -------------------------------------------------------------------------
    bool MediaQueriesEqual(const std::vector<MediaQuery>& a, const std::vector<MediaQuery>& b, const CrossFileEqualityCheck* check) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++) {
            if (!a[i].data->Equal(b[i].data.get(), check)) {
                return false;
            }
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // MediaQueriesEqualIgnoringWhitespace
    //
    // Compares two vectors of media queries for equality while ignoring
    // whitespace differences.  This is useful when comparing media queries
    // that may have different formatting but are semantically equivalent.
    //
    // Parameters:
    //   - a, b: The media query vectors to compare.
    //
    // Returns:
    //   - true if the vectors have the same size and all queries are equal
    //     (ignoring whitespace).
    //
    // Example:
    //   // Query: screen and (min-width: 1024px)
    //   // Query: screen and (min-width:1024px)
    //   // MediaQueriesEqualIgnoringWhitespace() == true
    //
    // Edge Cases:
    //   - Returns false immediately if vectors have different sizes.
    //   - Empty vectors are considered equal.
    // -------------------------------------------------------------------------
    bool MediaQueriesEqualIgnoringWhitespace(const std::vector<MediaQuery>& a, const std::vector<MediaQuery>& b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++) {
            if (!a[i].data->EqualIgnoringWhitespace(b[i].data.get())) {
                return false;
            }
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // HashMediaQueries
    //
    // Computes a hash value for a vector of media queries.  This hash is
    // used for efficient deduplication of media query sequences.
    //
    // Parameters:
    //   - hash: Initial hash value (seed).
    //   - queries: Vector of media queries to hash.
    //
    // Returns:
    //   - The combined hash value.
    //
    // Example:
    //   std::vector<MediaQuery> queries = {MediaQuery(MQType(...))};
    //   uint32_t h = HashMediaQueries(0, queries);
    //   // h is a deterministic hash for this query sequence
    //
    // Edge Cases:
    //   - Empty queries vector produces a hash based only on the size (0).
    //   - Each query's hash is computed via virtual Hash method.
    // -------------------------------------------------------------------------
    uint32_t HashMediaQueries(uint32_t hash, const std::vector<MediaQuery>& queries) {
        hash = HashCombine(hash, static_cast<uint32_t>(queries.size()));
        for (const auto& q : queries) {
            hash = HashCombine(hash, q.data->Hash());
        }
        return hash;
    }

    bool MQType::Equal(const MQ* query, const CrossFileEqualityCheck* check) const {
        auto p = dynamic_cast<const MQType*>(query);
        if (p != nullptr && op == p->op && type == p->type) {
            return (and_or_null.data == nullptr && p->and_or_null.data == nullptr) ||
                (and_or_null.data != nullptr && p->and_or_null.data != nullptr &&
                    and_or_null.data->Equal(p->and_or_null.data.get(), check));
        }
        return false;
    }

    bool MQType::EqualIgnoringWhitespace(const MQ* query) const {
        auto p = dynamic_cast<const MQType*>(query);
        if (p != nullptr && op == p->op && type == p->type) {
            return (and_or_null.data == nullptr && p->and_or_null.data == nullptr) ||
                (and_or_null.data != nullptr && p->and_or_null.data != nullptr &&
                    and_or_null.data->EqualIgnoringWhitespace(p->and_or_null.data.get()));
        }
        return false;
    }

    uint32_t MQType::Hash() const {
        uint32_t hash = 0;
        hash = HashCombine(hash, static_cast<uint32_t>(op));
        hash = HashCombineString(hash, type);
        if (and_or_null.data != nullptr) {
            hash = HashCombine(hash, and_or_null.data->Hash());
        }
        return hash;
    }

    std::unique_ptr<MQ> MQType::CloneWithImportRecords(
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out) const {
        auto result = std::make_unique<MQType>();
        result->op = op;
        result->type = type;
        if (and_or_null.data != nullptr) {
            result->and_or_null.data = and_or_null.data->CloneWithImportRecords(import_records_in, import_records_out);
        }
        return result;
    }

    bool MQNot::Equal(const MQ* query, const CrossFileEqualityCheck* check) const {
        auto p = dynamic_cast<const MQNot*>(query);
        return p != nullptr && inner.data->Equal(p->inner.data.get(), check);
    }

    bool MQNot::EqualIgnoringWhitespace(const MQ* query) const {
        auto p = dynamic_cast<const MQNot*>(query);
        return p != nullptr && inner.data->EqualIgnoringWhitespace(p->inner.data.get());
    }

    uint32_t MQNot::Hash() const {
        uint32_t hash = 1;
        hash = HashCombine(hash, inner.data->Hash());
        return hash;
    }

    std::unique_ptr<MQ> MQNot::CloneWithImportRecords(
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out) const {
        auto result = std::make_unique<MQNot>();
        result->inner.data = inner.data->CloneWithImportRecords(import_records_in, import_records_out);
        return result;
    }

    bool MQBinary::Equal(const MQ* query, const CrossFileEqualityCheck* check) const {
        auto p = dynamic_cast<const MQBinary*>(query);
        return p != nullptr && op == p->op && MediaQueriesEqual(terms, p->terms, check);
    }

    bool MQBinary::EqualIgnoringWhitespace(const MQ* query) const {
        auto p = dynamic_cast<const MQBinary*>(query);
        return p != nullptr && op == p->op && MediaQueriesEqualIgnoringWhitespace(terms, p->terms);
    }

    uint32_t MQBinary::Hash() const {
        uint32_t hash = 2;
        hash = HashCombine(hash, static_cast<uint32_t>(op));
        hash = HashMediaQueries(hash, terms);
        return hash;
    }

    std::unique_ptr<MQ> MQBinary::CloneWithImportRecords(
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out) const {
        auto result = std::make_unique<MQBinary>();
        result->op = op;
        for (const auto& term : terms) {
            MediaQuery clone;
            clone.data = term.data->CloneWithImportRecords(import_records_in, import_records_out);
            result->terms.push_back(std::move(clone));
        }
        return result;
    }

    bool MQArbitraryTokens::Equal(const MQ* query, const CrossFileEqualityCheck* check) const {
        auto p = dynamic_cast<const MQArbitraryTokens*>(query);
        return p != nullptr && TokensEqual(tokens, p->tokens, check);
    }

    bool MQArbitraryTokens::EqualIgnoringWhitespace(const MQ* query) const {
        auto p = dynamic_cast<const MQArbitraryTokens*>(query);
        return p != nullptr && TokensEqualIgnoringWhitespace(tokens, p->tokens);
    }

    uint32_t MQArbitraryTokens::Hash() const {
        uint32_t hash = 3;
        hash = HashTokens(hash, tokens);
        return hash;
    }

    std::unique_ptr<MQ> MQArbitraryTokens::CloneWithImportRecords(
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out) const {
        auto result = std::make_unique<MQArbitraryTokens>();
        result->tokens = CloneTokensWithImportRecords(tokens, import_records_in, import_records_out);
        return result;
    }

    bool MQPlainOrBoolean::Equal(const MQ* query, const CrossFileEqualityCheck* check) const {
        auto p = dynamic_cast<const MQPlainOrBoolean*>(query);
        return p != nullptr && name == p->name && TokensEqual(value_or_nil, p->value_or_nil, check);
    }

    bool MQPlainOrBoolean::EqualIgnoringWhitespace(const MQ* query) const {
        auto p = dynamic_cast<const MQPlainOrBoolean*>(query);
        return p != nullptr && name == p->name && TokensEqualIgnoringWhitespace(value_or_nil, p->value_or_nil);
    }

    uint32_t MQPlainOrBoolean::Hash() const {
        uint32_t hash = 4;
        hash = HashCombineString(hash, name);
        hash = HashTokens(hash, value_or_nil);
        return hash;
    }

    std::unique_ptr<MQ> MQPlainOrBoolean::CloneWithImportRecords(
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out) const {
        auto result = std::make_unique<MQPlainOrBoolean>();
        result->name = name;
        result->value_or_nil = CloneTokensWithImportRecords(value_or_nil, import_records_in, import_records_out);
        return result;
    }

    bool MQRange::Equal(const MQ* query, const CrossFileEqualityCheck* check) const {
        auto p = dynamic_cast<const MQRange*>(query);
        return p != nullptr && before_cmp == p->before_cmp && after_cmp == p->after_cmp && name == p->name &&
            TokensEqual(before, p->before, check) && TokensEqual(after, p->after, check);
    }

    bool MQRange::EqualIgnoringWhitespace(const MQ* query) const {
        auto p = dynamic_cast<const MQRange*>(query);
        return p != nullptr && before_cmp == p->before_cmp && after_cmp == p->after_cmp && name == p->name &&
            TokensEqualIgnoringWhitespace(before, p->before) && TokensEqualIgnoringWhitespace(after, p->after);
    }

    uint32_t MQRange::Hash() const {
        uint32_t hash = 5;
        hash = HashTokens(hash, before);
        hash = HashCombine(hash, static_cast<uint32_t>(before_cmp));
        hash = HashCombineString(hash, name);
        hash = HashCombine(hash, static_cast<uint32_t>(after_cmp));
        hash = HashTokens(hash, after);
        return hash;
    }

    std::unique_ptr<MQ> MQRange::CloneWithImportRecords(
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out) const {
        auto result = std::make_unique<MQRange>();
        result->before = CloneTokensWithImportRecords(before, import_records_in, import_records_out);
        result->before_cmp = before_cmp;
        result->name = name;
        result->after_cmp = after_cmp;
        result->after = CloneTokensWithImportRecords(after, import_records_in, import_records_out);
        return result;
    }

    // -------------------------------------------------------------------------
    // ToString(MQCmp)
    //
    // Converts a media query comparison operator to its string representation.
    // This is used for debugging and error messages.
    //
    // Example:
    //   ToString(MQCmp::kMQCmpLt) == "<"
    //   ToString(MQCmp::kMQCmpLe) == "<="
    //   ToString(MQCmp::kMQCmpGt) == ">"
    //   ToString(MQCmp::kMQCmpGe) == ">="
    //   ToString(MQCmp::kMQCmpEq) == "="
    //
    // Edge Cases:
    //   - Returns "=" for unknown comparison operators (default case).
    // -------------------------------------------------------------------------
    const char* ToString(MQCmp cmp) {
        switch (cmp) {
        case MQCmp::kMQCmpLt: return "<";
        case MQCmp::kMQCmpLe: return "<=";
        case MQCmp::kMQCmpGt: return ">";
        case MQCmp::kMQCmpGe: return ">=";
        default: break;
        }
        return "=";
    }

    // -------------------------------------------------------------------------
    // Dir
    //
    // Returns the direction of a media query comparison operator:
    //   - -1 for "less than" operations (<, <=)
    //   - +1 for "greater than" operations (>, >=)
    //   - 0 for equality operations (=)
    //
    // This is used when evaluating media query conditions to determine the
    // direction of the comparison.
    //
    // Example:
    //   Dir(MQCmp::kMQCmpLt) == -1
    //   Dir(MQCmp::kMQCmpGt) == 1
    //   Dir(MQCmp::kMQCmpEq) == 0
    //
    // Edge Cases:
    //   - Returns 0 for unknown comparison operators (default case).
    // -------------------------------------------------------------------------
    int Dir(MQCmp cmp) {
        switch (cmp) {
        case MQCmp::kMQCmpLt:
        case MQCmp::kMQCmpLe:
            return -1;
        case MQCmp::kMQCmpGt:
        case MQCmp::kMQCmpGe:
            return 1;
        default: break;
        }
        return 0;
    }

    // -------------------------------------------------------------------------
    // Flip
    //
    // Flips a comparison operator to its logical opposite for range queries.
    // This is used when normalizing media query ranges like "400px < width < 800px".
    //
    // The mapping is:
    //   - < becomes >=
    //   - <= becomes >
    //   - > becomes <=
    //   - >= becomes <
    //   - = stays =
    //
    // Example:
    //   Flip(MQCmp::kMQCmpLt) == MQCmp::kMQCmpGe
    //   Flip(MQCmp::kMQCmpLe) == MQCmp::kMQCmpGt
    //   Flip(MQCmp::kMQCmpGt) == MQCmp::kMQCmpLe
    //   Flip(MQCmp::kMQCmpGe) == MQCmp::kMQCmpLt
    //   Flip(MQCmp::kMQCmpEq) == MQCmp::kMQCmpEq
    //
    // Edge Cases:
    //   - Returns the same operator for equality (no flip needed).
    // -------------------------------------------------------------------------
    MQCmp Flip(MQCmp cmp) {
        switch (cmp) {
        case MQCmp::kMQCmpLt: return MQCmp::kMQCmpGe;
        case MQCmp::kMQCmpLe: return MQCmp::kMQCmpGt;
        case MQCmp::kMQCmpGt: return MQCmp::kMQCmpLe;
        case MQCmp::kMQCmpGe: return MQCmp::kMQCmpLt;
        default: break;
        }
        return cmp;
    }

    // -------------------------------------------------------------------------
    // Reverse
    //
    // Reverses a comparison operator to its mirror image.  This is used when
    // comparing media queries from both sides (e.g., "min-width: 400px" is
    // equivalent to "width >= 400px").
    //
    // The mapping is:
    //   - < becomes >
    //   - <= becomes >=
    //   - > becomes <
    //   - >= becomes <=
    //   - = stays =
    //
    // Example:
    //   Reverse(MQCmp::kMQCmpLt) == MQCmp::kMQCmpGt
    //   Reverse(MQCmp::kMQCmpLe) == MQCmp::kMQCmpGe
    //   Reverse(MQCmp::kMQCmpGt) == MQCmp::kMQCmpLt
    //   Reverse(MQCmp::kMQCmpGe) == MQCmp::kMQCmpLe
    //   Reverse(MQCmp::kMQCmpEq) == MQCmp::kMQCmpEq
    //
    // Edge Cases:
    //   - Returns the same operator for equality (no reversal needed).
    // -------------------------------------------------------------------------
    MQCmp Reverse(MQCmp cmp) {
        switch (cmp) {
        case MQCmp::kMQCmpLt: return MQCmp::kMQCmpGt;
        case MQCmp::kMQCmpLe: return MQCmp::kMQCmpGe;
        case MQCmp::kMQCmpGt: return MQCmp::kMQCmpLt;
        case MQCmp::kMQCmpGe: return MQCmp::kMQCmpLe;
        default: break;
        }
        return cmp;
    }

    // =============================================================================
    // Selectors
    // =============================================================================

    // -------------------------------------------------------------------------
    // ComplexSelectorsEqual
    //
    // Compares two vectors of complex selectors for structural equality.
    // Each complex selector is compared using its virtual Equal method.
    //
    // Parameters:
    //   - a, b: The selector vectors to compare.
    //   - check: Optional cross-file equality check (nullptr for same-file).
    //
    // Returns:
    //   - true if the vectors have the same size and all selectors are equal.
    //
    // Example:
    //   std::vector<ComplexSelector> sels_a = {ComplexSelector(...)};
    //   std::vector<ComplexSelector> sels_b = {ComplexSelector(...)};
    //   ComplexSelectorsEqual(sels_a, sels_b, nullptr) == true
    //
    // Edge Cases:
    //   - Returns false immediately if vectors have different sizes.
    //   - Empty vectors are considered equal.
    // -------------------------------------------------------------------------
    bool ComplexSelectorsEqual(const std::vector<ComplexSelector>& a, const std::vector<ComplexSelector>& b, const CrossFileEqualityCheck* check) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++) {
            if (!a[i].Equal(b[i], check)) {
                return false;
            }
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // HashComplexSelectors
    //
    // Computes a hash value for a vector of complex selectors.  This hash
    // incorporates:
    //   - The number of compound selectors in each complex selector.
    //   - Each compound selector's type selector name, subclass selectors,
    //     and combinator type.
    //
    // Parameters:
    //   - hash: Initial hash value (seed).
    //   - selectors: Vector of complex selectors to hash.
    //
    // Returns:
    //   - The combined hash value.
    //
    // Example:
    //   std::vector<ComplexSelector> sels = {ComplexSelector(...)};
    //   uint32_t h = HashComplexSelectors(0, sels);
    //   // h is a deterministic hash for this selector sequence
    //
    // Edge Cases:
    //   - Empty selectors vector produces a hash based only on the sizes.
    //   - Null type selectors contribute 0 to the hash.
    // -------------------------------------------------------------------------
    uint32_t HashComplexSelectors(uint32_t hash, const std::vector<ComplexSelector>& selectors) {
        for (const auto& complex : selectors) {
            hash = HashCombine(hash, static_cast<uint32_t>(complex.selectors.size()));
            for (const auto& sel : complex.selectors) {
                if (sel.type_selector != nullptr) {
                    hash = HashCombineString(hash, sel.type_selector->name.text);
                } else {
                    hash = HashCombine(hash, 0);
                }
                hash = HashCombine(hash, static_cast<uint32_t>(sel.subclass_selectors.size()));
                for (const auto& ss : sel.subclass_selectors) {
                    hash = HashCombine(hash, ss.data->Hash());
                }
                hash = HashCombine(hash, static_cast<uint32_t>(sel.combinator.byte_));
            }
        }
        return hash;
    }

    ComplexSelector ComplexSelector::Clone() const {
        ComplexSelector clone;
        clone.selectors.reserve(selectors.size());
        for (const auto& sel : selectors) {
            clone.selectors.push_back(sel.Clone());
        }
        return clone;
    }

    // -------------------------------------------------------------------------
    // ComplexSelector::ContainsNestingCombinator
    //
    // Checks if this complex selector contains a nesting combinator (&).
    // The nesting combinator is used in CSS Nesting to reference the parent
    // selector.
    //
    // Behavior:
    //   - Searches all compound selectors for nesting_selector_locs.
    //   - Recursively checks pseudo-class selectors with selector lists
    //     (e.g., :is(), :has(), :not()) for nested nesting combinators.
    //
    // Returns:
    //   - true if a nesting combinator is found anywhere in the selector.
    //
    // Example:
    //   // Selector: & .child
    //   // contains_nesting == true
    //
    //   // Selector: .parent & .child
    //   // contains_nesting == true
    //
    //   // Selector: :is(&) .child
    //   // contains_nesting == true (nested in pseudo-class)
    //
    // Edge Cases:
    //   - Returns false for selectors without any nesting combinators.
    //   - Handles deeply nested pseudo-class structures.
    // -------------------------------------------------------------------------
    bool ComplexSelector::ContainsNestingCombinator() const {
        for (const auto& inner : selectors) {
            if (!inner.nesting_selector_locs.empty()) {
                return true;
            }
            for (const auto& ss : inner.subclass_selectors) {
                if (auto pseudo = dynamic_cast<const SSPseudoClassWithSelectorList*>(ss.data.get())) {
                    for (const auto& nested : pseudo->selectors) {
                        if (nested.ContainsNestingCombinator()) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // ComplexSelector::IsRelative
    //
    // Checks if this complex selector is a "relative" selector according
    // to the CSS Nesting specification.  A relative selector:
    //   - Has no combinator on the first compound selector.
    //   - Contains a nesting combinator (&) somewhere in the selector.
    //
    // This distinction is important for CSS Nesting: relative selectors
    // cannot be used at the top level of a stylesheet.
    //
    // Reference: https://www.w3.org/TR/css-nesting-1/#syntax
    //
    // Example:
    //   // Selector: & .child
    //   // IsRelative() == true (no combinator, has &)
    //
    //   // Selector: .parent & .child
    //   // IsRelative() == false (has descendant combinator)
    //
    //   // Selector: .parent > & .child
    //   // IsRelative() == false (has child combinator)
    //
    // Edge Cases:
    //   - Assumes selectors[0] exists (non-empty selector).
    //   - Returns false if the selector has a combinator on the first compound.
    // -------------------------------------------------------------------------
    bool ComplexSelector::IsRelative() const {
        // https://www.w3.org/TR/css-nesting-1/#syntax
        if (selectors[0].combinator.byte_ == 0 && ContainsNestingCombinator()) {
            return false;
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // TokensContainAmpersandRecursive
    //
    // Checks if a token tree contains an ampersand (&) delimiter token.
    // The ampersand is used in CSS Nesting to reference the parent selector.
    //
    // This function recursively searches through all tokens and their children
    // for the kDelimAmpersand token type.
    //
    // Parameters:
    //   - tokens: The token tree to search.
    //
    // Returns:
    //   - true if an ampersand is found anywhere in the tree.
    //
    // Example:
    //   std::vector<Token> tokens = {Token("&"), Token(" "), Token(".child")};
    //   TokensContainAmpersandRecursive(tokens) == true
    //
    //   std::vector<Token> tokens = {Token(".parent"), Token(" "), Token(".child")};
    //   TokensContainAmpersandRecursive(tokens) == false
    //
    // Edge Cases:
    //   - Empty token vector returns false.
    //   - Searches recursively through all nested children.
    // -------------------------------------------------------------------------
    bool TokensContainAmpersandRecursive(const std::vector<Token>& tokens) {
        for (const auto& t : tokens) {
            if (t.kind == TokenType::kDelimAmpersand) {
                return true;
            }
            if (t.children != nullptr && TokensContainAmpersandRecursive(*t.children)) {
                return true;
            }
        }
        return false;
    }

    // Check pseudo-element usage
    // -------------------------------------------------------------------------
    // ComplexSelector::UsesPseudoElement
    //
    // Checks if this complex selector contains a pseudo-element.  Pseudo-
    // elements are special selectors that style specific parts of an element
    // (e.g., ::before, ::after, ::first-line).
    //
    // Behavior:
    //   - Searches all compound selectors for pseudo-class selectors.
    //   - Checks if the pseudo-class is a pseudo-element (is_element flag).
    //   - Also recognizes legacy single-colon pseudo-elements:
    //     :before, :after, :first-line, :first-letter.
    //
    // Returns:
    //   - true if a pseudo-element is found anywhere in the selector.
    //
    // Example:
    //   // Selector: .content::before
    //   // UsesPseudoElement() == true (::before)
    //
    //   // Selector: :first-line { ... }
    //   // UsesPseudoElement() == true (legacy :first-line)
    //
    //   // Selector: .parent > .child
    //   // UsesPseudoElement() == false
    //
    // Edge Cases:
    //   - Returns false if no pseudo-element is found.
    //   - Handles both modern (::) and legacy (:) pseudo-element syntax.
    // -------------------------------------------------------------------------
    bool ComplexSelector::UsesPseudoElement() const
    {
        for (const auto& sel : selectors) {
            for (const auto& ss : sel.subclass_selectors) {

                // Check pseudo-element usage
                if (auto class_ =
                        dynamic_cast<const SSPseudoClass*>(ss.data.get())) {

                    // If it is directly a pseudo-element, return true
                    if (class_->is_element) {
                        return true;
                    }

                    // Check pseudo-element usage
                    if (class_->name == "before" ||
                        class_->name == "after" ||
                        class_->name == "first-line" ||
                        class_->name == "first-letter") {
                        return true;
                    }
                }
            }
        }

        // Check pseudo-element usage
        return false;
    }

    // -------------------------------------------------------------------------
    // ComplexSelector::Equal
    //
    // Compares two complex selectors for structural equality.  Two complex
    // selectors are equal if they have the same number of compound selectors,
    // and each compound selector matches in type selector, subclass selectors,
    // combinator, and nesting combinator locations.
    //
    // Parameters:
    //   - b: The complex selector to compare against.
    //   - check: Optional cross-file equality check (nullptr for same-file).
    //
    // Returns:
    //   - true if the selectors are structurally equal.
    //
    // Example:
    //   // Selector: .parent > .child
    //   // Selector: .parent > .child
    //   // Equal() == true
    //
    //   // Selector: .parent > .child
    //   // Selector: .parent .child
    //   // Equal() == false (different combinators)
    //
    // Edge Cases:
    //   - Returns false if the selectors have different numbers of compounds.
    //   - Compares nesting combinator locations (not just presence).
    // -------------------------------------------------------------------------
    bool ComplexSelector::Equal(const ComplexSelector& b, const CrossFileEqualityCheck* check) const {
        if (selectors.size() != b.selectors.size()) {
            return false;
        }

        for (size_t i = 0; i < selectors.size(); i++) {
            auto& ai = selectors[i];
            auto& bi = b.selectors[i];
            if (ai.nesting_selector_locs.size() != bi.nesting_selector_locs.size() ||
                ai.combinator.byte_ != bi.combinator.byte_) {
                return false;
            }

            auto& ats = ai.type_selector;
            auto& bts = bi.type_selector;
            if ((ats == nullptr) != (bts == nullptr)) {
                return false;
            } else if (ats != nullptr && bts != nullptr && !ats->Equal(*bts)) {
                return false;
            }

            if (ai.subclass_selectors.size() != bi.subclass_selectors.size()) {
                return false;
            }
            for (size_t j = 0; j < ai.subclass_selectors.size(); j++) {
                if (!ai.subclass_selectors[j].data->Equal(bi.subclass_selectors[j].data.get(), check)) {
                    return false;
                }
            }
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // NameToken::Equal
    //
    // Compares two name tokens for equality.  Two name tokens are equal if
    // they have the same text and the same kind (e.g., both are identifiers
    // or both are strings).
    //
    // Example:
    //   NameToken a("div", NameKind::Ident);
    //   NameToken b("div", NameKind::Ident);
    //   a.Equal(b) == true
    //
    //   NameToken a("div", NameKind::Ident);
    //   NameToken b("div", NameKind::String);
    //   a.Equal(b) == false (different kinds)
    //
    // Edge Cases:
    //   - Does not compare source locations (only text and kind).
    // -------------------------------------------------------------------------
    bool NameToken::Equal(const NameToken& b) const {
        return text == b.text && kind == b.kind;
    }

    // -------------------------------------------------------------------------
    // NamespacedName::Range
    //
    // Returns the source range of this namespaced name.  The range encompasses
    // both the namespace prefix (if present) and the local name.
    //
    // Behavior:
    //   - If a namespace prefix exists, the range starts at the prefix and
    //     ends at the end of the local name.
    //   - If no namespace prefix, the range is just the local name's range.
    //
    // Returns:
    //   - A Range object representing the source location.
    //
    // Example:
    //   // For namespace|element
    //   // Range covers: "namespace|element"
    //
    //   // For element (no namespace)
    //   // Range covers: "element"
    //
    // Edge Cases:
    //   - Returns the name's range if namespace_prefix is null.
    // -------------------------------------------------------------------------
    guchho::logger::Range NamespacedName::Range() const {
        if (namespace_prefix != nullptr) {
            auto loc = namespace_prefix->range.loc;
            return guchho::logger::Range{loc, name.range.End() - loc.start};
        }
        return name.range;
    }

    // -------------------------------------------------------------------------
    // NamespacedName::Clone
    //
    // Deep-copies this namespaced name, creating an independent copy of the
    // namespace prefix (if present).
    //
    // Behavior:
    //   - Copies basic fields by value.
    //   - If a namespace prefix exists, clones it via NameToken copy constructor.
    //
    // Returns:
    //   - A new NamespacedName that is a deep copy of this one.
    //
    // Example:
    //   NamespacedName original = ...;
    //   NamespacedName clone = original.Clone();
    //   // Modifying clone does not affect original
    //
    // Edge Cases:
    //   - Handles null namespace_prefix correctly (no clone needed).
    // -------------------------------------------------------------------------
    NamespacedName NamespacedName::Clone() const {
        NamespacedName clone = *this;
        if (namespace_prefix != nullptr) {
            clone.namespace_prefix = std::make_shared<NameToken>(*namespace_prefix);
        }
        return clone;
    }

    // -------------------------------------------------------------------------
    // NamespacedName::Equal
    //
    // Compares two namespaced names for equality.  This comparison has a
    // subtle behavior: it compares the namespace prefix against the other
    // name (not the other prefix) when both prefixes exist.
    //
    // Comparison Rules:
    //   1. Local names must be equal.
    //   2. Either both must have a namespace prefix, or neither must.
    //   3. If both have prefixes, compare this prefix against the other name.
    //
    // This intentional asymmetry matches the reference implementation behavior.
    //
    // Example:
    //   // NamespacedName("ns", "element") == NamespacedName("ns", "element")
    //   // This is true because:
    //   //   - name.Equal(b.name) == true
    //   //   - Both have namespace_prefix
    //   //   - namespace_prefix->Equal(b.name) == true
    //
    // Edge Cases:
    //   - Returns true if both have null namespace_prefix and same name.
    //   - Returns false if one has a prefix and the other doesn't.
    // -------------------------------------------------------------------------
    bool NamespacedName::Equal(const NamespacedName& b) const {
        // Note: this intentionally mirrors the Go reference, which compares the
        // namespace prefix against the other name instead of the other prefix.
        return name.Equal(b.name) && (namespace_prefix == nullptr) == (b.namespace_prefix == nullptr) &&
            (namespace_prefix == nullptr || b.namespace_prefix == nullptr || namespace_prefix->Equal(b.name));
    }

    bool SSHash::Equal(const SS* ss, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const SSHash*>(ss);
        return b != nullptr &&
            (check == nullptr ? name.ref == b->name.ref : check->RefsAreEquivalent(name.ref, b->name.ref));
    }

    uint32_t SSHash::Hash() const {
        return 1;
    }

    std::unique_ptr<SS> SSHash::Clone() const {
        return std::make_unique<SSHash>(*this);
    }

    bool SSClass::Equal(const SS* ss, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const SSClass*>(ss);
        return b != nullptr &&
            (check == nullptr ? name.ref == b->name.ref : check->RefsAreEquivalent(name.ref, b->name.ref));
    }

    uint32_t SSClass::Hash() const {
        return 2;
    }

    std::unique_ptr<SS> SSClass::Clone() const {
        return std::make_unique<SSClass>(*this);
    }

    bool SSAttribute::Equal(const SS* ss, const CrossFileEqualityCheck* check) const {
        (void)check;
        auto b = dynamic_cast<const SSAttribute*>(ss);
        return b != nullptr && namespaced_name.Equal(b->namespaced_name) && matcher_op == b->matcher_op &&
            matcher_value == b->matcher_value && matcher_modifier == b->matcher_modifier;
    }

    uint32_t SSAttribute::Hash() const {
        uint32_t hash = 3;
        hash = HashCombineString(hash, namespaced_name.name.text);
        hash = HashCombineString(hash, matcher_op);
        hash = HashCombineString(hash, matcher_value);
        return hash;
    }

    std::unique_ptr<SS> SSAttribute::Clone() const {
        auto clone = std::make_unique<SSAttribute>(*this);
        clone->namespaced_name = namespaced_name.Clone();
        return clone;
    }

    bool SSPseudoClass::Equal(const SS* ss, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const SSPseudoClass*>(ss);
        bool args_equal = (args == nullptr && b->args == nullptr) ||
                        (args != nullptr && b->args != nullptr && TokensEqual(*args, *b->args, check));
        return b != nullptr && name == b->name && args_equal && is_element == b->is_element;
    }

    uint32_t SSPseudoClass::Hash() const {
        uint32_t hash = 4;
        hash = HashCombineString(hash, name);
        if (args != nullptr) {
            hash = HashTokens(hash, *args);
        }
        return hash;
    }

    std::unique_ptr<SS> SSPseudoClass::Clone() const {
        auto clone = std::make_unique<SSPseudoClass>(*this);
        if (args != nullptr) {
            clone->args = std::make_shared<std::vector<Token>>(CloneTokensWithoutImportRecords(*args));
        }
        return clone;
    }

    // -------------------------------------------------------------------------
    // HasNthIndex
    //
    // Checks if a pseudo-class kind represents an An+B index pseudo-class.
    // These pseudo-classes use the An+B notation to select elements based on
    // their position in a parent's list of children.
    //
    // The An+B pseudo-classes are:
    //   - nth-child(an+b)
    //   - nth-last-child(an+b)
    //   - nth-of-type(an+b)
    //   - nth-last-of-type(an+b)
    //
    // Parameters:
    //   - kind: The pseudo-class kind to check.
    //
    // Returns:
    //   - true if the kind is one of the An+B pseudo-classes.
    //
    // Example:
    //   HasNthIndex(PseudoClassKind::kPseudoClassNthChild) == true
    //   HasNthIndex(PseudoClassKind::kPseudoClassIs) == false
    //   HasNthIndex(PseudoClassKind::kPseudoClassNot) == false
    //
    // Edge Cases:
    //   - Returns false for non-An+B pseudo-classes.
    //   - Assumes enum values are contiguous (range check).
    // -------------------------------------------------------------------------
    bool HasNthIndex(PseudoClassKind kind) {
        return kind >= PseudoClassKind::kPseudoClassNthChild && kind <= PseudoClassKind::kPseudoClassNthOfType;
    }

    // -------------------------------------------------------------------------
    // ToString(PseudoClassKind)
    //
    // Converts a PseudoClassKind enum to its CSS string representation.
    // This is used for debugging, error messages, and code generation.
    //
    // Returns:
    //   - A string literal representing the pseudo-class name (e.g., "nth-child").
    //   - Returns "" for unknown kinds.
    //
    // Example:
    //   ToString(PseudoClassKind::kPseudoClassNthChild) == "nth-child"
    //   ToString(PseudoClassKind::kPseudoClassIs) == "is"
    //   ToString(PseudoClassKind::kPseudoClassNot) == "not"
    //   ToString(PseudoClassKind::kPseudoClassWhere) == "where"
    //
    // Edge Cases:
    //   - Returns "" for any unrecognized PseudoClassKind value.
    //   - Returns string literals (no allocation needed).
    // -------------------------------------------------------------------------
    const char* ToString(PseudoClassKind kind) {
        switch (kind) {
        case PseudoClassKind::kPseudoClassGlobal: return "global";
        case PseudoClassKind::kPseudoClassHas: return "has";
        case PseudoClassKind::kPseudoClassIs: return "is";
        case PseudoClassKind::kPseudoClassLocal: return "local";
        case PseudoClassKind::kPseudoClassNot: return "not";
        case PseudoClassKind::kPseudoClassNthChild: return "nth-child";
        case PseudoClassKind::kPseudoClassNthLastChild: return "nth-last-child";
        case PseudoClassKind::kPseudoClassNthLastOfType: return "nth-last-of-type";
        case PseudoClassKind::kPseudoClassNthOfType: return "nth-of-type";
        case PseudoClassKind::kPseudoClassWhere: return "where";
        }
        return "";
    }

    // -------------------------------------------------------------------------
    // NthIndex::Minify
    //
    // Minifies an An+B notation by converting it to the shortest equivalent
    // form.  This is used to normalize CSS pseudo-class selectors like
    // :nth-child(), :nth-last-child(), etc.
    //
    // The A+B notation represents a mathematical expression where:
    //   - A is the coefficient of n (can be empty, implying 0).
    //   - B is the constant offset (can be empty, implying 0).
    //
    // Minification Rules:
    //   - "even" → "2n" (shorter form)
    //   - "2n+1" → "odd" (keyword form)
    //   - "0n+1" → "1" (remove zero coefficient)
    //   - "0n" → "0" (zero coefficient, no offset)
    //   - "1n+0" → "1n" (remove zero offset)
    //
    // Example:
    //   NthIndex idx;
    //   idx.a = "even";
    //   idx.Minify();
    //   // idx.a == "2", idx.b == ""
    //
    //   idx.a = "2";
    //   idx.b = "1";
    //   idx.Minify();
    //   // idx.a == "", idx.b == "odd"
    //
    // Edge Cases:
    //   - Does not validate the mathematical expression.
    //   - Assumes a and b are valid integer strings.
    // -------------------------------------------------------------------------
    void NthIndex::Minify() {
        // "even" => "2n"
        if (b == "even") {
            a = "2";
            b = "";
            return;
        }

        // "2n+1" => "odd"
        if (a == "2" && b == "1") {
            a = "";
            b = "odd";
            return;
        }

        // "0n+1" => "1"
        if (a == "0") {
            a = "";
            if (b == "") {
                // "0n" => "0"
                b = "0";
            }
            return;
        }

        // "1n+0" => "1n"
        if (b == "0" && a != "") {
            b = "";
        }
    }

    bool SSPseudoClassWithSelectorList::Equal(const SS* ss, const CrossFileEqualityCheck* check) const {
        auto b = dynamic_cast<const SSPseudoClassWithSelectorList*>(ss);
        return b != nullptr && kind == b->kind && index == b->index && ComplexSelectorsEqual(selectors, b->selectors, check);
    }

    uint32_t SSPseudoClassWithSelectorList::Hash() const {
        uint32_t hash = 5;
        hash = HashCombine(hash, static_cast<uint32_t>(kind));
        hash = HashCombineString(hash, index.a);
        hash = HashCombineString(hash, index.b);
        hash = HashComplexSelectors(hash, selectors);
        return hash;
    }

    std::unique_ptr<SS> SSPseudoClassWithSelectorList::Clone() const {
        auto clone = std::make_unique<SSPseudoClassWithSelectorList>(*this);
        for (size_t i = 0; i < selectors.size(); i++) {
            clone->selectors[i] = selectors[i].Clone();
        }
        return clone;
    }

    // -------------------------------------------------------------------------
    // CompoundSelector::IsSingleAmpersand
    //
    // Checks if this compound selector is a single ampersand (&) with no
    // other components.  This is the simplest form of a nesting selector,
    // representing "the parent element itself".
    //
    // Returns:
    //   - true if the selector is exactly "&" with no combinator, type
    //     selector, or subclass selectors.
    //
    // Example:
    //   // Selector: &
    //   // IsSingleAmpersand() == true
    //
    //   // Selector: & .child
    //   // IsSingleAmpersand() == false (has subclass selector)
    //
    //   // Selector: .parent &
    //   // IsSingleAmpersand() == false (has combinator)
    //
    // Edge Cases:
    //   - Returns false if there are multiple nesting selector locations.
    // -------------------------------------------------------------------------
    bool CompoundSelector::IsSingleAmpersand() const {
        return nesting_selector_locs.size() == 1 && combinator.byte_ == 0 &&
            type_selector == nullptr && subclass_selectors.empty();
    }

    // -------------------------------------------------------------------------
    // CompoundSelector::IsInvalidBecauseEmpty
    //
    // Checks if this compound selector is empty (has no components).  An
    // empty compound selector is invalid CSS and should be flagged as an error.
    //
    // Returns:
    //   - true if the selector has no nesting combinator, no type selector,
    //     and no subclass selectors.
    //
    // Example:
    //   // Selector: (empty)
    //   // IsInvalidBecauseEmpty() == true
    //
    //   // Selector: .
    //   // IsInvalidBecauseEmpty() == true (just a dot, no class name)
    //
    //   // Selector: .class
    //   // IsInvalidBecauseEmpty() == false (has subclass selector)
    //
    // Edge Cases:
    //   - Returns false if there are any nesting selector locations.
    // -------------------------------------------------------------------------
    bool CompoundSelector::IsInvalidBecauseEmpty() const {
        return nesting_selector_locs.empty() && type_selector == nullptr && subclass_selectors.empty();
    }

    // -------------------------------------------------------------------------
    // CompoundSelector::Range
    //
    // Returns the source range (start location and length) of this compound
    // selector.  The range encompasses all components: combinator, type
    // selector, nesting selectors, and subclass selectors.
    //
    // The range is computed by starting with the combinator (if present) and
    // expanding to include each additional component.
    //
    // Returns:
    //   - A Range object representing the source location of the selector.
    //
    // Example:
    //   // For selector: > div.class
    //   // Range covers: ">" (combinator) + "div" (type) + ".class" (subclass)
    //
    // Edge Cases:
    //   - Returns an empty range if the selector has no components.
    //   - Handles selectors with multiple nesting combinator locations.
    // -------------------------------------------------------------------------
    guchho::logger::Range CompoundSelector::Range() const {
        guchho::logger::Range r;
        if (combinator.byte_ != 0) {
            r = guchho::logger::Range{combinator.loc, 1};
        }
        if (type_selector != nullptr) {
            r.ExpandBy(type_selector->Range());
        }
        for (const auto& loc : nesting_selector_locs) {
            r.ExpandBy(guchho::logger::Range{loc, 1});
        }
        if (!subclass_selectors.empty()) {
            for (const auto& ss : subclass_selectors) {
                r.ExpandBy(ss.range);
            }
        }
        return r;
    }

    // -------------------------------------------------------------------------
    // CompoundSelector::Clone
    //
    // Deep-copies this compound selector, creating independent copies of the
    // type selector and all subclass selectors.
    //
    // Behavior:
    //   - Copies basic fields (combinator, nesting_selector_locs) by value.
    //   - If a type selector exists, clones it via NamespacedName::Clone.
    //   - Each subclass selector's data is cloned via its virtual Clone method.
    //
    // Returns:
    //   - A new CompoundSelector that is a deep copy of this one.
    //
    // Example:
    //   CompoundSelector original = ...;
    //   CompoundSelector clone = original.Clone();
    //   // Modifying clone does not affect original
    //
    // Edge Cases:
    //   - Handles null type_selector correctly (no clone needed).
    //   - Subclass selectors are cloned polymorphically.
    // -------------------------------------------------------------------------
    CompoundSelector CompoundSelector::Clone() const {
        CompoundSelector clone = *this;

        if (type_selector != nullptr) {
            clone.type_selector = std::make_shared<NamespacedName>(type_selector->Clone());
        }

        for (size_t i = 0; i < subclass_selectors.size(); i++) {
            clone.subclass_selectors[i].data = subclass_selectors[i].data->Clone();
        }

        return clone;
    }

}