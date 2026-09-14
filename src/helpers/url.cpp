#include "guchho/helpers.hpp"

namespace guchho::helpers {

    // Guchho's URL subsystem — a self-contained RFC 3986 implementation used
    // throughout the bundler for asset rewriting, source-map "sources" entries,
    // CSS url() resolution, and file:// <-> native path conversion. Only the
    // five components Guchho needs are modelled: scheme, host (authority
    // without userinfo), path, query, and fragment. Path, query, and fragment
    // are kept in their percent-encoded form so reserializing with
    // URL::String is always lossless. Reference resolution follows RFC 3986
    // Section 5.2.2 and dot-segment removal follows Section 5.2.4, ensuring
    // relative specifiers inside imported files resolve identically to how
    // browsers resolve them.

    namespace {

        // IsSchemeChar — predicate for characters allowed inside a URI scheme
        // name (RFC 3986 Section 3.1): ASCII letters, digits, '+', '-', '.'.
        // TrySplitScheme calls this while scanning to determine whether a
        // "scheme:" prefix is present. The check here is intentionally
        // permissive about the first character; enforcing that the scheme
        // begins with a letter is TrySplitScheme's responsibility.
        // ':', '/', '?', '#', and any non-ASCII byte always return false.
        // Example: IsSchemeChar('a') -> true,  IsSchemeChar('+') -> true
        //          IsSchemeChar('/') -> false, IsSchemeChar(':') -> false
        bool IsSchemeChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
        }

        // TrySplitScheme — extract a scheme prefix from the front of a raw URL
        // string. Guchho relies on this to distinguish absolute URIs
        // ("https://example.com/app.js") from bare relative paths
        // ("assets/img.png"). The function scans byte-by-byte looking for ':'
        // or a character that cannot appear in a scheme. A valid scheme is
        // returned only when a ':' is found, the text before it is non-empty,
        // begins with an ASCII letter, and every preceding byte passes
        // IsSchemeChar. Any violation produces an empty view ("no scheme").
        //
        // Edge cases:
        //   - A leading digit ("123abc:foo") fails the first-char letter test.
        //   - A bare string without ':' ("no-colon") falls through the loop.
        //   - An immediate ':' (":rest") fails the i == 0 guard.
        //
        // Example: TrySplitScheme("https://example.com/foo") -> "https"
        //          TrySplitScheme("a+b-c.d:rest")            -> "a+b-c.d"
        //          TrySplitScheme("123abc:foo")               -> {} (digit start)
        //          TrySplitScheme("no-colon")                 -> {}
        std::string_view TrySplitScheme(std::string_view s)
        {
            for (size_t i = 0; i < s.size(); ++i) {
                char c = s[i];
                if (c == ':') {
                    if (i == 0) {
                        return {};
                    }
                    std::string_view scheme = s.substr(0, i);
                    char first = scheme[0];
                    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))) {
                        return {};
                    }
                    return scheme;
                }
                if (!IsSchemeChar(c)) {
                    return {};
                }
            }
            return {};
        }

        // ResolvePath — merge a base path with a reference path, then strip
        // dot segments. This is the RFC 3986 Section 5.2.4 normalization that
        // backs URL::ResolveReference whenever a source-map "sources" entry,
        // CSS import, or asset reference is made absolute against a base path.
        //
        // Merge rules (RFC 3986 Section 5.2.2):
        //   - Empty ref → base is kept unchanged.
        //   - Ref not starting with '/' → appended after base's last '/'.
        //   - Ref starting with '/' → replaces base entirely.
        //
        // Dot-segment removal walks the merged path element by element:
        //   - "." is silently consumed.
        //   - ".." pops the most recently pushed segment (if any).
        //   - A trailing "." or ".." forces a trailing '/' so directory
        //     semantics are preserved in the output.
        //   - A leading "//" is returned verbatim so protocol-relative
        //     references survive normalization.
        //
        // Example: ResolvePath("/a/b/c", "d")       -> "/a/b/d"
        //          ResolvePath("/a/b/", "../x")       -> "/a/x"
        //          ResolvePath("/a/b", "/abs/path")   -> "/abs/path"
        //          ResolvePath("/a/b/c", "../../..")   -> "/"
        //          ResolvePath("/a/b", "//cdn/x.js")  -> "//cdn/x.js" (verbatim)
        std::string ResolvePath(const std::string& base, const std::string& ref)
        {
            std::string full;
            if (ref.empty()) {
                full = base;
            } else if (ref[0] != '/') {
                size_t slash = base.rfind('/');
                full          = slash == std::string::npos ? ref : base.substr(0, slash + 1) + ref;
            } else {
                full = ref;
            }
            if (full.empty()) {
                return "";
            }

            std::string dst = "/";
            bool       first = true;
            std::string remaining = full.substr(full[0] == '/' ? 1 : 0);
            std::string elem;

            while (!remaining.empty() || first) {
                size_t slash = remaining.find('/');
                if (slash == std::string::npos) {
                    elem      = remaining;
                    remaining.clear();
                } else {
                    elem      = remaining.substr(0, slash);
                    remaining = remaining.substr(slash + 1);
                }

                if (elem == ".") {
                } else if (elem == "..") {
                    if (dst.size() > 1) {
                        size_t prev = dst.rfind('/', dst.size() - 2);
                        if (prev == std::string::npos) {
                            dst = "/";
                            first = true;
                        } else {
                            dst.resize(prev + 1);
                        }
                    }
                } else {
                    if (!first && dst.back() != '/') {
                        dst += '/';
                    }
                    dst += elem;
                    first = false;
                }

                if (remaining.empty()) {
                    break;
                }
            }

            if ((elem == "." || elem == "..") && dst != "/") {
                dst += '/';
            }

            if (full.size() >= 2 && full[0] == '/' && full[1] == '/') {
                return full;
            }
            return dst;
        }

    }

    // URL::String — reassemble the parsed URL components into their textual
    // URI form. Guchho emits this into every output that needs a URL: rewritten
    // import specifiers, source-map "sources" and "sourceRoot", HTML asset
    // attributes, and file:// URLs in diagnostics.
    //
    // Format follows RFC 3986 Section 5.3:
    //   [scheme ":"] ["//" host] path ["?" query] ["#" fragment]
    //
    // Path, query, and fragment are written verbatim because they were kept
    // percent-encoded during parsing, making this round-trip lossless. A '?'
    // separator is emitted whenever either query or fragment is present (even
    // when the query itself is empty), and a '#' is emitted only when
    // fragment is non-empty.
    //
    // Example: {scheme:"https", host:"example.com", path:"/a/b",
    //           query:"x=1", fragment:"top"}.String()
    //          -> "https://example.com/a/b?x=1#top"
    //
    //          {scheme:"", host:"", path:"/local/file.js",
    //           query:"", fragment:""}.String()
    //          -> "/local/file.js"
    std::string URL::String() const
    {
        std::string buf;
        if (!scheme.empty()) {
            buf += scheme;
            buf += ':';
        }
        if (!host.empty()) {
            buf += "//";
            buf += host;
        }
        buf += path;

        if (!query.empty() || !fragment.empty()) {
            buf += '?';
            if (!query.empty()) {
                buf += query;
            }
        }
        if (!fragment.empty()) {
            buf += '#';
            buf += fragment;
        }
        return buf;
    }

    // URL::ResolveReference — resolve a (possibly relative) reference URL
    // against this URL used as the base. This implements RFC 3986 Section
    // 5.2.2 and is the main entry point Guchho uses when a source-map
    // "sources" entry, stylesheet import, or asset reference needs to be made
    // absolute.
    //
    // Resolution algorithm:
    //   1. If the reference has no scheme, inherit the base's scheme.
    //   2. If the reference carries its own scheme or a non-empty host, it is
    //      already absolute — normalize its path with an empty base and keep
    //      its query/fragment verbatim. Host and scheme from the base are
    //      ignored.
    //   3. Otherwise (relative reference) the host comes from the base; the
    //      path is merged via ResolvePath(base.path, ref.path).
    //   4. When ref has an empty path AND an empty query, the base's query is
    //      inherited (this matches the browser behavior for "same-document
    //      references" like "#section"). Fragment inheritance follows the RFC:
    //      the base's fragment survives only when the reference supplies none.
    //
    // Edge cases:
    //   - Empty reference returns the base unchanged.
    //   - A reference like "https://other.com/y?z#q" is returned as-is after
    //     path normalization (its own scheme short-circuits step 2).
    //   - A fragment-only reference ("#new") inherits scheme, host, path,
    //     and query from the base, replacing only the fragment.
    //
    // Example: base "https://example.com/a/b" + ref "c"
    //          -> "https://example.com/a/c"
    //
    //          base "https://example.com/a/b?old=1#old" + ref ""
    //          -> "https://example.com/a/b?old=1#old"
    //
    //          base "https://example.com/a/b" + ref "https://other.com/y?z#q"
    //          -> "https://other.com/y?z#q"
    //
    //          base "https://example.com/a/b" + ref "#sec"
    //          -> "https://example.com/a/b#sec"
    URL URL::ResolveReference(const URL& ref) const
    {
        URL url = ref;

        if (url.scheme.empty()) {
            url.scheme = scheme;
        }

        if (!ref.scheme.empty() || !ref.host.empty()) {
            url.path = ResolvePath(ref.path, "");
            url.query = ref.query;
            url.fragment = ref.fragment;
            return url;
        }

        if (ref.path.empty() && ref.query.empty()) {
            url.query = query;
            if (ref.fragment.empty()) {
                url.fragment = fragment;
            }
        }

        url.host = host;
        url.path = ResolvePath(path, ref.path);
        return url;
    }

    // ParseURL — split a raw URL string into Guchho's URL struct. This parser
    // is lossless: path, query, and fragment remain percent-encoded so
    // URL::String can reconstruct the original text. It handles user-facing
    // paths, source-map "sources" entries, file:// URLs, and URLs found in
    // CSS and HTML.
    //
    // Parsing order (left-to-right peeling):
    //   1. Fragment — everything after the first '#'.
    //   2. Query    — everything after the first '?' in the remainder.
    //   3. Scheme   — TrySplitScheme on what's left; lowercased.
    //   4. Authority — only when the scheme was followed by "//". Any
    //      userinfo (text before the last '@') is discarded because Guchho
    //      only needs the host portion.
    //   5. Path     — whatever remains after authority extraction.
    //
    // Edge cases:
    //   - Relative URLs (no scheme, no "//") produce an empty scheme and host
    //     with the entire string captured as path.
    //   - "file:///tmp/file.js" yields host="" and path="/tmp/file.js"
    //     (triple-slash means empty authority).
    //   - "mailto:user@example.com" yields scheme="mailto" with no host.
    //
    // Example: ParseURL("https://user@example.com/foo?bar=1#sec")
    //          -> {scheme:"https", host:"example.com", path:"/foo",
    //              query:"bar=1", fragment:"sec"}
    //
    //          ParseURL("/assets/img.png")
    //          -> {scheme:"", host:"", path:"/assets/img.png"}
    //
    //          ParseURL("file:///tmp/file.js")
    //          -> {scheme:"file", host:"", path:"/tmp/file.js"}
    std::optional<URL> ParseURL(std::string_view raw_url)
    {
        URL u;

        std::string_view rest = raw_url;

        if (size_t hash = rest.find('#'); hash != std::string_view::npos) {
            u.fragment = std::string(rest.substr(hash + 1));
            rest       = rest.substr(0, hash);
        }

        if (size_t qmark = rest.find('?'); qmark != std::string_view::npos) {
            u.query = std::string(rest.substr(qmark + 1));
            rest    = rest.substr(0, qmark);
        }

        std::string_view scheme = TrySplitScheme(rest);
        if (!scheme.empty()) {
            u.scheme = ToLowerASCII(scheme);
            rest     = rest.substr(scheme.size() + 1);

            if (rest.substr(0, 2) == "//") {
                rest = rest.substr(2);
                size_t end = rest.find_first_of("/?#");
                if (end == std::string_view::npos) {
                    end = rest.size();
                }
                std::string_view authority = rest.substr(0, end);
                rest                       = rest.substr(end);

                if (size_t at = authority.rfind('@'); at != std::string_view::npos) {
                    authority = authority.substr(at + 1);
                }
                u.host = std::string(authority);
            }
        }

        u.path = std::string(rest);
        return u;
    }

    namespace {

        // IsHexDigitASCII — predicate for valid hexadecimal digits used in
        // percent-decoding. Guchho calls this to validate the two characters
        // that follow '%' in sequences like "%2F" before converting them into
        // a single byte value.
        // Example: IsHexDigitASCII('0') -> true,  IsHexDigitASCII('a') -> true
        //          IsHexDigitASCII('F') -> true,  IsHexDigitASCII('G') -> false
        bool IsHexDigitASCII(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        // Unescape — core percent-decoding engine. Converts "%HH" triplets to
        // their raw byte values and, when plus_to_space is set, maps '+' to
        // space. This backs both PathUnescape and QueryUnescape.
        //
        // The function iterates left-to-right with an up-front reserve to avoid
        // repeated allocations:
        //   - A valid "%XX" (two hex digits after '%') is decoded in place.
        //   - A stray '%' not followed by two hex digits yields nullopt — this
        //     signals malformed input and prevents silent data corruption.
        //   - '+' is converted to space only when plus_to_space is true,
        //     respecting the convention that '+' is literal in paths but means
        //     space in query strings.
        //
        // Example: Unescape("%20hello%21", false) -> " hello!"
        //          Unescape("a+b", true)          -> "a b"
        //          Unescape("a+b", false)         -> "a+b"
        //          Unescape("%2G", true)          -> nullopt (invalid hex)
        std::optional<std::string> Unescape(std::string_view s, bool plus_to_space)
        {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size();) {
                if (s[i] == '%' && i + 2 < s.size() &&
                    IsHexDigitASCII(s[i + 1]) && IsHexDigitASCII(s[i + 2])) {
                    auto hex_val = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        return c - 'A' + 10;
                    };
                    out.push_back(static_cast<char>((hex_val(s[i + 1]) << 4) | hex_val(s[i + 2])));
                    i += 3;
                    continue;
                }
                if (s[i] == '%') {
                    return std::nullopt;
                }
                if (plus_to_space && s[i] == '+') {
                    out.push_back(' ');
                    ++i;
                    continue;
                }
                out.push_back(s[i]);
                ++i;
            }
            return out;
        }

    }

    // PathUnescape — decode percent-escapes in a URL path where '+' remains a
    // literal plus sign. Guchho uses this when converting file:// URL paths
    // into native filesystem paths for reading, watching, or emitting in
    // diagnostics. A thin wrapper over Unescape with plus_to_space = false.
    //
    // Example: PathUnescape("/foo%20bar/baz%2Fqux") -> "/foo bar/baz/qux"
    //          PathUnescape("/a+b/c")                -> "/a+b/c" (plus stays)
    //          PathUnescape("/bad%2G")               -> nullopt (invalid escape)
    std::optional<std::string> PathUnescape(std::string_view s)
    {
        return Unescape(s, false);
    }

    // QueryUnescape — decode percent-escapes in a query string where '+'
    // represents a space (application/x-www-form-urlencoded convention). Guchho
    // inspects query strings inside asset URLs and data URLs, so both
    // "hello+world" and "hello%20world" must produce "hello world". Wraps
    // Unescape with plus_to_space = true.
    //
    // Example: QueryUnescape("hello+world%21") -> "hello world!"
    //          QueryUnescape("a%20b+c")        -> "a b c"
    //          QueryUnescape("bad%2G")         -> nullopt
    std::optional<std::string> QueryUnescape(std::string_view s)
    {
        return Unescape(s, true);
    }

    // ClassifyURL — determine the reference type of a raw URL string for the
    // bundler's URL resolver. This mirrors the classification logic used by the
    // alias resolver and the HTML output's local-href test.
    //
    // Categories (checked in order):
    //   - kEmpty           — empty string.
    //   - kFragment        — starts with '#' (e.g. "#section").
    //   - kProtocolRelative — starts with "//" (e.g. "//cdn.example.com/x").
    //   - kRootRelative    — starts with '/' (e.g. "/assets/app.js").
    //   - kAbsolute        — a ':' appears before any '/' or '?', indicating a
    //     scheme-prefixed URL ("https:", "data:", "mailto:", even "C:\foo").
    //   - kRelative        — everything else, including paths with queries
    //     ("app.js?v=2").
    //
    // Only kAbsolute and kProtocolRelative are treated as external references
    // that bypass bundler rewriting in HTML and CSS output.
    //
    // Example: ClassifyURL("")               -> kEmpty
    //          ClassifyURL("#top")            -> kFragment
    //          ClassifyURL("//cdn/x.js")      -> kProtocolRelative
    //          ClassifyURL("/assets/app.js")  -> kRootRelative
    //          ClassifyURL("https://x.com")   -> kAbsolute
    //          ClassifyURL("data:text/html")  -> kAbsolute
    //          ClassifyURL("app.js?v=2")      -> kRelative
    URLKind ClassifyURL(std::string_view raw_url)
    {
        if (raw_url.empty()) {
            return URLKind::kEmpty;
        }
        if (raw_url[0] == '#') {
            return URLKind::kFragment;
        }
        if (raw_url.size() >= 2 && raw_url[0] == '/' && raw_url[1] == '/') {
            return URLKind::kProtocolRelative;
        }
        if (raw_url[0] == '/') {
            return URLKind::kRootRelative;
        }
        for (char c : raw_url) {
            if (c == '/' || c == '?') {
                break;
            }
            if (c == ':') {
                return URLKind::kAbsolute;
            }
        }
        return URLKind::kRelative;
    }

    // IsExternalURL — true for absolute ("scheme:...") and protocol-relative
    // ("//host") URLs. These are never bundled or rewritten by Guchho because
    // they point to resources outside the project scope.
    bool IsExternalURL(std::string_view raw_url)
    {
        const URLKind kind = ClassifyURL(raw_url);
        return kind == URLKind::kAbsolute || kind == URLKind::kProtocolRelative;
    }

    // IsDataURL — true when the URL begins with the "data:" scheme prefix
    // (already lowercased by Guchho's parsing conventions). Guchho uses this to
    // identify inline data URLs in HTML src attributes and CSS url() calls so
    // they can be handled differently from file-backed references.
    bool IsDataURL(std::string_view raw_url)
    {
        return raw_url.rfind("data:", 0) == 0;
    }

} // namespace guchho::helpers
