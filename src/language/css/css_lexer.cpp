#include "guchho/css/css_lexer.hpp"
#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"

// =============================================================================
// CSS Lexer Implementation
//
// This file implements the CSS tokenizer (lexer) defined in css_lexer.hpp.
// The tokenizer converts raw CSS source text into a flat sequence of tokens
// following the CSS Syntax Level 3 specification.
//
// Architecture:
//   The core is the Lexer struct, which maintains mutable state as it walks
//   through the source one code point at a time via step().  Higher-level
//   consume* methods build tokens by delegating to step() and to the
//   character predicates exported from the header (IsNameStart, etc.).
//
// Error handling:
//   Malformed constructs (unterminated strings, bad URLs, invalid escapes)
//   are reported through the guchho::logger::Log interface and the lexer
//   produces best-effort tokens so that parsing can continue.
//
// Escape decoding:
//   decodeEscapesInToken() and consumeEscape() handle CSS escape sequences.
//   They map hex escapes to code points, discard line-continuation newlines,
//   and replace null bytes and surrogates with U+FFFD.
// =============================================================================

namespace guchho::css {

    namespace {

        // Sentinel value representing end-of-file.  Set by step() when there
        // are no more bytes to consume.  Comparisons against kEOF are used
        // throughout the lexer to detect premature termination.
        constexpr char32_t kEOF = static_cast<char32_t>(-1);

        // Unicode replacement character.  Emitted whenever the source contains
        // an invalid byte sequence, a null byte, or a surrogate code point.
        constexpr char32_t kRuneError = 0xFFFD;

        // -------------------------------------------------------------------------
        // hexDigit
        //
        // Converts a single ASCII character to its hexadecimal numeric value.
        // Returns -1 for characters that are not valid hex digits.
        //
        // Example:
        //   hexDigit('A') == 10
        //   hexDigit('0') == 0
        //   hexDigit('z') == -1
        // -------------------------------------------------------------------------
        int hexDigit(char32_t c) noexcept {
            if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<int>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<int>(c - 'A' + 10);
            return -1;
        }

        // -------------------------------------------------------------------------
        // isNewline
        //
        // Returns true for the three ASCII newline code points recognized by
        // CSS: line feed (\n), carriage return (\r), and form feed (\f).
        // Carriage return + line feed pairs are treated as a single newline
        // elsewhere in the lexer; this function treats them individually.
        //
        // Example:
        //   isNewline('\n') == true
        //   isNewline('\r') == true
        //   isNewline(' ')  == false
        // -------------------------------------------------------------------------
        bool isNewline(char32_t c) noexcept {
            return c == '\n' || c == '\r' || c == '\f';
        }

        // -------------------------------------------------------------------------
        // isWhitespace
        //
        // Returns true for the five ASCII characters that CSS considers
        // whitespace: space, tab, line feed, carriage return, and form feed.
        // This is the predicate used when collapsing runs of whitespace into
        // a single kWhitespace token.
        //
        // Example:
        //   isWhitespace(' ')  == true
        //   isWhitespace('\t') == true
        //   isWhitespace('a')  == false
        // -------------------------------------------------------------------------
        bool isWhitespace(char32_t c) noexcept {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
        }

        // -------------------------------------------------------------------------
        // isNonPrintable
        //
        // Returns true for code points that the CSS specification classifies
        // as non-printable.  These are: U+0000–U+0008, U+000B, U+000E–U+001F,
        // and U+007F.  Non-printable characters inside a url() token cause it
        // to be flagged as a bad URL.
        //
        // Example:
        //   isNonPrintable(0x00) == true
        //   isNonPrintable(0x09) == false  (tab is printable whitespace)
        //   isNonPrintable(0x7F) == true
        // -------------------------------------------------------------------------
        bool isNonPrintable(char32_t c) noexcept {
            return c <= 0x08 || c == 0x0B || (c >= 0x0E && c <= 0x1F) || c == 0x7F;
        }

        // -------------------------------------------------------------------------
        // appendRune
        //
        // Encodes a single Unicode code point as WTF-8 and appends the resulting
        // bytes to the string.  Used by escape-decoding routines to build up
        // the decoded text of tokens.
        //
        // Example:
        //   std::string s;
        //   appendRune(s, 0x41);  // s == "A"
        //   appendRune(s, 0xE9);  // s == "Aé"
        // -------------------------------------------------------------------------
        void appendRune(std::string& s, char32_t cp) {
            char buf[4];
            auto n = guchho::helpers::EncodeWTF8Rune(buf, cp);
            s.append(buf, static_cast<size_t>(n));
        }

        // -------------------------------------------------------------------------
        // containsAtPreserveOrAtLicense
        //
        // Scans `text` for the substring "@preserve" or "@license".  These
        // at-keywords inside a comment signal that the comment is semantically
        // meaningful (e.g. a license header) and should be preserved as a
        // "legal comment" by the lexer.
        //
        // Example:
        //   containsAtPreserveOrAtLicense("/* @license MIT */") == true
        //   containsAtPreserveOrAtLicense("/* just a note */")  == false
        // -------------------------------------------------------------------------
        bool containsAtPreserveOrAtLicense(std::string_view text) {
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '@') {
                    auto rest = text.substr(i + 1);
                    if (rest.starts_with("preserve") || rest.starts_with("license")) {
                        return true;
                    }
                }
            }
            return false;
        }

        // -------------------------------------------------------------------------
        // decodeEscapesInToken
        //
        // Processes the raw text of a token and resolves every CSS escape
        // sequence into its decoded code point.  The input is the raw source
        // slice between delimiters (e.g. the characters between quotes for a
        // string token, or after '#' for a hash token).
        //
        // Behavior:
        //   - Hex escapes (e.g. "\41") are decoded to the corresponding code
        //     point.  Up to 6 hex digits are consumed.
        //   - A whitespace character immediately following a hex escape is
        //     consumed as part of the escape (per spec).
        //   - Line-continuation newlines (\A\n, \D\r\n) are silently removed.
        //   - Null bytes (\0) are replaced with U+FFFD.
        //   - Surrogates (U+D800–U+DFFF) and U+0000 are replaced with U+FFFD.
        //   - A trailing backslash at EOF produces U+FFFD.
        //
        // Input => Output examples:
        //   decodeEscapesInToken("foo\\2Dbar")  => "foo-bar"
        //   decodeEscapesInToken("\\41")         => "A"
        //   decodeEscapesInToken("a\\0b")        => "a\uFFFD"  (null => replacement)
        //   decodeEscapesInToken("x\\A\n")       => "x"        (line continuation)
        // -------------------------------------------------------------------------
        std::string decodeEscapesInToken(std::string_view inner) {
            size_t i = 0;
            for (; i < inner.size(); ++i) {
                auto c = inner[i];
                if (c == '\\' || c == '\x00') break;
            }

            if (i == inner.size()) {
                return std::string(inner);
            }

            std::string result;
            result.reserve(inner.size());
            result.append(inner.substr(0, i));
            inner = inner.substr(i);

            while (!inner.empty()) {
                auto [cp, width] = guchho::helpers::DecodeWTF8Rune(inner);
                inner = inner.substr(static_cast<size_t>(width));

                if (cp != '\\') {
                    if (cp == '\x00') cp = 0xFFFD;
                    appendRune(result, cp);
                    continue;
                }

                if (inner.empty()) {
                    appendRune(result, 0xFFFD);
                    continue;
                }

                auto [c, w] = guchho::helpers::DecodeWTF8Rune(inner);
                inner = inner.substr(static_cast<size_t>(w));
                auto hex = hexDigit(c);

                if (hex < 0) {
                    if (c == '\n' || c == '\f') continue;
                    if (c == '\r') {
                        auto [c2, w2] = guchho::helpers::DecodeWTF8Rune(inner);
                        if (c2 == '\n') inner = inner.substr(static_cast<size_t>(w2));
                        continue;
                    }
                    appendRune(result, c);
                    continue;
                }

                for (int j = 0; j < 5 && !inner.empty(); ++j) {
                    auto [c2, w2] = guchho::helpers::DecodeWTF8Rune(inner);
                    auto h2 = hexDigit(c2);
                    if (h2 >= 0) {
                        inner = inner.substr(static_cast<size_t>(w2));
                        hex = hex * 16 + h2;
                    } else {
                        break;
                    }
                }

                if (!inner.empty()) {
                    auto [c2, w2] = guchho::helpers::DecodeWTF8Rune(inner);
                    if (isWhitespace(c2)) {
                        inner = inner.substr(static_cast<size_t>(w2));
                    }
                }

                if (hex == 0 || (hex >= 0xD800 && hex <= 0xDFFF) || hex > 0x10FFFF) {
                    appendRune(result, 0xFFFD);
                    continue;
                }

                appendRune(result, static_cast<char32_t>(hex));
            }

            return result;
        }

        // =========================================================================
        // Lexer
        //
        // Mutable state machine that walks through the CSS source one code point
        // at a time and produces tokens.  The lexer is not re-entrant; each call
        // to next() advances the internal cursor and populates `token` with the
        // next lexical unit.
        //
        // The lexer handles:
        //   - All token types defined in TokenType (identifiers, strings,
        //     numbers, dimensions, percentages, functions, URLs, delimiters,
        //     at-keywords, hashes, comments, whitespace).
        //   - CSS escape sequences in identifiers, strings, and URLs.
        //   - Error recovery for unterminated strings, bad URLs, and invalid
        //     escapes.
        //   - Collection of "legal comments" (comments containing @preserve,
        //     @license, or starting with '!').
        //   - Extraction of source-map URLs from comments.
        //   - Diagnostics for single-line comments (which are not valid CSS).
        // =========================================================================

        struct Lexer {
            // Configuration options (e.g. whether to record all comments).
            lexer::Options                     options;

            // Diagnostic log; never null when the lexer is in active use.
            guchho::logger::Log*               log{};

            // The source being tokenized; set once before tokenization begins.
            const guchho::logger::Source*      source{};

            // Ranges of every comment encountered (populated only when
            // options.record_all_comments is true).
            std::vector<guchho::logger::Range> all_comments;

            // Comments deemed "legal" (containing @preserve/@license or
            // starting with '!').  Flushed into the result after each token.
            std::vector<lexer::Comment>        legal_comments;

            // The source-map URL extracted from the first "//#" comment.
            lexer::Span                        source_mapping_url;

            // Tracks line and column for diagnostic messages.
            guchho::logger::LineColumnTracker  tracker;

            // Heuristic count of newlines encountered (used for line count).
            int                                approximate_newline_count{};

            // Byte offset of the next character to consume (0-indexed).
            int current{};

            // Byte offset past the end of the most recent single-line comment.
            // Used to suppress duplicate warnings for overlapping comments.
            guchho::logger::Loc                 old_single_line_comment_end;

            // The most recently consumed code point, or kEOF at end of input.
            char32_t code_point{};

            // The token currently being built; replaced on each next() call.
            lexer::Token token;

            // -------------------------------------------------------------------------
            // step
            //
            // Advances the lexer by one UTF-8 code point.  Updates code_point
            // to the next character (or kEOF if at end of input), increments
            // the approximate newline counter on '\n', and extends the current
            // token's range length.
            //
            // Example:
            //   Given source "ab" with current == 0:
            //     step() => code_point == 'a', current == 1
            //     step() => code_point == 'b', current == 2
            //     step() => code_point == kEOF, current == 2
            // -------------------------------------------------------------------------
            void step() {
                auto& contents = source->contents;
                auto remaining = static_cast<int>(contents.size()) - current;
                if (remaining <= 0) {
                    code_point = kEOF;
                    token.range.len = static_cast<int32_t>(current) - token.range.loc.start;
                    return;
                }
                auto view = std::string_view(contents).substr(static_cast<size_t>(current));
                auto [cp, width] = guchho::helpers::DecodeWTF8Rune(view);
                if (width == 0) {
                    cp = kEOF;
                }
                if (cp == '\n') {
                    ++approximate_newline_count;
                }
                code_point = cp;
                token.range.len = static_cast<int32_t>(current) - token.range.loc.start;
                current += width;
            }

            // -------------------------------------------------------------------------
            // isValidEscape
            //
            // Returns true if the current code point is a backslash and the
            // following code point (if any) forms a valid CSS escape sequence.
            // A backslash followed by a newline is considered invalid here
            // because it is a line continuation, not a real escape.  This
            // method peeks ahead without consuming characters.
            //
            // Example:
            //   Source "\41" at backslash => isValidEscape() == true
            //   Source "\"  at backslash  => isValidEscape() == false (EOF)
            //   Source "\\" at backslash  => isValidEscape() == false (newline follows)
            // -------------------------------------------------------------------------
            bool isValidEscape() {
                if (code_point != '\\') return false;

                auto view = std::string_view(source->contents)
                    .substr(static_cast<size_t>(current));

                auto [c, width] = guchho::helpers::DecodeWTF8Rune(view);
                (void)width;

                return c != '\n' &&
                    c != '\r' &&
                    c != '\f';
            }

            // -------------------------------------------------------------------------
            // wouldStartIdentifier
            //
            // Returns true if the current code point (and possibly the next)
            // would start a CSS identifier.  Handles the three cases:
            //   1. A name-start character (letter, underscore, non-ASCII).
            //   2. A hyphen followed by a name-start character or another hyphen.
            //   3. A hyphen followed by a valid escape sequence.
            //
            // This is used by the lexer's decision logic in next() to choose
            // between different token types when encountering '-' or '#'.
            //
            // Example:
            //   Source "foo" at 'f' => wouldStartIdentifier() == true
            //   Source "-a"  at '-' => wouldStartIdentifier() == true
            //   Source "--"  at '-' => wouldStartIdentifier() == true
            //   Source "123" at '1' => wouldStartIdentifier() == false
            // -------------------------------------------------------------------------
            bool wouldStartIdentifier() {
                if (code_point == kEOF) return false;
                if (IsNameStart(code_point)) return true;

                if (code_point == '-') {
                    auto view = std::string_view(source->contents)
                        .substr(static_cast<size_t>(current));

                    auto [c, width] = guchho::helpers::DecodeWTF8Rune(view);
                    if (c == kRuneError && width <= 1) return false;
                    if (IsNameStart(c) || c == '-') return true;
                    if (c == '\\') {
                        auto rest = view.substr(static_cast<size_t>(width));
                        auto [c2, width2] = guchho::helpers::DecodeWTF8Rune(rest);
                        return c2 != '\n' &&
                            c2 != '\r' &&
                            c2 != '\f';
                    }
                    return false;
                }

                return false;
            }

            // -------------------------------------------------------------------------
            // wouldStartNumber
            //
            // Returns true if the current code point (and possibly the next
            // one or two) would start a CSS number token.  Numbers can begin
            // with:
            //   - A digit (0-9)
            //   - A decimal point followed by a digit
            //   - A sign (+/-) followed by a digit or a decimal point + digit
            //
            // This is a lookahead predicate; it does not consume characters.
            //
            // Example:
            //   Source "42"   at '4' => wouldStartNumber() == true
            //   Source ".5"   at '.' => wouldStartNumber() == true
            //   Source "+3"   at '+' => wouldStartNumber() == true
            //   Source "-.1"  at '-' => wouldStartNumber() == true
            //   Source "abc"  at 'a' => wouldStartNumber() == false
            // -------------------------------------------------------------------------
            bool wouldStartNumber() {
                if (code_point >= '0' && code_point <= '9') return true;
                if (code_point == '.') {
                    auto& contents = source->contents;
                    if (current < static_cast<int>(contents.size())) {
                        auto c = static_cast<unsigned char>(contents[static_cast<size_t>(current)]);
                        return c >= '0' && c <= '9';
                    }
                }
                if (code_point == '+' || code_point == '-') {
                    auto& contents = source->contents;
                    auto n = static_cast<int>(contents.size());
                    if (current < n) {
                        auto c = static_cast<unsigned char>(contents[static_cast<size_t>(current)]);
                        if (c >= '0' && c <= '9') return true;
                        if (c == '.' && current + 1 < n) {
                            auto c2 = static_cast<unsigned char>(contents[static_cast<size_t>(current) + 1]);
                            return c2 >= '0' && c2 <= '9';
                        }
                    }
                }
                return false;
            }

            // -------------------------------------------------------------------------
            // consumeEscape
            //
            // Consumes a CSS escape sequence starting at the character after the
            // backslash (the backslash itself must already be the current code
            // point when this is called).  Returns the decoded code point.
            //
            // Behavior:
            //   - If the next character is a hex digit, reads up to 6 hex digits
            //     to form a code point.  A trailing whitespace character is
            //     consumed as part of the escape.
            //   - If the next character is EOF, returns U+FFFD.
            //   - Otherwise, returns the next character literally.
            //   - Code points U+0000, U+D800–U+DFFF, and > U+10FFFF are
            //     replaced with U+FFFD.
            //
            // Example:
            //   Source "\41x" => consumeEscape() == 'A', current advanced past "41"
            //   Source "\2D"  => consumeEscape() == '-'
            //   Source "\0"   => consumeEscape() == 0xFFFD
            // -------------------------------------------------------------------------
            char32_t consumeEscape() {
                step();
                auto c = code_point;
                auto hex = hexDigit(c);

                if (hex >= 0) {
                    step();
                    for (int i = 0; i < 5; ++i) {
                        auto h2 = hexDigit(code_point);
                        if (h2 >= 0) {
                            step();
                            hex = hex * 16 + h2;
                        } else {
                            break;
                        }
                    }
                    if (isWhitespace(code_point)) {
                        step();
                    }
                    if (hex == 0 || (hex >= 0xD800 && hex <= 0xDFFF) || hex > 0x10FFFF) {
                        return 0xFFFD;
                    }
                    return static_cast<char32_t>(hex);
                }

                if (c == kEOF) {
                    return 0xFFFD;
                }

                step();
                return c;
            }

            // -------------------------------------------------------------------------
            // consumeName
            //
            // Consumes a CSS identifier name starting at the current code point.
            // The name may contain escape sequences, which are decoded as the
            // name is consumed.  Returns the fully decoded name as a string.
            //
            // The method first scans any contiguous name-continue characters
            // (fast ASCII path), then falls back to character-by-character
            // processing to handle escapes.
            //
            // Example:
            //   Source "foo" => consumeName() == "foo"
            //   Source "foo\\2Dbar" => consumeName() == "foo-bar"
            //   Source "\\41" => consumeName() == "A"
            // -------------------------------------------------------------------------
            std::string consumeName() {
                auto& contents = source->contents;

                if (IsNameContinue(code_point)) {
                    auto n = static_cast<int>(contents.size());
                    auto i = current;
                    for (; i < n && IsNameContinue(static_cast<unsigned char>(contents[static_cast<size_t>(i)])); ++i) {
                    }
                    current = i;
                    step();
                }

                auto raw_start = static_cast<size_t>(token.range.loc.start);
                auto raw_len = static_cast<size_t>(token.range.len);

                if (!isValidEscape()) {
                    return std::string(contents.substr(raw_start, raw_len));
                }

                std::string result;
                result.reserve(raw_len + 16);
                result.append(contents, raw_start, raw_len);
                appendRune(result, consumeEscape());

                while (true) {
                    if (IsNameContinue(code_point)) {
                        appendRune(result, code_point);
                        step();
                    } else if (isValidEscape()) {
                        appendRune(result, consumeEscape());
                    } else {
                        break;
                    }
                }

                return result;
            }

            // -------------------------------------------------------------------------
            // consumeIdentLike
            //
            // Consumes an identifier-like token: either a plain identifier, a
            // function token, or a URL token.  The decision is made by looking
            // at the character after the identifier name:
            //   - If '(' follows, it is a function token.  Special handling
            //     applies when the name is "url" (case-insensitive): if the
            //     content after optional whitespace is not a quoted string, the
            //     lexer switches to consumeURL() to parse a bare URL.
            //   - Otherwise, the token is a plain identifier.
            //
            // Example:
            //   Source "rgba(" => function token "rgba("
            //   Source "url(https://example.com/img.png)"
            //     => URL token with value "https://example.com/img.png"
            //   Source "div" => identifier token "div"
            // -------------------------------------------------------------------------
            TokenType consumeIdentLike() {
                auto name = consumeName();

                if (code_point == '(') {
                    auto matching_loc = guchho::logger::Loc{static_cast<int32_t>(token.range.End())};
                    step();

                    if (name.size() == 3) {
                        auto u = static_cast<unsigned char>(name[0]);
                        auto r = static_cast<unsigned char>(name[1]);
                        auto l = static_cast<unsigned char>(name[2]);
                        if ((u == 'u' || u == 'U') && (r == 'r' || r == 'R') && (l == 'l' || l == 'L')) {
                            auto saved_approximate_newline_count = approximate_newline_count;
                            auto saved_code_point = code_point;
                            auto saved_token_range_len = token.range.len;
                            auto saved_current = current;

                            while (isWhitespace(code_point)) {
                                step();
                            }
                            if (code_point != '"' && code_point != '\'') {
                                return consumeURL(matching_loc);
                            }

                            approximate_newline_count = saved_approximate_newline_count;
                            code_point = saved_code_point;
                            token.range.len = saved_token_range_len;
                            current = saved_current;
                        }
                    }
                    return TokenType::kFunction;
                }

                return TokenType::kIdent;
            }

            // -------------------------------------------------------------------------
            // consumeURL
            //
            // Consumes the contents of a url() token.  The opening '(' and the
            // "url" name have already been consumed; this method reads characters
            // until the closing ')' or EOF.
            //
            // Behavior:
            //   - Whitespace before and after the URL value is stripped.
            //   - Escape sequences in the URL value are consumed (but the
            //     decoded text is returned by DecodedText, not here).
            //   - If a closing ')' is never found, a diagnostic is emitted and
            //     the token is tagged as kUrl (best-effort recovery).
            //   - If illegal characters are encountered (quotes, non-printable
            //     chars, invalid escapes), the token is tagged as kBadUrl and
            //     the lexer skips to the next ')' or EOF.
            //
            // Input => Output example:
            //   Source: "url(  https://x.com/img.png  )"
            //   => TokenType::kUrl, raw range covers the full url(...)
            //
            // Edge cases:
            //   - Empty url(): "url()" => kUrl with empty value.
            //   - Unterminated url(): "url(foo" => diagnostic + kUrl.
            //   - url("foo"): quotes cause kBadUrl (quotes not allowed bare).
            // -------------------------------------------------------------------------
            TokenType consumeURL(guchho::logger::Loc matching_loc) {
                while (true) {
                    switch (code_point) {
                    case ')':
                        step();
                        return TokenType::kUrl;

                    case kEOF:
                        log->AddIDWithNotes(guchho::logger::MsgID::kCSS_CSSSyntaxError, guchho::logger::MsgKind::kWarning,
                            &tracker, guchho::logger::Range{guchho::logger::Loc{token.range.End()}},
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ExpectedParenEndURLToken),
                            {tracker.MakeMsgData(guchho::logger::Range{matching_loc, 1},
                                                 guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_UnbalancedParenNote))});
                        return TokenType::kUrl;

                    case ' ':
                    case '\t':
                    case '\n':
                    case '\r':
                    case '\f':
                        step();
                        while (isWhitespace(code_point)) {
                            step();
                        }
                        if (code_point != ')') {
                            log->AddIDWithNotes(guchho::logger::MsgID::kCSS_CSSSyntaxError, guchho::logger::MsgKind::kWarning,
                                &tracker, guchho::logger::Range{guchho::logger::Loc{token.range.End()}},
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ExpectedParenEndURLToken),
                                {tracker.MakeMsgData(guchho::logger::Range{matching_loc, 1},
                                                     guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_UnbalancedParenNote))});
                            if (code_point == kEOF) {
                                return TokenType::kUrl;
                            }
                            goto badURL;
                        }
                        step();
                        return TokenType::kUrl;

                    case '"':
                    case '\'':
                    case '(':
                        log->AddIDWithNotes(guchho::logger::MsgID::kCSS_CSSSyntaxError, guchho::logger::MsgKind::kWarning,
                            &tracker, guchho::logger::Range{guchho::logger::Loc{token.range.End()}, 1},
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ExpectedParenEndURLToken),
                            {tracker.MakeMsgData(guchho::logger::Range{matching_loc, 1},
                                                 guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_UnbalancedParenNote))});
                        goto badURL;

                    case '\\':
                        if (!isValidEscape()) {
                            log->AddID(guchho::logger::MsgID::kCSS_CSSSyntaxError, guchho::logger::MsgKind::kWarning,
                                &tracker, guchho::logger::Range{guchho::logger::Loc{token.range.End()}, 1},
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_InvalidEscape));
                            goto badURL;
                        }
                        consumeEscape();
                        break;

                    default:
                        if (isNonPrintable(code_point)) {
                            log->AddID(guchho::logger::MsgID::kCSS_CSSSyntaxError, guchho::logger::MsgKind::kWarning,
                                &tracker, guchho::logger::Range{guchho::logger::Loc{token.range.End()}, 1},
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_NonPrintCharURLToken));
                            goto badURL;
                        }
                        step();
                    }
                }

            badURL:
                while (true) {
                    switch (code_point) {
                    case ')':
                    case kEOF:
                        step();
                        return TokenType::kBadUrl;
                    case '\\':
                        if (isValidEscape()) {
                            consumeEscape();
                        }
                        break;
                    }
                    step();
                }
            }

            // -------------------------------------------------------------------------
            // consumeString
            //
            // Consumes a CSS string token.  The opening quote character (either
            // '"' or '\'') is the current code point.  The method reads until
            // the matching closing quote, handling escape sequences along the way.
            //
            // Behavior:
            //   - Backslash + newline is a line continuation (both are consumed,
            //     no character is emitted).
            //   - An EOF, newline, or form feed before the closing quote
            //     produces a kUnterminatedString token and a diagnostic.
            //   - The closing quote is consumed but not included in the raw
            //     token range (it is part of the delimiter).
            //
            // Example:
            //   Source "'hello world'" => kString, raw range "'hello world'"
            //   Source "\"line1\\nline2\"" => kString with embedded newline
            //   Source "'unterminated"  => kUnterminatedString + diagnostic
            // -------------------------------------------------------------------------
            TokenType consumeString() {
                auto quote = code_point;
                step();

                while (true) {
                    switch (code_point) {
                    case '\\':
                        step();
                        if (code_point == '\r') {
                            step();
                            if (code_point == '\n') step();
                            continue;
                        }
                        break;

                    case kEOF:
                    case '\n':
                    case '\r':
                    case '\f':
                        log->AddID(guchho::logger::MsgID::kCSS_CSSSyntaxError, guchho::logger::MsgKind::kWarning,
                            &tracker, guchho::logger::Range{guchho::logger::Loc{token.range.End()}},
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_UnterminatedStringToken));
                        return TokenType::kUnterminatedString;

                    case '"':
                    case '\'':
                        if (code_point == quote) {
                            step();
                            return TokenType::kString;
                        }
                        break;
                    }
                    step();
                }
            }

            // -------------------------------------------------------------------------
            // consumeNumeric
            //
            // Consumes a CSS numeric token.  The numeric value may include an
            // optional sign (+/-), integer part, decimal fraction, and exponent
            // (e/E with optional sign).  After the number itself:
            //   - If an identifier follows, the token is a dimension (e.g.
            //     "12px", "3em").  The unit offset is recorded so that
            //     DecodedText can separate the number from the unit.
            //   - If '%' follows, the token is a percentage.
            //   - Otherwise, the token is a plain number.
            //
            // Example:
            //   Source "42px"   => kDimension, number "42", unit "px"
            //   Source "3.14"   => kNumber
            //   Source "50%"    => kPercentage
            //   Source "+1e3em" => kDimension, number "+1e3", unit "em"
            // -------------------------------------------------------------------------
            TokenType consumeNumeric() {
                if (code_point == '+' || code_point == '-') step();

                while (code_point >= '0' && code_point <= '9') step();

                if (code_point == '.') {
                    step();
                    while (code_point >= '0' && code_point <= '9') step();
                }

                if (code_point == 'e' || code_point == 'E') {
                    auto& contents = source->contents;
                    auto n = static_cast<int>(contents.size());
                    if (current < n) {
                        auto c = static_cast<unsigned char>(contents[static_cast<size_t>(current)]);
                        if ((c == '+' || c == '-') && current + 1 < n) {
                            c = static_cast<unsigned char>(contents[static_cast<size_t>(current) + 1]);
                        }
                        if (c >= '0' && c <= '9') {
                            step();
                            if (code_point == '+' || code_point == '-') step();
                            while (code_point >= '0' && code_point <= '9') step();
                        }
                    }
                }

                if (wouldStartIdentifier()) {
                    token.unit_offset = static_cast<uint16_t>(token.range.len);
                    consumeName();
                    return TokenType::kDimension;
                }
                if (code_point == '%') {
                    step();
                    return TokenType::kPercentage;
                }
                return TokenType::kNumber;
            }

            // -------------------------------------------------------------------------
            // consumeToEndOfMultiLineComment
            //
            // Consumes everything from the current position up to (and including)
            // the closing "*/" of a multi-line comment.  Also handles:
            //
            //   - Source-map URL extraction: if the comment contains a line
            //     starting with "# sourceMappingURL=" or "@ sourceMappingURL=",
            //     the URL is captured in source_mapping_url.
            //   - Legal comment detection: comments that start with '!' or
            //     contain "@preserve" / "@license" are recorded in
            //     legal_comments so downstream consumers can preserve them.
            //   - Error recovery: if EOF is reached before "*/", a diagnostic
            //     is emitted and the method returns.
            //
            // Example:
            //   Source "/* @license MIT */" => legal_comments entry added.
            //   Source "/*# sourceMappingURL=app.css.map */"
            //     => source_mapping_url set to "app.css.map".
            // -------------------------------------------------------------------------
            void consumeToEndOfMultiLineComment(guchho::logger::Range start_range) {
                auto start_of_source_mapping_url = 0;
                auto is_legal_comment = false;

                switch (code_point) {
                case '#':
                case '@':
                    if (source->contents.substr(static_cast<size_t>(current)).starts_with(" sourceMappingURL=")) {
                        start_of_source_mapping_url = current + static_cast<int>(sizeof(" sourceMappingURL=") - 1);
                    }
                    break;

                case '!':
                    is_legal_comment = true;
                    break;
                }

                while (true) {
                    switch (code_point) {
                    case '*': {
                        auto end_of_source_mapping_url = current - 1;
                        step();
                        if (code_point == '/') {
                            auto comment_end = current;
                            step();

                            if (start_of_source_mapping_url != 0) {
                                guchho::logger::Range r{guchho::logger::Loc{static_cast<int32_t>(start_of_source_mapping_url)}};
                                auto text = std::string_view(source->contents).substr(
                                    static_cast<size_t>(start_of_source_mapping_url),
                                    static_cast<size_t>(static_cast<size_t>(end_of_source_mapping_url) - static_cast<size_t>(start_of_source_mapping_url)));
                                while (static_cast<size_t>(r.len) < text.size() && !isWhitespace(static_cast<unsigned char>(text[static_cast<size_t>(r.len)]))) {
                                    r.len++;
                                }
                                source_mapping_url = lexer::Span{std::string(text.substr(0, static_cast<size_t>(r.len))), r};
                            }

                            auto comment_range = guchho::logger::Range{start_range.loc, static_cast<int32_t>(comment_end) - start_range.loc.start};
                            if (options.record_all_comments) {
                                all_comments.push_back(comment_range);
                            }

                            auto comment_text = std::string_view(source->contents).substr(
                                static_cast<size_t>(start_range.loc.start),
                                static_cast<size_t>(static_cast<size_t>(comment_end) - static_cast<size_t>(start_range.loc.start)));
                            if (is_legal_comment || containsAtPreserveOrAtLicense(comment_text)) {
                                auto text = source->CommentTextWithoutIndent(comment_range);
                                legal_comments.push_back(lexer::Comment{std::move(text), start_range.loc, 0});
                            }
                            return;
                        }
                        break;
                    }

                    case kEOF:
                        log->AddErrorWithNotes(&tracker,
                            guchho::logger::Range{guchho::logger::Loc{token.range.End()}},
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ExpectedCommentTerminator_2),
                            {tracker.MakeMsgData(start_range,
                                                 guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_CommentStartsHere_2))});
                        return;

                    default:
                        step();
                    }
                }
            }

            // -------------------------------------------------------------------------
            // next
            //
            // Main tokenizer dispatch.  Consumes characters starting from the
            // current position and populates `token` with the next lexical unit.
            // The method is a large switch on the current code point; each case
            // either produces a simple single-character token or delegates to a
            // consume* method for multi-character constructs.
            //
            // After this method returns, `token` is fully populated with its
            // kind, range, flags, and (for dimensions) unit_offset.
            //
            // Notable handling:
            //   - '/': if followed by '*', the multi-line comment is consumed
            //     and the loop continues (comments are not tokens).  If followed
            //     by '/', a warning about single-line comments is emitted.
            //   - '-': disambiguates CDC ("-->"), numbers, identifiers, and
            //     bare delimiters.
            //   - '<': disambiguates CDO ("<!--") from the '<' delimiter.
            //   - '\': valid escapes start an identifier-like token; invalid
            //     escapes produce a diagnostic and a kDelim token.
            //   - Whitespace: collapses runs of whitespace and embeds comments
            //     that appear between whitespace runs.
            //
            // Example:
            //   Source "color: red;" => sequence of kIdent, kColon, kWhitespace,
            //     kIdent, kSemicolon tokens.
            // -------------------------------------------------------------------------
            void next() {
                while (true) {
                    token = lexer::Token{guchho::logger::Range{guchho::logger::Loc{token.range.End()}}};

                    switch (code_point) {
                    case kEOF:
                        token.kind = TokenType::kEndOfFile;
                        break;

                    case '/':
                        step();
                        switch (code_point) {
                        case '*':
                            step();
                            consumeToEndOfMultiLineComment(token.range);
                            continue;
                        case '/': {
                            auto loc = token.range.loc;
                            if (loc.start >= old_single_line_comment_end.start) {
                                auto& contents = source->contents;
                                auto end = current;
                                auto n = static_cast<int>(contents.size());
                                while (end < n && !isNewline(static_cast<unsigned char>(contents[static_cast<size_t>(end)]))) {
                                    ++end;
                                }
                                log->AddID(guchho::logger::MsgID::kCSS_JSCommentInCSS, guchho::logger::MsgKind::kWarning,
                                    &tracker, guchho::logger::Range{loc, 2},
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_CommentsUseSlashStar));
                                old_single_line_comment_end.start = static_cast<int32_t>(end);
                                token.flags |= kDidWarnAboutSingleLineComment;
                            }
                            break;
                        }
                        }
                        token.kind = TokenType::kDelimSlash;
                        break;

                    case ' ':
                    case '\t':
                    case '\n':
                    case '\r':
                    case '\f':
                        step();
                        while (true) {
                            if (isWhitespace(code_point)) {
                                step();
                            } else if (code_point == '/' &&
                                    current < static_cast<int>(source->contents.size()) &&
                                    source->contents[static_cast<size_t>(current)] == '*') {
                                auto start_range = guchho::logger::Range{guchho::logger::Loc{token.range.End()}, 2};
                                step();
                                step();
                                consumeToEndOfMultiLineComment(start_range);
                            } else {
                                break;
                            }
                        }
                        token.kind = TokenType::kWhitespace;
                        break;

                    case '"':
                    case '\'':
                        token.kind = consumeString();
                        break;

                    case '#':
                        step();
                        if (IsNameContinue(code_point) || isValidEscape()) {
                            token.kind = TokenType::kHash;
                            if (wouldStartIdentifier()) {
                                token.flags |= kIsID;
                            }
                            consumeName();
                        } else {
                            token.kind = TokenType::kDelim;
                        }
                        break;

                    case '(':
                        step();
                        token.kind = TokenType::kOpenParen;
                        break;

                    case ')':
                        step();
                        token.kind = TokenType::kCloseParen;
                        break;

                    case '[':
                        step();
                        token.kind = TokenType::kOpenBracket;
                        break;

                    case ']':
                        step();
                        token.kind = TokenType::kCloseBracket;
                        break;

                    case '{':
                        step();
                        token.kind = TokenType::kOpenBrace;
                        break;

                    case '}':
                        step();
                        token.kind = TokenType::kCloseBrace;
                        break;

                    case ',':
                        step();
                        token.kind = TokenType::kComma;
                        break;

                    case ':':
                        step();
                        token.kind = TokenType::kColon;
                        break;

                    case ';':
                        step();
                        token.kind = TokenType::kSemicolon;
                        break;

                    case '+':
                        if (wouldStartNumber()) {
                            token.kind = consumeNumeric();
                        } else {
                            step();
                            token.kind = TokenType::kDelimPlus;
                        }
                        break;

                    case '.':
                        if (wouldStartNumber()) {
                            token.kind = consumeNumeric();
                        } else {
                            step();
                            token.kind = TokenType::kDelimDot;
                        }
                        break;

                    case '-':
                        if (wouldStartNumber()) {
                            token.kind = consumeNumeric();
                        } else if (current + 2 <= static_cast<int>(source->contents.size()) &&
                                source->contents.substr(static_cast<size_t>(current), 2) == "->") {
                            step();
                            step();
                            step();
                            token.kind = TokenType::kCdc;
                        } else if (wouldStartIdentifier()) {
                            token.kind = consumeIdentLike();
                        } else {
                            step();
                            token.kind = TokenType::kDelimMinus;
                        }
                        break;

                    case '<':
                        if (current + 3 <= static_cast<int>(source->contents.size()) &&
                            source->contents.substr(static_cast<size_t>(current), 3) == "!--") {
                            step();
                            step();
                            step();
                            step();
                            token.kind = TokenType::kCdo;
                        } else {
                            step();
                            token.kind = TokenType::kDelimLessThan;
                        }
                        break;

                    case '@':
                        step();
                        if (wouldStartIdentifier()) {
                            consumeName();
                            token.kind = TokenType::kAtKeyword;
                        } else {
                            token.kind = TokenType::kDelim;
                        }
                        break;

                    case '\\':
                        if (isValidEscape()) {
                            token.kind = consumeIdentLike();
                        } else {
                            step();
                            log->AddError(&tracker, token.range,
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_InvalidEscape));
                            token.kind = TokenType::kDelim;
                        }
                        break;

                    case '0': case '1': case '2': case '3': case '4':
                    case '5': case '6': case '7': case '8': case '9':
                        token.kind = consumeNumeric();
                        break;

                    case '>': step(); token.kind = TokenType::kDelimGreaterThan; break;
                    case '~': step(); token.kind = TokenType::kDelimTilde; break;
                    case '&': step(); token.kind = TokenType::kDelimAmpersand; break;
                    case '*': step(); token.kind = TokenType::kDelimAsterisk; break;
                    case '|': step(); token.kind = TokenType::kDelimBar; break;
                    case '!': step(); token.kind = TokenType::kDelimExclamation; break;
                    case '=': step(); token.kind = TokenType::kDelimEquals; break;
                    case '^': step(); token.kind = TokenType::kDelimCaret; break;
                    case '$': step(); token.kind = TokenType::kDelimDollar; break;

                    default:
                        if (IsNameStart(code_point)) {
                            token.kind = consumeIdentLike();
                        } else {
                            step();
                            token.kind = TokenType::kDelim;
                        }
                    }

                    return;
                }
            }
        };

        // -------------------------------------------------------------------------
        // kTokenToString
        //
        // Lookup table that maps each TokenType enum value to a human-readable
        // string.  Used by the ToString() function for diagnostics and
        // debugging output.  The order must match the TokenType enum exactly.
        // -------------------------------------------------------------------------
        const char* kTokenToString[] = {
            "end of file",
            "@-keyword",
            "bad string token",
            "bad URL token",
            "\"-->\"",
            "\"<!--\"",
            "\"}\"",
            "\"]\"",
            "\")\"",
            "\":\"",
            "\",\"",
            "delimiter",
            "\"&\"",
            "\"*\"",
            "\"|\"",
            "\"^\"",
            "\"$\"",
            "\".\"",
            "\"=\"",
            "\"!\"",
            "\">\"",
            "\"<\"",
            "\"-\"",
            "\"+\"",
            "\"/\"",
            "\"~\"",
            "dimension",
            "function token",
            "hash token",
            "identifier",
            "number",
            "\"{\"",
            "\"[\"",
            "\"(\"",
            "percentage",
            "\";\"",
            "string token",
            "URL token",
            "whitespace",
            "identifier",
        };

    }

    // -------------------------------------------------------------------------
    // IsNameStart
    //
    // Returns true if `cp` may appear as the first character of a CSS
    // identifier (an <ident-start code point>).  Valid starts are:
    //   - ASCII letters (a-z, A-Z)
    //   - Underscore (_)
    //   - Any code point >= U+0080 (non-ASCII)
    //   - U+0000 (null byte — treated as a name start per the WTF-8 variant
    //     used here; the null byte will be replaced with U+FFFD later during
    //     escape decoding)
    //
    // Digits (0-9) and most punctuation are NOT valid name starts.
    //
    // Example:
    //   IsNameStart('a')  == true
    //   IsNameStart('_')  == true
    //   IsNameStart(0xE9) == true  (é)
    //   IsNameStart('3')  == false
    // -------------------------------------------------------------------------
    bool IsNameStart(char32_t cp) noexcept {
        return (cp >= 'a' && cp <= 'z') ||
            (cp >= 'A' && cp <= 'Z') ||
            cp == '_' ||
            (cp >= 0x80 && cp != kEOF) ||
            cp == '\x00';
    }

    // -------------------------------------------------------------------------
    // IsNameContinue
    //
    // Returns true if `cp` may appear after the first character of a CSS
    // identifier (an <ident code point>).  This is a superset of IsNameStart
    // that additionally allows:
    //   - ASCII digits (0-9)
    //   - Hyphen (-)
    //
    // Example:
    //   IsNameContinue('0') == true
    //   IsNameContinue('-') == true
    //   IsNameContinue('!') == false
    // -------------------------------------------------------------------------
    bool IsNameContinue(char32_t cp) noexcept {
        return IsNameStart(cp) || (cp >= '0' && cp <= '9') || cp == '-';
    }

    // -------------------------------------------------------------------------
    // WouldStartIdentifierWithoutEscapes
    //
    // Fast, ASCII-only check that determines whether `text` begins with the
    // characters of a CSS identifier without interpreting any escape sequences.
    // This is used by external callers who need a quick peek at the start of a
    // string (e.g. when deciding whether a selector starts an identifier).
    //
    // Behavior:
    //   - Decodes the first UTF-8 code point from `text`.
    //   - Returns true if it is a name-start character.
    //   - If the first character is '-', checks the second code point: an
    //     identifier starts if the second is a name-start character or
    //     another hyphen.
    //   - Returns false for empty strings, invalid UTF-8, or non-identifier
    //     starts.
    //
    // Example:
    //   WouldStartIdentifierWithoutEscapes("foo") == true
    //   WouldStartIdentifierWithoutEscapes("-a")  == true
    //   WouldStartIdentifierWithoutEscapes("--")  == true
    //   WouldStartIdentifierWithoutEscapes("123") == false
    //   WouldStartIdentifierWithoutEscapes("")     == false
    // -------------------------------------------------------------------------
    bool WouldStartIdentifierWithoutEscapes(std::string_view text) noexcept {
        auto [c, width] = guchho::helpers::DecodeWTF8Rune(text);
        if (c == 0xFFFD && width <= 1) return false;

        if (IsNameStart(c)) return true;

        if (c == '-') {
            if (static_cast<size_t>(width) >= text.size()) return false;
            auto rest = text.substr(static_cast<size_t>(width));
            auto [c2, w2] = guchho::helpers::DecodeWTF8Rune(rest);
            if (c2 == 0xFFFD && w2 <= 1) return false;
            if (IsNameStart(c2) || c2 == '-') return true;
        }

        return false;
    }

    // -------------------------------------------------------------------------
    // RangeOfIdentifier
    //
    // Starting at `loc` (which must point to the first code point of a CSS
    // identifier), advances through the source and returns the full (inclusive)
    // range of the identifier.  The range includes any escape sequences that
    // are part of the identifier.
    //
    // Behavior:
    //   - Scans name-continue characters via IsNameContinue().
    //   - Handles CSS escape sequences: a backslash followed by a hex digit
    //     consumes up to 6 hex digits plus an optional trailing whitespace.
    //   - Invalid bytes (width <= 0 from DecodeWTF8Rune) are consumed as part
    //     of the identifier rather than terminating it.
    //   - If the identifier ends with a whitespace character, that whitespace
    //     is excluded from the returned range (it is not part of the name).
    //
    // Input => Output example:
    //   Source: "foo\\2Dbar rest"
    //   loc pointing at 'f'
    //   => Range covers "foo\\2Dbar" (the 'r' at the end of "bar"),
    //     not including the trailing space.
    //
    // Edge case:
    //   If `loc` points past the end of the source, an empty range is
    //   returned.
    // -------------------------------------------------------------------------
    guchho::logger::Range RangeOfIdentifier(const guchho::logger::Source& source, guchho::logger::Loc loc) {
        auto text = std::string_view(source.contents).substr(static_cast<size_t>(loc.start));
        if (text.empty()) {
            return guchho::logger::Range{loc, 0};
        }

        size_t i = 0;
        auto n = text.size();

        while (i < n) {
            auto view = text.substr(i);
            auto [c, width] = guchho::helpers::DecodeWTF8Rune(view);

            if (width <= 0) {
                i++;
                continue;
            }

            if (IsNameContinue(c)) {
                i += static_cast<size_t>(width);
                continue;
            }

            if (c == '\\' && i + 1 < n && !isNewline(static_cast<unsigned char>(text[i + 1]))) {
                i += static_cast<size_t>(width);
                auto rest = text.substr(i);
                auto [c2, w2] = guchho::helpers::DecodeWTF8Rune(rest);
                if (w2 <= 0) break;
                auto hex = hexDigit(c2);
                if (hex >= 0) {
                    i += static_cast<size_t>(w2);
                    for (int j = 0; j < 5; ++j) {
                        rest = text.substr(i);
                        auto [c3, w3] = guchho::helpers::DecodeWTF8Rune(rest);
                        if (w3 <= 0) break;
                        auto h3 = hexDigit(c3);
                        if (h3 >= 0) {
                            i += static_cast<size_t>(w3);
                        } else {
                            break;
                        }
                    }
                    rest = text.substr(i);
                    auto [c3, w3] = guchho::helpers::DecodeWTF8Rune(rest);
                    if (w3 > 0 && isWhitespace(c3)) {
                        i += static_cast<size_t>(w3);
                    }
                }
                continue;
            }

            break;
        }

        if (i > 0 && isWhitespace(static_cast<unsigned char>(text[i - 1]))) {
            --i;
        }

        return guchho::logger::Range{loc, static_cast<int32_t>(i)};
    }

    // -------------------------------------------------------------------------
    // WouldStartIdentifier
    //
    // Full-featured check that determines whether an identifier starts at the
    // given location in the source, understanding CSS escape sequences.  This
    // is the escape-aware counterpart of WouldStartIdentifierWithoutEscapes.
    //
    // Internally creates a temporary Lexer positioned at `loc` and delegates
    // to Lexer::wouldStartIdentifier().
    //
    // Example:
    //   Source "\\2Dfoo" at loc pointing at the backslash:
    //     WouldStartIdentifier(source, loc) == true
    //     (the escaped '-' is a valid identifier start)
    // -------------------------------------------------------------------------
    bool WouldStartIdentifier(const guchho::logger::Source& source, guchho::logger::Loc loc) {
        Lexer lexer;
        lexer.source = &source;
        lexer.current = loc.start;
        lexer.step();
        return lexer.wouldStartIdentifier();
    }

    // -------------------------------------------------------------------------
    // IsValidEscapeAt
    //
    // Returns true if a backslash at `loc` forms a valid CSS escape sequence.
    // This is the escape-aware counterpart of Lexer::isValidEscape(), usable
    // by external code that does not have access to a Lexer instance.
    //
    // Internally creates a temporary Lexer positioned at `loc` and delegates
    // to Lexer::isValidEscape().
    //
    // Example:
    //   IsValidEscapeAt(source, loc) where source[loc] is '\\' and the next
    //   character is 'A' => true.
    //   IsValidEscapeAt(source, loc) where source[loc] is '\\' and the next
    //   character is '\n' => false (line continuation, not a real escape).
    // -------------------------------------------------------------------------
    bool IsValidEscapeAt(const guchho::logger::Source& source, guchho::logger::Loc loc) {
        Lexer lexer;
        lexer.source = &source;
        lexer.current = loc.start;
        lexer.step();
        return lexer.isValidEscape();
    }

    // -------------------------------------------------------------------------
    // IsNumeric
    //
    // Convenience predicate that returns true for token types representing
    // numeric values: kNumber, kPercentage, and kDimension.  Useful when a
    // parser or analyzer needs to accept any numeric form without caring
    // about the specific kind.
    //
    // Example:
    //   IsNumeric(TokenType::kNumber)      == true
    //   IsNumeric(TokenType::kPercentage)   == true
    //   IsNumeric(TokenType::kDimension)    == true
    //   IsNumeric(TokenType::kIdent)        == false
    //   IsNumeric(TokenType::kString)       == false
    // -------------------------------------------------------------------------
    bool IsNumeric(TokenType kind) {
        return kind == TokenType::kNumber || kind == TokenType::kPercentage || kind == TokenType::kDimension;
    }

    namespace lexer {

        // -------------------------------------------------------------------------
        // ToString
        //
        // Returns a human-readable name for the given token type.  The returned
        // string is a string literal from kTokenToString and must not be freed.
        // If `kind` is out of range, an empty string is returned.
        //
        // Example:
        //   ToString(TokenType::kIdent) == "identifier"
        //   ToString(TokenType::kString) == "string token"
        //   ToString(TokenType::kOpenBrace) == "\"{\""
        // -------------------------------------------------------------------------
        std::string ToString(TokenType kind) {
            auto idx = static_cast<size_t>(kind);
            if (idx < sizeof(kTokenToString) / sizeof(kTokenToString[0])) {
                return kTokenToString[idx];
            }
            return {};
        }

        // -------------------------------------------------------------------------
        // Token::DecodedText
        //
        // Returns the decoded (escape-expanded) text of this token.  The raw
        // source slice is extracted from `contents` using the token's range,
        // then processed according to the token kind:
        //
        //   kIdent / kDimension  – full slice decoded (escapes in the name).
        //   kAtKeyword / kHash   – slice after the leading '@' or '#' decoded.
        //   kFunction            – slice before the trailing '(' decoded.
        //   kString              – slice between the quotes decoded.
        //   kUrl                 – content between "url(" and ")" decoded,
        //                          with leading/trailing whitespace stripped.
        //   Everything else      – raw slice returned as-is.
        //
        // Example:
        //   For a kIdent token with raw text "foo\\2Dbar":
        //     DecodedText(contents) == "foo-bar"
        //
        //   For a kString token with raw text "'hello\\nworld'":
        //     DecodedText(contents) == "hello\nworld"
        // -------------------------------------------------------------------------
        std::string Token::DecodedText(std::string_view contents) const {
            auto raw = contents.substr(
                static_cast<size_t>(range.loc.start),
                static_cast<size_t>(range.len));

            switch (kind) {
            case TokenType::kIdent:
            case TokenType::kDimension:
                return decodeEscapesInToken(raw);

            case TokenType::kAtKeyword:
            case TokenType::kHash:
                return decodeEscapesInToken(raw.substr(1));

            case TokenType::kFunction:
                return decodeEscapesInToken(raw.substr(0, raw.size() - 1));

            case TokenType::kString:
                return decodeEscapesInToken(raw.substr(1, raw.size() - 2));

            case TokenType::kUrl: {
                auto start = size_t{4};
                auto end = raw.size();
                if (raw[end - 1] == ')') {
                    --end;
                }
                while (start < end && isWhitespace(static_cast<unsigned char>(raw[start]))) {
                    ++start;
                }
                while (start < end && isWhitespace(static_cast<unsigned char>(raw[end - 1]))) {
                    --end;
                }
                return decodeEscapesInToken(raw.substr(start, end - start));
            }

            default:
                return std::string(raw);
            }
        }

        // -------------------------------------------------------------------------
        // Tokenize
        //
        // Entry point for the CSS tokenizer.  Consumes the entire source and
        // returns a TokenizeResult containing all lexical tokens, collected
        // comments, and a source-map comment if present.
        //
        // Behavior:
        //   - Skips a leading U+FEFF (BOM) if present.
        //   - Calls Lexer::next() in a loop until kEndOfFile is produced.
        //   - Flushes legal comments after each token so that each comment's
        //     token_index_after points to the token that follows it.
        //   - The final result includes an approximate line count derived from
        //     the number of newlines encountered during tokenization.
        //
        // Example:
        //   Source "a { color: red; }"
        //   => tokens: [kIdent("a"), kWhitespace, kOpenBrace, kWhitespace,
        //             kIdent("color"), kColon, kWhitespace, kIdent("red"),
        //             kSemicolon, kWhitespace, kCloseBrace, kEndOfFile]
        //
        // Edge cases:
        //   - Empty source => single kEndOfFile token.
        //   - Unterminated constructs => best-effort tokens + diagnostics.
        // -------------------------------------------------------------------------
        TokenizeResult Tokenize(guchho::logger::Log& log, const guchho::logger::Source& source, const Options& options) {
            Lexer lexer;
            lexer.options = options;
            lexer.log = &log;
            lexer.source = &source;
            lexer.tracker = guchho::logger::LineColumnTracker(&source);
            lexer.step();

            if (lexer.code_point == 0xFEFF) {
                lexer.step();
            }

            lexer.next();
            std::vector<Token> tokens;
            std::vector<Comment> legal_comments;
            while (lexer.token.kind != TokenType::kEndOfFile) {
                if (!lexer.legal_comments.empty()) {
                    for (auto& comment : lexer.legal_comments) {
                        comment.token_index_after = static_cast<uint32_t>(tokens.size());
                        legal_comments.push_back(std::move(comment));
                    }
                    lexer.legal_comments.clear();
                }
                tokens.push_back(lexer.token);
                lexer.next();
            }
            if (!lexer.legal_comments.empty()) {
                for (auto& comment : lexer.legal_comments) {
                    comment.token_index_after = static_cast<uint32_t>(tokens.size());
                    legal_comments.push_back(std::move(comment));
                }
                lexer.legal_comments.clear();
            }

            return TokenizeResult{
                std::move(tokens),
                std::move(lexer.all_comments),
                std::move(legal_comments),
                std::move(lexer.source_mapping_url),
                static_cast<int32_t>(lexer.approximate_newline_count) + 1,
            };
        }

    }
}

