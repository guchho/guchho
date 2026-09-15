#include "guchho/css/css_helpers.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/helpers.hpp"

#include <string>
#include <string_view>


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

        // Expands a 1-to-4 token list into the four sides of a CSS box
        // (top, right, bottom, left), following the standard CSS shorthand
        // expansion rules. This is used by margin, padding, inset, and
        // other CSS properties that accept 1-to-4 values.
        //
        // The CSS shorthand rules are:
        //   - 1 value:  all four sides get the same token
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
        // Input:  tokens = [{auto}], allowed_ident = "auto"
        // Output: result = [{auto}, {auto}, {auto}, {auto}], ok = true
        //
        // Edge cases:
        //   - Returns false if the token count is outside 1-4.
        //   - Returns false if any token is not numeric and not the allowed
        //     identifier (e.g., "var()" or other function calls).
        //   - The allowed_ident parameter permits a specific keyword token
        //     (e.g., "auto") to pass through alongside numeric values.
        std::pair<std::array<Token, 4>, bool> ExpandTokenQuad(
            std::span<const Token> tokens,
            std::string_view allowed_ident)
        {
            std::array<Token, 4> result;

            size_t n = tokens.size();

            // Token count must be between 1 and 4 inclusive.
            if (n < 1 || n > 4) {
                return {result, false};
            }

            // Unexpected tokens such as "var()" cannot be expanded.
            // Only numeric tokens and the specific allowed identifier are accepted.
            for (size_t i = 0; i < n; i++) {
                const Token& t = tokens[i];

                if (!IsNumeric(t.kind) &&
                    (t.kind != TokenType::kIdent ||
                    allowed_ident.empty() ||
                    t.text != allowed_ident)) {
                    return {result, false};
                }
            }

            // First token => Top
            result[0] = tokens[0];

            // Second token => Right, or copy from Top if not present.
            if (n > 1) {
                result[1] = tokens[1];
            } else {
                result[1] = result[0];
            }

            // Third token => Bottom, or copy from Top if not present.
            if (n > 2) {
                result[2] = tokens[2];
            } else {
                result[2] = result[0];
            }

            // Fourth token => Left, or copy from Right if not present.
            if (n > 3) {
                result[3] = tokens[3];
            } else {
                result[3] = result[1];
            }

            return {result, true};
        }

        // Compacts four side tokens back into a minimal representation
        // using CSS shorthand rules. This is the inverse of ExpandTokenQuad.
        // Returns the smallest valid token list that would expand back to
        // the same four sides.
        //
        // The compaction rules are:
        //   - If all four sides are identical:  return 1 token
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
            bool minify_whitespace)
        {
            // Start with all four sides: top, right, bottom, left.
            std::vector<Token> tokens = {a, b, c, d};

            // If Left matches Right, Left can be omitted.
            if (tokens[3].EqualIgnoringWhitespace(tokens[1])) {

                // If Top also matches Bottom, further compaction is possible.
                if (tokens[2].EqualIgnoringWhitespace(tokens[0])) {

                    // All four values are identical; one token is sufficient.
                    if (tokens[1].EqualIgnoringWhitespace(tokens[0])) {
                        tokens.resize(1);
                    } else {
                        // Top/Bottom match and Left/Right match; two tokens suffice.
                        tokens.resize(2);
                    }
                } else {
                    // Only Left/Right match; three tokens are sufficient.
                    tokens.resize(3);
                }
            }

            // Set whitespace flags before and after each token as needed.
            for (size_t i = 0; i < tokens.size(); i++) {
                WhitespaceFlags whitespace = WhitespaceFlags::kNone;

                // Whitespace is added before each token except the first
                // when minifying.
                if (!minify_whitespace || i > 0) {
                    whitespace = SetWhitespaceFlag(
                        whitespace,
                        WhitespaceFlags::kWhitespaceBefore);
                }

                // Whitespace is added after each token except the last.
                if (i + 1 < tokens.size()) {
                    whitespace = SetWhitespaceFlag(
                        whitespace,
                        WhitespaceFlags::kWhitespaceAfter);
                }

                tokens[i].whitespace = whitespace;
            }

            return tokens;
        }

   }

    // Updates a single side of the box model (margin, padding, or inset)
    // with a new value. This method handles the logic for replacing an
    // existing side value when a more specific declaration overrides a shorthand.
    //
    // The replacement rules are:
    //   - If the old side has no value (kEndOfFile), always replace.
    //   - If the new side was from a single-rule shorthand and the old
    //     was not, keep the old value (more specific wins).
    //   - If both sides have safe units, replace the old with the new.
    //
    // Input:  old = {token=kEndOfFile}, new = {token=10px}
    // Output: old is replaced with new (no previous value existed)
    //
    // Input:  old = {token=10px, was_single_rule=false}, new = {token=20px, was_single_rule=true}
    // Output: old is NOT replaced (shorthand loses to longhand)
    //
    // Edge cases:
    //   - The rule_index is updated to track which declaration in the
    //     rules vector owns this side value.
    void BoxTracker::UpdateSide(std::vector<Rule>& rules, int side, const BoxSide& new_side) {
        const BoxSide& old = sides[static_cast<size_t>(side)];
        if (old.token.kind != TokenType::kEndOfFile && (!new_side.was_single_rule || old.was_single_rule) &&
            old.unit_safety.status == UnitSafetyTracker::Status::kUnitSafe &&
            new_side.unit_safety.status == UnitSafetyTracker::Status::kUnitSafe) {
            rules[old.rule_index] = Rule{};
        }
        sides[static_cast<size_t>(side)] = new_side;
    }

    // Processes a box shorthand declaration (margin, padding, or inset) that
    // contains 1-to-4 values. This method:
    //
    //   1. Resets tracking state if the !important flag changes.
    //   2. Expands the token list into a quad (top, right, bottom, left).
    //   3. Validates unit safety across all tokens.
    //   4. Updates each side with the expanded value.
    //   5. Attempts to compact the four sides into a minimal shorthand.
    //
    // Input:  decl = "10px" (single value for all sides)
    // Output: All four sides get {token=10px}
    //
    // Input:  decl = "10px 20px" (vertical/horizontal pair)
    // Output: top=10px, right=20px, bottom=10px, left=20px
    //
    // Input:  decl = "10px 20px 30px" (top/horizontal/bottom)
    // Output: top=10px, right=20px, bottom=30px, left=20px
    //
    // Input:  decl = "10px 20px 30px 40px" (all four sides)
    // Output: top=10px, right=20px, bottom=30px, left=40px
    //
    // Edge cases:
    //   - If token expansion fails (e.g., due to var() or invalid tokens),
    //     tracking state is reset.
    //   - Zero values may be collapsed from "0px" to "0" if unit safety allows.
    //   - The "auto" keyword is allowed when allow_auto is true (for margins).
    void BoxTracker::MangleSides(
        std::vector<Rule>& rules, 
        RDeclaration* decl,
        bool minify_whitespace
    ) {
        // Reset if we see a change in the "!important" flag
        if (important != decl->important) {
            sides = {};
            important = decl->important;
        }

        std::string_view allowed_ident;
        if (allow_auto) {
            allowed_ident = "auto";
        }
        auto [quad, quad_ok] = ExpandTokenQuad(std::span<const Token>(decl->value), allowed_ident);
        if (quad_ok) {
            // Use a single tracker for the whole rule
            UnitSafetyTracker unit_safety;
            for (const Token& t : quad) {
                if (!allow_auto || IsNumeric(t.kind)) {
                    unit_safety.IncludeUnitOf(t);
                }
            }
            for (int side = 0; side < 4; side++) {
                Token t = quad[static_cast<size_t>(side)];
                if (unit_safety.status == UnitSafetyTracker::Status::kUnitSafe) {
                    t.TurnLengthIntoNumberIfZero();
                }
                UpdateSide(rules, side, BoxSide{t, unit_safety,
                                                static_cast<uint32_t>(rules.size() - 1), false});
            }
            CompactRules(rules, decl->key_range, minify_whitespace);
        } else {
            sides = {};
        }
    }

    // Processes a single side of the box model (e.g., "margin-top: 10px").
    // This method handles the longhand form of box properties.
    //
    // The processing logic is:
    //   1. Reset tracking state if the !important flag changes.
    //   2. Validate that the declaration has exactly one token.
    //   3. Accept numeric values or the "auto" keyword if allowed.
    //   4. Track unit safety for the token.
    //   5. Collapse "0px" to "0" if units are safe.
    //   6. Update the side and attempt to compact other sides.
    //
    // Input:  decl = "10px", side = 0 (top)
    // Output: side gets {token=10px}
    //
    // Input:  decl = "auto", side = 1 (right), allow_auto = true
    // Output: side gets {token=auto}
    //
    // Edge cases:
    //   - Declarations with non-numeric tokens (e.g., "var(--s)") cause
    //     the tracking state to reset.
    //   - The "auto" keyword is only accepted if allow_auto is true.
    //   - Case-insensitive comparison is used for the "auto" keyword.
    void BoxTracker::MangleSide(
        std::vector<Rule>& rules, 
        RDeclaration* decl, 
        bool minify_whitespace,
        int side
    ) {
        // Reset if we see a change in the "!important" flag
        if (important != decl->important) {
            sides = {};
            important = decl->important;
        }

        std::vector<Token>& tokens = decl->value;
        if (tokens.size() == 1) {
            Token t = tokens[0];
            if (IsNumeric(t.kind) ||
                (t.kind == TokenType::kIdent && allow_auto && helpers::ToLowerASCII(t.text) == "auto")) {
                UnitSafetyTracker unit_safety;
                if (!allow_auto || IsNumeric(t.kind)) {
                    unit_safety.IncludeUnitOf(t);
                }
                if (unit_safety.status == UnitSafetyTracker::Status::kUnitSafe &&
                    t.TurnLengthIntoNumberIfZero()) {
                    tokens[0] = t;
                }
                UpdateSide(rules, side, BoxSide{t, unit_safety,
                                                static_cast<uint32_t>(rules.size() - 1), true});
                CompactRules(rules, decl->key_range, minify_whitespace);
                return;
            }
        }

        sides = {};
    }

    // Attempts to compact four separate box side declarations (margin-top,
    // margin-right, margin-bottom, margin-left) into a single shorthand
    // property (margin). This is the final step in the box property
    // optimization pipeline.
    //
    // The compaction process:
    //   1. Verify all four sides have values (no kEndOfFile tokens).
    //   2. Verify all sides have compatible unit safety status.
    //   3. Compact the four tokens into a minimal form.
    //   4. Remove the original four declarations from the rules vector.
    //   5. Insert the combined shorthand at the position of the last rule.
    //
    // Input:  sides = [{10px}, {10px}, {10px}, {10px}]
    // Output: Single declaration: "margin: 10px"
    //
    // Input:  sides = [{10px}, {20px}, {10px}, {20px}]
    // Output: Single declaration: "margin: 10px 20px"
    //
    // Input:  sides = [{10px}, {20px}, {30px}, {20px}]
    // Output: Single declaration: "margin: 10px 20px 30px"
    //
    // Input:  sides = [{10px}, {20px}, {30px}, {40px}]
    // Output: Single declaration: "margin: 10px 20px 30px 40px"
    //
    // Edge cases:
    //   - If any side is missing, no compaction occurs.
    //   - If sides have incompatible units, no compaction occurs.
    //   - The combined declaration is placed at the position of the last
    //     original declaration to preserve source order.
    //   - The source location (min_loc) is the earliest position among
    //     the original declarations.
    //   - If the property key is kDUnknown, no compaction occurs.
    void BoxTracker::CompactRules(
        std::vector<Rule>& rules, 
        guchho::logger::Range key_range,
        bool minify_whitespace
    ) {
        if (key == kDUnknown) {
            return;
        }

        // All tokens must be present
        if (sides[0].token.kind == TokenType::kEndOfFile || sides[1].token.kind == TokenType::kEndOfFile ||
            sides[2].token.kind == TokenType::kEndOfFile || sides[3].token.kind == TokenType::kEndOfFile) {
            return;
        }

        // All tokens must have the same unit
        for (size_t i = 1; i < sides.size(); i++) {
            if (!sides[i].unit_safety.IsSafeWith(sides[0].unit_safety)) {
                return;
            }
        }

        // Generate the most minimal representation
        std::vector<Token> tokens =
            CompactTokenQuad(sides[0].token, sides[1].token, sides[2].token, sides[3].token,
                            minify_whitespace);

        // Remove all of the existing declarations
        guchho::logger::Loc min_loc;
        bool is_first = true;
        for (size_t i = 0; i < sides.size(); i++) {
            guchho::logger::Loc loc = rules[sides[i].rule_index].loc;
            if (is_first || loc.start < min_loc.start) {
                min_loc = loc;
                is_first = false;
            }
            rules[sides[i].rule_index] = Rule{};
        }

        // Insert the combined declaration where the last rule was
        auto decl = std::make_shared<RDeclaration>();
        decl->key = key;
        decl->key_text = key_text;
        decl->value = std::move(tokens);
        decl->key_range = key_range;
        decl->important = important;
        rules[sides[3].rule_index] = Rule{std::move(decl), min_loc};
    } 
}
