#include "guchho/javascript/js_lexer.hpp"
#include "guchho/javascript/js_helpers.hpp"
#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace guchho::javascript {

    namespace {

        // Lookup table that maps each token type (by its numeric enum value) to a
        // human-readable string. Used for error messages and debugging output. The
        // entries must be in the exact same order as the T enum so that a static
        // cast from T to size_t gives the correct index.
        [[maybe_unused]] const char* kTokenToString[] = {
            "end of file",
            "syntax error",
            "hashbang comment",

            "template literal",
            "number",
            "string",
            "bigint",

            "template literal",
            "template literal",
            "template literal",

            "\"&\"",
            "\"&&\"",
            "\"*\"",
            "\"**\"",
            "\"@\"",
            "\"|\"",
            "\"||\"",
            "\"^\"",
            "\"}\"",
            "\"]\"",
            "\")\"",
            "\":\"",
            "\",\"",
            "\".\"",
            "\"...\"",
            "\"==\"",
            "\"===\"",
            "\"=>\"",
            "\"!\"",
            "\"!=\"",
            "\"!==\"",
            "\">\"",
            "\">=\"",
            "\">>\"",
            "\">>>\"",
            "\"<\"",
            "\"<=\"",
            "\"<<\"",
            "\"-\"",
            "\"--\"",
            "\"{\"",
            "\"[\"",
            "\"(\"",
            "\"%\"",
            "\"+\"",
            "\"++\"",
            "\"?\"",
            "\"?.\"",
            "\"??\"",
            "\";\"",
            "\"/\"",
            "\"~\"",

            "\"&&=\"",
            "\"&=\"",
            "\"**=\"",
            "\"*=\"",
            "\"||=\"",
            "\"|=\"",
            "\"^=\"",
            "\"=\"",
            "\">>=\"",
            "\">>>=\"",
            "\"<<=\"",
            "\"-=\"",
            "\"%=\"",
            "\"+=\"",
            "\"\?\?=\"",
            "\"/=\"",

            "private identifier",

            "identifier",
            "escaped keyword",

            "\"break\"",
            "\"case\"",
            "\"catch\"",
            "\"class\"",
            "\"const\"",
            "\"continue\"",
            "\"debugger\"",
            "\"default\"",
            "\"delete\"",
            "\"do\"",
            "\"else\"",
            "\"enum\"",
            "\"export\"",
            "\"extends\"",
            "\"false\"",
            "\"finally\"",
            "\"for\"",
            "\"function\"",
            "\"if\"",
            "\"import\"",
            "\"in\"",
            "\"instanceof\"",
            "\"new\"",
            "\"null\"",
            "\"return\"",
            "\"super\"",
            "\"switch\"",
            "\"this\"",
            "\"throw\"",
            "\"true\"",
            "\"try\"",
            "\"typeof\"",
            "\"var\"",
            "\"void\"",
            "\"while\"",
            "\"with\"",
        };

        constexpr size_t kTokenToStringSize = std::size(kTokenToString);

        // Appends a Unicode code point to a UTF-16 string, encoding it as one or
        // two char16_t units. Code points in the Basic Multilingual Plane (U+0000
        // to U+FFFF) are encoded as a single unit. Code points above U+FFFF are
        // encoded as a surrogate pair (high surrogate + low surrogate) per UTF-16.
        //
        // Input:  decoded="ab", c=U+1F600  =>  decoded="ab\xD83D\xDE00"
        // Input:  decoded="x",  c=U+0041   =>  decoded="xA"
        void AppendUTF16Rune(std::u16string& decoded, char32_t c) {
            if (c <= 0xFFFF) {
                decoded.push_back(static_cast<char16_t>(c));
            } else {
                c -= 0x10000;
                decoded.push_back(static_cast<char16_t>(0xD800 + ((c >> 10) & 0x3FF)));
                decoded.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
            }
        }

        // Returns the numeric value of a hexadecimal digit character (0-9, a-f, A-F).
        // Returns -1 if the character is not a valid hex digit. Used when parsing
        // \xNN and \uNNNN escape sequences in strings and identifiers.
        //
        // Input:  'a'  =>  10
        // Input:  'F'  =>  15
        // Input:  'z'  =>  -1
        int HexDigit(char32_t c) noexcept {
            if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<int>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<int>(c - 'A' + 10);
            return -1;
        }



        // Parses a decimal or hexadecimal floating-point number from a string.
        // Uses std::from_chars for fast, locale-independent parsing. Returns 0.0
        // on failure (the caller is responsible for validating input before calling).
        //
        // Input:  "3.14"     =>  3.14
        // Input:  "1e10"     =>  10000000000.0
        // Input:  "0xff"     =>  255.0
        // Input:  ""         =>  0.0
        double ParseFloat(std::string_view text) {
            double value = 0;
            auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            (void)ptr;
            (void)ec;
            return value;
        }

        // Checks whether `text` starts with `prefix` and the character immediately
        // after the prefix is not an identifier continuation character. This ensures
        // the prefix is matched as a complete word, not as a prefix of a longer
        // identifier (e.g. "#__PURE__" matches but "#__PURE__X" does not).
        //
        // Input:  "#__PURE__ foo", "#__PURE__"  =>  true
        // Input:  "#__PURE__X",    "#__PURE__"  =>  false
        // Input:  "#__PURE__",     "#__PURE__"  =>  true (exact match)
        bool HasPrefixWithWordBoundary(std::string_view text, std::string_view prefix) {
            size_t t = text.size();
            size_t p = prefix.size();
            if (t >= p && text.substr(0, p) == prefix) {
                if (t == p) {
                    return true;
                }
                auto [c, width] = helpers::DecodeRuneInString(text.substr(p));
                (void)width;
                return !IsIdentifierContinue(c);
            }
            return false;
        }

        // Controls how ScanForPragmaArg handles leading whitespace before the
        // pragma argument. kNoSpaceFirst requires the argument to immediately
        // follow the pragma keyword (e.g. "# sourceMappingURL=url"). kSkipSpaceFirst
        // skips whitespace before the argument (e.g. "@jsx React.createElement").
        enum class PragmaArg : uint8_t {
            kNoSpaceFirst,
            kSkipSpaceFirst,
        };

        // Extracts the argument text for a pragma comment directive. The `pragma`
        // parameter is the keyword already matched (e.g. "sourceMappingURL=" or
        // "jsx"). The function strips the pragma from the front of the remaining
        // text, optionally skips whitespace (based on `kind`), and then captures
        // the argument up to the next whitespace or end of comment.
        //
        // Returns {Span, true} on success where the Span contains the argument
        // text and its source range. Returns {Span{}, false} if the argument
        // cannot be parsed (e.g. missing whitespace when required, or empty text).
        //
        // Input:  kind=kNoSpaceFirst, text="sourceMappingURL=data:...", pragma="sourceMappingURL="
        //         =>  {Span{"data:..."}, true}
        // Input:  kind=kSkipSpaceFirst, text=" jsx React", pragma="jsx"
        //         =>  {Span{"React"}, true}
        std::pair<Span, bool> ScanForPragmaArg(PragmaArg kind, int start, std::string_view pragma, std::string_view text) {
            text.remove_prefix(pragma.size());
            start += static_cast<int>(pragma.size());

            if (text.empty()) {
                return {Span{}, false};
            }

            auto [c, width] = helpers::DecodeRuneInString(text);
            if (kind == PragmaArg::kSkipSpaceFirst) {
                if (!IsWhitespace(c)) {
                    return {Span{}, false};
                }
                for (;;) {
                    text.remove_prefix(static_cast<size_t>(width));
                    start += width;
                    if (text.empty()) {
                        return {Span{}, false};
                    }
                    auto [c2, w2] = helpers::DecodeRuneInString(text);
                    c = c2;
                    width = w2;
                    if (!IsWhitespace(c)) {
                        break;
                    }
                }
            }

            size_t i = 0;
            while (!IsWhitespace(c)) {
                i += static_cast<size_t>(width);
                if (i >= text.size()) {
                    break;
                }
                auto [c2, w2] = helpers::DecodeRuneInString(text.substr(i));
                c = c2;
                width = w2;
                if (IsWhitespace(c)) {
                    break;
                }
            }

            return {Span{std::string(text.substr(0, i)),
                        logger::Range{logger::Loc{static_cast<int32_t>(start)}, static_cast<int32_t>(i)}},
                    true};
        }

        // Decodes HTML entities within a JSX text string and appends the result to
        // the decoded UTF-16 string. Handles both named entities (e.g. "&amp;" => '&')
        // and numeric entities (e.g. "&#65;" or "&#x41;" => 'A'). Unrecognized
        // entities are left as-is (the '&' is passed through literally).
        //
        // Input:  decoded="", text="hello &amp; world"  =>  "hello & world"
        // Input:  decoded="", text="&#x1F600;"           =>  "\uD83D\uDE00"
        // Input:  decoded="", text="&unknown;"           =>  "&unknown;"
        std::u16string DecodeJSXEntities(std::u16string decoded, std::string_view text) {
            size_t i = 0;

            while (i < text.size()) {
                auto [c, width] = helpers::DecodeRuneInString(text.substr(i));
                i += static_cast<size_t>(width);

                if (c == '&') {
                    size_t semicolon = text.find(';', i);
                    size_t length = (semicolon == std::string_view::npos) ? 0 : semicolon - i;
                    if (length > 0) {
                        std::string_view entity = text.substr(i, length);
                        if (entity[0] == '#') {
                            std::string_view number = entity.substr(1);
                            int base = 10;
                            if (number.size() > 1 && number[0] == 'x') {
                                number.remove_prefix(1);
                                base = 16;
                            }
                            int value = 0;
                            auto [ptr, ec] = std::from_chars(number.data(), number.data() + number.size(), value, base);
                            if (ec == std::errc()) {
                                c = static_cast<char32_t>(value);
                                i += length + 1;
                            }
                        } else {
                            auto it = JSXEntity.find(entity);
                            if (it != JSXEntity.end()) {
                                c = it->second;
                                i += length + 1;
                            }
                        }
                    }
                }

                AppendUTF16Rune(decoded, c);
            }

            return decoded;
        }

        // Normalizes whitespace and decodes HTML entities in a JSX text node. All
        // runs of whitespace (spaces, tabs, newlines, line/paragraph separators)
        // are collapsed into a single space character. Leading and trailing whitespace
        // is stripped entirely. Non-ASCII line separators (U+2028, U+2029) are treated
        // as whitespace for collapsing purposes. The remaining text segments are
        // passed through DecodeJSXEntities for entity resolution.
        //
        // Input:  "  hello   \n  world  "  =>  "hello world"
        // Input:  "  a  \n\n  b  "         =>  "a b"
        std::u16string FixWhitespaceAndDecodeJSXEntities(std::string_view text) {
            int after_last_non_whitespace = -1;
            std::u16string decoded;
            size_t i = 0;

            int first_non_whitespace = 0;

            while (i < text.size()) {
                auto [c, width] = helpers::DecodeRuneInString(text.substr(i));

                switch (c) {
                case '\r':
                case '\n':
                case 0x2028:
                case 0x2029:
                    if (first_non_whitespace != -1 && after_last_non_whitespace != -1) {
                        if (!decoded.empty()) {
                            decoded.push_back(u' ');
                        }
                        decoded = DecodeJSXEntities(std::move(decoded),
                            text.substr(static_cast<size_t>(first_non_whitespace),
                                        static_cast<size_t>(after_last_non_whitespace - first_non_whitespace)));
                    }
                    first_non_whitespace = -1;
                    break;

                case '\t':
                case ' ':
                    break;

                default:
                    if (!IsWhitespace(c)) {
                        after_last_non_whitespace = static_cast<int>(i) + width;
                        if (first_non_whitespace == -1) {
                            first_non_whitespace = static_cast<int>(i);
                        }
                    }
                    break;
                }

                i += static_cast<size_t>(width);
            }

            if (first_non_whitespace != -1) {
                if (!decoded.empty()) {
                    decoded.push_back(u' ');
                }
                decoded = DecodeJSXEntities(std::move(decoded), text.substr(static_cast<size_t>(first_non_whitespace)));
            }

            return decoded;
        }

    }

    // Converts a token type to its human-readable string representation. Returns
    // an empty string view if the token type is out of range (defensive, should
    // not happen in practice).
    std::string_view ToString(T kind) noexcept {
        auto idx = static_cast<size_t>(kind);
        if (idx < kTokenToStringSize) {
            return kTokenToString[idx];
        }
        return {};
    }

    // Map of HTML entity names to their Unicode code points. Used by JSX text
    // parsing to decode entities like "&amp;", "&lt;", "&#123;", etc. Covers the
    // standard HTML 4 entity set including Latin characters, Greek letters,
    // mathematical symbols, and common punctuation.
    const std::unordered_map<std::string_view, char32_t> JSXEntity = {
        {"quot", 0x0022},
        {"amp", 0x0026},
        {"apos", 0x0027},
        {"lt", 0x003C},
        {"gt", 0x003E},
        {"nbsp", 0x00A0},
        {"iexcl", 0x00A1},
        {"cent", 0x00A2},
        {"pound", 0x00A3},
        {"curren", 0x00A4},
        {"yen", 0x00A5},
        {"brvbar", 0x00A6},
        {"sect", 0x00A7},
        {"uml", 0x00A8},
        {"copy", 0x00A9},
        {"ordf", 0x00AA},
        {"laquo", 0x00AB},
        {"not", 0x00AC},
        {"shy", 0x00AD},
        {"reg", 0x00AE},
        {"macr", 0x00AF},
        {"deg", 0x00B0},
        {"plusmn", 0x00B1},
        {"sup2", 0x00B2},
        {"sup3", 0x00B3},
        {"acute", 0x00B4},
        {"micro", 0x00B5},
        {"para", 0x00B6},
        {"middot", 0x00B7},
        {"cedil", 0x00B8},
        {"sup1", 0x00B9},
        {"ordm", 0x00BA},
        {"raquo", 0x00BB},
        {"frac14", 0x00BC},
        {"frac12", 0x00BD},
        {"frac34", 0x00BE},
        {"iquest", 0x00BF},
        {"Agrave", 0x00C0},
        {"Aacute", 0x00C1},
        {"Acirc", 0x00C2},
        {"Atilde", 0x00C3},
        {"Auml", 0x00C4},
        {"Aring", 0x00C5},
        {"AElig", 0x00C6},
        {"Ccedil", 0x00C7},
        {"Egrave", 0x00C8},
        {"Eacute", 0x00C9},
        {"Ecirc", 0x00CA},
        {"Euml", 0x00CB},
        {"Igrave", 0x00CC},
        {"Iacute", 0x00CD},
        {"Icirc", 0x00CE},
        {"Iuml", 0x00CF},
        {"ETH", 0x00D0},
        {"Ntilde", 0x00D1},
        {"Ograve", 0x00D2},
        {"Oacute", 0x00D3},
        {"Ocirc", 0x00D4},
        {"Otilde", 0x00D5},
        {"Ouml", 0x00D6},
        {"times", 0x00D7},
        {"Oslash", 0x00D8},
        {"Ugrave", 0x00D9},
        {"Uacute", 0x00DA},
        {"Ucirc", 0x00DB},
        {"Uuml", 0x00DC},
        {"Yacute", 0x00DD},
        {"THORN", 0x00DE},
        {"szlig", 0x00DF},
        {"agrave", 0x00E0},
        {"aacute", 0x00E1},
        {"acirc", 0x00E2},
        {"atilde", 0x00E3},
        {"auml", 0x00E4},
        {"aring", 0x00E5},
        {"aelig", 0x00E6},
        {"ccedil", 0x00E7},
        {"egrave", 0x00E8},
        {"eacute", 0x00E9},
        {"ecirc", 0x00EA},
        {"euml", 0x00EB},
        {"igrave", 0x00EC},
        {"iacute", 0x00ED},
        {"icirc", 0x00EE},
        {"iuml", 0x00EF},
        {"eth", 0x00F0},
        {"ntilde", 0x00F1},
        {"ograve", 0x00F2},
        {"oacute", 0x00F3},
        {"ocirc", 0x00F4},
        {"otilde", 0x00F5},
        {"ouml", 0x00F6},
        {"divide", 0x00F7},
        {"oslash", 0x00F8},
        {"ugrave", 0x00F9},
        {"uacute", 0x00FA},
        {"ucirc", 0x00FB},
        {"uuml", 0x00FC},
        {"yacute", 0x00FD},
        {"thorn", 0x00FE},
        {"yuml", 0x00FF},
        {"OElig", 0x0152},
        {"oelig", 0x0153},
        {"Scaron", 0x0160},
        {"scaron", 0x0161},
        {"Yuml", 0x0178},
        {"fnof", 0x0192},
        {"circ", 0x02C6},
        {"tilde", 0x02DC},
        {"Alpha", 0x0391},
        {"Beta", 0x0392},
        {"Gamma", 0x0393},
        {"Delta", 0x0394},
        {"Epsilon", 0x0395},
        {"Zeta", 0x0396},
        {"Eta", 0x0397},
        {"Theta", 0x0398},
        {"Iota", 0x0399},
        {"Kappa", 0x039A},
        {"Lambda", 0x039B},
        {"Mu", 0x039C},
        {"Nu", 0x039D},
        {"Xi", 0x039E},
        {"Omicron", 0x039F},
        {"Pi", 0x03A0},
        {"Rho", 0x03A1},
        {"Sigma", 0x03A3},
        {"Tau", 0x03A4},
        {"Upsilon", 0x03A5},
        {"Phi", 0x03A6},
        {"Chi", 0x03A7},
        {"Psi", 0x03A8},
        {"Omega", 0x03A9},
        {"alpha", 0x03B1},
        {"beta", 0x03B2},
        {"gamma", 0x03B3},
        {"delta", 0x03B4},
        {"epsilon", 0x03B5},
        {"zeta", 0x03B6},
        {"eta", 0x03B7},
        {"theta", 0x03B8},
        {"iota", 0x03B9},
        {"kappa", 0x03BA},
        {"lambda", 0x03BB},
        {"mu", 0x03BC},
        {"nu", 0x03BD},
        {"xi", 0x03BE},
        {"omicron", 0x03BF},
        {"pi", 0x03C0},
        {"rho", 0x03C1},
        {"sigmaf", 0x03C2},
        {"sigma", 0x03C3},
        {"tau", 0x03C4},
        {"upsilon", 0x03C5},
        {"phi", 0x03C6},
        {"chi", 0x03C7},
        {"psi", 0x03C8},
        {"omega", 0x03C9},
        {"thetasym", 0x03D1},
        {"upsih", 0x03D2},
        {"piv", 0x03D6},
        {"ensp", 0x2002},
        {"emsp", 0x2003},
        {"thinsp", 0x2009},
        {"zwnj", 0x200C},
        {"zwj", 0x200D},
        {"lrm", 0x200E},
        {"rlm", 0x200F},
        {"ndash", 0x2013},
        {"mdash", 0x2014},
        {"lsquo", 0x2018},
        {"rsquo", 0x2019},
        {"sbquo", 0x201A},
        {"ldquo", 0x201C},
        {"rdquo", 0x201D},
        {"bdquo", 0x201E},
        {"dagger", 0x2020},
        {"Dagger", 0x2021},
        {"bull", 0x2022},
        {"hellip", 0x2026},
        {"permil", 0x2030},
        {"prime", 0x2032},
        {"Prime", 0x2033},
        {"lsaquo", 0x2039},
        {"rsaquo", 0x203A},
        {"oline", 0x203E},
        {"frasl", 0x2044},
        {"euro", 0x20AC},
        {"image", 0x2111},
        {"weierp", 0x2118},
        {"real", 0x211C},
        {"trade", 0x2122},
        {"alefsym", 0x2135},
        {"larr", 0x2190},
        {"uarr", 0x2191},
        {"rarr", 0x2192},
        {"darr", 0x2193},
        {"harr", 0x2194},
        {"crarr", 0x21B5},
        {"lArr", 0x21D0},
        {"uArr", 0x21D1},
        {"rArr", 0x21D2},
        {"dArr", 0x21D3},
        {"hArr", 0x21D4},
        {"forall", 0x2200},
        {"part", 0x2202},
        {"exist", 0x2203},
        {"empty", 0x2205},
        {"nabla", 0x2207},
        {"isin", 0x2208},
        {"notin", 0x2209},
        {"ni", 0x220B},
        {"prod", 0x220F},
        {"sum", 0x2211},
        {"minus", 0x2212},
        {"lowast", 0x2217},
        {"radic", 0x221A},
        {"prop", 0x221D},
        {"infin", 0x221E},
        {"ang", 0x2220},
        {"and", 0x2227},
        {"or", 0x2228},
        {"cap", 0x2229},
        {"cup", 0x222A},
        {"int", 0x222B},
        {"there4", 0x2234},
        {"sim", 0x223C},
        {"cong", 0x2245},
        {"asymp", 0x2248},
        {"ne", 0x2260},
        {"equiv", 0x2261},
        {"le", 0x2264},
        {"ge", 0x2265},
        {"sub", 0x2282},
        {"sup", 0x2283},
        {"nsub", 0x2284},
        {"sube", 0x2286},
        {"supe", 0x2287},
        {"oplus", 0x2295},
        {"otimes", 0x2297},
        {"perp", 0x22A5},
        {"sdot", 0x22C5},
        {"lceil", 0x2308},
        {"rceil", 0x2309},
        {"lfloor", 0x230A},
        {"rfloor", 0x230B},
        {"lang", 0x2329},
        {"rang", 0x232A},
        {"loz", 0x25CA},
        {"spades", 0x2660},
        {"clubs", 0x2663},
        {"hearts", 0x2665},
        {"diams", 0x2666},
    };


    // Map of JavaScript reserved words to their token types. The lexer uses this
    // to distinguish keywords from identifiers during scanning. Every entry here
    // produces a specific keyword token (e.g. T::kBreak) rather than a generic
    // T::kIdentifier.
    const std::unordered_map<std::string_view, T> kKeywords = {
        {"break", T::kBreak},
        {"case", T::kCase},
        {"catch", T::kCatch},
        {"class", T::kClass},
        {"const", T::kConst},
        {"continue", T::kContinue},
        {"debugger", T::kDebugger},
        {"default", T::kDefault},
        {"delete", T::kDelete},
        {"do", T::kDo},
        {"else", T::kElse},
        {"enum", T::kEnum},
        {"export", T::kExport},
        {"extends", T::kExtends},
        {"false", T::kFalse},
        {"finally", T::kFinally},
        {"for", T::kFor},
        {"function", T::kFunction},
        {"if", T::kIf},
        {"import", T::kImport},
        {"in", T::kIn},
        {"instanceof", T::kInstanceof},
        {"new", T::kNew},
        {"null", T::kNull},
        {"return", T::kReturn},
        {"super", T::kSuper},
        {"switch", T::kSwitch},
        {"this", T::kThis},
        {"throw", T::kThrow},
        {"true", T::kTrue},
        {"try", T::kTry},
        {"typeof", T::kTypeof},
        {"var", T::kVar},
        {"void", T::kVoid},
        {"while", T::kWhile},
        {"with", T::kWith},
    };

    // Words that are reserved only in strict mode. They are valid identifiers in
    // sloppy mode but cause a syntax error when used in strict mode code, class
    // bodies, or ES modules. The parser checks this set after the lexer returns
    // a T::kIdentifier token.
    const std::unordered_set<std::string_view> kStrictModeReservedWords = {
        "implements",
        "interface",
        "let",
        "package",
        "private",
        "protected",
        "public",
        "static",
        "yield",
    };

    // Computes the source range of an identifier starting at the given location.
    // Handles private names (prefixed with '#'), regular identifiers, and
    // identifiers with embedded unicode escape sequences (e.g. "\u0041BC"). For
    // escape sequences, the function scans over the raw source text to find the
    // end of the escaped identifier.
    //
    // When minifying, an identifier may have originally been a string literal that
    // was converted to a dot-access identifier (e.g. "foo.bar" => foo.bar). In
    // that case the function falls back to RangeOfString.
    //
    // Input:  source="let #foo = 1", loc=4  =>  Range for "#foo"
    // Input:  source="a\\u0042c", loc=0     =>  Range for "a\\u0042c"
    logger::Range RangeOfIdentifier(const logger::Source& source, logger::Loc loc) {
        auto text = std::string_view(source.contents).substr(static_cast<size_t>(loc.start));
        if (text.empty()) {
            return logger::Range{loc, 0};
        }

        int i = 0;
        auto [c, width] = helpers::DecodeRuneInString(text.substr(static_cast<size_t>(i)));
        (void)width;

        // Handle private names like #foo
        if (c == '#') {
            i++;
            c = helpers::DecodeRuneInString(text.substr(static_cast<size_t>(i))).first;
        }

        if (IsIdentifierStart(c) || c == '\\') {
            // Scan forward through identifier continuation characters, including
            // embedded unicode escape sequences like "\u{10000}".
            while (i < static_cast<int>(text.size())) {
                auto [c2, width2] = helpers::DecodeRuneInString(text.substr(static_cast<size_t>(i)));
                if (c2 == '\\') {
                    i += width2;

                    // Skip over bracketed unicode escapes such as "\u{10000}"
                    if (i + 2 < static_cast<int>(text.size()) &&
                        text[static_cast<size_t>(i)] == 'u' && text[static_cast<size_t>(i + 1)] == '{') {
                        i += 2;
                        while (i < static_cast<int>(text.size())) {
                            if (text[static_cast<size_t>(i)] == '}') {
                                i++;
                                break;
                            }
                            i++;
                        }
                    }
                } else if (!IsIdentifierContinue(c2)) {
                    return logger::Range{loc, static_cast<int32_t>(i)};
                } else {
                    i += width2;
                }
            }
        }

        // When minifying, this identifier may have originally been a string
        // literal that was converted to a property access (e.g. "foo.bar").
        return source.RangeOfString(loc);
    }

    // Returns the source range for a key or value in an import assertion/with
    // entry. Used when reporting errors about import assertion syntax.
    //
    // Input:  source="import x from 'y' assert { type: 'json' }", which=kKeyRange
    //         =>  Range for "type"
    // Input:  source="import x from 'y' assert { type: 'json' }", which=kValueRange
    //         =>  Range for "'json'"
    logger::Range RangeOfImportAssertOrWith(const logger::Source& source, const compiler::AssertOrWithEntry& assert_or_with, KeyOrValue which) {
        if (which == KeyOrValue::kKeyRange) {
            return RangeOfIdentifier(source, assert_or_with.key_loc);
        }
        if (which == KeyOrValue::kValueRange) {
            return source.RangeOfString(assert_or_with.value_loc);
        }
        logger::Loc loc = RangeOfIdentifier(source, assert_or_with.key_loc).loc;
        return logger::Range{loc, source.RangeOfString(assert_or_with.value_loc).End() - loc.start};
    }

    // Constructs a Lexer with default options. This is the standard constructor
    // used for parsing JavaScript and TypeScript source files.
    Lexer::Lexer(logger::Log log, logger::Source source, config::TSOptions ts)
        : Lexer(std::move(log), std::move(source), std::move(ts), InitOptions{}) {}

    // Constructs a Lexer with explicit options. The `error_suffix` is appended
    // to error messages (e.g. " in file.js"). The `json` flag enables JSON
    // parsing mode which disallows comments, trailing commas, and other
    // non-JSON syntax. The `for_global_name` flag enables a special mode for
    // parsing global names where '/' is treated as a division operator.
    Lexer::Lexer(logger::Log log, logger::Source source, config::TSOptions ts, InitOptions options)
        : error_suffix(std::move(options.error_suffix)),
        fn_or_arrow_start_loc(logger::Loc{-1}),
        prev_error_loc(logger::Loc{-1}),
        json(options.json),
        ts(std::move(ts)),
        for_global_name(options.for_global_name),
        log_(std::move(log)),
        source_(std::move(source)),
        tracker_(&source_) {
        step();
        Next();
    }

    // Creates a Lexer configured for parsing global names (e.g. the list of
    // globals in a banner comment). In this mode '/' is always treated as a
    // division operator, not the start of a regex or comment.
    Lexer Lexer::NewGlobalName(logger::Log log, logger::Source source) {
        InitOptions options;
        options.for_global_name = true;
        options.json = JSONFlavor::kNotJSON;
        return Lexer(std::move(log), std::move(source), config::TSOptions{}, std::move(options));
    }

    // Creates a Lexer configured for JSON parsing. The `json` flavor controls
    // which JSON variant to accept (strict JSON, JSONC for tsconfig.json, etc.).
    // The `error_suffix` is appended to any error messages produced.
    Lexer Lexer::NewJSON(logger::Log log, logger::Source source, JSONFlavor json, std::string error_suffix) {
        InitOptions options;
        options.json = json;
        options.error_suffix = std::move(error_suffix);
        return Lexer(std::move(log), std::move(source), config::TSOptions{}, std::move(options));
    }

    // Returns the start location of the current token as a Loc.
    logger::Loc Lexer::Loc() const {
        return logger::Loc{static_cast<int32_t>(start)};
    }

    // Returns the source range of the current token (start offset and length).
    logger::Range Lexer::Range() const {
        return logger::Range{logger::Loc{static_cast<int32_t>(start)}, static_cast<int32_t>(end - start)};
    }

    // Returns the raw source text of the current token (no escape decoding).
    std::string_view Lexer::Raw() const {
        return std::string_view(source_.contents).substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
    }

    // Returns the raw text of the current token as a MaybeSubstring, which
    // includes the byte offset. Used for identifiers and bigint literals.
    MaybeSubstring Lexer::rawIdentifier() {
        return MaybeSubstring{std::string(Raw()), static_cast<uint32_t>(start)};
    }

    // Returns the decoded UTF-16 content of the current string literal token.
    // Escape sequences (e.g. "\n", "\u0041", "\x41") are decoded on first access
    // and cached. If decoding fails (e.g. invalid unicode escape), a syntax error
    // is reported and the function throws LexerPanic.
    const std::u16string& Lexer::StringLiteral() {
        if (!decoded_string_literal_or_nil.has_value()) {
            std::u16string decoded;
            bool ok = false;
            int end_out = 0;
            tryToDecodeEscapeSequences(encoded_string_literal_start, encoded_string_literal_text, true, decoded, ok, end_out);
            if (!ok) {
                this->end = end_out;
                SyntaxError();
            } else {
                decoded_string_literal_or_nil = std::move(decoded);
            }
        }
        return *decoded_string_literal_or_nil;
    }

    // Returns both the cooked (decoded) and raw string contents of a template
    // literal token. The cooked value has escape sequences resolved; the raw
    // value preserves the original source text. For template heads and middles
    // (tokens ending with "${"), the "${" suffix is excluded. For tails and
    // no-substitution literals, the opening/closing backtick is excluded.
    //
    // Carriage returns are normalized to line feeds per the ECMAScript spec
    // (section 11.8.6.1 TV and TRV). "\r\n" becomes "\n" and standalone "\r"
    // becomes "\n".
    //
    // If the cooked value contains an invalid escape sequence, the cooked string
    // is set to empty and ok is false. The raw string is always populated.
    Lexer::CookedAndRawTemplateContentsResult Lexer::CookedAndRawTemplateContents() {
        std::string raw;

        switch (token) {
        case T::kNoSubstitutionTemplateLiteral:
        case T::kTemplateTail:
            raw = std::string(source_.contents.substr(static_cast<size_t>(start + 1), static_cast<size_t>((end - 1) - (start + 1))));
            break;

        case T::kTemplateHead:
        case T::kTemplateMiddle:
            raw = std::string(source_.contents.substr(static_cast<size_t>(start + 1), static_cast<size_t>((end - 2) - (start + 1))));
            break;

        default:
            break;  

        }
        
        if (raw.find('\r') != std::string::npos) {
            // Normalize carriage returns to line feeds. Per the ECMAScript spec,
            // both CR and CR+LF sequences become LF in template literal values.

            std::string bytes;
            bytes.reserve(raw.size());
            size_t i = 0;

            while (i < raw.size()) {
                char c = raw[i];
                i++;

                if (c == '\r') {
                    // Normalize "\r\n" to "\n"
                    if (i < raw.size() && raw[i] == '\n') {
                        i++;
                    }

                    // Normalize standalone "\r" to "\n"
                    c = '\n';
                }

                bytes.push_back(c);
            }

            raw = std::move(bytes);
        }

        // Decode escape sequences. On failure, the cooked string is empty and ok
        // is false — the caller treats this as "undefined" for tagged templates.
        std::u16string cooked;
        bool ok = false;
        int end_out = 0;
        tryToDecodeEscapeSequences(start + 1, raw, false, cooked, ok, end_out);
        (void)end_out;
        if (!ok) {
            cooked.clear();
        }
        return {std::move(cooked), std::move(raw), ok};
    }

    // Returns true if the current token is an identifier or a keyword. Keywords
    // have token types >= T::kIdentifier in the enum layout.
    bool Lexer::IsIdentifierOrKeyword() const {
        return token >= T::kIdentifier;
    }

    // Returns true if the current token is an identifier whose raw text matches
    // the given string. Used to detect contextual keywords like "of", "from",
    // "as", "async", "get", "set", etc. that are not reserved words but have
    // special meaning in certain grammatical positions.
    bool Lexer::IsContextualKeyword(std::string_view text) const {
        return token == T::kIdentifier && Raw() == text;
    }

    // Asserts that the current token is the given contextual keyword and advances
    // to the next token. If the token doesn't match, an error is reported with
    // the expected keyword name.
    void Lexer::ExpectContextualKeyword(std::string_view text) {
        if (!IsContextualKeyword(text)) {
            ExpectedString(std::format("\"{}\"", text));
        }
        Next();
    }

    // Reports a syntax error at the current token position. The error message
    // is chosen based on the character at the current position: control characters
    // get a hex escape description, non-ASCII characters get a unicode description,
    // regular printable characters get quoted, and end-of-file gets an "unexpected
    // end of file" message. After reporting, throws LexerPanic to unwind.
    void Lexer::SyntaxError() {
        logger::Loc loc{static_cast<int32_t>(end)};
        std::string message;
        if (end < static_cast<int>(source_.contents.size())) {
            auto [c, width] = helpers::DecodeRuneInString(std::string_view(source_.contents).substr(static_cast<size_t>(end)));
            (void)width;
            if (c < 0x20) {
                message = logger::FormatMsg(logger::MsgCat::kJS_SyntaxErrorHex, static_cast<uint32_t>(c));
            } else if (c >= 0x80) {
                message = logger::FormatMsg(logger::MsgCat::kJS_SyntaxErrorUnicode, static_cast<uint32_t>(c));
            } else if (c != '"') {
                message = logger::FormatMsg(logger::MsgCat::kJS_SyntaxError, std::string(1, static_cast<char>(c)));
            } else {
                message = logger::FormatMsg(logger::MsgCat::kJS_SyntaxErrorQuote);
            }
        } else {
            message = logger::FormatMsg(logger::MsgCat::kJS_UnexpectedEOF);
        }
        addRangeError(logger::Range{loc}, message);
        throw LexerPanic{};
    }

    // Reports an "expected X but found Y" error. Special-cases the "await"
    // keyword to provide a helpful suggestion about adding "async" to the
    // enclosing function. The `text` parameter is the expected token text
    // (e.g. "\"(\"" or "\"return\""). After reporting, throws LexerPanic.
    void Lexer::ExpectedString(std::string_view text) {
        if (prev_token_was_await_keyword) {
            std::vector<logger::MsgData> notes;
            if (fn_or_arrow_start_loc.start != -1) {
                auto note = tracker_.MakeMsgData(logger::Range{fn_or_arrow_start_loc},
                    logger::FormatMsg(logger::MsgCat::kJS_ConsiderAddingAsyncNote));
                if (note.location) {
                    note.location->suggestion = "async";
                }
                notes.push_back(std::move(note));
            }
            AddRangeErrorWithNotes(RangeOfIdentifier(source_, await_keyword_loc),
                logger::FormatMsg(logger::MsgCat::kJS_AwaitInAsyncFunction), notes);
            throw LexerPanic{};
        }

        std::string found = guchho::helpers::quoteString(Raw());
        if (start == static_cast<int>(source_.contents.size())) {
            found = "end of file";
        }

        std::string suggestion;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
            suggestion = std::string(text.substr(1, text.size() - 2));
        }

        addRangeErrorWithSuggestion(Range(),
            logger::FormatMsg(logger::MsgCat::kJS_ExpectedWithSuffix, std::string(text), error_suffix, found), suggestion);
        throw LexerPanic{};
    }

    // Reports an "expected <token> but found <current>" error. Delegates to
    // ExpectedString for the actual error message. Falls back to Unexpected()
    // if the token type has no string representation.
    void Lexer::Expected(T expected) {
        std::string_view text = ToString(expected);
        if (!text.empty()) {
            ExpectedString(text);
        } else {
            Unexpected();
        }
    }

    // Reports an "unexpected X" error at the current token. The "X" is the
    // quoted raw text of the current token, or "end of file" if at EOF.
    void Lexer::Unexpected() {
        std::string found = guchho::helpers::quoteString(Raw());
        if (start == static_cast<int>(source_.contents.size())) {
            found = "end of file";
        }
        addRangeError(Range(), logger::FormatMsg(logger::MsgCat::kJS_UnexpectedXF, found, error_suffix));
        throw LexerPanic{};
    }

    // Asserts that the current token matches `expected` and advances to the next
    // token. If it doesn't match, reports an error and throws LexerPanic.
    void Lexer::Expect(T expected) {
        if (token != expected) {
            Expected(expected);
        }
        Next();
    }

    // Implements automatic semicolon insertion (ASI). A semicolon is inserted
    // when:
    //   1. The current token is an explicit semicolon, OR
    //   2. There was a newline before the current token AND the current token
    //      is not a close brace or end of file (ASI at end of statement)
    //
    // If neither condition holds, a semicolon is expected and an error is reported.
    void Lexer::ExpectOrInsertSemicolon() {
        if (token == T::kSemicolon ||
            (!has_newline_before && token != T::kCloseBrace && token != T::kEndOfFile)) {
            Expect(T::kSemicolon);
        }
    }

    // Consumes a "<" token, handling the case where the current token is a
    // multi-character operator that starts with "<" (e.g. "<=", "<<", "<<=").
    // When inside a JSX element, advances using NextInsideJSXElement instead
    // of Next to handle JSX-specific tokenization.
    void Lexer::ExpectLessThan(bool is_inside_jsx_element) {
        switch (token) {
        case T::kLessThan:
            if (is_inside_jsx_element) {
                NextInsideJSXElement();
            } else {
                Next();
            }
            break;

        case T::kLessThanEquals:
            token = T::kEquals;
            start++;
            maybeExpandEquals();
            break;

        case T::kLessThanLessThan:
            token = T::kLessThan;
            start++;
            break;

        case T::kLessThanLessThanEquals:
            token = T::kLessThanEquals;
            start++;
            break;

        default:
            Expected(T::kLessThan);
        }
    }

    // Consumes a ">" token, handling the case where the current token is a
    // multi-character operator that starts with ">" (e.g. ">=", ">>", ">>>",
    // ">>=", ">>>="). When inside a JSX element, advances using
    // NextInsideJSXElement instead of Next.
    void Lexer::ExpectGreaterThan(bool is_inside_jsx_element) {
        switch (token) {
        case T::kGreaterThan:
            if (is_inside_jsx_element) {
                NextInsideJSXElement();
            } else {
                Next();
            }
            break;

        case T::kGreaterThanEquals:
            token = T::kEquals;
            start++;
            maybeExpandEquals();
            break;

        case T::kGreaterThanGreaterThan:
            token = T::kGreaterThan;
            start++;
            break;

        case T::kGreaterThanGreaterThanEquals:
            token = T::kGreaterThanEquals;
            start++;
            break;

        case T::kGreaterThanGreaterThanGreaterThan:
            token = T::kGreaterThanGreaterThan;
            start++;
            break;

        case T::kGreaterThanGreaterThanGreaterThanEquals:
            token = T::kGreaterThanGreaterThanEquals;
            start++;
            break;

        default:
            Expected(T::kGreaterThan);
        }
    }

    // Called after consuming a "=" to check if it's actually part of a longer
    // operator: "=>", "==", or "===". Updates the token type accordingly and
    // advances past any additional characters.
    void Lexer::maybeExpandEquals() {
        switch (code_point) {
        case '>':
            token = T::kEqualsGreaterThan;
            step();
            break;

        case '=':
            token = T::kEqualsEquals;
            step();

            if (token == static_cast<T>(U'=')) {
                token = T::kEqualsEqualsEquals;
                step();
            }
            break;
        }
    }

    // Asserts that the current token matches `expected` and advances to the next
    // JSX element child token. If it doesn't match, reports an error.
    void Lexer::ExpectJSXElementChild(T expected) {
        if (token != expected) {
            Expected(expected);
        }
        NextJSXElementChild();
    }

    // Scans the next token inside a JSX element's children. JSX children can be:
    //   - "{" to start a JavaScript expression
    //   - "<" to start a nested JSX element
    //   - Text content (everything else until "{", "<", or end of file)
    //
    // Text content is decoded as a string literal. The fast path copies ASCII
    // bytes directly; the slow path handles whitespace normalization and HTML
    // entity decoding when the text contains newlines, '&' characters, or
    // non-ASCII characters.
    //
    // Bare "}" and ">" characters in text produce an error with a suggestion
    // to use the entity escape form ("{'}'}" or "{'>'}").
    void Lexer::NextJSXElementChild() {
        has_newline_before = false;
        int original_start = end;

        start = end;
        token = static_cast<T>(0);

        switch (code_point) {
        case -1: // -1 signals end-of-file
            token = T::kEndOfFile;
            break;

        case '{':
            step();
            token = T::kOpenBrace;
            break;

        case '<':
            step();
            token = T::kLessThan;
            break;

        default: {
            bool needs_fixing = false;

            for (;;) {
                switch (code_point) {
                case -1:
                case '{':
                case '<':
                    goto string_literal_end;

                case '&':
                case '\r':
                case '\n':
                case 0x2028:
                case 0x2029:
                    // These require the slow path for entity decoding or
                    // whitespace normalization
                    needs_fixing = true;
                    step();
                    break;

                case '}':
                case '>': {
                    // Bare "}" and ">" are not valid JSX text characters. Report an
                    // error with a suggestion to escape them.
                    std::string replacement = code_point == '}' ? "{'}'}" : "{'>'}";
                    logger::Msg msg;
                    msg.kind = logger::MsgKind::kError;
                    msg.data = tracker_.MakeMsgData(logger::Range{logger::Loc{static_cast<int32_t>(end)}, 1},
                        logger::FormatMsg(logger::MsgCat::kJS_CharNotValidInJSX, std::string(1, static_cast<char>(code_point))));

                    // If this looks like "= >" (arrow function) in TSX mode, provide
                    // a disambiguation note instead of the generic escape suggestion.
                    if (could_be_bad_arrow_in_tsx > 0 && code_point == '>' &&
                        source_.contents[static_cast<size_t>(end - 1)] == '=') {
                        auto note = tracker_.MakeMsgData(bad_arrow_in_tsx_range,
                            logger::FormatMsg(logger::MsgCat::kJS_TSXArrowDisambiguation));
                        if (note.location) {
                            note.location->suggestion = bad_arrow_in_tsx_suggestion;
                        }
                        msg.notes.push_back(std::move(note));
                    } else {
                         logger::MsgData note;
                        note.text = logger::FormatMsg(logger::MsgCat::kJS_DidYouMeanEscape, guchho::helpers::quoteString(replacement));
                        msg.notes.push_back(std::move(note));
                        if (msg.data.location) {
                            msg.data.location->suggestion = replacement;
                        }
                        if (!ts.Parse) {
                            // TypeScript always errors on bare ">" in JSX text, but
                            // Babel allows it in JS mode for compatibility.
                            msg.kind = logger::MsgKind::kWarning;
                        }
                    }

                    log_.add_msg(std::move(msg));
                    step();
                    break;
                }

                default:
                    if (code_point >= 0x80) {
                        needs_fixing = true;
                    }
                    step();
                    break;
                }
            }

            string_literal_end:
            token = T::kStringLiteral;
            auto text = std::string_view(source_.contents).substr(static_cast<size_t>(original_start),
                static_cast<size_t>(end - original_start));

            if (needs_fixing) {
                decoded_string_literal_or_nil = FixWhitespaceAndDecodeJSXEntities(text);
            } else {
                std::u16string copy;
                copy.reserve(text.size());
                for (char c : text) {
                    copy.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
                }
                decoded_string_literal_or_nil = std::move(copy);
            }
            break;
        }
        }
    }

    // Asserts that the current token matches `expected` and advances to the next
    // token inside a JSX element. If it doesn't match, reports an error.
    void Lexer::ExpectInsideJSXElement(T expected) {
        if (token != expected) {
            Expected(expected);
        }
        NextInsideJSXElement();
    }

    // Scans the next token inside a JSX element (between the opening and closing
    // tags). This tokenizer is simpler than the main Next() because JSX elements
    // have a restricted syntax: only punctuation, string literals, identifiers,
    // and comments are valid. Whitespace and newlines are skipped. Comments
    // (both "//" and "/* */") are consumed and skipped. The function handles
    // JSX-specific string literals with HTML entity decoding.
    void Lexer::NextInsideJSXElement() {
        has_newline_before = false;

        for (;;) {
            start = end;
            token = static_cast<T>(0);

            switch (code_point) {
            case -1: // -1 signals end-of-file
                token = T::kEndOfFile;
                break;

            case '\r':
            case '\n':
            case 0x2028:
            case 0x2029:
                step();
                has_newline_before = true;
                continue;

            case '\t':
            case ' ':
                step();
                continue;

            case '.':
                step();
                token = T::kDot;
                break;

            case ':':
                step();
                token = T::kColon;
                break;

            case '=':
                step();
                token = T::kEquals;
                break;

            case '{':
                step();
                token = T::kOpenBrace;
                break;

            case '}':
                step();
                token = T::kCloseBrace;
                break;

            case '<':
                step();
                token = T::kLessThan;
                break;

            case '>':
                step();
                token = T::kGreaterThan;
                break;

            case '/':
                step();
                switch (code_point) {
                case '/':
                    for (;;) {
                        step();
                        switch (code_point) {
                        case '\r':
                        case '\n':
                        case 0x2028:
                        case 0x2029:
                        case -1:
                            goto single_line_comment_end;
                        }
                    }
                    single_line_comment_end:
                    continue;

                case '*':
                    step();
                    {
                        logger::Range start_range = Range();
                        for (;;) {
                            switch (code_point) {
                            case '*':
                                step();
                                if (code_point == '/') {
                                    step();
                                    goto multi_line_comment_end;
                                }
                                break;

                            case '\r':
                            case '\n':
                            case 0x2028:
                            case 0x2029:
                                step();
                                has_newline_before = true;
                                break;

                                case -1: // -1 signals end-of-file
                                start = end;
                                AddRangeErrorWithNotes(logger::Range{Loc()},
                                    logger::FormatMsg(logger::MsgCat::kJS_ExpectedCommentTerminator),
                                    {tracker_.MakeMsgData(start_range, logger::FormatMsg(logger::MsgCat::kJS_CommentStartsHere))});
                                throw LexerPanic{};

                            default:
                                step();
                            }
                        }
                        multi_line_comment_end:
                        ;
                    }
                    continue;

                default:
                    token = T::kSlash;
                    break;
                }
                break;

            case '\'':
            case '"': {
                logger::Range backslash;
                int32_t quote = code_point;
                bool needs_decode = false;
                step();

                for (;;) {
                    switch (code_point) {
                    case -1: // -1 signals end-of-file
                        addRangeError(logger::Range{logger::Loc{static_cast<int32_t>(end)}}, logger::FormatMsg(logger::MsgCat::kJS_UnterminatedStringLiteral));
                        throw LexerPanic{};

                    case '&':
                        needs_decode = true;
                        step();
                        break;

                    case '\\':
                        backslash = logger::Range{logger::Loc{static_cast<int32_t>(end)}, 1};
                        step();
                        continue;

                    default:
                        if (code_point == quote) {
                            if (backslash.len > 0) {
                                backslash.len++;
                                previous_backslash_quote_in_jsx = backslash;
                            }
                            step();
                            goto jsx_string_literal_end;
                        }

                        if (code_point >= 0x80) {
                            needs_decode = true;
                        }
                        step();
                        break;
                    }
                    backslash = logger::Range{};
                }

                jsx_string_literal_end:
                token = T::kStringLiteral;
                auto text = std::string_view(source_.contents).substr(static_cast<size_t>(start + 1),
                    static_cast<size_t>((end - 1) - (start + 1)));

                if (needs_decode) {
                    decoded_string_literal_or_nil = DecodeJSXEntities(std::u16string{}, text);
                } else {
                    std::u16string copy;
                    copy.reserve(text.size());
                    for (char c : text) {
                        copy.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
                    }
                    decoded_string_literal_or_nil = std::move(copy);
                }
                break;
            }

            default:
                if (IsWhitespace(static_cast<char32_t>(code_point))) {
                    step();
                    continue;
                }

                if (IsIdentifierStart(static_cast<char32_t>(code_point))) {
                    step();
                    while (IsIdentifierContinue(static_cast<char32_t>(code_point)) || code_point == '-') {
                        step();
                    }

                    identifier = rawIdentifier();
                    token = T::kIdentifier;
                    break;
                }

                end = current;
                step();
                token = T::kSyntaxError;
                break;
            }

            return;
        }
    }

    // Main lexer entry point. Scans the next token from the source, skipping
    // whitespace and comments. The token type is stored in `this->token` and
    // the raw text is accessible via Raw(). This function handles all JavaScript
    // token types including identifiers, keywords, numbers, strings, template
    // literals, regular expressions, and all punctuation/operators.
    //
    // Whitespace and comments are consumed silently (they don't produce tokens).
    // Single-line comments ("//...") and multi-line comments ("/* ... */") are
    // scanned and their text is stored for potential preservation in the output.
    // Legal annotations ("//@license", "/*!...") are tracked separately.
    void Lexer::Next() {
        has_newline_before = (end == 0);
        has_comment_before = CommentBefore::kNone;
        prev_token_was_await_keyword = false;
        legal_comments_before_token.clear();
        comments_before_token.clear();

        for (;;) {
            start = end;
            token = static_cast<T>(0);

            switch (code_point) {
            case -1: // -1 signals end-of-file
                token = T::kEndOfFile;
                break;

            case '#':
                if (start == 0 && std::string_view(source_.contents).starts_with("#!")) {
                    // Hashbang shebang line (e.g. "#!/usr/bin/env node")
                    token = T::kHashbang;
                    for (;;) {
                        step();
                        switch (code_point) {
                        case '\r':
                        case '\n':
                        case 0x2028:
                        case 0x2029:
                        case -1:
                            goto hashbang_end;
                        }
                    }
                    hashbang_end:
                    identifier = rawIdentifier();
                } else {
                    // Private identifier like #foo
                    step();
                    if (code_point == '\\') {
                        identifier = scanIdentifierWithEscapes(IdentifierKind::kPrivate).first;
                    } else {
                        if (!IsIdentifierStart(static_cast<char32_t>(code_point))) {
                            SyntaxError();
                        }
                        step();
                        while (IsIdentifierContinue(static_cast<char32_t>(code_point))) {
                            step();
                        }
                        if (code_point == '\\') {
                            identifier = scanIdentifierWithEscapes(IdentifierKind::kPrivate).first;
                        } else {
                            identifier = rawIdentifier();
                        }
                    }
                    token = T::kPrivateIdentifier;
                }
                break;

            case '\r':
            case '\n':
            case 0x2028:
            case 0x2029:
                step();
                has_newline_before = true;
                continue;

            case '\t':
            case ' ':
                step();
                continue;

            case '(':
                step();
                token = T::kOpenParen;
                break;

            case ')':
                step();
                token = T::kCloseParen;
                break;

            case '[':
                step();
                token = T::kOpenBracket;
                break;

            case ']':
                step();
                token = T::kCloseBracket;
                break;

            case '{':
                step();
                token = T::kOpenBrace;
                break;

            case '}':
                step();
                token = T::kCloseBrace;
                break;

            case ',':
                step();
                token = T::kComma;
                break;

            case ':':
                step();
                token = T::kColon;
                break;

            case ';':
                step();
                token = T::kSemicolon;
                break;

            case '@':
                step();
                token = T::kAt;
                break;

            case '~':
                step();
                token = T::kTilde;
                break;

            case '?':
                step();
                switch (code_point) {
                case '?':
                    step();
                    switch (code_point) {
                    case '=':
                        step();
                        token = T::kQuestionQuestionEquals;
                        break;
                    default:
                        token = T::kQuestionQuestion;
                        break;
                    }
                    break;
                case '.':
                    token = T::kQuestion;
                    {
                        int current2 = current;
                        auto& contents = source_.contents;

                        // Disambiguate "a?.1:b" (optional chaining on a numeric
                        // literal) from "a ? .1 : b" (ternary with a decimal).
                        // Only treat as optional chaining if the next character
                        // is not a digit.
                        if (current2 >= static_cast<int>(contents.size()) ||
                            (contents[static_cast<size_t>(current2)] < '0' || contents[static_cast<size_t>(current2)] > '9')) {
                            step();
                            token = T::kQuestionDot;
                        }
                    }
                    break;
                default:
                    token = T::kQuestion;
                    break;
                }
                break;

            case '%':
                step();
                switch (code_point) {
                case '=':
                    step();
                    token = T::kPercentEquals;
                    break;
                default:
                    token = T::kPercent;
                    break;
                }
                break;

            case '&':
                step();
                switch (code_point) {
                case '=':
                    step();
                    token = T::kAmpersandEquals;
                    break;
                case '&':
                    step();
                    switch (code_point) {
                    case '=':
                        step();
                        token = T::kAmpersandAmpersandEquals;
                        break;
                    default:
                        token = T::kAmpersandAmpersand;
                        break;
                    }
                    break;
                default:
                    token = T::kAmpersand;
                    break;
                }
                break;

            case '|':
                step();
                switch (code_point) {
                case '=':
                    step();
                    token = T::kBarEquals;
                    break;
                case '|':
                    step();
                    switch (code_point) {
                    case '=':
                        step();
                        token = T::kBarBarEquals;
                        break;
                    default:
                        token = T::kBarBar;
                        break;
                    }
                    break;
                default:
                    token = T::kBar;
                    break;
                }
                break;

            case '^':
                step();
                switch (code_point) {
                case '=':
                    step();
                    token = T::kCaretEquals;
                    break;
                default:
                    token = T::kCaret;
                    break;
                }
                break;

            case '+':
                step();
                switch (code_point) {
                case '=':
                    step();
                    token = T::kPlusEquals;
                    break;
                case '+':
                    step();
                    token = T::kPlusPlus;
                    break;
                default:
                    token = T::kPlus;
                    break;
                }
                break;

            case '-':
                step();
                switch (code_point) {
                case '=':
                    step();
                    token = T::kMinusEquals;
                    break;
                case '-':
                    step();

                    // Handle legacy HTML-style close comment "-->". This is not
                    // valid JavaScript but is tolerated for compatibility with
                    // older HTML embedding patterns. Only recognized when preceded
                    // by a newline.
                    if (code_point == '>' && has_newline_before) {
                        step();
                        legacy_html_comment_range = Range();
                        log_.AddID(logger::MsgID::kJS_HTMLCommentInJS, logger::MsgKind::kWarning, &tracker_, Range(),
                            "Treating \"-->\" as the start of a legacy HTML single-line comment");
                        for (;;) {
                            switch (code_point) {
                            case '\r':
                            case '\n':
                            case 0x2028:
                            case 0x2029:
                            case -1:
                                goto single_line_html_close_comment_end;
                            }
                            step();
                        }
                        single_line_html_close_comment_end:
                        continue;
                    }

                    token = T::kMinusMinus;
                    break;
                default:
                    token = T::kMinus;
                    if (json == JSONFlavor::kJSON && code_point != '.' &&
                        (code_point < '0' || code_point > '9')) {
                        Unexpected();
                    }
                    break;
                }
                break;

            case '*':
                step();
                switch (code_point) {
                case '=':
                    step();
                    token = T::kAsteriskEquals;
                    break;

                case '*':
                    step();
                    switch (code_point) {
                    case '=':
                        step();
                        token = T::kAsteriskAsteriskEquals;
                        break;

                    default:
                        token = T::kAsteriskAsterisk;
                        break;
                    }
                    break;

                default:
                    token = T::kAsterisk;
                    break;
                }
                break;

            case '/':
                step();
                if (for_global_name) {
                    token = T::kSlash;
                    break;
                }
                switch (code_point) {
                case '=':
                    step();
                    token = T::kSlashEquals;
                    break;

                case '/':
                    for (;;) {
                        step();
                        switch (code_point) {
                        case '\r':
                        case '\n':
                        case 0x2028:
                        case 0x2029:
                        case -1:
                            goto single_line_comment_end;
                        }
                    }
                    single_line_comment_end:
                    if (json == JSONFlavor::kJSON) {
                        addRangeError(Range(), logger::FormatMsg(logger::MsgCat::kJS_JSONNoComments));
                    }
                    scanCommentText();
                    continue;

                case '*':
                    step();
                    {
                        logger::Range start_range = Range();
                        for (;;) {
                            switch (code_point) {
                            case '*':
                                step();
                                if (code_point == '/') {
                                    step();
                                    goto multi_line_comment_end;
                                }
                                break;

                            case '\r':
                            case '\n':
                            case 0x2028:
                            case 0x2029:
                                step();
                                has_newline_before = true;
                                break;

                                case -1: // -1 signals end-of-file
                                start = end;
                                AddRangeErrorWithNotes(logger::Range{Loc()},
                                    logger::FormatMsg(logger::MsgCat::kJS_ExpectedCommentTerminator),
                                    {tracker_.MakeMsgData(start_range, logger::FormatMsg(logger::MsgCat::kJS_CommentStartsHere))});
                                throw LexerPanic{};

                            default:
                                step();
                            }
                        }
                        multi_line_comment_end:
                        ;
                    }
                    if (json == JSONFlavor::kJSON) {
                        addRangeError(Range(), logger::FormatMsg(logger::MsgCat::kJS_JSONNoComments));
                    }
                    scanCommentText();
                    continue;

                default:
                    token = T::kSlash;
                    break;
                }
                break;

            case '=':
                step();
                switch (code_point) {
                case '>':
                    step();
                    token = T::kEqualsGreaterThan;
                    break;
                case '=':
                    step();
                    switch (code_point) {
                    case '=':
                        step();
                        token = T::kEqualsEqualsEquals;
                        break;
                    default:
                        token = T::kEqualsEquals;
                        break;
                    }
                    break;
                default:
                    token = T::kEquals;
                    break;
                }
                break;

            case '<':
                step();
                switch (code_point) {
                case '=':
                    step();
                    token = T::kLessThanEquals;
                    break;
                case '<':
                    step();
                    switch (code_point) {
                    case '=':
                        step();
                        token = T::kLessThanLessThanEquals;
                        break;
                    default:
                        token = T::kLessThanLessThan;
                        break;
                    }
                    break;

                case '!':
                    if (std::string_view(source_.contents).substr(static_cast<size_t>(start)).starts_with("<!--")) {
                        step();
                        step();
                        step();
                        legacy_html_comment_range = Range();
                        log_.AddID(logger::MsgID::kJS_HTMLCommentInJS, logger::MsgKind::kWarning, &tracker_, Range(),
                            "Treating \"<!--\" as the start of a legacy HTML single-line comment");
                        for (;;) {
                            switch (code_point) {
                            case '\r':
                            case '\n':
                            case 0x2028:
                            case 0x2029:
                            case -1:
                                goto single_line_html_open_comment_end;
                            }
                            step();
                        }
                        single_line_html_open_comment_end:
                        continue;
                    }

                    token = T::kLessThan;
                    break;

                default:
                    token = T::kLessThan;
                    break;
                }
                break;

            case '>':
                step();
                switch (code_point) {
                case '=':
                    step();
                    token = T::kGreaterThanEquals;
                    break;
                case '>':
                    step();
                    switch (code_point) {
                    case '=':
                        step();
                        token = T::kGreaterThanGreaterThanEquals;
                        break;
                    case '>':
                        step();
                        switch (code_point) {
                        case '=':
                            step();
                            token = T::kGreaterThanGreaterThanGreaterThanEquals;
                            break;
                        default:
                            token = T::kGreaterThanGreaterThanGreaterThan;
                            break;
                        }
                        break;
                    default:
                        token = T::kGreaterThanGreaterThan;
                        break;
                    }
                    break;
                default:
                    token = T::kGreaterThan;
                    break;
                }
                break;

            case '!':
                step();
                switch (code_point) {
                case '=':
                    step();
                    switch (code_point) {
                    case '=':
                        step();
                        token = T::kExclamationEqualsEquals;
                        break;
                    default:
                        token = T::kExclamationEquals;
                        break;
                    }
                    break;
                default:
                    token = T::kExclamation;
                    break;
                }
                break;

            case '\'':
            case '"':
            case '`': {
                int32_t quote = code_point;
                bool needs_slow_path = false;
                int suffix_len = 1;

                if (quote != '`') {
                    token = T::kStringLiteral;
                } else if (rescan_close_brace_as_template_token) {
                    token = T::kTemplateTail;
                } else {
                    token = T::kNoSubstitutionTemplateLiteral;
                }
                step();

                for (;;) {
                    switch (code_point) {
                    case '\\':
                        needs_slow_path = true;
                        step();

                        // Treat "\r\n" as a single line continuation in strings
                        if (code_point == '\r' && json != JSONFlavor::kJSON) {
                            step();
                            if (code_point == '\n') {
                                step();
                            }
                            continue;
                        }
                        break;

                    case -1:
                        addRangeError(logger::Range{logger::Loc{static_cast<int32_t>(end)}}, logger::FormatMsg(logger::MsgCat::kJS_UnterminatedStringLiteral));
                        throw LexerPanic{};

                        needs_slow_path = true;
                        break;

                    case '\n':
                        if (quote != '`') {
                            addRangeError(logger::Range{logger::Loc{static_cast<int32_t>(end)}}, logger::FormatMsg(logger::MsgCat::kJS_UnterminatedStringLiteral));
                            throw LexerPanic{};
                        }
                        break;

                    case '\r':
                        if (quote != '`') {
                            addRangeError(logger::Range{logger::Loc{static_cast<int32_t>(end)}}, logger::FormatMsg(logger::MsgCat::kJS_UnterminatedStringLiteral));
                            throw LexerPanic{};
                        }

                        needs_slow_path = true;
                        break;

                    case '$':
                        if (quote == '`') {
                            step();
                            if (code_point == '{') {
                                suffix_len = 2;
                                step();
                                if (rescan_close_brace_as_template_token) {
                                    token = T::kTemplateMiddle;
                                } else {
                                    token = T::kTemplateHead;
                                }
                                goto string_literal_end;
                            }
                            continue;
                        }
                        break;

                    default:
                        if (code_point == quote) {
                            step();
                            goto string_literal_end;
                        }

                        if (code_point >= 0x80) {
                            needs_slow_path = true;
                        } else if (json == JSONFlavor::kJSON && code_point < 0x20) {
                            SyntaxError();
                        }
                        break;
                    }
                    step();
                }

                string_literal_end:
                auto text = std::string_view(source_.contents).substr(static_cast<size_t>(start + 1),
                    static_cast<size_t>((end - suffix_len) - (start + 1)));

                if (needs_slow_path) {
                    // Defer escape sequence decoding until StringLiteral() is called
                    decoded_string_literal_or_nil.reset();
                    encoded_string_literal_start = start + 1;
                    encoded_string_literal_text = std::string(text);
                } else {
                    // Copy ASCII bytes directly to UTF-16 without decoding
                    std::u16string copy;
                    copy.reserve(text.size());
                    for (char c : text) {
                        copy.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
                    }
                    decoded_string_literal_or_nil = std::move(copy);
                }

                if (quote == '\'' && (json == JSONFlavor::kJSON || json == JSONFlavor::kTSConfigJSON)) {
                    addRangeError(Range(), logger::FormatMsg(logger::MsgCat::kJS_JSONStringsDoubleQuotes));
                }
                break;
            }

            // Identifier scanning (hot path for ASCII identifiers)
            case '_':
            case '$':
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i':
            case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
            case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I':
            case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
            case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z': {
                // Fast path: scan contiguous ASCII identifier characters without
                // decoding UTF-8. This handles the vast majority of identifiers.
                auto& contents = source_.contents;
                size_t n = contents.size();
                size_t i = static_cast<size_t>(current);
                while (i < n) {
                    char c = contents[i];
                    if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') &&
                        (c < '0' || c > '9') && c != '_' && c != '$') {
                        break;
                    }
                    i++;
                }
                current = static_cast<int>(i);

                // Handle any non-ASCII continuation characters (e.g. Unicode letters)
                step();
                if (code_point >= 0x80) {
                    while (IsIdentifierContinue(static_cast<char32_t>(code_point))) {
                        step();
                    }
                }

                // If there's a backslash, the identifier has embedded escape sequences
                // (e.g. "\u0041BC"). Use the slow path to decode them.
                if (code_point == '\\') {
                    auto [ident, tok] = scanIdentifierWithEscapes(IdentifierKind::kNormal);
                    identifier = std::move(ident);
                    token = tok;
                    break;
                }

                // No escape sequences — slice the raw text directly and check
                // if it's a keyword
                identifier = rawIdentifier();
                auto it = kKeywords.find(Raw());
                token = (it != kKeywords.end()) ? it->second : T::kIdentifier;
                break;
            }

            case '\\':
                {
                    auto [ident, tok] = scanIdentifierWithEscapes(IdentifierKind::kNormal);
                    identifier = std::move(ident);
                    token = tok;
                }
                break;

            case '.':
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                parseNumericLiteralOrDot();
                break;

            default:
                if (IsWhitespace(static_cast<char32_t>(code_point))) {
                    step();
                    continue;
                }

                if (IsIdentifierStart(static_cast<char32_t>(code_point))) {
                    step();
                    while (IsIdentifierContinue(static_cast<char32_t>(code_point))) {
                        step();
                    }
                    if (code_point == '\\') {
                        auto [ident, tok] = scanIdentifierWithEscapes(IdentifierKind::kNormal);
                        identifier = std::move(ident);
                        token = tok;
                    } else {
                        token = T::kIdentifier;
                        identifier = rawIdentifier();
                    }
                    break;
                }

                end = current;
                step();
                token = T::kSyntaxError;
                break;
            }

            return;
        }
    }

    // Scans an identifier that contains unicode escape sequences (e.g. "\u0041BC"
    // or "fo\u006F"). This is the slow path for identifiers that the fast ASCII
    // scanner cannot handle. The function performs two passes:
    //
    //   Pass 1: Scan over the raw source to find the end of the identifier,
    //           skipping over escape sequences like "\uNNNN" and "\u{NNNN}".
    //
    //   Pass 2: Decode the raw text using the escape sequence decoder to produce
    //           the actual identifier string. Verify that the decoded result is
    //           a valid identifier. If it matches a keyword, return
    //           T::kEscapedKeyword (which is treated as an identifier in most
    //           positions but is not allowed to shadow strict-mode reserved words).
    //
    // Input:  source="fo\\u006Fbar", kind=kNormal  =>  {"foobar", T::kIdentifier}
    // Input:  source="#\\u0062ar", kind=kPrivate   =>  {"#bar", T::kIdentifier}
    std::pair<MaybeSubstring, T> Lexer::scanIdentifierWithEscapes(IdentifierKind kind) {
        // First pass: scan over the identifier to find its end
        for (;;) {
            // Handle unicode escape sequences. The entry point is a backslash
            // followed by 'u'.
            if (code_point == '\\') {
                step();
                if (code_point != 'u') {
                    SyntaxError();
                }
                step();
                if (code_point == '{') {
                    // Variable-length: \u{1F600}
                    step();
                    for (;;) {
                        if (code_point == '}') {
                            break;
                        }
                        switch (code_point) {
                        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                        case '8': case '9': case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
                            step();
                            break;
                        default:
                            SyntaxError();
                        }
                    }
                    step();
                } else {
                    // Fixed-length: \u0041
                    for (int j = 0; j < 4; j++) {
                        switch (code_point) {
                        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                        case '8': case '9': case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
                            step();
                            break;
                        default:
                            SyntaxError();
                        }
                    }
                }
                continue;
            }

            // Stop at the end of the identifier
            if (!IsIdentifierContinue(static_cast<char32_t>(code_point))) {
                break;
            }
            step();
        }

        // Second pass: decode the raw text using the escape sequence decoder
        std::u16string decoded;
        bool ok = false;
        int end_out = 0;
        tryToDecodeEscapeSequences(start, Raw(), true, decoded, ok, end_out);
        if (!ok) {
            this->end = end_out;
            SyntaxError();
        }
        std::string text = helpers::UTF16ToString(decoded);

        // Even though it was escaped, the decoded text must still be a valid
        // JavaScript identifier
        std::string_view decoded_identifier = text;
        if (kind == IdentifierKind::kPrivate) {
            decoded_identifier.remove_prefix(1); // Skip over the "#"
        }
        if (!IsIdentifier(decoded_identifier)) {
            addRangeError(logger::Range{logger::Loc{static_cast<int32_t>(start)}, static_cast<int32_t>(end - start)},
                logger::FormatMsg(logger::MsgCat::kJS_InvalidIdentifier, guchho::helpers::quoteString(text)));
        }

        // Escaped keywords (e.g. "\u0066or") are not treated as actual keywords
        // by the spec — they produce T::kEscapedKeyword instead. They are allowed
        // in identifier positions but not in places where a keyword is required.
        if (kKeywords.find(text) != kKeywords.end()) {
            return {MaybeSubstring{std::move(text)}, T::kEscapedKeyword};
        }
        return {MaybeSubstring{std::move(text)}, T::kIdentifier};
    }

    // Parses a numeric literal or a dot token. When the current character is a
    // digit or a dot followed by a digit, this function scans the full numeric
    // literal (integer, floating-point, hex, octal, binary, or bigint). When the
    // current character is a dot not followed by a digit, it produces a dot or
    // spread ("...") token. This function handles all the complexity of JavaScript
    // numeric literals including:
    //   - Decimal integers and floats (123, 3.14, 1e10, 1_000)
    //   - Hexadecimal (0xFF), octal (0o77), binary (0b1010)
    //   - Legacy octal (077 — forbidden in strict mode)
    //   - BigInt literals (42n, 0xFFn)
    //   - Numeric separators (1_000_000)
    void Lexer::parseNumericLiteralOrDot() {
        int32_t first = code_point;
        step();

        // A dot not followed by a digit is either "." or "..."
        if (first == '.' && (code_point < '0' || code_point > '9')) {
            // Spread operator "..."
            if (code_point == '.' &&
                current < static_cast<int>(source_.contents.size()) &&
                source_.contents[static_cast<size_t>(current)] == '.') {
                step();
                step();
                token = T::kDotDotDot;
                return;
            }

            // Dot operator
            token = T::kDot;
            return;
        }

        int underscore_count = 0;
        int last_underscore_end = 0;
        bool has_dot_or_exponent = first == '.';
        bool is_missing_digit_after_dot = false;
        double base = 0;
        is_legacy_octal_literal = false;

        // Default to numeric literal; may change to bigint if 'n' suffix is found
        token = T::kNumericLiteral;

        // Detect base from prefix: 0x (hex), 0o (octal), 0b (binary), or legacy octal
        if (first == '0') {
            switch (code_point) {
            case 'b':
            case 'B':
                base = 2;
                break;

            case 'o':
            case 'O':
                base = 8;
                break;

            case 'x':
            case 'X':
                base = 16;
                break;

            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '_':
                base = 8;
                is_legacy_octal_literal = true;
                break;

            case '8':
            case '9':
                is_legacy_octal_literal = true;
                break;
            }
        }

        if (base != 0) {
            // Integer literal
            bool is_first = true;
            bool is_invalid_legacy_octal_literal = false;
            number = 0;
            if (!is_legacy_octal_literal) {
                step();
            }

            for (;;) {
                switch (code_point) {
                case '_':
                    // Cannot have multiple underscores in a row
                    if (last_underscore_end > 0 && end == last_underscore_end + 1) {
                        SyntaxError();
                    }

                    // The first digit must exist
                    if (is_first || is_legacy_octal_literal) {
                        SyntaxError();
                    }

                    last_underscore_end = end;
                    underscore_count++;
                    break;

                case '0':
                case '1':
                    number = number * base + static_cast<double>(code_point - '0');
                    break;

                case '2': case '3': case '4': case '5': case '6': case '7':
                    if (base == 2) {
                        SyntaxError();
                    }
                    number = number * base + static_cast<double>(code_point - '0');
                    break;

                case '8':
                case '9':
                    if (is_legacy_octal_literal) {
                        is_invalid_legacy_octal_literal = true;
                    } else if (base < 10) {
                        SyntaxError();
                    }
                    number = number * base + static_cast<double>(code_point - '0');
                    break;

                case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
                    if (base != 16) {
                        SyntaxError();
                    }
                    number = number * base + static_cast<double>(code_point + 10 - 'A');
                    break;

                case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                    if (base != 16) {
                        SyntaxError();
                    }
                    number = number * base + static_cast<double>(code_point + 10 - 'a');
                    break;

                default:
                    // The first digit must exist
                    if (is_first) {
                        SyntaxError();
                    }
                    goto integer_literal_end;
                }

                step();
                is_first = false;
            }

            integer_literal_end:
            bool is_big_integer_literal = code_point == 'n' && !has_dot_or_exponent;

            // BigInt and legacy octal with digits 8-9 require re-scanning as text
            if (is_big_integer_literal || is_invalid_legacy_octal_literal) {
                MaybeSubstring text = rawIdentifier();

                // BigInt literals cannot have leading zeros (except "0n")
                if (is_big_integer_literal && is_legacy_octal_literal) {
                    SyntaxError();
                }

                // Remove numeric separators before parsing
                if (underscore_count > 0) {
                    std::string bytes;
                    bytes.reserve(text.str.size() - static_cast<size_t>(underscore_count));
                    for (char c : text.str) {
                        if (c != '_') {
                            bytes.push_back(c);
                        }
                    }
                    text = MaybeSubstring{std::move(bytes)};
                }

                // BigInts are stored as text to avoid floating-point precision loss
                if (is_big_integer_literal) {
                    identifier = std::move(text);
                } else if (is_invalid_legacy_octal_literal) {
                    // Legacy octal with 8-9 digits is actually a base-10 literal
                    number = ParseFloat(text.str);
                }
            }
        } else {
            // Floating-point or decimal integer literal
            bool is_invalid_legacy_octal_literal = first == '0' && (code_point == '8' || code_point == '9');

            // Initial digits
            for (;;) {
                if (code_point < '0' || code_point > '9') {
                    if (code_point != '_') {
                        break;
                    }

                    // Cannot have multiple underscores in a row
                    if (last_underscore_end > 0 && end == last_underscore_end + 1) {
                        SyntaxError();
                    }

                    // The specification forbids underscores in this case
                    if (is_invalid_legacy_octal_literal) {
                        SyntaxError();
                    }

                    last_underscore_end = end;
                    underscore_count++;
                }
                step();
            }

            // Fractional digits
            if (first != '.' && code_point == '.') {
                // An underscore must not come last
                if (last_underscore_end > 0 && end == last_underscore_end + 1) {
                    end--;
                    SyntaxError();
                }

                has_dot_or_exponent = true;
                step();
                if (code_point == '_') {
                    SyntaxError();
                }
                is_missing_digit_after_dot = true;
                for (;;) {
                    if (code_point >= '0' && code_point <= '9') {
                        is_missing_digit_after_dot = false;
                    } else {
                        if (code_point != '_') {
                            break;
                        }

                        // Cannot have multiple underscores in a row
                        if (last_underscore_end > 0 && end == last_underscore_end + 1) {
                            SyntaxError();
                        }

                        last_underscore_end = end;
                        underscore_count++;
                    }
                    step();
                }
            }

            // Exponent
            if (code_point == 'e' || code_point == 'E') {
                // An underscore must not come last
                if (last_underscore_end > 0 && end == last_underscore_end + 1) {
                    end--;
                    SyntaxError();
                }

                has_dot_or_exponent = true;
                step();
                if (code_point == '+' || code_point == '-') {
                    step();
                }
                if (code_point < '0' || code_point > '9') {
                    SyntaxError();
                }
                for (;;) {
                    if (code_point < '0' || code_point > '9') {
                        if (code_point != '_') {
                            break;
                        }

                        // Cannot have multiple underscores in a row
                        if (last_underscore_end > 0 && end == last_underscore_end + 1) {
                            SyntaxError();
                        }

                        last_underscore_end = end;
                        underscore_count++;
                    }
                    step();
                }
            }

            // Extract the raw text and remove numeric separators
            MaybeSubstring text = rawIdentifier();

            if (underscore_count > 0) {
                std::string bytes;
                bytes.reserve(text.str.size() - static_cast<size_t>(underscore_count));
                for (char c : text.str) {
                    if (c != '_') {
                        bytes.push_back(c);
                    }
                }
                text = MaybeSubstring{std::move(bytes)};
            }

            if (code_point == 'n' && !has_dot_or_exponent) {
                // Only "0n" is allowed as a zero-prefixed bigint literal
                if (text.str.size() > 1 && first == '0') {
                    SyntaxError();
                }

                // BigInts are stored as text to avoid floating-point precision loss
                identifier = std::move(text);
            } else if (!has_dot_or_exponent && end - start < 10) {
                // Fast path: parse short decimal integers as 32-bit unsigned
                uint32_t number32 = 0;
                for (char c : text.str) {
                    number32 = number32 * 10 + static_cast<uint32_t>(c - '0');
                }
                number = static_cast<double>(number32);
            } else {
                // Parse decimal integer or floating-point with exponent/fraction
                number = ParseFloat(text.str);
            }
        }

        // Reject trailing underscore (e.g. "1_")
        if (last_underscore_end > 0 && end == last_underscore_end + 1) {
            end--;
            SyntaxError();
        }

        // Consume 'n' suffix for bigint literals
        if (code_point == 'n' && !has_dot_or_exponent) {
            token = T::kBigIntegerLiteral;
            step();
        }

        // Adjacent identifiers after numbers are a syntax error (e.g. "1x")
        if (IsIdentifierStart(static_cast<char32_t>(code_point))) {
            SyntaxError();
        }

        // JSON disallows leading dots, hex/octal/binary prefixes, underscores,
        // and trailing dots without a digit
        if (json == JSONFlavor::kJSON &&
            (first == '.' || base != 0 || underscore_count > 0 || is_missing_digit_after_dot)) {
            Unexpected();
        }
    }

    // Scans a regular expression literal. Called after Next() has already consumed
    // the '/' token and determined that a regex should follow (rather than a divide
    // operator). This function reads the pattern body (handling character classes and
    // escape sequences), then reads the flag characters (d, g, i, m, s, u, v, y).
    // Duplicate flags are reported as errors.
    //
    // Input:  source="/abc/gi"   (starting after '/')
    // Output: token = T::kRegExpLiteral, pattern = "abc", flags = "gi"
    void Lexer::ScanRegExp() {
        auto validate_and_step = [&]() {
            if (code_point == '\\') {
                step();
            }

            switch (code_point) {
            case -1: // End of file
            case '\r':
            case '\n':
            case 0x2028:
            case 0x2029: // Line terminators are not allowed inside regex patterns
                addRangeError(logger::Range{logger::Loc{static_cast<int32_t>(end)}}, logger::FormatMsg(logger::MsgCat::kJS_UnterminatedRegexp));
                throw LexerPanic{};

            default:
                step();
            }
        };

        for (;;) {
            switch (code_point) {
            case '/':
                step();
                {
                    uint32_t bits = 0;
                    while (IsIdentifierContinue(static_cast<char32_t>(code_point))) {
                        switch (code_point) {
                        case 'd':
                        case 'g':
                        case 'i':
                        case 'm':
                        case 's':
                        case 'u':
                        case 'v':
                        case 'y': {
                            uint32_t bit = static_cast<uint32_t>(1) << static_cast<uint32_t>(code_point - 'a');
                            if ((bit & bits) != 0) {
                                // Duplicate flag — find its position and report
                                logger::Range r1{logger::Loc{static_cast<int32_t>(start)}, 1};
                                logger::Range r2{logger::Loc{static_cast<int32_t>(end)}, 1};
                                while (r1.loc.start < r2.loc.start &&
                                    source_.contents[static_cast<size_t>(r1.loc.start)] != static_cast<char>(code_point)) {
                                    r1.loc.start++;
                                }
                                log_.AddErrorWithNotes(&tracker_, r2,
                                    logger::FormatMsg(logger::MsgCat::kJS_DuplicateRegexpFlag, std::string(1, static_cast<char>(code_point))),
                                    {tracker_.MakeMsgData(r1, logger::FormatMsg(logger::MsgCat::kJS_DuplicateImportFirstNote, std::string(1, static_cast<char>(code_point))))});
                            } else {
                                bits |= bit;
                            }
                            step();
                            break;
                        }

                        default:
                            SyntaxError();
                        }
                    }
                }
                return;

            case '[':
                step();
                while (code_point != ']') {
                    validate_and_step();
                }
                step();
                break;

            default:
                validate_and_step();
            }
        }
    }

    // Decodes escape sequences in a string or template literal, converting the raw
    // source text into the actual character values. This function handles:
    //   - Simple escapes: \n, \t, \r, \b, \f, \v, \0
    //   - Character escapes: \a, \cX, etc.
    //   - Hex escapes: \xNN
    //   - Unicode escapes: \uNNNN and \u{NNNNN}
    //   - Octal escapes: \0NN (legacy)
    //   - Line continuations: \<newline> (removed from output)
    //   - '\r\n' and '\r' normalized to '\n'
    //
    // When report_errors is true, any invalid escape sequence adds a syntax error.
    // When report_errors is false (e.g. for template tag raw strings), errors are
    // silently ignored.
    //
    // Input:  text="hello\\nworld", report_errors=true  =>  decoded=L"hello\nworld", ok=true
    // Input:  text="\\xZZ", report_errors=true          =>  ok=false (invalid hex)
    void Lexer::tryToDecodeEscapeSequences(int input_start, std::string_view text, bool report_errors,
                                        std::u16string& decoded, bool& ok, int& out_end) {
        decoded.clear();
        ok = false;
        out_end = 0;
        size_t i = 0;

        while (i < text.size()) {
            auto [c, width] = helpers::DecodeRuneInString(text.substr(i));
            i += static_cast<size_t>(width);

            switch (c) {
            case '\r':
                // Normalize '\r\n' and standalone '\r' to '\n' per spec (TV/TRV)
                if (i < text.size() && text[i] == '\n') {
                    i++;
                }

                decoded.push_back(u'\n');
                continue;

            case '\\': {
                auto [c2, width2] = helpers::DecodeRuneInString(text.substr(i));
                i += static_cast<size_t>(width2);

                switch (c2) {
                case 'b':
                    decoded.push_back(u'\b');
                    continue;

                case 'f':
                    decoded.push_back(u'\f');
                    continue;

                case 'n':
                    decoded.push_back(u'\n');
                    continue;

                case 'r':
                    decoded.push_back(u'\r');
                    continue;

                case 't':
                    decoded.push_back(u'\t');
                    continue;

                case 'v':
                    if (json == JSONFlavor::kJSON) {
                        out_end = input_start + static_cast<int>(i) - width2;
                        return;
                    }
                    decoded.push_back(u'\v');
                    continue;

                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7': {
                    int octal_start = static_cast<int>(i) - 2;
                    if (json == JSONFlavor::kJSON) {
                        out_end = input_start + static_cast<int>(i) - width2;
                        return;
                    }

                    // Legacy octal escape: 1-3 digits in range 0-7
                    bool is_bad = false;
                    int value = static_cast<int>(c2 - '0');
                    auto [c3, width3] = helpers::DecodeRuneInString(text.substr(i));
                    switch (c3) {
                    case '0':
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                        value = value * 8 + static_cast<int>(c3 - '0');
                        i += static_cast<size_t>(width3);
                        {
                            auto [c4, width4] = helpers::DecodeRuneInString(text.substr(i));
                            switch (c4) {
                            case '0':
                            case '1':
                            case '2':
                            case '3':
                            case '4':
                            case '5':
                            case '6':
                            case '7': {
                                int temp = value * 8 + static_cast<int>(c4 - '0');
                                if (temp < 256) {
                                    value = temp;
                                    i += static_cast<size_t>(width4);
                                }
                                break;
                            }
                            case '8':
                            case '9':
                                is_bad = true;
                                break;
                            }
                        }
                        break;
                    case '8':
                    case '9':
                        is_bad = true;
                        break;
                    }
                    c = static_cast<char32_t>(value);

                    // Only \0 is allowed; other legacy octals are forbidden
                    if (is_bad ||
                        text.substr(static_cast<size_t>(octal_start), static_cast<size_t>(i) - static_cast<size_t>(octal_start)) != "\\0") {
                        legacy_octal_loc = logger::Loc{static_cast<int32_t>(input_start + octal_start)};
                    }
                    break;
                }

                case '8':
                case '9':
                    c = c2;

                    // \8 and \9 are not valid escape sequences
                    legacy_octal_loc = logger::Loc{static_cast<int32_t>(input_start + static_cast<int>(i) - 2)};
                    break;

                case 'x':
                    if (json == JSONFlavor::kJSON) {
                        out_end = input_start + static_cast<int>(i) - width2;
                        return;
                    }

                    // Hex escape: \xNN
                    {
                        int value = 0;
                        for (int j = 0; j < 2; j++) {
                            auto [c3, width3] = helpers::DecodeRuneInString(text.substr(i));
                            i += static_cast<size_t>(width3);
                            int d = HexDigit(c3);
                            if (d == -1) {
                                out_end = input_start + static_cast<int>(i) - width3;
                                return;
                            }
                            value = value * 16 | d;
                        }
                        c = static_cast<char32_t>(value);
                    }
                    break;

                case 'u': {
                    // Unicode escape: \uNNNN or \u{NNNNN}
                    int value = 0;

                    // Peek at the character after 'u' to distinguish the two forms
                    auto [c3, width3] = helpers::DecodeRuneInString(text.substr(i));
                    i += static_cast<size_t>(width3);

                    if (c3 == '{') {
                        if (json == JSONFlavor::kJSON) {
                            out_end = input_start + static_cast<int>(i) - width2;
                            return;
                        }

                        // Variable-length form: \u{1F600}
                        int hex_start = static_cast<int>(i) - width - width2 - width3;
                        bool is_first = true;
                        bool is_out_of_range = false;
                        for (;;) {
                            auto [c4, w4] = helpers::DecodeRuneInString(text.substr(i));
                            i += static_cast<size_t>(w4);

                            if (c4 == '}') {
                                if (is_first) {
                                    out_end = input_start + static_cast<int>(i) - w4;
                                    return;
                                }
                                break;
                            }

                            int d = HexDigit(c4);
                            if (d == -1) {
                                out_end = input_start + static_cast<int>(i) - w4;
                                return;
                            }
                            value = value * 16 | d;

                            if (value > 0x10FFFF) {
                                is_out_of_range = true;
                            }

                            is_first = false;
                        }

                        if (is_out_of_range && report_errors) {
                            addRangeError(logger::Range{logger::Loc{static_cast<int32_t>(input_start + hex_start)},
                                                    static_cast<int32_t>(i - static_cast<size_t>(hex_start))},
                                logger::FormatMsg(logger::MsgCat::kJS_UnicodeEscapeOutOfRange));
                            throw LexerPanic{};
                        }
                    } else {
                        // Fixed-length form: \u0041
                        for (int j = 0; j < 4; j++) {
                            int d = HexDigit(c3);
                            if (d == -1) {
                                out_end = input_start + static_cast<int>(i) - width3;
                                return;
                            }
                            value = value * 16 | d;

                            if (j < 3) {
                                auto [c4, w4] = helpers::DecodeRuneInString(text.substr(i));
                                i += static_cast<size_t>(w4);
                                c3 = c4;
                                width3 = w4;
                            }
                        }
                    }
                    c = static_cast<char32_t>(value);
                    break;
                }

                case '\r':
                    if (json == JSONFlavor::kJSON) {
                        out_end = input_start + static_cast<int>(i) - width2;
                        return;
                    }

                    // Line continuation: \\<CR><LF> or \\<CR> — consumed, not emitted
                    if (i < text.size() && text[i] == '\n') {
                        // Make sure Windows CRLF counts as a single newline
                        i++;
                    }
                    continue;

                case '\n':
                case 0x2028:
                case 0x2029:
                    if (json == JSONFlavor::kJSON) {
                        out_end = input_start + static_cast<int>(i) - width2;
                        return;
                    }

                    // Line continuation: \\<LF> or \\<LS>/<PS> — consumed, not emitted
                    continue;

                default:
                    if (json == JSONFlavor::kJSON) {
                        switch (c2) {
                        case '"':
                        case '\\':
                        case '/':
                            break;
                        default:
                            out_end = input_start + static_cast<int>(i) - width2;
                            return;
                        }
                    }

                    c = c2;
                    break;
                }
                break;
            }
            }

            AppendUTF16Rune(decoded, c);
        }

        ok = true;
    }

    // Re-scans a close brace '}' as the start of a template literal expression.
    // Called by the parser when it encounters a '}' inside a tagged template and
    // needs to re-lex it as '`' to restart the template literal scanning.
    void Lexer::RescanCloseBraceAsTemplateToken() {
        if (token != T::kCloseBrace) {
            Expected(T::kCloseBrace);
        }

        rescan_close_brace_as_template_token = true;
        code_point = static_cast<int32_t>('`');
        current = end;
        end -= 1;
        Next();
        rescan_close_brace_as_template_token = false;
    }

    // Advances the lexer by one UTF-8 code unit, decoding the next Unicode code point
    // and updating the current position. At end-of-file, sets code_point to -1.
    void Lexer::step() {
        auto [cp, width] = helpers::DecodeRuneInString(std::string_view(source_.contents).substr(static_cast<size_t>(current)));

        // -1 signals end-of-file
        if (width == 0) {
            cp = static_cast<char32_t>(-1);
        }

        // Maintain a line count for source map line-offset table preallocation
        if (cp == U'\n') {
            approximate_newline_count++;
        }

        code_point = static_cast<int32_t>(cp);
        end = current;
        current += width;
    }

    // Reports a lexer error at the given range, suppressing duplicate errors at
    // the same location.
    void Lexer::addRangeError(logger::Range r, const std::string& text) {
        // Avoid duplicate errors at the same source location
        if (r.loc.start == prev_error_loc.start) {
            return;
        }
        prev_error_loc = r.loc;

        if (!is_log_disabled) {
            log_.AddError(&tracker_, r, text);
        }
    }

    // Reports a lexer error with a suggested fix attached to the error message.
    void Lexer::addRangeErrorWithSuggestion(logger::Range r, const std::string& text, const std::string& suggestion) {
        // Avoid duplicate errors at the same source location
        if (r.loc.start == prev_error_loc.start) {
            return;
        }
        prev_error_loc = r.loc;

        if (!is_log_disabled) {
             logger::MsgData data = tracker_.MakeMsgData(r, text);
            if (data.location) {
                data.location->suggestion = suggestion;
            }
            logger::Msg msg;
            msg.kind = logger::MsgKind::kError;
            msg.data = std::move(data);
            log_.add_msg(std::move(msg));
        }
    }

    void Lexer::AddRangeErrorWithNotes(logger::Range r, const std::string& text, const std::vector< logger::MsgData>& notes) {
        // Avoid duplicate errors at the same source location
        if (r.loc.start == prev_error_loc.start) {
            return;
        }
        prev_error_loc = r.loc;

        if (!is_log_disabled) {
            log_.AddErrorWithNotes(&tracker_, r, text, notes);
        }
    }

    // Processes a comment's text to detect special annotations:
    //   - #__PURE__ / @__PURE__: marks expression as side-effect-free
    //   - #__KEY__ / @__KEY__: marks key for minification
    //   - #__NO_SIDE_EFFECTS__ / @__NO_SIDE_EFFECTS__: marks side-effect-free
    //   - @preserve / @license: marks comment for preservation
    //   - @jsx / @jsxFrag / @jsxRuntime / @jsxImportSource: JSX pragmas
    //   - sourceMappingURL: source map URL
    void Lexer::scanCommentText() {
        auto text = std::string_view(source_.contents).substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
        bool has_legal_annotation = text.size() > 2 && text[2] == '!';
        bool is_multi_line_comment = text[1] == '*';
        bool omit_from_general_comment_preservation = false;

        // Record the comment range so symbol minification can exclude it
        all_comments.push_back(Range());

        // Exclude the trailing "*/" from annotation scanning
        size_t end_of_comment_text = text.size();
        if (is_multi_line_comment) {
            end_of_comment_text -= 2;
        }

        for (size_t i = 0; i < text.size(); i++) {
            switch (text[i]) {
            case '#': {
                auto rest = (i + 1 <= end_of_comment_text)
                    ? text.substr(i + 1, end_of_comment_text - (i + 1))
                    : std::string_view{};
                if (HasPrefixWithWordBoundary(rest, "__PURE__")) {
                    omit_from_general_comment_preservation = true;
                    has_comment_before |= CommentBefore::kPure;
                } else if (HasPrefixWithWordBoundary(rest, "__KEY__")) {
                    omit_from_general_comment_preservation = true;
                    has_comment_before |= CommentBefore::kKey;
                } else if (HasPrefixWithWordBoundary(rest, "__NO_SIDE_EFFECTS__")) {
                    omit_from_general_comment_preservation = true;
                    has_comment_before |= CommentBefore::kNoSideEffects;
                } else if (i == 2 && rest.starts_with(" sourceMappingURL=")) {
                    if (auto [arg, found] = ScanForPragmaArg(PragmaArg::kNoSpaceFirst,
                            start + static_cast<int>(i) + 1, " sourceMappingURL=", rest); found) {
                        omit_from_general_comment_preservation = true;
                        source_mapping_url = std::move(arg);
                    }
                }
                break;
            }

            case '@': {
                auto rest = (i + 1 <= end_of_comment_text)
                    ? text.substr(i + 1, end_of_comment_text - (i + 1))
                    : std::string_view{};
                if (HasPrefixWithWordBoundary(rest, "__PURE__")) {
                    omit_from_general_comment_preservation = true;
                    has_comment_before |= CommentBefore::kPure;
                } else if (HasPrefixWithWordBoundary(rest, "__KEY__")) {
                    omit_from_general_comment_preservation = true;
                    has_comment_before |= CommentBefore::kKey;
                } else if (HasPrefixWithWordBoundary(rest, "__NO_SIDE_EFFECTS__")) {
                    omit_from_general_comment_preservation = true;
                    has_comment_before |= CommentBefore::kNoSideEffects;
                } else if (HasPrefixWithWordBoundary(rest, "preserve") ||
                        HasPrefixWithWordBoundary(rest, "license")) {
                    has_legal_annotation = true;
                } else if (HasPrefixWithWordBoundary(rest, "jsx")) {
                    if (auto [arg, found] = ScanForPragmaArg(PragmaArg::kSkipSpaceFirst,
                            start + static_cast<int>(i) + 1, "jsx", rest); found) {
                        jsx_factory_pragma_comment = std::move(arg);
                    }
                } else if (HasPrefixWithWordBoundary(rest, "jsxFrag")) {
                    if (auto [arg, found] = ScanForPragmaArg(PragmaArg::kSkipSpaceFirst,
                            start + static_cast<int>(i) + 1, "jsxFrag", rest); found) {
                        jsx_fragment_pragma_comment = std::move(arg);
                    }
                } else if (HasPrefixWithWordBoundary(rest, "jsxRuntime")) {
                    if (auto [arg, found] = ScanForPragmaArg(PragmaArg::kSkipSpaceFirst,
                            start + static_cast<int>(i) + 1, "jsxRuntime", rest); found) {
                        jsx_runtime_pragma_comment = std::move(arg);
                    }
                } else if (HasPrefixWithWordBoundary(rest, "jsxImportSource")) {
                    if (auto [arg, found] = ScanForPragmaArg(PragmaArg::kSkipSpaceFirst,
                            start + static_cast<int>(i) + 1, "jsxImportSource", rest); found) {
                        jsx_import_source_pragma_comment = std::move(arg);
                    }
                } else if (i == 2 && rest.starts_with(" sourceMappingURL=")) {
                    if (auto [arg, found] = ScanForPragmaArg(PragmaArg::kNoSpaceFirst,
                            start + static_cast<int>(i) + 1, " sourceMappingURL=", rest); found) {
                        omit_from_general_comment_preservation = true;
                        source_mapping_url = std::move(arg);
                    }
                }
                break;
            }
            }
        }

        if (has_legal_annotation) {
            legal_comments_before_token.push_back(Range());
        }

        if (!omit_from_general_comment_preservation) {
            comments_before_token.push_back(Range());
        }
    }






    

}