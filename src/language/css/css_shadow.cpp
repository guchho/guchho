#include "guchho/css/css_helpers.hpp"
#include "guchho/helpers.hpp"

#include <string_view>

namespace guchho::css {
    
    namespace {

        // LowerAndMangleBoxShadow
        // -----------------------
        // Takes the token list for a single CSS box-shadow value (one entry
        // from a possibly comma-separated list) and returns a mangled version
        // that is as short as possible while remaining valid.
        //
        // The function performs three passes over the tokens:
        //
        //   Pass 1 — Classify every token.  We count how many numeric offsets
        //   appear (offset-x, offset-y, optional blur, optional spread), how
        //   many color tokens appear, and how many "inset" keywords appear.
        //   If a token does not fall into any of these categories, or if
        //   numbers appear in two non-contiguous groups, the shadow is
        //   treated as malformed and returned unmodified.
        //
        //   Pass 2 — Trim trailing zero-valued length components.  The CSS
        //   spec allows two, three, or four numeric values.  When the last
        //   one or two are exactly zero they can be dropped (e.g. "0px" at
        //   the end becomes nothing), but we always keep at least the two
        //   mandatory offsets.
        //
        //   Pass 3 — Rewrite whitespace flags so that each token is
        //   surrounded by the correct space-or-no-space markers, respecting
        //   the caller's minify_whitespace preference.
        //
        // If would_clip_color is non-null it is set to true whenever the
        // color-minification step might clip a channel value (e.g. when a
        // named color is converted to a shorter hex form and rounding
        // changes the value).
        //
        // Input:  [kDimension("10px"), kDimension("20px"), kDimension("5px"),
        //          kDimension("0px"), kIdent("red")]
        //         options.minify_syntax = true
        // Output: [kDimension("10px"), kDimension("20px"), kDimension("5px"),
        //          kNumber("0"), kIdent("red")]
        //         — the trailing "0px" is shortened to "0".
        //
        // Edge case: if the shadow contains two non-adjacent groups of
        // numbers (e.g. "10px red 20px"), the token is flagged as
        // unexpected and returned unmodified.
        std::vector<Token> LowerAndMangleBoxShadow(
            std::vector<Token> tokens,
            const ColorDeclOptions& options,
            bool minify_whitespace,
            bool* would_clip_color
        ) {
        int inset_count = 0;
        int color_count = 0;
        size_t numbers_begin = 0;
        size_t numbers_count = 0;
        bool numbers_done = false;
        bool found_unexpected_token = false;

        for (size_t i = 0; i < tokens.size(); i++) {
            Token& t = tokens[i];
            if (t.kind == TokenType::kNumber || t.kind == TokenType::kDimension) {
                if (numbers_done) {
                    found_unexpected_token = true;
                }
                if (options.minify_syntax && t.TurnLengthIntoNumberIfZero()) {
                }
                if (numbers_count == 0) {
                    numbers_begin = i;
                }
                numbers_count++;
            } else {
                if (numbers_count != 0) {
                    numbers_done = true;
                }

                if (LooksLikeColor(t)) {
                    color_count++;
                    t = LowerAndMinifyColor(t, options, would_clip_color);
                } else if (t.kind == TokenType::kIdent && helpers::EqualFoldASCII(t.text, "inset")) {
                    inset_count++;
                } else {
                    found_unexpected_token = true;
                }
            }
        }

        // Trim trailing zero-valued numeric components when minifying.
        // Valid length sequences are exactly 2, 3, or 4 numbers long.
        // We may strip the last one or two values when they are zero,
        // but we never remove the mandatory offset-x / offset-y pair.
        if (options.minify_syntax && inset_count <= 1 && color_count <= 1 &&
            numbers_count > 2 && numbers_count <= 4 && !found_unexpected_token) {
            size_t numbers_end = numbers_begin + numbers_count;
            while (numbers_count > 2 && tokens[numbers_begin + numbers_count - 1].IsZero()) {
                numbers_count--;
            }
            tokens.erase(tokens.begin() + static_cast<ptrdiff_t>(numbers_begin + numbers_count),
                        tokens.begin() + static_cast<ptrdiff_t>(numbers_end));
        }

        // Rewrite whitespace flags on every token.  The first token gets
        // a leading space only when minify_whitespace is false; all
        // interior tokens get both leading and trailing spaces.
        for (size_t i = 0; i < tokens.size(); i++) {
            WhitespaceFlags whitespace = WhitespaceFlags::kNone;
            if (i > 0 || !minify_whitespace) {
                whitespace = whitespace | WhitespaceFlags::kWhitespaceBefore;
            }
            if (i + 1 < tokens.size()) {
                whitespace = whitespace | WhitespaceFlags::kWhitespaceAfter;
            }
            tokens[i].whitespace = whitespace;
        }
        return tokens;
    }

    } // namespace

    // LowerAndMangleBoxShadows
    // ------------------------
    // Entry point for minifying a complete box-shadow declaration value,
    // which may contain one or more comma-separated shadow entries.
    //
    // The function walks the token list, splitting on commas, and passes
    // each individual shadow to LowerAndMangleBoxShadow for
    // simplification.  The results are written back into the same token
    // vector in-place so that no extra allocation is needed beyond the
    // per-shadow temporary.
    //
    // Because the function reuses the input vector as scratch space, it
    // tracks a separate write cursor (`end`) that advances independently
    // of the read cursor (`i`).  After the loop the vector is truncated
    // to `end` to discard any leftover tokens from the original input.
    //
    // Input:  [kDimension("10px"), kDimension("20px"), kComma,
    //          kDimension("5px"), kDimension("15px")]
    //         minify_whitespace = true
    // Output: [kDimension("10px"), kDimension("20px"), kComma,
    //          kDimension("5px"), kDimension("15px")]
    //         — each shadow is individually mangled but the structure
    //         is preserved.
    //
    // Edge case: an empty token list returns an empty result without
    // touching would_clip_color.
    std::vector<Token> LowerAndMangleBoxShadows(
        std::vector<Token> tokens,
        const ColorDeclOptions& options,
        bool minify_whitespace,
        bool* would_clip_color) {
        size_t n = tokens.size();
        size_t end = 0;
        size_t i = 0;

        while (i < n) {
            // Advance `comma` to the next comma delimiter or the end of the list.
            size_t comma = i;
            while (comma < n && tokens[comma].kind != TokenType::kComma) {
                comma++;
            }

            // Mangle the individual shadow that lives between `i` and `comma`.
            std::vector<Token> mangled = LowerAndMangleBoxShadow(
                std::vector<Token>(tokens.begin() + static_cast<ptrdiff_t>(i),
                                tokens.begin() + static_cast<ptrdiff_t>(comma)),
                options, minify_whitespace, would_clip_color);
            for (const Token& token : mangled) {
                tokens[end++] = token;
            }

            // If there was a comma, copy it to the output and advance past it.
            if (comma < n) {
                tokens[end] = tokens[comma];
                end++;
                comma++;
            }
            i = comma;
        }

        tokens.resize(end);
        return tokens;
    }
}
