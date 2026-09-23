#include "guchho/helpers.hpp"

#include <algorithm>
#include <unordered_map>

namespace guchho::helpers {

    namespace {

        // Maps lowercase file extensions (with the leading dot) to their
        // preferred MIME types.  This is a built-in registry used instead
        // of the operating system's MIME database, which is unreliable on
        // certain platforms.  Every entry uses charset=utf-8 for text
        // types so that data URLs carry the correct encoding hint.
        const std::unordered_map<std::string_view, std::string_view>& BuiltinTypesLower()
        {
            static const std::unordered_map<std::string_view, std::string_view> types = {
                // Text
                {".css", "text/css; charset=utf-8"},
                {".htm", "text/html; charset=utf-8"},
                {".html", "text/html; charset=utf-8"},
                {".js", "text/javascript; charset=utf-8"},
                {".json", "application/json; charset=utf-8"},
                {".markdown", "text/markdown; charset=utf-8"},
                {".md", "text/markdown; charset=utf-8"},
                {".mjs", "text/javascript; charset=utf-8"},
                {".xhtml", "application/xhtml+xml; charset=utf-8"},
                {".xml", "text/xml; charset=utf-8"},

                // Images
                {".avif", "image/avif"},
                {".gif", "image/gif"},
                {".jpeg", "image/jpeg"},
                {".jpg", "image/jpeg"},
                {".png", "image/png"},
                {".svg", "image/svg+xml"},
                {".webp", "image/webp"},

                // Audio
                {".mp3", "audio/mpeg"},

                // Fonts
                {".eot", "application/vnd.ms-fontobject"},
                {".otf", "font/otf"},
                {".sfnt", "font/sfnt"},
                {".ttf", "font/ttf"},
                {".woff", "font/woff"},
                {".woff2", "font/woff2"},

                // Other
                {".pdf", "application/pdf"},
                {".wasm", "application/wasm"},
                {".webmanifest", "application/manifest+json"},
            };
            return types;
        }

        // Returns true when `c` is a hexadecimal digit (0-9, a-f, or A-F).
        // Used to detect pre-existing percent-encoded sequences (%XX) in the
        // input so they are re-escaped rather than double-encoded, and to
        // validate the two characters that follow a '%' while decoding a
        // data URL payload.
        bool IsHexDigit(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        // ------------------------------------------------------------------
        // Base64 alphabet lookup
        //
        // Maps one standard Base64 alphabet character onto its 6-bit value.
        // Guchho calls this while folding each character of an inline
        // "data:...;base64,..." payload into the bit accumulator, and relies
        // on the -1 sentinel to reject anything outside the alphabet.
        //
        //   Input:  'A'  ->  Output: 0
        //   Input:  '7'  ->  Output: 59
        //   Input:  '/'  ->  Output: 63
        //   Input:  '!'  ->  Output: -1   (illegal character)
        // ------------------------------------------------------------------
        int Base64Value(uint8_t c)
        {
            if (c >= 'A' && c <= 'Z') {
                return c - 'A';
            }
            if (c >= 'a' && c <= 'z') {
                return c - 'a' + 26;
            }
            if (c >= '0' && c <= '9') {
                return c - '0' + 52;
            }
            if (c == '+') {
                return 62;
            }
            if (c == '/') {
                return 63;
            }
            return -1;
        }

        // ------------------------------------------------------------------
        // Strict Base64 decoding for Guchho data URLs
        //
        // Consumes a standard Base64 string and writes the recovered bytes
        // into dst. Carriage returns and line feeds are skipped so that
        // Base64 blobs which have been soft-wrapped inside a stylesheet still
        // decode cleanly, and the input must terminate on a complete
        // four-character quantum with legal padding ("=" may only appear in
        // the final group and only in the last two slots). On the first
        // illegal byte, corrupt_at is set to that byte's offset so Guchho can
        // point at the exact spot in the URL when reporting the failure.
        //
        //   Input:  "aGVs"        ->  Output: true,  dst = "hel"
        //   Input:  "aGVsbG8="    ->  Output: true,  dst = "hello"
        //   Input:  "aG=\nV"      ->  Output: false, corrupt_at = 2
        //   Input:  "aGV"         ->  Output: false  (truncated quantum)
        // ------------------------------------------------------------------
        bool DecodeBase64Std(std::string_view src, std::string& dst, size_t& corrupt_at)
        {
            dst.clear();
            dst.reserve(src.size() / 4 * 3);
            uint32_t buffer      = 0;
            int      count       = 0;
            int      pad_count   = 0;
            bool     saw_padding = false;

            for (size_t i = 0; i < src.size(); ++i) {
                char c = src[i];
                if (c == '\r' || c == '\n') {
                    continue;
                }
                uint32_t value = 0;
                if (c == '=') {
                    if (count < 2 || pad_count >= 2 || (count + pad_count) > 4) {
                        corrupt_at = i;
                        return false;
                    }
                    value = 0;
                    ++pad_count;
                } else {
                    if (saw_padding) {
                        corrupt_at = i;
                        return false;
                    }
                    int v = Base64Value(static_cast<uint8_t>(c));
                    if (v < 0) {
                        corrupt_at = i;
                        return false;
                    }
                    value = static_cast<uint32_t>(v);
                }
                buffer = (buffer << 6) | value;
                ++count;
                if (count == 4) {
                    dst.push_back(static_cast<char>(buffer >> 16));
                    if (pad_count < 2) {
                        dst.push_back(static_cast<char>(buffer >> 8));
                    }
                    if (pad_count == 0) {
                        dst.push_back(static_cast<char>(buffer));
                    }
                    buffer      = 0;
                    count       = 0;
                    pad_count   = 0;
                    saw_padding = false;
                }
            }

            if (count != 0) {
                corrupt_at = src.size() - static_cast<size_t>(count);
                return false;
            }
            return true;
        }

        // ------------------------------------------------------------------
        // Single hex digit to value
        //
        // Converts one already-validated hex digit into its numeric value in
        // the range 0..15. Guchho pairs this with IsHexDigit to turn the two
        // characters of a "%XX" escape into one output byte.
        //
        //   Input: '0'  ->  Output: 0
        //   Input: '9'  ->  Output: 9
        //   Input: 'a'  ->  Output: 10
        //   Input: 'F'  ->  Output: 15
        // ------------------------------------------------------------------
        inline int HexValue(char c)
        {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            return c - 'A' + 10;
        }

        // ------------------------------------------------------------------
        // Percent-escape expansion
        //
        // Walks a data URL payload and replaces every well-formed "%XX"
        // triplet with the byte it names, copying all other characters
        // through unchanged. When a '%' is not followed by two hex digits,
        // the function stops, fills error with a JSON-quoted snippet of the
        // offending fragment, and returns false so Guchho can report exactly
        // which escape was broken.
        //
        //   Input:  "a%20b"   ->  Output: true,  out = "a b"
        //   Input:  "%41%42"  ->  Output: true,  out = "AB"
        //   Input:  "a%zz"    ->  Output: false, error mentions "%zz"
        //   Input:  "100%"    ->  Output: false, error mentions "%"
        // ------------------------------------------------------------------
        bool UnescapePath(std::string_view text, std::string& out, std::string& error)
        {
            out.clear();
            for (size_t i = 0; i < text.size();) {
                char c = text[i];
                if (c == '%') {
                    if (i + 2 >= text.size() || !IsHexDigit(text[i + 1]) || !IsHexDigit(text[i + 2])) {
                        size_t length = std::min<size_t>(3, text.size() - i);
                        error         = "invalid URL escape " +
                                QuoteForJSON(text.substr(i, length), false);
                        return false;
                    }
                    out.push_back(static_cast<char>(HexValue(text[i + 1]) * 16 + HexValue(text[i + 2])));
                    i += 3;
                } else {
                    out.push_back(c);
                    ++i;
                }
            }
            return true;
        }

    }

    // Looks up the MIME type for a file extension.
    //
    // The extension should include the leading dot (e.g. ".js", ".png").
    // The lookup is case-insensitive: both ".PNG" and ".png" resolve to
    // "image/png".  First a direct lookup in the built-in table is
    // attempted; on miss, the extension is folded to lower case and
    // looked up again.
    //
    // Returns an empty string_view when the extension is not recognised.
    //
    // Example:
    //   MimeTypeByExtension(".woff2") => "font/woff2"
    //   MimeTypeByExtension(".PNG")    => "image/png"
    //   MimeTypeByExtension(".xyz")    => ""
    std::string_view MimeTypeByExtension(std::string_view ext)
    {
        const auto& types = BuiltinTypesLower();
        if (auto it = types.find(ext); it != types.end()) {
            return it->second;
        }
        std::string lowered = ToLowerASCII(ext);
        auto        it      = types.find(lowered);
        return it != types.end() ? it->second : std::string_view{};
    }

    // Encodes arbitrary text into a data URL using percent-encoding.
    //
    // The output has the form `data:<mime_type>,<encoded_text>`.  Only
    // characters that are not safe inside a URI are percent-encoded:
    // control characters (tab, newline, carriage return), the hash
    // character, trailing whitespace, and any bare '%' that is followed
    // by two hex digits (to avoid confusing the encoder's own escapes).
    // All other bytes - including printable ASCII and multi-byte UTF-8 -
    // pass through unmodified, which typically produces a shorter URL
    // than base64 for text-like content.
    //
    // Returns a pair of (encoded_url, success).  On failure the result
    // is empty and success is false - this happens when the input
    // contains invalid UTF-8 that DecodeRuneInString cannot handle.
    //
    // Example:
    //   mime_type = "text/plain"
    //   text      = "hello world"
    //   result    = "data:text/plain,hello world"  (no escaping needed)
    //
    // Example with escaping:
    //   mime_type = "text/plain"
    //   text      = "line1\nline2"
    //   result    = "data:text/plain,line1%0Aline2"
    //
    // Edge cases:
    //   - Empty input: output is "data:<mime>," (valid but trivial).
    //   - Invalid UTF-8 byte: returns {"", false}.
    //   - Input ending in whitespace: trailing whitespace is always
    //     percent-encoded to prevent browsers from stripping it.
    std::pair<std::string, bool> EncodeStringAsPercentEscapedDataURL(std::string_view mime_type,
                                                                     std::string_view text)
    {
        constexpr char hex[] = "0123456789ABCDEF";
        std::string    out;
        size_t         n  = text.size();
        size_t         i  = 0;
        size_t         run_start = 0;
        out.reserve(n + 16);
        out += "data:";
        out += mime_type;
        out += ',';

        // Scan for trailing characters that need to be escaped
        size_t trailing_start = n;
        while (trailing_start > 0) {
            char c = text[trailing_start - 1];
            if (static_cast<unsigned char>(c) > 0x20 || c == '\t' || c == '\n' || c == '\r') {
                break;
            }
            --trailing_start;
        }

        while (i < n) {
            auto [c, width] = DecodeRuneInString(text.substr(i));

            // We can't encode invalid UTF-8 data
            if (c == 0xFFFD && width == 1) {
                return {{}, false};
            }
            width = std::max(width, 1);

            // Escape this character if needed
            bool is_percent_with_hex_suffix =
                c == '%' && i + 2 < n && IsHexDigit(text[i + 1]) && IsHexDigit(text[i + 2]);
            if (c == '\t' || c == '\n' || c == '\r' || c == '#' || i >= trailing_start ||
                is_percent_with_hex_suffix) {
                if (run_start < i) {
                    out += text.substr(run_start, i - run_start);
                }
                uint32_t code = static_cast<uint32_t>(c);
                out.push_back('%');
                out.push_back(hex[(code >> 4) & 15]);
                out.push_back(hex[code & 15]);
                run_start = i + static_cast<size_t>(width);
            }

            i += static_cast<size_t>(width);
        }

        if (run_start < n) {
            out += text.substr(run_start);
        }

        return {out, true};
    }

    // Encodes text into the shortest possible data URL.
    //
    // Two candidate encodings are produced and the smaller one is
    // returned:
    //   1. Percent-encoding - compact for ASCII-heavy text because most
    //      bytes pass through unescaped.
    //   2. Base64 - more compact for binary or high-byte content because
    //      every byte maps to a printable ASCII character without the
    //      overhead of percent signs and hex digits.
    //
    // The MIME type is looked up from the extension via MimeTypeByExtension.
    // If the percent-encoding attempt fails (invalid UTF-8), the base64
    // fallback is used unconditionally.
    //
    // Example:
    //   mime_type = "text/javascript"
    //   text      = "var x = 1;"
    //   result    = "data:text/javascript,var%20x%20%3D%201%3B"
    //               (percent encoding is shorter than base64 here)
    std::string EncodeStringAsShortestDataURL(std::string_view mime_type, std::string_view text)
    {
        std::string encoded = Base64StdEncode(text);
        std::string url     = "data:" + std::string(mime_type) + ";base64," + encoded;
        auto [percent_url, ok] = EncodeStringAsPercentEscapedDataURL(mime_type, text);
        if (ok && percent_url.size() < url.size()) {
            return percent_url;
        }
        return url;
    }

    // ------------------------------------------------------------------
    // Data URL recognition and splitting
    //
    // First entry point of Guchho's data URL handling. It checks for the
    // mandatory "data:" prefix, locates the comma that separates the header
    // from the payload, and records the media type (minus any ";base64"
    // marker, which also flips is_base64) alongside the raw payload. URLs
    // that are not data URLs, or data URLs missing their comma, yield
    // nullopt so callers can fall through to other URL handling.
    //
    //   Input:  "data:text/css;base64,Ym9keXs="
    //     ->  Output: DataURL{ mime_type = "text/css", data = "Ym9keXs=", is_base64 = true }
    //
    //   Input:  "data:text/css,body%7B%7D"
    //     ->  Output: DataURL{ mime_type = "text/css", data = "body%7B%7D", is_base64 = false }
    //
    //   Input:  "https://example.com/app.css"
    //     ->  Output: std::nullopt   (no "data:" prefix)
    //
    //   Input:  "data:text/css"
    //     ->  Output: std::nullopt   (no comma separating header and payload)
    // ------------------------------------------------------------------
    std::optional<DataURL> ParseDataURL(const std::string& url)
    {
        constexpr std::string_view kPrefix = "data:";
        if (url.compare(0, kPrefix.size(), kPrefix) != 0) {
            return std::nullopt;
        }

        size_t comma = url.find(',');
        if (comma == std::string::npos) {
            return std::nullopt;
        }

        DataURL parsed;
        parsed.mime_type = url.substr(kPrefix.size(), comma - kPrefix.size());
        parsed.data      = url.substr(comma + 1);

        constexpr std::string_view kBase64Suffix = ";base64";
        if (parsed.mime_type.size() >= kBase64Suffix.size() &&
            parsed.mime_type.compare(parsed.mime_type.size() - kBase64Suffix.size(),
                                     kBase64Suffix.size(), kBase64Suffix) == 0) {
            parsed.mime_type.resize(parsed.mime_type.size() - kBase64Suffix.size());
            parsed.is_base64 = true;
        }
        return parsed;
    }

    // ------------------------------------------------------------------
    // Media type classification
    //
    // Narrows the stored media type down to the small set of content kinds
    // Guchho actually acts on. Any parameter section beginning at the first
    // ';' (for example ";charset=utf-8") is chopped off first, then the bare
    // type is matched against the supported list. Everything else is
    // reported as unsupported so callers can skip the payload.
    //
    //   Input:  mime_type = "text/css;charset=utf-8"  ->  Output: MIMEType::kTextCSS
    //   Input:  mime_type = "text/javascript"         ->  Output: MIMEType::kTextJavaScript
    //   Input:  mime_type = "application/json"        ->  Output: MIMEType::kApplicationJSON
    //   Input:  mime_type = "image/png"               ->  Output: MIMEType::kUnsupported
    // ------------------------------------------------------------------
    MIMEType DataURL::DecodeMIMEType() const
    {
        std::string bare_type = this->mime_type;
        size_t      semicolon = bare_type.find(';');
        if (semicolon != std::string::npos) {
            bare_type.resize(semicolon);
        }

        if (bare_type == "text/css") {
            return MIMEType::kTextCSS;
        }
        if (bare_type == "text/javascript") {
            return MIMEType::kTextJavaScript;
        }
        if (bare_type == "application/json") {
            return MIMEType::kApplicationJSON;
        }
        return MIMEType::kUnsupported;
    }

    // ------------------------------------------------------------------
    // Payload decoding
    //
    // Turns the raw payload captured by ParseDataURL back into bytes that
    // the rest of Guchho can consume. Base64 payloads go through the strict
    // decoder, which reports the offset of the first bad byte; plain payloads
    // go through the percent-unescaper, whose message is wrapped with a
    // distinguishing prefix. Both failure paths fill error and return
    // nullopt.
    //
    //   Input:  is_base64 = true,   data = "aGVsbG8="
    //     ->  Output: "hello"
    //
    //   Input:  is_base64 = false,  data = "a%20b"
    //     ->  Output: "a b"
    //
    //   Input:  is_base64 = true,   data = "aG=V"
    //     ->  Output: std::nullopt, error = "could not decode base64 data: illegal base64 data at input byte 2"
    //
    //   Input:  is_base64 = false,  data = "%zz"
    //     ->  Output: std::nullopt, error = "could not decode percent-escaped data: invalid URL escape \"%zz\""
    // ------------------------------------------------------------------
    std::optional<std::string> DataURL::DecodeData(std::string& error) const
    {
        if (is_base64) {
            std::string bytes;
            size_t      corrupt_at = 0;
            if (!DecodeBase64Std(data, bytes, corrupt_at)) {
                error = "could not decode base64 data: illegal base64 data at input byte " +
                        std::to_string(corrupt_at);
                return std::nullopt;
            }
            return bytes;
        }

        std::string content;
        if (!UnescapePath(data, content, error)) {
            error = "could not decode percent-escaped data: " + error;
            return std::nullopt;
        }
        return content;
    }

} // namespace guchho::helpers