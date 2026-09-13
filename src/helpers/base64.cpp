#include "guchho/helpers.hpp"

namespace guchho::helpers {

    namespace {

        // The standard Base64 alphabet uses A-Z, a-z, 0-9, '+', and '/'.
        // This is the conventional encoding used in MIME (RFC 4648, Section 4).
        constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        
        // The URL-safe Base64 alphabet replaces '+' with '-' and '/' with '_'
        // so that the encoded output can appear unescaped inside URLs and
        // file paths without requiring additional percent-encoding.
        constexpr char kBase64URLAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        // The standard Base32 alphabet uses A-Z followed by the digits 2-7,
        // producing 32 distinct symbols (RFC 4648, Section 5).
        constexpr char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

        // Core Base64 encoder shared by both the standard and URL-safe variants.
        //
        // Reads binary data from `src` in groups of 3 bytes, packs each group
        // into a 24-bit integer, and emits 4 Base64 characters by splitting
        // the integer into four 6-bit indices into `alphabet`.  When the input
        // length is not a multiple of 3 the final group is padded with '='
        // characters to produce exactly 4 output characters.
        //
        // The output is appended to `out` (not replaced), so callers can
        // accumulate results from multiple calls if desired.
        //
        // Example:
        //   Input:  "\x00\x00\x00"  (3 null bytes)
        //   Output: "AAAA"          (each null byte maps to index 0)
        //
        // Edge cases:
        //   - Empty input produces no output.
        //   - Input with 1 remaining byte produces 2 data characters + "==".
        //   - Input with 2 remaining bytes produces 3 data characters + "=".
        void Base64EncodeImpl(std::string& out, std::string_view src, const char* alphabet)
        {
            size_t i     = 0;
            size_t n     = src.size();
            size_t chunks = n / 3;
            out.reserve(out.size() + ((n + 2) / 3) * 4);

            for (size_t c = 0; c < chunks; ++c) {
                uint32_t v = (static_cast<uint32_t>(static_cast<uint8_t>(src[i])) << 16) |
                             (static_cast<uint32_t>(static_cast<uint8_t>(src[i + 1])) << 8) |
                             static_cast<uint32_t>(static_cast<uint8_t>(src[i + 2]));
                out.push_back(alphabet[(v >> 18) & 0x3F]);
                out.push_back(alphabet[(v >> 12) & 0x3F]);
                out.push_back(alphabet[(v >> 6) & 0x3F]);
                out.push_back(alphabet[v & 0x3F]);
                i += 3;
            }

            size_t remaining = n - i;
            if (remaining == 1) {
                uint32_t v = static_cast<uint32_t>(static_cast<uint8_t>(src[i])) << 16;
                out.push_back(alphabet[(v >> 18) & 0x3F]);
                out.push_back(alphabet[(v >> 12) & 0x3F]);
                out += "==";
            } else if (remaining == 2) {
                uint32_t v = (static_cast<uint32_t>(static_cast<uint8_t>(src[i])) << 16) |
                             (static_cast<uint32_t>(static_cast<uint8_t>(src[i + 1])) << 8);
                out.push_back(alphabet[(v >> 18) & 0x3F]);
                out.push_back(alphabet[(v >> 12) & 0x3F]);
                out.push_back(alphabet[(v >> 6) & 0x3F]);
                out.push_back('=');
            }
        }

        // Appends the first `count` Base32 characters derived from the
        // upper bits of a 40-bit value.  Only the topmost `count * 5` bits
        // of `value` are used; lower bits are ignored.
        //
        // This helper is called once per 5-byte input chunk (producing 8
        // characters) and once for each partial trailing chunk where fewer
        // than 5 input bytes remain.
        //
        // Example:
        //   value = 0xA0'00'00'00'00, count = 2
        //   Binary of top 10 bits: 101000_0000
        //   Indices: 20, 0 => "UA"
        void Base32Emit(std::string& out, uint64_t value, unsigned count)
        {
            static_assert(sizeof(value) >= 8);
            for (unsigned j = 0; j < count; ++j) {
                out.push_back(kBase32Alphabet[(value >> (35 - j * 5)) & 0x1F]);
            }
        }

    }

    // Encodes binary data into standard Base64 (A-Z, a-z, 0-9, +, /).
    //
    // Output length is always ceil(n / 3) * 4 where n is the input length,
    // including '=' padding characters.
    //
    // Example:
    //   Input:  "Hello"  (5 bytes: 48 65 6C 6C 6F)
    //   Output: "SGVsbG8="
    //
    // This is suitable for embedding small binary literals in source text or
    // for encoding data transported over channels that require ASCII.
    std::string Base64StdEncode(std::string_view src)
    {
        std::string out;
        Base64EncodeImpl(out, src, kBase64Alphabet);
        return out;
    }

    // Encodes binary data into URL-safe Base64 (A-Z, a-z, 0-9, -, _).
    //
    // The output is identical to Base64StdEncode except that '+' is replaced
    // by '-' and '/' is replaced by '_', making the string safe to embed
    // directly in URLs, file names, and other contexts where the standard
    // alphabet characters have special meaning.
    //
    // Example:
    //   Input:  "??" (two bytes 0xFB 0xFF)
    //   Standard: "+/8="  (contains '+' and '/')
    //   URL-safe: "-_8="  (contains '-' and '_')
    std::string Base64URLEncode(std::string_view src)
    {
        std::string out;
        Base64EncodeImpl(out, src, kBase64URLAlphabet);
        return out;
    }

    // Encodes binary data into standard Base32 (A-Z, 2-7).
    //
    // Each group of 5 input bytes is expanded into 8 output characters.
    // When the input length is not a multiple of 5, the final group is
    // padded with '=' characters so that the output length is always
    // ceil(n / 5) * 8.
    //
    // Base32 is more space-efficient than Base64 when case-insensitive
    // transport is required (e.g. DNS, case-preserving file systems),
    // at the cost of a ~37% expansion ratio versus Base64's ~33%.
    //
    // Example:
    //   Input:  "Hello"  (5 bytes)
    //   Output: "JBSWY3DP"
    //
    // Edge cases:
    //   - Empty input returns an empty string.
    //   - Input with 1 byte  => 2 data chars + "======"  (8 total).
    //   - Input with 2 bytes => 4 data chars + "===="    (8 total).
    //   - Input with 3 bytes => 5 data chars + "==="     (8 total).
    //   - Input with 4 bytes => 7 data chars + "="       (8 total).
    std::string Base32StdEncode(std::string_view src)
    {
        std::string out;
        size_t   n      = src.size();
        size_t   chunks = n / 5;
        out.reserve(((n + 4) / 5) * 8);

        size_t i = 0;
        for (size_t c = 0; c < chunks; ++c) {
            uint64_t v = (static_cast<uint64_t>(static_cast<uint8_t>(src[i])) << 32) |
                         (static_cast<uint64_t>(static_cast<uint8_t>(src[i + 1])) << 24) |
                         (static_cast<uint64_t>(static_cast<uint8_t>(src[i + 2])) << 16) |
                         (static_cast<uint64_t>(static_cast<uint8_t>(src[i + 3])) << 8) |
                          static_cast<uint64_t>(static_cast<uint8_t>(src[i + 4]));
            Base32Emit(out, v, 8);
            i += 5;
        }

        switch (n % 5) {
            case 0: break;
            case 1:
                Base32Emit(out, static_cast<uint64_t>(static_cast<uint8_t>(src[i])) << 32, 2);
                out += "======";
                break;
            case 2:
                Base32Emit(out,
                           (static_cast<uint64_t>(static_cast<uint8_t>(src[i])) << 32) |
                               (static_cast<uint64_t>(static_cast<uint8_t>(src[i + 1])) << 24),
                           4);
                out += "====";
                break;
            case 3:
                Base32Emit(out,
                           (static_cast<uint64_t>(static_cast<uint8_t>(src[i])) << 32) |
                               (static_cast<uint64_t>(static_cast<uint8_t>(src[i + 1])) << 24) |
                               (static_cast<uint64_t>(static_cast<uint8_t>(src[i + 2])) << 16),
                           5);
                out += "===";
                break;
            case 4:
                Base32Emit(out,
                           (static_cast<uint64_t>(static_cast<uint8_t>(src[i])) << 32) |
                               (static_cast<uint64_t>(static_cast<uint8_t>(src[i + 1])) << 24) |
                               (static_cast<uint64_t>(static_cast<uint8_t>(src[i + 2])) << 16) |
                               (static_cast<uint64_t>(static_cast<uint8_t>(src[i + 3])) << 8),
                           7);
                out += "=";
                break;
        }

        return out;
    }

} // namespace guchho::helpers
