#include "guchho/helpers.hpp"

namespace guchho::helpers {

    // Guchho's plain-text utilities. Everything here sees input as raw bytes and
    // sticks to ASCII first, so results stay byte-stable on any system. Guchho
    // leans on these to standardize names, compare path lists, and format the
    // text it prints when reporting problems.

    // ToLowerASCII - lowercase every capital ASCII letter in the text.
    // Guchho normalizes scheme names, file extensions, and comparison keys so
    // later lookups ignore the case the caller happened to type.
    // The output buffer is sized up front so the loop never reallocates.
    // Only 'A'–'Z' is rewritten; everything else (digits, punctuation, bytes
    // >= 0x80 in UTF-8) passes through untouched. No locale is consulted.
    // Example: ToLowerASCII("HeLLo-GUCHHO_123") -> "hello-guchho_123"
    //          ToLowerASCII("https") -> "https" (nothing to lower)
    std::string ToLowerASCII(std::string_view text) {
        std::string result;
        result.reserve(text.size());
        for (char c : text) {
            result.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
        }
        return result;
    }

    // StringArraysEqual - true when two string lists hold the same elements in
    // the same order. Guchho checks whether entry-point or chunk-name lists from
    // a previous step still match the current ones.
    // Sizes are compared first for a cheap early exit; then each slot is
    // compared verbatim and case-sensitively. Order matters.
    // Example: StringArraysEqual({"a","b"}, {"a","b"}) -> true
    //          StringArraysEqual({"a","b"}, {"a","c"}) -> false
    bool StringArraysEqual(const std::vector<std::string>& a, const std::vector<std::string>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) return false;
        }
        return true;
    }

    // StringArrayArraysEqual - true when two nested lists are equal at both
    // levels, in order. Guchho uses this on two-dimensional shapes such as
    // grouped import patterns or per-entry dependency lists.
    // Outer sizes are checked first, then each outer slot is handed to
    // StringArraysEqual. Order is significant at both levels; no hashing and no
    // extra allocation.
    // Example: StringArrayArraysEqual({{"a"},{"b","c"}}, {{"a"},{"b","c"}}) -> true
    //          StringArrayArraysEqual({{"x","y"}}, {{"x"},{"y"}}) -> false
    bool StringArrayArraysEqual(const std::vector<std::vector<std::string>>& a, const std::vector<std::vector<std::string>>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (!StringArraysEqual(a[i], b[i])) return false;
        }
        return true;
    }

    // StringArrayToQuotedCommaSeparatedString - render a list as one readable
    // string with each item in quotes, joined by ", ". Guchho fills in the
    // quoted choices shown in warnings and errors.
    // A ", " separator is added only before every item after the first; each
    // item gets surrounding quote characters. Nothing is escaped inside items.
    // Example: StringArrayToQuotedCommaSeparatedString({"foo","bar"}) -> "\"foo\", \"bar\""
    //          StringArrayToQuotedCommaSeparatedString({"single"}) -> "\"single\""
    std::string StringArrayToQuotedCommaSeparatedString(const std::vector<std::string>& a)
    {
        std::string result;
        for (size_t i = 0; i < a.size(); ++i) {
            if (i > 0) result.append(", ");
            result.push_back('"');
            result.append(a[i]);
            result.push_back('"');
        }
        return result;
    }

    // EqualFoldASCII - true when two views match after ASCII case is folded.
    // Guchho compares scheme names, header names, and extensions this way, so
    // "README.md" and "readme.MD" resolve to the same thing, without any
    // locale- or Unicode-aware folding.
    // Lengths are compared first; then each byte is folded on the fly ('A'–'Z'
    // shifted by the 'a' - 'A' offset) before comparing. No extra storage.
    // Example: EqualFoldASCII("Hello", "hELLo") -> true
    //          EqualFoldASCII("abc", "abd") -> false
    bool EqualFoldASCII(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++) {
            char ca = a[i];
            char cb = b[i];
            if (ca >= 'A' && ca <= 'Z') {
                ca = static_cast<char>(ca - 'A' + 'a');
            }
            if (cb >= 'A' && cb <= 'Z') {
                cb = static_cast<char>(cb - 'A' + 'a');
            }
            if (ca != cb) {
                return false;
            }
        }
        return true;
    }
}
