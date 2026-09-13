// XXH64 - 64-bit non-cryptographic hash function.
//
// Implements at http://cyan4973.github.io/xxHash/.
// xxHash is a fast hashing algorithm designed for high throughput on both
// large and small inputs. It is commonly used for checksums, hash tables,
// and bloom filters where speed matters more than collision resistance.
//
// The algorithm works by processing input in 32-byte blocks using four
// 64-bit accumulators, each mixed with prime constants. A final
// avalanche pass combines the accumulators into a single 64-bit hash.
//
// The state can be fed data incrementally via Write(); call Sum64()
// to obtain the hash at any point without resetting the state.

#include "guchho/helpers.hpp"

namespace guchho::helpers {

    namespace {

        // Prime constants used throughout the mixing functions.
        // These were chosen for their favorable distribution properties
        // in bitwise and arithmetic operations.
        constexpr uint64_t kPrime1 = 11400714785074694791ULL;
        constexpr uint64_t kPrime2 = 14029467366897019727ULL;
        constexpr uint64_t kPrime3 = 1609587929392839161ULL;
        constexpr uint64_t kPrime4 = 9650029242287828579ULL;
        constexpr uint64_t kPrime5 = 2870177450012600261ULL;

        // Bitwise left rotation. Shifts bits left by r positions, with
        // bits that overflow the high end wrapping around to the low end.
        inline uint64_t RotL(uint64_t x, unsigned r)
        {
            return (x << r) | (x >> (64 - r));
        }

        // Single round of accumulation: multiply the input by kPrime2,
        // add to the accumulator, rotate left by 31, then multiply by kPrime1.
        // This mixes the input bits thoroughly into the accumulator state.
        inline uint64_t Round(uint64_t acc, uint64_t input)
        {
            acc += input * kPrime2;
            acc = RotL(acc, 31);
            acc *= kPrime1;
            return acc;
        }

        // Merges a single 8-byte value into the accumulator by hashing it
        // from scratch, XOR-ing the result into the accumulator, and
        // applying a final multiply-add mix.
        inline uint64_t MergeRound(uint64_t acc, uint64_t val)
        {
            val = Round(0, val);
            acc ^= val;
            acc = acc * kPrime1 + kPrime4;
            return acc;
        }

        // Reads 8 bytes from a little-endian byte buffer into a uint64.
        // Each byte is placed at its correct bit position without relying
        // on the platform's endianness or alignment.
        inline uint64_t Load64(const uint8_t* p)
        {
            return static_cast<uint64_t>(p[0]) |
                   (static_cast<uint64_t>(p[1]) << 8) |
                   (static_cast<uint64_t>(p[2]) << 16) |
                   (static_cast<uint64_t>(p[3]) << 24) |
                   (static_cast<uint64_t>(p[4]) << 32) |
                   (static_cast<uint64_t>(p[5]) << 40) |
                   (static_cast<uint64_t>(p[6]) << 48) |
                   (static_cast<uint64_t>(p[7]) << 56);
        }

        // Reads 4 bytes from a little-endian byte buffer into a uint32.
        inline uint32_t Load32(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0]) |
                   (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) |
                   (static_cast<uint32_t>(p[3]) << 24);
        }

    } // namespace

    Xxh64::Xxh64()
    {
        Reset();
    }

    // Resets the hasher to its initial state, as if no data had been written.
    // The four accumulators are seeded with prime-derived constants that
    // ensure good avalanche behavior even for small inputs.
    void Xxh64::Reset()
    {
        v1_    = kPrime1 + kPrime2;
        v2_    = kPrime2;
        v3_    = 0;
        v4_    = 0 - kPrime1;
        total_ = 0;
        n_     = 0;
    }

    // Feeds data into the hasher. Write() can be called multiple times;
    // internal buffering ensures that the four 32-byte accumulators are
    // only updated when a full block is available.
    void Xxh64::Write(const void* data, size_t len)
    {
        const uint8_t* b = static_cast<const uint8_t*>(data);
        size_t         n = len;
        total_ += n;

        if (n_ + n < 32) {
            // Not enough new data to complete a 32-byte block; buffer it
            // for the next Write() call.
            for (size_t i = 0; i < n; ++i) {
                mem_[n_] = b[i];
                ++n_;
            }
            return;
        }

        if (n_ > 0) {
            // Drain the leftover bytes from the previous write into a full
            // 32-byte block and process it through the four accumulators.
            while (n_ < 32) {
                mem_[n_] = *b;
                ++n_;
                ++b;
                --n;
            }
            v1_ = Round(v1_, Load64(mem_.data()));
            v2_ = Round(v2_, Load64(mem_.data() + 8));
            v3_ = Round(v3_, Load64(mem_.data() + 16));
            v4_ = Round(v4_, Load64(mem_.data() + 24));
            n_  = 0;
        }

        while (n >= 32) {
            // Process complete 32-byte blocks directly from the input
            // buffer, updating each of the four accumulators in turn.
            v1_ = Round(v1_, Load64(b));
            v2_ = Round(v2_, Load64(b + 8));
            v3_ = Round(v3_, Load64(b + 16));
            v4_ = Round(v4_, Load64(b + 24));
            b += 32;
            n -= 32;
        }

        // Buffer any leftover bytes that didn't fill a complete block.
        for (size_t i = 0; i < n; ++i) {
            mem_[i] = b[i];
        }
        n_ = static_cast<uint8_t>(n);
    }

    // Produces the final 64-bit hash value. This can be called without
    // calling Reset() first - the state is not consumed, so Sum64() can
    // be invoked repeatedly to inspect intermediate results.
    //
    // The finalization works in three stages:
    //   1. Combine the four accumulators into a single value, then fold
    //      each accumulator's contribution through MergeRound().
    //   2. Process any remaining bytes in the internal buffer (less than
    //      32 bytes) in 8-byte, 4-byte, and 1-byte chunks.
    //   3. Apply an avalanche mix (three rounds of shift-XOR and multiply)
    //      to ensure every input bit affects every output bit.
    uint64_t Xxh64::Sum64() const
    {
        uint64_t h;

        if (total_ >= 32) {
            h = RotL(v1_, 1) + RotL(v2_, 7) + RotL(v3_, 12) + RotL(v4_, 18);
            h = MergeRound(h, v1_);
            h = MergeRound(h, v2_);
            h = MergeRound(h, v3_);
            h = MergeRound(h, v4_);
        } else {
            h = v3_ + kPrime5;
        }

        h += total_;

        size_t i   = 0;
        size_t end = n_;
        for (; i + 8 <= end; i += 8) {
            uint64_t k1 = Round(0, Load64(mem_.data() + i));
            h ^= k1;
            h = RotL(h, 27) * kPrime1 + kPrime4;
        }
        if (i + 4 <= end) {
            h ^= static_cast<uint64_t>(Load32(mem_.data() + i)) * kPrime1;
            h = RotL(h, 23) * kPrime2 + kPrime3;
            i += 4;
        }
        for (; i < end; ++i) {
            h ^= static_cast<uint64_t>(mem_[i]) * kPrime5;
            h = RotL(h, 11) * kPrime1;
        }

        h ^= h >> 33;
        h *= kPrime2;
        h ^= h >> 29;
        h *= kPrime3;
        h ^= h >> 32;

        return h;
    }

} // namespace guchho::helpers
