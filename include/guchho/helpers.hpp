#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>

#include <utility>
#include <tuple>
#include <string_view>
#include <string>
#include <span>
#include <optional>
#include <unordered_map>
#include <vector>


namespace guchho::helpers {

    // A simple data structure for storing a fixed number of bits.
    class BitSet {
        public:
            // Creates a bit set that can hold "bit_count" bits, all initialised false.
            explicit BitSet(uint32_t bit_count = 0)
                : bits_((static_cast<size_t>(bit_count) + 63) >> 6, 0) {}

            // True when the bit at index i is set.
            bool HasBit(uint32_t i) const {
                return (bits_[i >> 6] & (uint64_t{1} << (i & 63))) != 0;
            }

            // Sets or clears the bit at index i.
            void SetBit(uint32_t i, bool value) {
                uint64_t bit = uint64_t{1} << (i & 63);
                if (value) {
                    bits_[i >> 6] |= bit;
                } else {
                    bits_[i >> 6] &= ~bit;
                }
            }

            // ORs every bit of other into this set.
            void Union(const BitSet& other) {
                for (size_t i = 0; i < bits_.size(); i++) {
                    bits_[i] |= other.bits_[i];
                }
            }

            // True when no bit is set.
            bool IsEmpty() const {
                for (uint64_t x : bits_) {
                    if (x != 0) {
                        return false;
                    }
                }
                return true;
            }

            // Serializes the packed bits into a string, least-significant byte first.
            std::string ToString() const {
                std::string result;
                result.reserve(bits_.size() * 8);
                for (uint64_t x : bits_) {
                    for (int i = 0; i < 8; i++) {
                        result.push_back(static_cast<char>((x >> (i * 8)) & 0xFF));
                    }
                }
                return result;
            }

            // True when both sets hold the same bits.
            bool operator==(const BitSet& other) const {
                if (bits_.size() != other.bits_.size()) return false;
                for (size_t i = 0; i < bits_.size(); i++) {
                    if (bits_[i] != other.bits_[i]) return false;
                }
                return true;
            }

        private:
            std::vector<uint64_t> bits_;
    };

    
    // F64 wraps a double so every floating-point operation flows through one
    // interface and each result is stored back in another F64. Wrapping each
    // arithmetic step this way keeps behavior stable and predictable across
    // operations.
    class F64 {
        public:
            // Default constructor initialises the value to 0.0.
            F64() : value_(0.0) {}

            // Builds an F64 from a double, storing it as a plain double.
            explicit F64(double a) : value_(static_cast<double>(a)) {}

            // Returns the stored double value.
            double Value() const { return value_; }

            // Allows explicit conversion of an F64 to double when needed.
            explicit operator double() const { return value_; }

            // True when the value is NaN (Not a Number).
            bool IsNaN() const { return std::isnan(value_); }

            // Returns a new F64 with the sign flipped.
            F64 Neg() const { return F64(-value_); }

            // Returns the absolute value.
            F64 Abs() const { return F64(std::abs(value_)); }

            // Returns the sine of the value.
            F64 Sin() const { return F64(std::sin(value_)); }

            // Returns the cosine of the value.
            F64 Cos() const { return F64(std::cos(value_)); }

            // Returns the base-2 logarithm of the value.
            F64 Log2() const { return F64(std::log2(value_)); }

            // Rounds the value to the nearest integer.
            F64 Round() const { return F64(std::round(value_)); }

            // Returns the largest integer value not above the value.
            F64 Floor() const { return F64(std::floor(value_)); }

            // Returns the smallest integer value not below the value.
            F64 Ceil() const { return F64(std::ceil(value_)); }

            // Multiplies the value by itself to yield its square.
            F64 Squared() const { return Mul(*this); }

            // Multiplies the value by itself twice to yield its cube.
            F64 Cubed() const { return Mul(*this).Mul(*this); }

            // Returns the square root of the value.
            F64 Sqrt() const { return F64(std::sqrt(value_)); }

            // Returns the cube root of the value.
            F64 Cbrt() const { return F64(std::cbrt(value_)); }

            // Adds two F64 values and returns a new F64.
            F64 Add(const F64& b) const {
                return F64(value_ + b.value_);
            }

            // Subtracts two F64 values and returns a new F64.
            F64 Sub(const F64& b) const {
                return F64(value_ - b.value_);
            }

            // Multiplies two F64 values and returns a new F64.
            F64 Mul(const F64& b) const {
                return F64(value_ * b.value_);
            }

            // Divides two F64 values and returns a new F64.
            F64 Div(const F64& b) const {
                return F64(value_ / b.value_);
            }

            // Raises this value to the power of another F64 value.
            F64 Pow(const F64& b) const {
                return F64(std::pow(value_, b.value_));
            }

            // Computes the arc tangent of value/b.
            F64 Atan2(const F64& b) const {
                return F64(std::atan2(value_, b.value_));
            }

            // Adds a double constant directly to the F64.
            F64 AddConst(double b) const {
                return F64(value_ + b);
            }

            // Subtracts a double constant directly from the F64.
            F64 SubConst(double b) const {
                return F64(value_ - b);
            }

            // Multiplies the F64 by a double constant.
            F64 MulConst(double b) const {
                return F64(value_ * b);
            }

            // Divides the F64 by a double constant.
            F64 DivConst(double b) const {
                return F64(value_ / b);
            }

            // Raises the F64 value to the power of a double constant.
            F64 PowConst(double b) const {
                return F64(std::pow(value_, b));
            }

            // Keeps this value's magnitude but takes the sign from the other value.
            F64 WithSignFrom(const F64& b) const {
                return F64(std::copysign(value_, b.value_));
            }

        private:
            // The underlying floating-point value.
            double value_;
    };


    // Returns the smaller of two F64 values.
    inline F64 Min2(const F64& a, const F64& b) {
        return F64(std::min(a.Value(), b.Value()));
    }

    // Returns the larger of two F64 values.
    inline F64 Max2(const F64& a, const F64& b) {
        return F64(std::max(a.Value(), b.Value()));
    }

    // Returns the smallest of three F64 values.
    inline F64 Min3(
        const F64& a,
        const F64& b,
        const F64& c)
    {
        return Min2(Min2(a, b), c);
    }

    // Returns the largest of three F64 values.
    inline F64 Max3(
        const F64& a,
        const F64& b,
        const F64& c)
    {
        return Max2(Max2(a, b), c);
    }

    // Linearly interpolates between two values: t = 0 yields a, t = 1 yields b.
    // Computed as a + (b - a) * t through F64 methods.
    inline F64 Lerp(
        const F64& a,
        const F64& b,
        const F64& t)
    {
        return b.Sub(a).Mul(t).Add(a);
    }


    //---------------------------------------
    // -------------- timer.cpp -------------
    //---------------------------------------
    // Simple timing helper.
    class Timer {
    public:
        explicit Timer(std::string name);

        void Log(std::string_view message) const;

        // Nested timers can be created with Fork and combined with Join.
        // The result is that a joined timer starts at the earliest start
        // time among all of its parts.
        Timer Fork() const;
        void Join(const Timer& other);

        void Begin(const std::string& name);
        void End(const std::string& name);

    private:
        std::string name_;
        std::chrono::steady_clock::time_point start_;
        std::chrono::steady_clock::time_point last_time_;
    };


    //---------------------------------------
    // ------------ typo.cpp ----------------
    //---------------------------------------
    // Detects a probable typo among a known set of valid words.
    class TypoDetector {
        public:
            // Builds the typo lookup table from a list of valid words.
            explicit TypoDetector(const std::vector<std::string>& valid);

            // If the given typo closely matches a valid word, returns that correct
            // word; otherwise returns std::nullopt.
            std::optional<std::string> MaybeCorrectTypo(
                std::string_view typo) const;

        private:
            // Maps a one-character-lost typo back to its correct word.
            std::unordered_map<std::string, std::string> oneCharTypos_;
    };


    //---------------------------------------
    // ------------ joiner.cpp --------------
    //---------------------------------------
    class Joiner {
        public:
            void AddString(std::string_view data);
            void AddBytes(std::span<const char> data);

            uint8_t  LastByte() const { return lastByte_; }
            uint32_t Length()   const { return length_; }

            void EnsureNewlineAtEnd();

            std::string Done() const;

            bool Contains(std::string_view s, std::span<const char> b) const;

        private:
            struct JoinerString {
                std::string data;
                uint32_t    offset;
            };
            struct JoinerBytes {
                std::span<const char> data;
                uint32_t              offset;
            };

            std::vector<JoinerString> strings_;
            std::vector<JoinerBytes>  bytes_;
            uint32_t                  length_   = 0;
            uint8_t                   lastByte_ = 0;
    };



    //---------------------------------------
    // ------------ comment.cpp -------------
    //---------------------------------------   
    std::string EscapeClosingTag(std::string_view text, std::string_view slashTag);


    //---------------------------------------
    // --------------- path.cpp -------------
    //---------------------------------------   
    bool        IsInsideNodeModules(std::string_view path);
    bool        IsFileURL(std::string_view scheme, std::string_view host, std::string_view path);
    std::string FileURLFromFilePath(std::string_view filePath);
    std::string FilePathFromFileURL(std::string_view urlPath, std::string_view cwd);

    // Splits a "/"-separated path into its segments, dropping empty and "."
    // segments ("a//b/./c" -> {"a","b","c"}); ".." segments are kept as-is.
    // Shared by the relative-path resolver and the CSS asset path matcher.
    std::vector<std::string> SplitPathSegments(std::string_view path);

    // Computes the "/"-separated path from "from_file"'s directory to "to_file"
    // ("dist/admin/index.html" -> "dist/assets/admin.js" yields
    // "../assets/admin.js"). Returns "." when the target is the source
    // directory itself. Used to rewrite HTML resources relative to the output.
    std::string MakeRelativePath(std::string_view from_file, std::string_view to_file);

    // Ensures a relative resource path carries its leading "./" so browsers
    // treat it as a relative URL, unless it already has a path prefix ("../",
    // ".") or starts with "/" (server-root-relative). "app.js" -> "./app.js".
    std::string AddDotSlashPrefix(std::string_view rel_path);

    // True when a PublicPath is configured. Empty, "." and "./" count as "no
    // base", meaning the HTML keeps its current relative resource URLs.
    bool IsPublicPathConfigured(std::string_view public_path);

    // Joins "rel_path" (relative to the output directory) onto a configured
    // PublicPath ("/app/" or "https://cdn.example.com/"), stripping redundant
    // "./" and empty segments from the joined result like the linker does.
    // When no PublicPath is configured, "rel_path" is returned unchanged.
    std::string JoinPublicPath(std::string_view public_path, std::string_view rel_path);



    //---------------------------------------
    // ------------ strings.cpp -------------
    //---------------------------------------
    // Lowers every ASCII capital letter in the text.
    std::string ToLowerASCII(std::string_view text);

    // True when two string lists are identical, in order.
    bool StringArraysEqual(const std::vector<std::string>& a,
                        const std::vector<std::string>& b);

    // True when two lists of string lists are identical at both levels.
    bool StringArrayArraysEqual(
        const std::vector<std::vector<std::string>>& a,
        const std::vector<std::vector<std::string>>& b);

    // Render a string list as comma-separated, quoted text.
    std::string StringArrayToQuotedCommaSeparatedString(
        const std::vector<std::string>& a);
    
    // True when two strings match ignoring ASCII case.
    bool EqualFoldASCII(std::string_view a, std::string_view b);

    std::string QuoteSingle(std::string_view text, bool asciiOnly);
    std::string QuoteForJSON(std::string_view text, bool asciiOnly);
    std::string quoteString(std::string_view text);



    //---------------------------------------
    // ------------ utf8.cpp ----------------
    //---------------------------------------
    // Decodes one Unicode code point at the start of text.
    // Returns the decoded code point and how many bytes it consumed.
    std::pair<char32_t, int> DecodeWTF8Rune(std::string_view text);

    // Decodes the final Unicode code point of the string.
    // Returns the code point and its byte width.
    std::pair<char32_t, int> DecodeLastRuneInString(std::string_view text);

    // Decodes the first Unicode code point of the string.
    // Returns the code point and its byte width.
    std::pair<char32_t, int> DecodeRuneInString(std::string_view text);

    // Encodes one Unicode code point as WTF-8 into the buffer.
    // Returns the number of bytes written.
    int EncodeWTF8Rune(char* buffer, char32_t code_point);

    // True when the string holds any Unicode code point beyond the BMP.
    bool ContainsNonBMPCodePoint(std::string_view text);

    // True when the UTF-16 text holds any code point beyond the BMP.
    bool ContainsNonBMPCodePointUTF16(
        std::span<const char16_t> text);

    // Converts a UTF-8/WTF-8 string to a UTF-16 string.
    std::u16string StringToUTF16(std::string_view text);

    // Converts UTF-16 text to a UTF-8/WTF-8 string.
    std::string UTF16ToString(
        std::span<const char16_t> text);

    // Converts UTF-16 text and reports validation results.
    // Returns the converted text, the offending UTF-16 value on failure, and
    // whether the text was valid.
    std::tuple<std::string, char16_t, bool> UTF16ToStringWithValidation(
        std::span<const char16_t> text);

    // True when the UTF-16 text and the UTF-8/WTF-8 string represent the same text.
    bool UTF16EqualsString(
        std::span<const char16_t> text,
        std::string_view str);

    // True when two UTF-16 text sequences are identical.
    bool UTF16EqualsUTF16(
        std::span<const char16_t> a,
        std::span<const char16_t> b);


    //---------------------------------------
    // ------------- base64.cpp -------------
    //---------------------------------------
    // Encodes using the standard base64 alphabet (A-Z, a-z, 0-9, +, /).
    std::string Base64StdEncode(std::string_view src);

    // Encodes using the URL-safe base64 alphabet (A-Z, a-z, 0-9, -, _).
    std::string Base64URLEncode(std::string_view src);

    // Encodes using the RFC 4648 base32 alphabet (A-Z, 2-7) with padding.
    std::string Base32StdEncode(std::string_view src);


    //---------------------------------------
    // ------------ xxhash.cpp --------------
    //---------------------------------------
    // The 64-bit variant of xxHash.
    class Xxh64 {
    public:
        Xxh64();

        void     Reset();
        void     Write(const void* data, size_t len);
        uint64_t Sum64() const;

    private:
        uint64_t v1_ = 0;
        uint64_t v2_ = 0;
        uint64_t v3_ = 0;
        uint64_t v4_ = 0;
        size_t   total_ = 0;
        uint8_t  n_ = 0;
        std::array<uint8_t, 32> mem_{};
    };


    //---------------------------------------
    // --------------- url.cpp --------------
    //---------------------------------------
    // Minimal URL representation covering the needs of the bundler
    // (scheme/host/path/query/fragment). Opaque URLs and user info are not modelled.
    struct URL {
        // Scheme is always stored in lowercase.
        std::string scheme;
        std::string host;
        // Path and query stay percent-encoded as given ("as-is" form).
        std::string path;
        std::string query;     // Without the leading '?'
        std::string fragment;  // Without the leading '#'

        // Reassembles the URL into a URL string (RFC 3986 section 5.3).
        std::string String() const;

        // Resolves a URI reference relative to this base URL
        // (RFC 3986 section 5.2.2).
        URL ResolveReference(const URL& ref) const;
    };

    // Parses a raw URL string. Returns std::nullopt on malformed input.
    std::optional<URL> ParseURL(std::string_view raw_url);

    // How a resource reference (an HTML "src"/"href", a CSS "url()", ...) should
    // be treated by the URL resolver. "kRelative" includes query strings
    // ("app.js?v=2"); "kAbsolute" covers every "scheme:..." reference, matched
    // by scanning up to the first '/' or '?' exactly like the alias resolver.
    enum class URLKind {
        kEmpty,            // ""
        kFragment,         // "#anchor"
        kRelative,         // "app.js", "./a/b.css", "../x.css", "a.css?v=2"
        kRootRelative,     // "/assets/app.js"
        kProtocolRelative, // "//cdn.example.com/app.js"
        kAbsolute,         // any "scheme:..." ("https:", "data:", "mailto:", ...)
    };

    // Classifies a raw URL string for the bundler: which URLs must be bundled
    // and rewritten (relative/root-relative) and which must be left untouched
    // (absolute, protocol-relative, fragment-only, empty).
    URLKind ClassifyURL(std::string_view raw_url);

    // True when the URL must never be bundled or rewritten: absolute with a
    // scheme ("https://...", "data:...", ...) or protocol-relative ("//host").
    bool IsExternalURL(std::string_view raw_url);

    // True when the URL is a "data:" URL (matched case-sensitively like the
    // existing inlining checks).
    bool IsDataURL(std::string_view raw_url);

    // Percent-decodes each 3-byte substring of the form "%AB". Returns
    // std::nullopt on invalid escapes. QueryUnescape also converts '+' to ' '.
    std::optional<std::string> PathUnescape(std::string_view s);
    std::optional<std::string> QueryUnescape(std::string_view s);



    //---------------------------------------
    // ---------- dataurl.cpp ---------------
    //---------------------------------------
    // Looks up the MIME type for a file extension; returns an empty view if none.
    std::string_view MimeTypeByExtension(std::string_view ext);

    // Tries to encode text as a percent-escaped data URL. Returns {empty, false}
    // when the text is not valid UTF-8.
    std::pair<std::string, bool> EncodeStringAsPercentEscapedDataURL(
        std::string_view mime_type,
        std::string_view text);

    // Builds both percent-escaped and base64 data URLs and picks the shorter one.
    std::string EncodeStringAsShortestDataURL(
        std::string_view mime_type,
        std::string_view text);



    //---------------------------------------
    // ---------- glob.cpp ------------------
    //---------------------------------------
    // Describes the kind of wildcard present in a glob pattern.
    enum class GlobWildcard : uint8_t {
        // No wildcard.
        kNone,

        // "*" - matches any character except '/'.
        kAllExceptSlash,

        // "**" - matches any characters, including '/'.
        kAllIncludingSlash,
    };

    // Stores one segment of a parsed glob pattern.
    struct GlobPart {
        // The plain text preceding the wildcard.
        std::string prefix;

        // The kind of wildcard in this segment.
        GlobWildcard wildcard{GlobWildcard::kNone};
    };

    // Splits a glob pattern string into separate GlobPart segments.
    std::vector<GlobPart> ParseGlobPattern(std::string_view text);

    // Recombines parsed GlobParts back into a single glob pattern string.
    std::string GlobPatternToString(const std::vector<GlobPart>& pattern);



    //---------------------------------------
    // ---------- glob.cpp ------------------
    //---------------------------------------
    // Combines two hash values into a new hash.
    uint32_t HashCombine(uint32_t seed, uint32_t hash);

    // Hashes a string and combines the result into the seed.
    uint32_t HashCombineString(uint32_t seed, std::string_view text);


    //---------------------------------------
    // ------------- sha.cpp ----------------
    //---------------------------------------
    // SHA-2 cryptographic digests (FIPS 180-4). Each returns the raw digest
    // bytes: 32 for SHA-256, 48 for SHA-384, 64 for SHA-512. Feed the returned
    // bytes through Base64StdEncode() for e.g. `integrity` attribute values.
    std::vector<uint8_t> Sha256(std::string_view data);
    std::vector<uint8_t> Sha384(std::string_view data);
    std::vector<uint8_t> Sha512(std::string_view data);

}