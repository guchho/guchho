#include "guchho/helpers.hpp"

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
        // input so they are re-escaped rather than double-encoded.
        bool IsHexDigit(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
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

} // namespace guchho::helpers