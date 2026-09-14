#include "guchho/helpers.hpp"

namespace guchho::helpers {

// Combines two 32-bit hash values into a new hash.
//
// Uses the boost::hash_combine algorithm, which mixes the seed and hash
// through a sequence of bit shifts and addition with the golden ratio
// constant 0x9e3779b9.  This produces a well-distributed combined hash
// even when the two inputs are similar or correlated.
//
// The function is order-dependent: HashCombine(a, b) != HashCombine(b, a)
// in general.
//
// Example:
//   HashCombine(0, 42)   => some value X
//   HashCombine(X, 100)  => a different value Y
uint32_t HashCombine(uint32_t seed, uint32_t hash)
{
    return seed ^ (hash + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

// Produces a 32-bit hash for a UTF-8 or WTF-8 string.
//
// The hash incorporates both the byte length of the string and each
// Unicode code point decoded from it.  This means two strings that
// differ only in trailing ASCII characters will still produce different
// hashes, and strings with the same code points in different byte
// representations (which should not happen in valid UTF-8/WTF-8) will
// also differ.
//
// The code points are decoded using DecodeWTF8Rune, which handles
// surrogate pairs and multi-byte sequences.  Processing stops early
// if a zero-width rune is encountered, indicating invalid or truncated
// input - in that case only the bytes processed so far contribute to
// the hash.
//
// Example:
//   HashCombineString(0, "hello")
//   => hash incorporating length 5 and code points h, e, l, l, o
//
// Example:
//   HashCombineString(0, "\xC3\xA9")  (UTF-8 for 'é', U+00E9)
//   => hash incorporating length 2 and code point 0xE9
uint32_t HashCombineString(uint32_t seed, std::string_view text)
{
    // Include the byte length so that strings of different lengths
    // collide less frequently even when their prefixes match.
    seed = HashCombine(seed, static_cast<uint32_t>(text.size()));

    size_t i = 0;
    size_t n = text.size();

    // Decode and hash one Unicode code point at a time.  Each code
    // point may occupy 1-4 bytes depending on its value.
    while (i < n) {
        auto [cp, width] =
            helpers::DecodeWTF8Rune(text.substr(i));

        // A zero-width return signals invalid or incomplete input;
        // stop processing to avoid reading past the end.
        if (width == 0) break;

        seed = HashCombine(seed, static_cast<uint32_t>(cp));

        i += static_cast<size_t>(width);
    }

    return seed;
}

} // namespace guchho::helpers