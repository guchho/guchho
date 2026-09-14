#include "guchho/helpers.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace guchho::helpers {
namespace {

// ---------------------------------------------------------------------------
// SHA-2 primitives — compact, self-contained FIPS 180-4 implementation.
// The entire file is a single generic driver parameterized on word type
// (uint32_t for SHA-256, uint64_t for SHA-384/512), round count, and
// digest length. Guchho uses these three digest lengths for content-
// addressed caching and asset fingerprinting.
// ---------------------------------------------------------------------------

// Rotr — circular right shift by N bits.
// The fundamental bit-rotation used by every σ and Σ mixing function.
// Example: Rotr<uint32_t, 2>(0x80000001) -> 0x60000000
template <typename T, int N>
inline T Rotr(T x) {
    return (x >> N) | (x << (static_cast<int>(8 * sizeof(T)) - N));
}

// Shr — logical right shift by n bits (zero-filled).
// Unlike Rotr, bits that shift out are not re-inserted at the top.
// Example: Shr<uint32_t>(0xFF, 4) -> 0x0F
template <typename T>
inline T Shr(T x, int n) {
    return x >> n;
}

// Csel — conditional select: (x & y) ^ (~x & z).
// When bit i of x is 1, bit i of y is chosen; when 0, z is chosen.
// Equivalent to `x ? y : z` at the per-bit level.
// Example: Csel(0xF0, 0xAA, 0x55) -> 0xAA (high bits from y, low from z)
template <typename T>
inline T Csel(T x, T y, T z) {
    return (x & y) ^ (~x & z);
}

// Maj — majority function: returns the majority bit among x, y, z.
// Each output bit is 1 when at least two of the three inputs are 1.
// Example: Maj(0xF0, 0xCC, 0xAA) -> 0x88 (only bits set in ≥2 inputs)
template <typename T>
inline T Maj(T x, T y, T z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

// BigSigma0 — Σ0 in FIPS 180-4 notation.
// Three rotations combined with XOR. The rotation constants differ
// between SHA-256 and SHA-384/512 (selected at compile time by the
// size of T). Used in the compression function to mix the working
// variables.
// Example (SHA-256): BigSigma0(0x12345678) -> Rotr(2) ^ Rotr(13) ^ Rotr(22)
template <typename T>
inline T BigSigma0(T x) {
    if constexpr (sizeof(T) == 4) {
        return Rotr<T, 2>(x) ^ Rotr<T, 13>(x) ^ Rotr<T, 22>(x);
    } else {
        return Rotr<T, 28>(x) ^ Rotr<T, 34>(x) ^ Rotr<T, 39>(x);
    }
}

// BigSigma1 — Σ1 in FIPS 180-4 notation. Same structure as BigSigma0
// but with different rotation constants. Paired with BigSigma0 to
// produce the two halves of the compression-function update.
template <typename T>
inline T BigSigma1(T x) {
    if constexpr (sizeof(T) == 4) {
        return Rotr<T, 6>(x) ^ Rotr<T, 11>(x) ^ Rotr<T, 25>(x);
    } else {
        return Rotr<T, 14>(x) ^ Rotr<T, 18>(x) ^ Rotr<T, 41>(x);
    }
}

// SmallSigma0 — σ0 in FIPS 180-4 notation. Two rotations plus a
// logical right shift (not rotate). Used in the message-schedule
// expansion to compute new message-schedule words from older ones.
template <typename T>
inline T SmallSigma0(T x) {
    if constexpr (sizeof(T) == 4) {
        return Rotr<T, 7>(x) ^ Rotr<T, 18>(x) ^ Shr(x, 3);
    } else {
        return Rotr<T, 1>(x) ^ Rotr<T, 8>(x) ^ Shr(x, 7);
    }
}

// SmallSigma1 — σ1 in FIPS 180-4 notation. Same pattern as
// SmallSigma0 with different constants. Used alongside σ0 in the
// message-schedule expansion.
template <typename T>
inline T SmallSigma1(T x) {
    if constexpr (sizeof(T) == 4) {
        return Rotr<T, 17>(x) ^ Rotr<T, 19>(x) ^ Shr(x, 10);
    } else {
        return Rotr<T, 19>(x) ^ Rotr<T, 61>(x) ^ Shr(x, 6);
    }
}

// ---------------------------------------------------------------------------
// Round constants — FIPS 180-4 section 4.2.
// Each round i uses k[i] = floor(2^30 * abs(cuberoot(i))), where c is
// the word size (32 or 64 bits). These ensure the compression function
// does not have a fixed point at zero.
// ---------------------------------------------------------------------------

template <typename T, size_t NumRounds>
struct RoundConstants;

template <>
struct RoundConstants<uint32_t, 64> {
    static inline constexpr std::array<uint32_t, 64> k = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
};

template <>
struct RoundConstants<uint64_t, 80> {
    static inline constexpr std::array<uint64_t, 80> k = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
        0xb5c0fbcfec4d3b2fULL,  0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL,  0x59f111f1b605d019ULL,
        0x923f82a4af194f9bULL,  0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL,  0x12835b0145706fbeULL,
        0x243185be4ee4b28cULL,  0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL,  0x80deb1fe3b1696b1ULL,
        0x9bdc06a725c71235ULL,  0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL,  0xefbe4786384f25e3ULL,
        0x0fc19dc68b8cd5b5ULL,  0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL,  0x4a7484aa6ea6e483ULL,
        0x5cb0a9dcbd41fbd4ULL,  0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL,  0xa831c66d2db43210ULL,
        0xb00327c898fb213fULL,  0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL,  0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL,  0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL,  0x2e1b21385c26c926ULL,
        0x4d2c6dfc5ac42aedULL,  0x53380d139d95b3dfULL,
        0x650a73548baf63deULL,  0x766a0abb3c77b2a8ULL,
        0x81c2c92e47edaee6ULL,  0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL,  0xa81a664bbc423001ULL,
        0xc24b8b70d0f89791ULL,  0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL,  0xd69906245565a910ULL,
        0xf40e35855771202aULL,  0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL,  0x1e376c085141ab53ULL,
        0x2748774cdf8eeb99ULL,  0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL,  0x4ed8aa4ae3418acbULL,
        0x5b9cca4f7763e373ULL,  0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL,  0x78a5636f43172f60ULL,
        0x84c87814a1f0ab72ULL,  0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL,  0xa4506cebde82bde9ULL,
        0xbef9a3f7b2c67915ULL,  0xc67178f2e372532bULL,
        0xca273eceea26619cULL,  0xd186b8c721c0c207ULL,
        0xeada7dd6cde0eb1eULL,  0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL,  0x0a637dc5a2c898a6ULL,
        0x113f9804bef90daeULL,  0x1b710b35131c471bULL,
        0x28db77f523047d84ULL,  0x32caab7b40c72493ULL,
        0x3c9ebe0a15c9bebcULL,  0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL,  0x597f299cfc657e2aULL,
        0x5fcb6fab3ad6faecULL,  0x6c44198c4a475817ULL,
    };
};

// ---------------------------------------------------------------------------
// Initial hash values — FIPS 180-4 section 5.3.3.
// The eight working variables are seeded from fractional parts of the
// square roots (SHA-256) or cube roots (SHA-384/512) of the first
// eight primes.
// ---------------------------------------------------------------------------

template <typename T, size_t DigestBytes>
struct InitialHash;

template <>
struct InitialHash<uint32_t, 32> {
    static inline constexpr std::array<uint32_t, 8> k = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
};

template <>
struct InitialHash<uint64_t, 48> {
    static inline constexpr std::array<uint64_t, 8> k = {
        0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL,
        0x9159015a3070dd17ULL,  0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL,  0x8eb44a8768581511ULL,
        0xdb0c2e0d64f98fa7ULL,  0x47b5481dbefa4fa4ULL,
    };
};

template <>
struct InitialHash<uint64_t, 64> {
    static inline constexpr std::array<uint64_t, 8> k = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL,  0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL,  0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL,  0x5be0cd19137e2179ULL,
    };
};

// ---------------------------------------------------------------------------
// Sha2 — generic SHA-2 driver.
// Parameterized on word type (uint32_t or uint64_t), round count (64
// for SHA-256, 80 for SHA-384/512), and digest length in bytes (32,
// 48, or 64). The public wrappers Sha256, Sha384, Sha512 instantiate
// this template with the appropriate parameters.
//
// Returns the raw digest as a byte vector. No hex encoding is applied
// — callers format the output as needed.
//
// Input → Output examples:
//   Sha2<uint32_t, 64, 32>("")  -> { 0xe3, 0xb0, 0xc4, 0x42, ... } (SHA-256 of "")
//   Sha2<uint64_t, 80, 64>("")  -> { 0xcf, 0x83, 0xe1, 0x35, ... } (SHA-512 of "")
//
// Steps performed for each 512-bit (SHA-256) or 1024-bit (SHA-512)
// block:
//   1. Load the 16 input words in big-endian order.
//   2. Expand into the full message schedule (NumRounds words) using
//      the σ0/σ1 mixing functions.
//   3. Run the compression function: 64 or 80 rounds of the standard
//      Σ0/Σ1/Maj/Csel/BigSigma update.
//   4. Add the compressed state back into the running hash.
//
// FIPS 180-4 padding is applied before processing: a 0x80 byte, then
// enough zero bytes to leave room for a big-endian bit-length field
// at the end of the final block.
template <typename T, size_t NumRounds, size_t DigestBytes>
std::vector<uint8_t> Sha2(std::string_view data) {
    static constexpr size_t kWordBytes = sizeof(T);
    static constexpr size_t kBlockWords = 16;
    static constexpr size_t kBlockBytes = kBlockWords * kWordBytes;
    static constexpr size_t kLengthBytes = 2 * kWordBytes;

    std::array<T, 8> h = InitialHash<T, DigestBytes>::k;

    // FIPS 180-4 padding: a single 0x80 byte, then zeros until the block
    // size leaves room for a big-endian encoding of the message bit length.
    const uint64_t bit_length = static_cast<uint64_t>(data.size()) * 8;
    const size_t pad_zeros =
        (kBlockBytes - ((data.size() + kLengthBytes + 1) % kBlockBytes)) %
        kBlockBytes;
    std::vector<uint8_t> buf(data.begin(), data.end());
    buf.push_back(0x80);
    buf.insert(buf.end(), pad_zeros, 0x00);
    // Big-endian message bit-length: a 64-bit field for SHA-256 and a 128-bit
    // field (high word zero) for SHA-384/512.
    const size_t high_length_bytes = kLengthBytes - kWordBytes;
    buf.insert(buf.end(), high_length_bytes, 0x00);
    for (size_t i = kWordBytes; i > 0; i--) {
        buf.push_back(
            static_cast<uint8_t>(bit_length >> ((i - 1) * 8)));
    }

    for (size_t offset = 0; offset < buf.size(); offset += kBlockBytes) {
        std::array<T, NumRounds> w{};
        for (size_t i = 0; i < kBlockWords; i++) {
            T word = 0;
            for (size_t b = 0; b < kWordBytes; b++) {
                word = static_cast<T>((word << 8) |
                                      buf[offset + i * kWordBytes + b]);
            }
            w[i] = word;
        }
        for (size_t i = kBlockWords; i < NumRounds; i++) {
            w[i] = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) +
                   w[i - 16];
        }
        std::array<T, 8> a = h;
        for (size_t i = 0; i < NumRounds; i++) {
            const T t1 = a[7] + BigSigma1(a[4]) + Csel(a[4], a[5], a[6]) +
                         RoundConstants<T, NumRounds>::k[i] + w[i];
            const T t2 = BigSigma0(a[0]) + Maj(a[0], a[1], a[2]);
            a[7] = a[6];
            a[6] = a[5];
            a[5] = a[4];
            a[4] = a[3] + t1;
            a[3] = a[2];
            a[2] = a[1];
            a[1] = a[0];
            a[0] = t1 + t2;
        }
        for (size_t i = 0; i < 8; i++) {
            h[i] += a[i];
        }
    }

    std::vector<uint8_t> digest;
    digest.reserve(DigestBytes);
    for (size_t i = 0; i < DigestBytes / kWordBytes; i++) {
        for (size_t b = 0; b < kWordBytes; b++) {
            digest.push_back(
                static_cast<uint8_t>(h[i] >> ((kWordBytes - 1 - b) * 8)));
        }
    }
    return digest;
}

} // namespace

// Sha256 — 256-bit (32-byte) SHA-2 digest.
// Guchho uses this for content-addressed asset caching: the hex-encoded
// digest uniquely identifies file contents.
//
// Example: Sha256("") -> 0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
//          Sha256("abc") -> 0xba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
std::vector<uint8_t> Sha256(std::string_view data) {
    return Sha2<uint32_t, 64, 32>(data);
}

// Sha384 — 384-bit (48-byte) SHA-2 digest.
// The truncated variant of SHA-512, providing a higher security
// margin than SHA-256. Returns the first 48 bytes of the SHA-512
// digest.
std::vector<uint8_t> Sha384(std::string_view data) {
    return Sha2<uint64_t, 80, 48>(data);
}

// Sha512 — 512-bit (64-byte) SHA-2 digest.
// Full 512-bit output using 64-bit words and 80 rounds.
//
// Example: Sha512("") -> 0xcf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce
//          Sha512("abc") -> 0xddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f
std::vector<uint8_t> Sha512(std::string_view data) {
    return Sha2<uint64_t, 80, 64>(data);
}

} // namespace guchho::helpers
