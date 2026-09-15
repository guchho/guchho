#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace guchho::unicode {

    // A range of code points in the BMP (16-bit) space, described by a low
    // bound, a high bound, and a stride between valid values.
    struct Range16 {
        uint16_t lo;
        uint16_t hi;
        uint16_t stride;
    };

    // A range of code points beyond the BMP (32-bit) space.
    struct Range32 {
        uint32_t lo;
        uint32_t hi;
        uint32_t stride;
    };

    // A packed set of ranges describing which code points belong to a Unicode
    // category (an "Is..." table), split between the 16-bit and 32-bit tables.
    struct RangeTable {
        uint32_t latin_offset;
        const Range16* r16;
        size_t r16_count;
        const Range32* r32;
        size_t r32_count;
    };

    // True when the code point cp falls in any of the given sorted ranges.
    template<typename Range>
    inline bool IsInRanges(const Range* ranges, size_t count, uint32_t cp) noexcept {
        if (count == 0) return false;

        auto it = std::upper_bound(ranges, ranges + count, cp,
            [](uint32_t v, const Range& r) { return v < r.lo; });

        if (it == ranges) return false;
        --it;
        return cp <= it->hi && ((cp - it->lo) % it->stride == 0);
    }

    // True when the code point belongs to the set described by the table,
    // checking the 16-bit table for low code points and the 32-bit table for
    // high ones.
    inline bool IsInRangeTable(const RangeTable& table, char32_t cp) noexcept {
        if (table.r16_count > 0 && cp <= table.r16[table.r16_count - 1].hi) {
            return IsInRanges(table.r16, table.r16_count, static_cast<uint32_t>(cp));
        }
        if (table.r32_count > 0 && cp >= table.r32[0].lo) {
            return IsInRanges(table.r32, table.r32_count, static_cast<uint32_t>(cp));
        }
        return false;
    }

    // Code point sets for identifiers, per spec version: ES5 and ESNext, split
    // into "start" (first character) and "continue" (subsequent characters).
    extern const RangeTable kIDStartES5;
    extern const RangeTable kIDContinueES5;
    extern const RangeTable kIDStartESNext;
    extern const RangeTable kIDContinueESNext;

    // Printable code points: the L, M, N, P, and S categories plus ASCII space
    // (Unicode 16.0.0).
    extern const RangeTable kPrint;

    // True when the whole text is a valid identifier.
    bool IsIdentifier(std::string_view text) noexcept;
    // True when the text is a valid identifier under both the ES5 and ESNext
    // rules.
    bool IsIdentifierES5AndESNext(std::string_view text) noexcept;

    // True when the code point may begin an identifier.
    inline bool IsIdentifierStart(char32_t cp) noexcept {
        if (cp < 0x80) {
            return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_' || cp == '$';
        }
        return IsInRangeTable(kIDStartES5, cp) || IsInRangeTable(kIDStartESNext, cp);
    }

    // True when the code point may appear after the first character.
    inline bool IsIdentifierContinue(char32_t cp) noexcept {
        if (cp < 0x80) {
            return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
                (cp >= '0' && cp <= '9') || cp == '_' || cp == '$';
        }
        if (cp == 0x200C || cp == 0x200D) return true;
        return IsInRangeTable(kIDContinueES5, cp) || IsInRangeTable(kIDContinueESNext, cp);
    }

    // True when the code point may begin an identifier under both ES5 and
    // ESNext rules simultaneously.
    inline bool IsIdentifierStartES5AndESNext(char32_t cp) noexcept {
        if (cp < 0x80) {
            return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_' || cp == '$';
        }
        if (cp >= 0x10000) return false;
        return IsInRangeTable(kIDStartES5, cp) && IsInRangeTable(kIDStartESNext, cp);
    }

    // True when the code point may appear after the first character under both
    // ES5 and ESNext rules simultaneously.
    inline bool IsIdentifierContinueES5AndESNext(char32_t cp) noexcept {
        if (cp < 0x80) {
            return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
                (cp >= '0' && cp <= '9') || cp == '_' || cp == '$';
        }
        if (cp == 0x200C || cp == 0x200D) return true;
        if (cp >= 0x10000) return false;
        return IsInRangeTable(kIDContinueES5, cp) && IsInRangeTable(kIDContinueESNext, cp);
    }

}
