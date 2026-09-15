#include "guchho/helpers.hpp"

namespace guchho::helpers {

    namespace {

        // The WHATWG MIME sniffing algorithm examines at most this many
        // bytes to determine the content type.
        constexpr size_t kSniffLen = 512;

        // Returns true for ASCII whitespace characters (tab, newline,
        // form feed, carriage return, and space).
        bool IsWS(uint8_t b)
        {
            switch (b) {
                case '\t':
                case '\n':
                case '\x0C':
                case '\r':
                case ' ': return true;
                default: return false;
            }
        }

        // A tag-terminating byte as defined by the WHATWG spec: either
        // a space or '>'.  Used after an HTML tag name to confirm the
        // tag is well-formed.
        bool IsTT(uint8_t b)
        {
            return b == ' ' || b == '>';
        }

        // Describes a single byte-pattern signature used for MIME type
        // detection.  Each signature has a kind that determines the
        // matching algorithm:
        //   kExact  - the first N bytes must match the pattern exactly.
        //   kMasked - each byte is ANDed with a mask before comparing.
        //   kHtml   - case-insensitive ASCII match followed by a tag-
        //             terminating byte.
        //   kMp4    - special ISO base media file format detection.
        struct Sig {
            enum class Kind : uint8_t {
                kExact,
                kMasked,
                kHtml,
                kMp4,
            };

            Kind             kind;
            std::string_view pat;
            std::string_view mask;
            bool             skip_ws{};
            std::string_view ct;
        };

        // Helper to construct an exact-match signature at compile time.
        template <size_t N>
        constexpr Sig Exact(const char (&pat)[N], std::string_view ct)
        {
            return {Sig::Kind::kExact, std::string_view(pat, N - 1), {}, false, ct};
        }

        // Helper to construct a masked-match signature at compile time.
        // `skip_ws` causes leading whitespace to be ignored before
        // matching.
        template <size_t N, size_t M>
        constexpr Sig Masked(const char (&mask)[N], const char (&pat)[M], bool skip_ws,
                             std::string_view ct)
        {
            return {Sig::Kind::kMasked, std::string_view(pat, M - 1),
                    std::string_view(mask, N - 1), skip_ws, ct};
        }

        // Helper to construct an HTML tag signature at compile time.
        // Always skips leading whitespace and requires a tag-terminating
        // byte after the pattern.
        template <size_t N>
        constexpr Sig Html(const char (&pat)[N])
        {
            return {Sig::Kind::kHtml, std::string_view(pat, N - 1), {}, true,
                    "text/html; charset=utf-8"};
        }

        // Attempts to detect ISO base media file format (MP4) containers.
        //
        // The first 4 bytes encode the size of the initial box, bytes
        // 5-8 must be "ftyp", and one of the brand strings in the box
        // must start with "mp4".  The box size must be a multiple of 4
        // and no larger than the available data.
        //
        // Returns "video/mp4" on match, or an empty string_view.
        //
        // Example:
        //   MatchMp4("\x00\x00\x00\x1Cftypmp41...") => "video/mp4"
        std::string_view MatchMp4(std::string_view data)
        {
            if (data.size() < 12) {
                return {};
            }
            uint32_t box_size = (static_cast<uint32_t>(static_cast<uint8_t>(data[0])) << 24) |
                                (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 16) |
                                (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 8) |
                                static_cast<uint32_t>(static_cast<uint8_t>(data[3]));
            if (data.size() < box_size || box_size % 4 != 0) {
                return {};
            }
            if (data.substr(4, 4) != "ftyp") {
                return {};
            }
            for (uint32_t st = 8; st < box_size; st += 4) {
                if (st == 12) {
                    // Skip the four-byte major brand version field.
                    continue;
                }
                if (data.substr(st, 3) == "mp4") {
                    return "video/mp4";
                }
            }
            return {};
        }

        // Checks whether the data looks like plain text.
        //
        // Scans from the first non-whitespace byte to the end.  If every
        // byte is either a printable character, a tab/newline/CR, or
        // outside the forbidden control range, the data is considered
        // text.  This is the WHATWG spec's step 4, checked last.
        //
        // Example:
        //   MatchText("  hello\nworld", 2) => true
        //   MatchText("\x00\x01\x02", 0)   => false
        bool MatchText(std::string_view data, size_t first_non_ws)
        {
            for (size_t i = first_non_ws; i < data.size(); ++i) {
                uint8_t b = static_cast<uint8_t>(data[i]);
                if (b <= 0x08 || b == 0x0B || (b >= 0x0E && b <= 0x1A) ||
                    (b >= 0x1C && b <= 0x1F)) {
                    return false;
                }
            }
            return true;
        }

    }

    // Detects the MIME content type by inspecting the first 512 bytes.
    //
    // The function implements the WHATWG MIME sniffing algorithm.  It
    // tries the signatures in order - exact byte matches first, then
    // masked matches, then HTML tag matches, then the MP4 special case.
    // If no signature matches but the data looks like text, it returns
    // "text/plain; charset=utf-8".  The final fallback is
    // "application/octet-stream".
    //
    // The input may be shorter than 512 bytes; the algorithm works on
    // whatever is available.
    //
    // Example:
    //   DetectContentType("\x89PNG\r\n\x1A\n...") => "image/png"
    //   DetectContentType("<!DOCTYPE html>...")     => "text/html; charset=utf-8"
    //   DetectContentType("\x00\x00\x00\x00...")   => "application/octet-stream"
    std::string DetectContentType(std::string_view data)
    {
        if (data.size() > kSniffLen) {
            data = data.substr(0, kSniffLen);
        }

        // Index of the first non-whitespace byte in data.
        size_t first_non_ws = 0;
        for (; first_non_ws < data.size() && IsWS(static_cast<uint8_t>(data[first_non_ws]));
             ++first_non_ws) {
        }

        // Signature table from section 6 of the WHATWG MIME sniff spec.
        static const Sig sigs[] = {
            Html("<!DOCTYPE HTML"),
            Html("<HTML"),
            Html("<HEAD"),
            Html("<SCRIPT"),
            Html("<IFRAME"),
            Html("<H1"),
            Html("<DIV"),
            Html("<FONT"),
            Html("<TABLE"),
            Html("<A"),
            Html("<STYLE"),
            Html("<TITLE"),
            Html("<B"),
            Html("<BODY"),
            Html("<BR"),
            Html("<P"),
            Html("<!--"),
            Masked("\xFF\xFF\xFF\xFF\xFF", "<?xml", true, "text/xml; charset=utf-8"),
            Exact("%PDF-", "application/pdf"),
            Exact("%!PS-Adobe-", "application/postscript"),

            // Byte-order marks for Unicode encodings.
            Masked("\xFF\xFF\x00\x00", "\xFE\xFF\x00\x00", false,
                   "text/plain; charset=utf-16be"),
            Masked("\xFF\xFF\x00\x00", "\xFF\xFE\x00\x00", false,
                   "text/plain; charset=utf-16le"),
            Masked("\xFF\xFF\xFF\x00", "\xEF\xBB\xBF\x00", false,
                   "text/plain; charset=utf-8"),

            // Image types
            Exact("\x00\x00\x01\x00", "image/x-icon"),
            Exact("\x00\x00\x02\x00", "image/x-icon"),
            Exact("BM", "image/bmp"),
            Exact("GIF87a", "image/gif"),
            Exact("GIF89a", "image/gif"),
            Masked("\xFF\xFF\xFF\xFF\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF",
                   "RIFF\x00\x00\x00\x00WEBPVP", false, "image/webp"),
            Exact("\x89PNG\x0D\x0A\x1A\x0A", "image/png"),
            Exact("\xFF\xD8\xFF", "image/jpeg"),

            // Audio and video types
            Masked("\xFF\xFF\xFF\xFF\x00\x00\x00\x00\xFF\xFF\xFF\xFF",
                   "FORM\x00\x00\x00\x00AIFF", false, "audio/aiff"),
            Masked("\xFF\xFF\xFF", "ID3", false, "audio/mpeg"),
            Masked("\xFF\xFF\xFF\xFF\xFF", "OggS\x00", false, "application/ogg"),
            Masked("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", "MThd\x00\x00\x00\x06", false,
                   "audio/midi"),
            Masked("\xFF\xFF\xFF\xFF\x00\x00\x00\x00\xFF\xFF\xFF\xFF",
                   "RIFF\x00\x00\x00\x00AVI ", false, "video/avi"),
            Masked("\xFF\xFF\xFF\xFF\x00\x00\x00\x00\xFF\xFF\xFF\xFF",
                   "RIFF\x00\x00\x00\x00WAVE", false, "audio/wave"),

            // ISO base media file format (MP4) - uses a special handler.
            {Sig::Kind::kMp4, {}, {}, false, "video/mp4"},

            // WebM container
            Exact("\x1A\x45\xDF\xA3", "video/webm"),

            // Font types
            Masked("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                   "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                   "\x00\x00\xFF\xFF",
                   "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                   "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                   "\x00\x00LP",
                   false, "application/vnd.ms-fontobject"),
            Exact("\x00\x01\x00\x00", "font/ttf"),
            Exact("OTTO", "font/otf"),
            Exact("ttcf", "font/collection"),
            Exact("wOFF", "font/woff"),
            Exact("wOF2", "font/woff2"),

            // Archive types
            Exact("\x1F\x8B\x08", "application/x-gzip"),
            Exact("PK\x03\x04", "application/zip"),
            // RAR signatures per RAR Labs definitions (the WHATWG spec
            // has incorrect values - see mimesniff issue #63).
            Exact("Rar!\x1A\x07\x00", "application/x-rar-compressed"),     // RAR v1.5-v4.0
            Exact("Rar!\x1A\x07\x01\x00", "application/x-rar-compressed"), // RAR v5+

            Exact("\x00\x61\x73\x6D", "application/wasm"),
        };

        for (const Sig& sig : sigs) {
            switch (sig.kind) {
                case Sig::Kind::kExact: {
                    if (data.substr(0, sig.pat.size()) == sig.pat) {
                        return std::string(sig.ct);
                    }
                    break;
                }

                case Sig::Kind::kMasked: {
                    // Pattern matching algorithm (section 6 of the spec):
                    // each input byte is ANDed with the corresponding mask
                    // byte, then compared to the pattern byte.
                    std::string_view d =
                        sig.skip_ws ? data.substr(first_non_ws) : data;
                    if (sig.pat.size() != sig.mask.size()) {
                        break;
                    }
                    if (d.size() < sig.pat.size()) {
                        break;
                    }
                    bool ok = true;
                    for (size_t j = 0; j < sig.pat.size(); ++j) {
                        uint8_t masked_data =
                            static_cast<uint8_t>(d[j]) & static_cast<uint8_t>(sig.mask[j]);
                        if (masked_data != static_cast<uint8_t>(sig.pat[j])) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        return std::string(sig.ct);
                    }
                    break;
                }

                case Sig::Kind::kHtml: {
                    std::string_view d = data.substr(first_non_ws);
                    if (d.size() < sig.pat.size() + 1) {
                        break;
                    }
                    bool ok = true;
                    for (size_t j = 0; j < sig.pat.size(); ++j) {
                        char b  = sig.pat[j];
                        char db = d[j];
                        if (b >= 'A' && b <= 'Z') {
                            db &= 0xDF;
                        }
                        if (b != db) {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) {
                        break;
                    }
                    // The byte immediately after the tag name must be a
                    // tag-terminating byte (space or '>') to confirm
                    // this is a real HTML tag and not a prefix of one.
                    if (!IsTT(static_cast<uint8_t>(d[sig.pat.size()]))) {
                        break;
                    }
                    return std::string(sig.ct);
                }

                case Sig::Kind::kMp4: {
                    if (std::string_view ct = MatchMp4(data); !ct.empty()) {
                        return std::string(ct);
                    }
                    break;
                }
            }
        }

        if (MatchText(data, first_non_ws)) {
            return "text/plain; charset=utf-8";
        }

        // No signature matched and the data does not look like text.
        return "application/octet-stream";
    }

} // namespace guchho::helpers