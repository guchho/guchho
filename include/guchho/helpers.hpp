#pragma once

#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <functional>
#include <locale.h>
#include <memory>
#include <utility>
#include <tuple>
#include <string_view>
#include <string>
#include <span>
#include <optional>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#  include <xlocale.h>  // strtod_l
#endif


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

        // Writes the one-line report to the standard output. This is where a
        // build's summary belongs: nothing on that stream is a program, so a
        // line of prose in front of it is only noise.
        void Log(std::string_view message) const;

        // The same line on the error stream instead, for a run whose standard
        // output is a program's. A transform is a filter, so its output is the
        // transformed source and nothing else; a report written there is a
        // syntax error at the top of somebody's file.
        void LogToStderr(std::string_view message) const;

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
    // --------------- charconv -------------
    //---------------------------------------
    namespace detail {

        // Parses a NUL-terminated string as a double using an explicit C
        // locale, so the result never depends on the process locale. Used by
        // ParseDouble below; not part of the public API.
        inline double StrtodInCLocale(const char* text, char** end) {
#if defined(_WIN32)
            static _locale_t locale = _create_locale(LC_ALL, "C");
            return locale ? _strtod_l(text, end, locale) : std::strtod(text, end);
#else
            // glibc, musl, bionic and the BSDs all expose strtod_l through
            // <stdlib.h>/<locale.h>; libstdc++ and libc++ define _GNU_SOURCE
            // for us on those platforms.
            static locale_t locale = newlocale(LC_ALL_MASK, "C", (locale_t)0);
            return locale ? strtod_l(text, end, locale) : std::strtod(text, end);
#endif
        }

    }

    // Parses `text` as a double in the C locale and stores the result in
    // `*out`. The entire input must be a valid number: leading whitespace,
    // trailing characters, and values outside the representable range are
    // rejected. The semantics match std::from_chars with the default
    // chars_format ("inf"/"nan" are accepted, a leading "+" is accepted).
    //
    // This exists instead of std::from_chars(double) because Apple's libc++
    // only provides the floating-point std::from_chars overload on macOS 26+,
    // and using it would require that deployment target for every binary --
    // which is impossible for x86_64, since macOS 26 never ships for Intel.
    // strtod_l is correctly rounded on every platform we support and has
    // been available since macOS 10.x, glibc 2.3 and Windows Vista.
    //
    // Input:  "3.14"     => true, *out = 3.14
    // Input:  "-50"      => true, *out = -50.0
    // Input:  "1e3"      => true, *out = 1000.0
    // Input:  "1.5px"    => false (trailing characters)
    // Input:  " 1.5"     => false (leading whitespace)
    // Input:  ""         => false
    inline bool ParseDouble(std::string_view text, double* out) {
        if (out == nullptr || text.empty()) {
            return false;
        }
        // std::from_chars rejects leading whitespace; strtod skips it.
        if (std::isspace(static_cast<unsigned char>(text.front())) != 0) {
            return false;
        }
        // std::from_chars has no hex-float support, so "0x10" fails there
        // (only "0" is consumed); strtod would accept it as 16.0.
        {
            size_t i = (text[0] == '+' || text[0] == '-') ? 1 : 0;
            if (text.size() >= i + 2 && text[i] == '0' &&
                (text[i + 1] == 'x' || text[i + 1] == 'X')) {
                return false;
            }
        }

        // strtod needs a NUL-terminated buffer. Numbers in source code are
        // short, so keep them on the stack and only allocate for long ones.
        char stack_buffer[32];
        std::string heap_buffer;
        const char* begin;
        if (text.size() < sizeof(stack_buffer)) {
            std::memcpy(stack_buffer, text.data(), text.size());
            stack_buffer[text.size()] = '\0';
            begin = stack_buffer;
        } else {
            heap_buffer.assign(text.data(), text.size());
            begin = heap_buffer.c_str();
        }

        errno = 0;
        char* end = nullptr;
        const double value = detail::StrtodInCLocale(begin, &end);
        if (end != begin + text.size()) {
            return false;
        }
        // strtod reports gradual underflow (subnormal results) through
        // ERANGE too; only reject the cases std::from_chars rejects:
        // underflow all the way to zero, and overflow to infinity.
        if (errno == ERANGE && (value == 0.0 || !std::isfinite(value))) {
            return false;
        }
        *out = value;
        return true;
    }



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


    //---------------------------------------
    // ------------ quote.cpp ---------------
    //---------------------------------------
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


    // The short, fixed set of content kinds Guchho actually understands once a
    // data URL's media type has been normalized by DataURL::DecodeMIMEType.
    //
    // Any type outside this list is reported as kUnsupported so the caller can
    // decide whether the payload is still worth decoding (for example, opaque
    // binary data such as "image/png" that Guchho cannot interpret further).
    enum class MIMEType : uint8_t {
        kUnsupported,
        kTextCSS,
        kTextJavaScript,
        kApplicationJSON,
    };

    // The two halves of a "data:" URL after the "data:" prefix has been peeled
    // away and the payload has been fenced into dedicated fields.
    //
    // "mime_type" keeps the media type exactly as written in the URL, which
    // may still carry parameters such as ";charset=utf-8" and may be empty.
    // "data" holds the raw payload still in either base64 or percent-escaped
    // form - no decoding has happened at this point. "is_base64" records
    // whether the URL explicitly asked for the base64 interpretation of
    // "data".
    //
    // Examples of the stored raw form:
    //   "data:text/css;charset=utf-8;base64,Ym9keXt9" ->
    //     mime_type: "text/css;charset=utf-8"
    //     data:      "Ym9keXt9"
    //     is_base64: true
    //   "data:,hello%20world" ->
    //     mime_type: ""
    //     data:      "hello%20world"
    //     is_base64: false
    struct DataURL {
        std::string mime_type;
        std::string data;
        bool        is_base64 = false;

        // Maps the raw media type stored in "mime_type" to a canonical
        // MIMEType. Any ";"-separated parameters are dropped first, so values
        // like "text/css;charset=utf-8" reduce to their bare base type.
        //
        // The comparison is exact and case sensitive, and it is deliberately
        // strict: only the three recognized types resolve to a supported kind.
        // Everything else - including equal-looking types with different
        // casing, or recognized types polluted by whitespace - falls back to
        // kUnsupported.
        //
        // Examples:
        //   mime_type "text/css;charset=utf-8" -> kTextCSS
        //   mime_type "text/javascript"         -> kTextJavaScript
        //   mime_type "application/json"        -> kApplicationJSON
        //   mime_type "image/png"               -> kUnsupported
        //   mime_type "TEXT/CSS"                -> kUnsupported
        MIMEType DecodeMIMEType() const;

        // Decodes the raw payload stored in "data" and returns the underlying
        // resource bytes, or nullopt (with "error" filled in) when the payload
        // is malformed.
        //
        // When "is_base64" is true, "data" is decoded with the standard
        // base64 alphabet. Line breaks ("\r" and "\n") inside the payload are
        // skipped silently, the input must align on full four-character
        // quanta, and padding must appear only at the very end. An illegal
        // character, out-of-place padding, or leftover characters at the tail
        // all abort the decode: "error" is set to a message that includes the
        // byte offset of the corruption and nullopt is returned.
        //
        // When "is_base64" is false, "data" is treated as percent-escaped
        // text: every "%xx" escape is replaced by the single byte it codes,
        // and any other character passes through unchanged. A malformed
        // escape - a "%" not followed by two hex digits, or a dangling "%" at
        // the end of the string - fails the whole decode, quoting the
        // offending escape inside "error".
        //
        // The produced bytes are not guaranteed to be valid UTF-8: base64
        // output is arbitrary data and a "%00" escape embeds a real NUL byte.
        // When text is expected it is up to the caller to validate the result.
        //
        // Examples:
        //   is_base64 = true,  data "Zm9vIGJhcg=="    -> "foo bar"
        //   is_base64 = true,  data "bGluZQo="        -> "line\n"
        //   is_base64 = false, data "hello%20world"   -> "hello world"
        //   is_base64 = false, data "%E2%98%83"       -> the 3-byte UTF-8 for "\u2603"
        //   is_base64 = false, data "plain x"         -> "plain x"
        //   is_base64 = true,  data "a!bc"            -> nullopt, corrupting byte reported
        std::optional<std::string> DecodeData(std::string& error) const;
    };

    // Splits a URL string into its raw data-URL parts. The input must begin with
    // the exact, case-sensitive prefix "data:" and must contain a comma;
    // failing either requirement yields nullopt.
    //
    // The stored media type is whatever sits between the prefix and the first
    // comma (parameters such as ";charset=utf-8" are preserved untouched and
    // the type may even be empty), while the stored payload is everything
    // after that first comma - any comma inside the payload itself is simply
    // carried along. When the media type ends in the literal, case-sensitive
    // suffix ";base64", that suffix is stripped from the stored type and
    // "is_base64" is set to true; no other interpretation or validation takes
    // place here (see DataURL::DecodeMIMEType and DataURL::DecodeData for the
    // decoding stages).
    //
    // Examples:
    //   "data:text/css;charset=utf-8;base64,Ym9keXt9"      ->
    //     mime_type "text/css;charset=utf-8", data "Ym9keXt9", is_base64 true
    //   "data:text/plain,a,b"                              ->
    //     mime_type "text/plain", data "a,b", is_base64 false
    //   "data:,hello"                                      ->
    //     mime_type "", data "hello", is_base64 false
    //   "https://example.com/asset.css"                    -> nullopt
    //   "data:only-a-mime-type" (no comma)                 -> nullopt
    //   "data:text/plain;base64,Zm9v"                      ->
    //     mime_type "text/plain", data "Zm9v", is_base64 true
    std::optional<DataURL> ParseDataURL(const std::string& url);



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
    // ------------ mime.cpp ----------------
    //---------------------------------------
    // Implements content sniffing (WHATWG mimesniff) and always returns a valid MIME type.
    std::string DetectContentType(std::string_view data);


    //---------------------------------------
    // ------------- sha.cpp ----------------
    //---------------------------------------
    // SHA-2 cryptographic digests (FIPS 180-4). Each returns the raw digest
    // bytes: 32 for SHA-256, 48 for SHA-384, 64 for SHA-512. Feed the returned
    // bytes through Base64StdEncode() for e.g. `integrity` attribute values.
    std::vector<uint8_t> Sha256(std::string_view data);
    std::vector<uint8_t> Sha384(std::string_view data);
    std::vector<uint8_t> Sha512(std::string_view data);


    //---------------------------------------
    // ------------ process.cpp -------------
    //---------------------------------------
    // The result of running a child process.
    struct ProcessResult {
        // True when the requested program actually started: the process
        // image was created (CreateProcessW) on Windows, execvp took over
        // the child on POSIX. A child that came into being and then died
        // before becoming the program — no such executable, unreachable
        // working directory — is not a start, and is reported with the
        // exit code still at its -1 default.
        bool started = false;

        // The child's exit code, or -1 when it could not be determined.
        int exit_code = -1;

        // The captured standard output of the child.
        std::string stdout_data;

        // The captured standard error of the child.
        std::string stderr_data;
    };

    // Runs "argv" (argv[0] is the executable, found on PATH) in "cwd" and
    // captures both stdout and stderr. The pipes are drained on separate
    // threads so a chatty child can not deadlock the caller. Returns when the
    // child has exited.
    ProcessResult RunProcess(const std::vector<std::string>& argv, const std::string& cwd);


    //---------------------------------------
    // ------------- thread.cpp -------------
    //---------------------------------------
    // Worker threads that can recurse deeply need more stack than some
    // platforms hand out by default. The JavaScript parser is recursive
    // descent, so parseStmt takes a new frame per nesting level (the ~333
    // nested labels in "TestMinifyNestedLabelsNoBundle" are one frame
    // each), and the printer walks the same tree again when it prints it.
    //
    // Windows solves this process-wide: every thread inherits the image
    // stack reserve named in cmake/StandardProjectSettings.cmake. POSIX
    // has no such switch, and a std::thread there is a pthread whose
    // default stack is only 512 KB on macOS, so the same input dies on
    // the guard page with a SIGBUS. Ask for kWorkerStackSize bytes when
    // the thread is created instead.
    //
    // The size is a reservation, not a commitment: pages are only
    // touched if the thread really walks that deep, so nothing is
    // wasted on the shallow threads that share this helper.
    constexpr size_t kWorkerStackSize = 16 * 1024 * 1024;

    // A joinable thread with a caller-chosen stack size. Mirrors
    // std::thread's interface (move-only, joinable(), join(), and
    // std::terminate() if destroyed while still joinable) so call sites
    // can switch over by changing the type and passing a stack size.
    class Thread {
        public:
            // Creates a non-joinable thread.
            Thread() = default;

            // Runs "fn" on a new thread whose stack is at least
            // "stack_size" bytes. Throws std::system_error if the
            // thread cannot be created.
            Thread(size_t stack_size, std::function<void()> fn);

            Thread(Thread&& other) noexcept;
            Thread& operator=(Thread&& other) noexcept;
            Thread(const Thread&) = delete;
            Thread& operator=(const Thread&) = delete;

            ~Thread();

            // True when this thread has not yet been joined.
            bool joinable() const;

            // Waits for the thread to finish. Throws std::system_error
            // when this thread is not joinable.
            void join();

        private:
            struct Impl;
            std::unique_ptr<Impl> impl_;
    };

}