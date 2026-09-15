#include "guchho/css/css_helpers.hpp"
#include "guchho/css/css_lexer.hpp"

#include <array>
#include <span>
#include <utility>


namespace guchho::css {

    namespace {

        // Sets one or more flags in a WhitespaceFlags bitmask.
        //
        // Input:  flags = kWhitespaceBefore, flag = kWhitespaceAfter
        // Output: flags | kWhitespaceAfter (both flags now set)
        //
        // Edge cases:
        //   - If the flag is already set, the value is unchanged (idempotent).
        //   - WhitespaceFlags::kNone with any flag yields that flag alone.
        WhitespaceFlags SetWhitespaceFlag(WhitespaceFlags flags, WhitespaceFlags flag) {
            return static_cast<WhitespaceFlags>(static_cast<uint8_t>(flags) | static_cast<uint8_t>(flag));
        }

        // Clears one or more flags from a WhitespaceFlags bitmask.
        //
        // Input:  flags = kWhitespaceBefore | kWhitespaceAfter, flag = kWhitespaceAfter
        // Output: flags with kWhitespaceAfter cleared
        //
        // Edge cases:
        //   - If the flag is not set, the value is unchanged.
        //   - Clearing kNone has no effect.
        WhitespaceFlags ClearWhitespaceFlag(WhitespaceFlags flags, WhitespaceFlags flag) {
            return static_cast<WhitespaceFlags>(static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(flag));
        }

        // Expands a 1-to-4 token list into the four corners of a CSS quad
        // (top, right, bottom, left), following the standard CSS shorthand
        // expansion rules. This is used by border-radius, margin, padding,
        // and other CSS properties that accept 1-to-4 values.
        //
        // The CSS shorthand rules are:
        //   - 1 value:  all four corners get the same token
        //   - 2 values: top/bottom get the first, right/left get the second
        //   - 3 values: top gets first, right/left get second, bottom gets third
        //   - 4 values: top, right, bottom, left in order
        //
        // Input:  tokens = [{10px}], allowed_ident = ""
        // Output: result = [{10px}, {10px}, {10px}, {10px}], ok = true
        //
        // Input:  tokens = [{10px}, {20px}], allowed_ident = ""
        // Output: result = [{10px}, {20px}, {10px}, {20px}], ok = true
        //
        // Input:  tokens = [{10px}, {20px}, {30px}], allowed_ident = ""
        // Output: result = [{10px}, {20px}, {30px}, {20px}], ok = true
        //
        // Edge cases:
        //   - Returns false if the token count is outside 1-4.
        //   - Returns false if any token is not numeric and not the allowed
        //     identifier (e.g., "var()" or other function calls).
        //   - The allowed_ident parameter permits a specific keyword token
        //     (e.g., "auto") to pass through alongside numeric values.
        std::pair<std::array<Token, 4>, bool> ExpandTokenQuad(
            std::span<const Token> tokens,
            std::string_view allowed_ident
        ) {
            std::array<Token, 4> result;
            size_t n = tokens.size();
            if (n < 1 || n > 4) {
                return {result, false};
            }

            // Don't do this if we encounter any unexpected tokens such as "var()"
            for (size_t i = 0; i < n; i++) {
                const Token& t = tokens[i];
                if (!IsNumeric(t.kind) &&
                    (t.kind != TokenType::kIdent || allowed_ident.empty() || t.text != allowed_ident)) {
                    return {result, false};
                }
            }

            result[0] = tokens[0];
            if (n > 1) {
                result[1] = tokens[1];
            } else {
                result[1] = result[0];
            }
            if (n > 2) {
                result[2] = tokens[2];
            } else {
                result[2] = result[0];
            }
            if (n > 3) {
                result[3] = tokens[3];
            } else {
                result[3] = result[1];
            }
            return {result, true};
        }


        // Compacts four corner tokens back into a minimal representation
        // using CSS shorthand rules. This is the inverse of ExpandTokenQuad.
        // Returns the smallest valid token list that would expand back to
        // the same four corners.
        //
        // The compaction rules are:
        //   - If all four corners are identical:  return 1 token
        //   - If top/bottom and right/left match: return 2 tokens
        //   - If left matches right:              return 3 tokens
        //   - Otherwise:                          return 4 tokens
        //
        // Input:  a=10px, b=10px, c=10px, d=10px
        // Output: [{10px}]
        //
        // Input:  a=10px, b=20px, c=10px, d=20px
        // Output: [{10px}, {20px}]
        //
        // Input:  a=10px, b=20px, c=30px, d=20px
        // Output: [{10px}, {20px}, {30px}]
        //
        // Input:  a=10px, b=20px, c=30px, d=40px
        // Output: [{10px}, {20px}, {30px}, {40px}]
        //
        // Edge cases:
        //   - Whitespace flags are set on each token to ensure proper
        //     formatting when the tokens are serialized.
        //   - When minify_whitespace is true, the first token omits
        //     leading whitespace to allow minified output.
        std::vector<Token> CompactTokenQuad(
            const Token& a, 
            const Token& b, 
            const Token& c, 
            const Token& d,
            bool minify_whitespace
        ) {
            std::vector<Token> tokens = {a, b, c, d};
            if (tokens[3].EqualIgnoringWhitespace(tokens[1])) {
                if (tokens[2].EqualIgnoringWhitespace(tokens[0])) {
                    if (tokens[1].EqualIgnoringWhitespace(tokens[0])) {
                        tokens.resize(1);
                    } else {
                        tokens.resize(2);
                    }
                } else {
                    tokens.resize(3);
                }
            }
            for (size_t i = 0; i < tokens.size(); i++) {
                WhitespaceFlags whitespace = WhitespaceFlags::kNone;
                if (!minify_whitespace || i > 0) {
                    whitespace = SetWhitespaceFlag(whitespace, WhitespaceFlags::kWhitespaceBefore);
                }
                if (i + 1 < tokens.size()) {
                    whitespace = SetWhitespaceFlag(whitespace, WhitespaceFlags::kWhitespaceAfter);
                }
                tokens[i].whitespace = whitespace;
            }
            return tokens;
        }

    }

    // Checks whether this unit safety tracker is compatible with another.
    // Two trackers are compatible if:
    //   1. Both have the same safety status (kUnitSafe, kUnitUnsafeSingle, etc.)
    //   2. If both are kUnitUnsafeSingle, they must have the same unit string
    //
    // This is used when merging border-radius declarations to ensure that
    // all corners use compatible units. For example, "10px 20px" and "5em 15em"
    // are not compatible because they mix different unit types.
    //
    // Input:  this = {status=kUnitSafe}, b = {status=kUnitSafe}
    // Output: true (both safe, no unit conflicts)
    //
    // Input:  this = {status=kUnitUnsafeSingle, unit="px"}, b = {status=kUnitUnsafeSingle, unit="em"}
    // Output: false (different unsafe units)
    //
    // Input:  this = {status=kUnitUnsafeSingle, unit="px"}, b = {status=kUnitUnsafeSingle, unit="px"}
    // Output: true (same unsafe unit, compatible)
    //
    // Edge cases:
    //   - kUnitUnsafeMixed is never safe with any other status.
    bool UnitSafetyTracker::IsSafeWith(
        const UnitSafetyTracker& b
    ) const {
        return status == b.status && status != Status::kUnitUnsafeMixed &&
            (status != Status::kUnitUnsafeSingle || unit == b.unit);
    }

    // Tracks the unit type of a token for safety analysis. This is used to
    // determine whether it's safe to collapse "0px" into "0" or to merge
    // border-radius declarations with different units.
    //
    // The safety status progresses as follows:
    //   1. kUnitSafe: No non-zero lengths encountered yet, or all lengths
    //      use safe units (e.g., all are percentages or unitless numbers).
    //   2. kUnitUnsafeSingle: A non-zero length with a specific unit was
    //      encountered (e.g., "10px"). More tokens with the same unit are safe.
    //   3. kUnitUnsafeMixed: Tokens with different units were encountered
    //      (e.g., "10px" and "5em"), or an unsafe token was found.
    //
    // Input:  token = {kind=kDimension, text="10px", unit="px"}
    // Output: status becomes kUnitUnsafeSingle, unit = "px"
    //
    // Input:  token = {kind=kPercentage, text="50%"}
    // Output: status unchanged (percentages are always safe)
    //
    // Input:  token = {kind=kNumber, text="0"}
    // Output: status unchanged (zero is always safe)
    //
    // Edge cases:
    //   - Zero values ("0") are considered safe regardless of their context.
    //   - Dimension tokens with safe length units (e.g., "em", "rem") are
    //     treated as safe and don't change the status.
    //   - Once status becomes kUnitUnsafeMixed, it cannot be reversed.
    void UnitSafetyTracker::IncludeUnitOf(const Token& token) {
        switch (token.kind) {
        case TokenType::kNumber:
            if (token.text == "0") {
                return;
            }
            break;

        case TokenType::kPercentage:
            return;

        case TokenType::kDimension:
            if (token.DimensionUnitIsSafeLength()) {
                return;
            } else if (const std::string& token_unit = token.DimensionUnit();
                    status == Status::kUnitSafe) {
                status = Status::kUnitUnsafeSingle;
                unit = token_unit;
                return;
            } else if (status == Status::kUnitUnsafeSingle && unit == token_unit) {
                return;
            }
            break;

        default:
            break;
        }

        status = Status::kUnitUnsafeMixed;
    }

    // Updates a single corner of the border-radius with a new value.
    // This method handles the logic for replacing an existing corner
    // value when a more specific declaration overrides a shorthand.
    //
    // The replacement rules are:
    //   - If the old corner has no value (kEndOfFile), always replace.
    //   - If the new corner was from a single-rule shorthand and the old
    //     was not, keep the old value (more specific wins).
    //   - If both corners have safe units, replace the old with the new.
    //
    // Input:  old = {first_token=kEndOfFile}, new = {first_token=10px}
    // Output: old is replaced with new (no previous value existed)
    //
    // Input:  old = {first_token=10px, was_single_rule=false}, new = {first_token=20px, was_single_rule=true}
    // Output: old is NOT replaced (shorthand loses to longhand)
    //
    // Edge cases:
    //   - The rule_index is updated to track which declaration in the
    //     rules vector owns this corner value.
    void BorderRadiusTracker::UpdateCorner(
        std::vector<Rule>& rules, 
        int corner,
        const Corner& new_corner
    ) {
        const Corner& old = corners[static_cast<size_t>(corner)];
        if (old.first_token.kind != TokenType::kEndOfFile &&
            (!new_corner.was_single_rule || old.was_single_rule) &&
            old.unit_safety.status == UnitSafetyTracker::Status::kUnitSafe &&
            new_corner.unit_safety.status == UnitSafetyTracker::Status::kUnitSafe) {
            rules[old.rule_index] = Rule{};
        }
        corners[static_cast<size_t>(corner)] = new_corner;
    }

    // Processes a border-radius shorthand declaration that may contain a
    // slash separator for elliptical radii (e.g., "10px / 20px" or
    // "10px 20px / 30px 40px"). This method:
    //
    //   1. Resets tracking state if the !important flag changes.
    //   2. Finds the slash separator (if any) to split horizontal/vertical radii.
    //   3. Validates unit safety across all tokens.
    //   4. Expands both the horizontal and vertical radii into quads.
    //   5. Updates each corner with both the first (horizontal) and
    //      second (vertical) radius values.
    //   6. Attempts to compact the four corners into a minimal shorthand.
    //
    // Input:  decl = "10px / 20px" (elliptical, same radius for all corners)
    // Output: All four corners get {first_token=10px, second_token=20px}
    //
    // Input:  decl = "10px 20px 30px 40px / 5px 10px 15px 20px"
    // Output: Each corner gets its own first/second token pair
    //
    // Edge cases:
    //   - Multiple slashes are treated as an error; tracking state is reset.
    //   - If token expansion fails (e.g., due to var() or invalid tokens),
    //     tracking state is reset.
    //   - Zero values may be collapsed from "0px" to "0" if unit safety allows.
    void BorderRadiusTracker::MangleCorners(
        std::vector<Rule>& rules, 
        RDeclaration* decl,
        bool minify_whitespace
    ) {
        // Reset if we see a change in the "!important" flag
        if (important != decl->important) {
            corners = {};
            important = decl->important;
        }

        std::vector<Token>& tokens = decl->value;
        size_t before_split = tokens.size();
        size_t after_split = tokens.size();

        // Search for the single slash if present
        for (size_t i = 0; i < tokens.size(); i++) {
            if (tokens[i].kind == TokenType::kDelimSlash) {
                if (before_split == tokens.size()) {
                    before_split = i;
                    after_split = i + 1;
                } else {
                    // Multiple slashes are an error
                    corners = {};
                    return;
                }
            }
        }

        // Use a single tracker for the whole rule
        UnitSafetyTracker unit_safety;
        for (size_t i = 0; i < before_split; i++) {
            unit_safety.IncludeUnitOf(tokens[i]);
        }
        for (size_t i = after_split; i < tokens.size(); i++) {
            unit_safety.IncludeUnitOf(tokens[i]);
        }

        auto [first_radii, first_radii_ok] = ExpandTokenQuad(
            std::span<const Token>(tokens.begin(),
                                tokens.begin() + static_cast<std::ptrdiff_t>(before_split)),
            "");
        auto [last_radii, last_radii_ok] = ExpandTokenQuad(
            std::span<const Token>(tokens.begin() + static_cast<std::ptrdiff_t>(after_split),
                                tokens.begin() + static_cast<std::ptrdiff_t>(tokens.size())),
            "");

        // Stop now if the pattern wasn't matched
        if (!first_radii_ok || (before_split < after_split && !last_radii_ok)) {
            corners = {};
            return;
        }

        // Handle the first radii
        for (int corner = 0; corner < 4; corner++) {
            Token t = first_radii[static_cast<size_t>(corner)];
            if (unit_safety.status == UnitSafetyTracker::Status::kUnitSafe) {
                t.TurnLengthIntoNumberIfZero();
            }
            UpdateCorner(rules, corner, Corner{t, t, unit_safety,
                                            static_cast<uint32_t>(rules.size() - 1), false});
        }

        // Handle the last radii
        if (last_radii_ok) {
            for (int corner = 0; corner < 4; corner++) {
                Token t = last_radii[static_cast<size_t>(corner)];
                if (unit_safety.status == UnitSafetyTracker::Status::kUnitSafe) {
                    t.TurnLengthIntoNumberIfZero();
                }
                corners[static_cast<size_t>(corner)].second_token = t;
            }
        }

        // Success
        CompactRules(rules, decl->key_range, minify_whitespace);
    }

    // Processes a single corner of the border-radius property (e.g.,
    // "border-top-left-radius: 10px" or "border-top-left-radius: 10px 20px").
    // This method handles the elliptical radius syntax where two values
    // represent horizontal and vertical radii.
    //
    // The processing logic is:
    //   1. Reset tracking state if the !important flag changes.
    //   2. Validate that the declaration has 1-2 numeric tokens.
    //   3. Track unit safety across both tokens.
    //   4. Collapse "0px" to "0" if units are safe.
    //   5. Merge two identical tokens into one (e.g., "10px 10px" => "10px").
    //   6. Update the corner and attempt to compact other corners.
    //
    // Input:  decl = "10px" (single radius)
    // Output: corner gets {first_token=10px, second_token=10px}
    //
    // Input:  decl = "10px 20px" (elliptical)
    // Output: corner gets {first_token=10px, second_token=20px}
    //
    // Edge cases:
    //   - Declarations with non-numeric tokens (e.g., "var(--r)") cause
    //     the tracking state to reset.
    //   - If both tokens are equal after processing, the declaration is
    //     simplified to a single token.
    void BorderRadiusTracker::MangleCorner(
        std::vector<Rule>& rules, 
        RDeclaration* decl,
        bool minify_whitespace, 
        int corner
    ) {
        if (important != decl->important) {
            corners = {};
            important = decl->important;
        }

        std::vector<Token>& tokens = decl->value;
        if ((tokens.size() == 1 && IsNumeric(tokens[0].kind)) ||
            (tokens.size() == 2 && IsNumeric(tokens[0].kind) && IsNumeric(tokens[1].kind))) {
            Token first_token = tokens[0];
            Token second_token = first_token;
            if (tokens.size() == 2) {
                second_token = tokens[1];
            }

            // Check to see if these units are safe to use in every browser
            UnitSafetyTracker unit_safety;
            unit_safety.IncludeUnitOf(first_token);
            unit_safety.IncludeUnitOf(second_token);

            // Only collapse "0unit" into "0" if the unit is safe
            if (unit_safety.status == UnitSafetyTracker::Status::kUnitSafe &&
                first_token.TurnLengthIntoNumberIfZero()) {
                tokens[0] = first_token;
            }
            if (tokens.size() == 2) {
                if (unit_safety.status == UnitSafetyTracker::Status::kUnitSafe &&
                    second_token.TurnLengthIntoNumberIfZero()) {
                    tokens[1] = second_token;
                }

                // If both tokens are equal, merge them into one
                if (first_token.EqualIgnoringWhitespace(second_token)) {
                    tokens[0].whitespace =
                        ClearWhitespaceFlag(tokens[0].whitespace, WhitespaceFlags::kWhitespaceAfter);
                    tokens.resize(1);
                }
            }

            UpdateCorner(rules, corner,
                        Corner{std::move(first_token), std::move(second_token), std::move(unit_safety),
                                static_cast<uint32_t>(rules.size() - 1), true});
            CompactRules(rules, decl->key_range, minify_whitespace);
        } else {
            corners = {};
        }
    }

    // Attempts to compact four separate border-radius declarations into a
    // single shorthand property. This is the final step in the border-radius
    // optimization pipeline.
    //
    // The compaction process:
    //   1. Verify all four corners have values (no kEndOfFile tokens).
    //   2. Verify all corners have compatible unit safety status.
    //   3. Compact the first tokens (horizontal radii) into a minimal form.
    //   4. Compact the second tokens (vertical radii) into a minimal form.
    //   5. If horizontal and vertical radii differ, insert a "/" separator.
    //   6. Remove the original four declarations from the rules vector.
    //   7. Insert the combined shorthand at the position of the last rule.
    //
    // Input:  corners = [{10px,10px}, {10px,10px}, {10px,10px}, {10px,10px}]
    // Output: Single declaration: "border-radius: 10px"
    //
    // Input:  corners = [{10px,20px}, {10px,20px}, {10px,20px}, {10px,20px}]
    // Output: Single declaration: "border-radius: 10px / 20px"
    //
    // Input:  corners = [{10px,10px}, {20px,20px}, {30px,30px}, {40px,40px}]
    // Output: Single declaration: "border-radius: 10px 20px 30px 40px"
    //
    // Edge cases:
    //   - If any corner is missing, no compaction occurs.
    //   - If corners have incompatible units, no compaction occurs.
    //   - The combined declaration is placed at the position of the last
    //     original declaration to preserve source order.
    //   - The source location (min_loc) is the earliest position among
    //     the original declarations.
    void BorderRadiusTracker::CompactRules(
        std::vector<Rule>& rules, 
        guchho::logger::Range key_range,
        bool minify_whitespace
    ) {
        // All tokens must be present
        if (corners[0].first_token.kind == TokenType::kEndOfFile || corners[1].first_token.kind == TokenType::kEndOfFile ||
            corners[2].first_token.kind == TokenType::kEndOfFile || corners[3].first_token.kind == TokenType::kEndOfFile) {
            return;
        }

        // All tokens must have the same unit
        for (size_t i = 1; i < corners.size(); i++) {
            if (!corners[i].unit_safety.IsSafeWith(corners[0].unit_safety)) {
                return;
            }
        }

        // Generate the most minimal representation
        std::vector<Token> tokens =
            CompactTokenQuad(corners[0].first_token, corners[1].first_token, corners[2].first_token,
                            corners[3].first_token, minify_whitespace);
        std::vector<Token> second_tokens =
            CompactTokenQuad(corners[0].second_token, corners[1].second_token, corners[2].second_token,
                            corners[3].second_token, minify_whitespace);
        if (!TokensEqualIgnoringWhitespace(tokens, second_tokens)) {
            WhitespaceFlags whitespace = WhitespaceFlags::kNone;
            if (!minify_whitespace) {
                whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;
            }
            Token slash;
            slash.loc = tokens.back().loc;
            slash.kind = TokenType::kDelimSlash;
            slash.text = "/";
            slash.whitespace = whitespace;
            tokens.push_back(slash);
            tokens.insert(tokens.end(), second_tokens.begin(), second_tokens.end());
        }

        // Remove all of the existing declarations
        guchho::logger::Loc min_loc;
        bool is_first = true;
        for (size_t i = 0; i < corners.size(); i++) {
            guchho::logger::Loc loc = rules[corners[i].rule_index].loc;
            if (is_first || loc.start < min_loc.start) {
                min_loc = loc;
                is_first = false;
            }
            rules[corners[i].rule_index] = Rule{};
        }

        // Insert the combined declaration where the last rule was
        auto decl = std::make_shared<RDeclaration>();
        decl->key = kDBorderRadius;
        decl->key_text = "border-radius";
        decl->value = std::move(tokens);
        decl->key_range = key_range;
        decl->important = important;
        rules[corners[3].rule_index] = Rule{std::move(decl), min_loc};
    }
}
