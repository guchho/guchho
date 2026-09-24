// Guchho's HTML output stage, kept together as a single unit. All of the
// element rewriting, link injection, and serialization work for an HTML entry
// file is performed here by a sequence of focused passes, each of which only
// touches the document tree (or the serialized string) in one specific way.
// The passes borrow shared tree-scanning primitives from the html analysis
// module and shared path/URL math from the helpers module, so this file stays
// focused on HTML specifics rather than reimplementing low-level utilities.

#include "guchho/bundler.hpp"
#include "guchho/html/html_analysis.hpp"
#include "guchho/html/html_printer.hpp"


namespace guchho::bundler {

namespace config = guchho::config;

// helper: obtain a reference to the document's <head> element,
// synthesizing one when the authored markup does not declare it. Synthesis
// happens in two flavors depending on whether a <body> exists: with a body,
// the fabricated <head> is slotted immediately ahead of it (which mirrors the
// order browsers would build anyway); without one, the new <head> is appended
// as the last child of the root container. Several downstream passes
// (resource-hint insertion, nonce meta injection) call back into this helper
// because they have the same requirement: a reliable parent whose first child
// position acts as the insertion point for generated tags.
//
//   input:  a parsed tree whose root has a <body> child but no <head>
//   output: a pointer to the newly created <head>, already inserted directly
//           before that <body>
static html::Node* FindOrCreateHead(html::Node& root) {
    if (html::Node* head = html::FindHead(root)) {
        return head;
    }
    auto new_head = html::CreateElement("head");
    html::Node* head = new_head.get();
    html::Node* body = html::FindBody(root);
    if (body != nullptr) {
        html::InsertBefore(root, std::move(new_head), *body);
    } else {
        html::AppendChild(root, std::move(new_head));
    }
    return head;
}

// Helper: construct a fresh <link> element carrying exactly the relationship
// attribute, the target URL, and an optional empty boolean crossorigin flag.
// Every generated hint link (modulepreload dependency links, stylesheet
// companions, preconnect/dns-prefetch hints) is built through here so all of
// them share the same attribute order and the same empty-value convention for
// the crossorigin flag, which keeps the emitted markup uniform across the
// bundle.
//
//   input:  rel "modulepreload", href "assets/chunk-a.js", crossorigin true
//   output: a <link rel="modulepreload" crossorigin href="..."> node
static std::unique_ptr<html::Node> CreateLink(const std::string& rel,
                                               const std::string& href,
                                               bool crossorigin) {
    std::vector<html::Attribute> attrs;
    html::Attribute rel_attr;
    rel_attr.name = "rel";
    rel_attr.value = rel;
    attrs.push_back(std::move(rel_attr));
    if (crossorigin) {
        html::Attribute cr_attr;
        cr_attr.name = "crossorigin";
        cr_attr.value = "";
        attrs.push_back(std::move(cr_attr));
    }
    html::Attribute href_attr;
    href_attr.name = "href";
    href_attr.value = href;
    attrs.push_back(std::move(href_attr));
    return html::CreateElement("link", html::NS::kHtml, std::move(attrs));
}

// Thin passthrough onto the shared helpers implementation of relative path
// resolution. Both arguments are expressed relative to the output base using
// forward slashes, and the result is computed purely from those strings by
// the canonical helper so that every user of the site's path arithmetic gets
// the same answer. This local wrapper exists only so the rewriting passes can
// read like a natural-language call rather than reaching into the helpers
// namespace at each site.
//
//   input:  from_file "pages/home/index.html", to_file "assets/site.css"
//   output: "../../assets/site.css"
static std::string MakeRelativePath(const std::string& from_file,
                                     const std::string& to_file) {
    return helpers::MakeRelativePath(from_file, to_file);
}

// Helper: normalize a relative resource path into the "safe relative URL"
// spelling Guchho emits throughout rewritten HTML. When the path already
// begins with "../", or with a single "." directory, or with a root "/", it
// is already unambiguous and stays as-is; otherwise the "./" prefix is added
// so the reference is explicitly anchored to the current directory instead of
// relying on a bare filename. This keeps browsers from resolving a rewritten
// href against a different base than the one intended.
//
//   input:  "assets/app.js"        output: "./assets/app.js"
//   input:  "../lib/util.js"       output: "../lib/util.js"
static std::string ToPrefixedRelPath(const std::string& rel) {
    return helpers::AddDotSlashPrefix(rel);
}

// helper: decide whether the PublicPath setting is strong enough to
// override the default relative URLs. An empty string or the literal "./"
// both mean "no base configured", in which case the page keeps its naturally
// derived relative resource references rather than prefixing anything.
//
//   input:  "https://cdn.guchho.example/ex"   output: true
//   input:  "./"                              output: false
static bool IsPublicPathConfigured(std::string_view public_path) {
    return helpers::IsPublicPathConfigured(public_path);
}

// helper: stitch a configured PublicPath together with an output-
// relative resource path to build the final URL used inside the generated
// HTML. The joining logic lives in the shared helpers module so that the same
// treatment of trailing slashes and empty segments applies to CSS, scripts,
// and images alike; this wrapper gives the HTML passes a short local name.
//
//   input:  public_path "https://cdn.guchho.example", rel_path "assets/x.js"
//   output: "https://cdn.guchho.example/assets/x.js"
static std::string ApplyPublicPath(std::string_view public_path,
                                    const std::string& rel_path) {
    return helpers::JoinPublicPath(public_path, rel_path);
}

// Helper: sanitize chunk text so it is safe to embed verbatim inside an
// inline <script>/<style> body. Three substrings would otherwise terminate
// the enclosure early ("<!--", "</script", "</style"), so each one is
// rewritten into the identical characters expressed as a JavaScript unicode
// escape, which the browser's own parser later re-expands to the original
// bytes. The scan is a straightforward left-to-right pass because every
// interesting pattern begins at a '<'; matched bytes are consumed together
// with the escape that replaces them so nothing is counted twice (keeping the
// trailing "t" of "script"/"style" as the next loop iteration's input, which
// deliberately leaves the enclosure unterminated).
//
//   input:  "</script>alert(1)</script>"
//   output: "\u003c/scriptt>alert(1)\u003c/scriptt>"
static std::string EscapeChunkForInlineHTML(std::string_view contents) {
    std::string result;
    result.reserve(contents.size());
    for (size_t i = 0; i < contents.size(); i++) {
        if (contents.substr(i, 4) == "<!--") {
            result += "\\u003c!--";
            i += 3;
        } else if (i + 7 <= contents.size() && contents.substr(i, 7) == "</scrip") {
            result += "\\u003c/script";
            i += 7;
        } else if (i + 7 <= contents.size() && contents.substr(i, 7) == "</style") {
            result += "\\u003c/style";
            i += 6;
        } else {
            result.push_back(contents[i]);
        }
    }
    return result;
}

// Helper: set the text payload of an element to a single text node holding
// "code", discarding whatever children it previously had. When "code" is
// empty the element is left with no children at all, which is how the inline
// pass empties out the redundant members of a merged script/style run while
// still preserving the element shell and its attributes.
//
//   input:  element holding "old", code "export { x }"
//   output: element with a single text child "export { x }"
static void InjectElementText(html::Node& element, const std::string& code) {
    element.child_nodes.clear();
    if (!code.empty()) {
        html::AppendChild(element, html::CreateTextNode(code));
    }
}

// Tracks a surgical, in-place edit to a URL that lives in the middle of an
// attribute value rather than occupying the whole value. This is how
// style-attribute url(...) references and srcset/imagesrcset candidates are
// rewritten: only the byte span described by "offset" and "old_len" within
// the attribute is swapped out for "replacement", leaving surrounding
// spacing, descriptors, and punctuation intact.
struct AttrUrlRewrite {
    html::Node* element;
    std::string_view attr_name;
    uint32_t offset;
    uint32_t old_len;
    std::string replacement;
};

// helper: produce the Subresource Integrity digest string for a
// chunk's final emitted bytes. The digest algorithm is chosen by the "sha256",
// "sha384", or "sha512" spelling; anything else is rejected up front with an
// empty string returned. The result is the canonical "algorithm-base64digest"
// form browsers check against the integrity attribute.
//
//   input:  contents "body{}", algorithm "sha384"
//   output: "sha384-<base64digest>"
static std::string ComputeSRI(std::string_view contents,
                               const std::string& algorithm) {
    std::vector<uint8_t> digest;
    if (algorithm == "sha256") {
        digest = helpers::Sha256(contents);
    } else if (algorithm == "sha384") {
        digest = helpers::Sha384(contents);
    } else if (algorithm == "sha512") {
        digest = helpers::Sha512(contents);
    } else {
        return {};
    }
    return algorithm + "-" +
           helpers::Base64StdEncode(std::string_view(
               reinterpret_cast<const char*>(digest.data()), digest.size()));
}

// helper: reduce an https:// URL to just its origin (scheme plus
// host), which is the granularity the preconnect/dns-prefetch hints operate
// at -- the browser opens one connection per origin, so all resources on the
// same host are served by that single early connection. Non-https URLs
// (http/relative/protocol-relative/data) return nothing because they either
// share the page's own origin or cannot be hinted safely.
//
//   input:  "https://fonts.example.com/css2?fam=serif"
//   output: "https://fonts.example.com"
static std::string ExtractExternalOrigin(std::string_view url) {
    if (url.rfind("https://", 0) != 0) {
        return std::string{};
    }
    const size_t host_end = url.find('/', 8);
    if (host_end == std::string_view::npos) {
        return std::string(url);
    }
    return std::string(url.substr(0, host_end));
}

// helper: normalize a resolved output path into the stable lookup
// key used to locate it in the SRI contents table. Paths arrive here in
// slightly inconsistent shapes (occasionally dotted with "./", sometimes with
// doubled slashes), so this routine strips a leading "./", consumes any run
// of slashes that follows it, and collapses interior duplicate slash/backslash
// runs into one forward slash. The cleaned form then matches the keys under
// which the hashed file contents are stored.
//
//   input:  ".///assets//app.js"     output: "assets/app.js"
//   input:  ".//assets\\app.js"      output: "assets/app.js"
static std::string HashLookupKey(std::string_view path) {
    size_t i = 0;
    if (path.size() >= 2 && path[0] == '.' && path[1] == '/') {
        i = 2;
    }
    while (i < path.size() && (path[i] == '/' || path[i] == '\\')) {
        i++;
    }
    std::string out;
    for (; i < path.size(); i++) {
        const char c = path[i];
        if (c == '/' || c == '\\') {
            if (!out.empty() && out.back() != '/') {
                out.push_back('/');
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

//, the central rewriting pass. It walks every import record attached
// to the HTML grammar and redirects the owning element's resource attribute
// (href, src, srcset candidate, or inline style url) to the bundled output
// path for that record. Records that resolved to external sites, data URLs,
// or nothing at all are skipped entirely. A "guchho-ignore" attribute on the
// element also suppresses rewriting, and non-stylesheet <link> elements are
// left alone unless they carry an imagesrcset (the preload case).
//
// Script records take their destination from the per-record output path when
// available so an inlined facade resolves to its actual dependency chunk;
// other records consult the shared record-to-path map. PublicPath, when
// configured, becomes the final URL prefix; otherwise a relative reference is
// derived from the HTML file's own output location and given the "./" prefix
// as needed. Module scripts additionally receive an empty crossorigin
// attribute (matching Guchho's modulepreload treatment so browsers fetch them
// with the credentials mode preloading requires).
//
// SRI work is folded into the same loop: for bundled script/stylesheet
// resources whose emitted file is present in the SRI table, the integrity
// hash is computed and injected along with the crossorigin attribute browsers
// demand before honoring integrity. When resource hints are enabled and the
// pointer is supplied, unique external https origins are also collected so
//
// Sub-URL edits for style/srcset attributes are deferred: they accumulate in
// a list during the loop and are applied only once every record has been
// visited, in reverse byte order, so an earlier replacement never invalidates
// the offsets recorded for a later one.
//
//   input:  <img src="images/logo.png"> whose record maps to assets/logo.png
//   output: <img src="./assets/logo.png"> (HTML located one directory up)
void PassResourceRewrite(html::AST& ast, const HTMLOutputContext& ctx,
                          std::vector<std::string>* external_origins) {
    const std::unordered_map<uint32_t, std::string>& rel_paths =
        *ctx.record_source_to_rel_path;
    const std::string& html_output_rel_path = ctx.html_output_rel_path;
    const std::vector<HTMLScriptInfo>& script_info = *ctx.script_info;
    const std::string& public_path = ctx.public_path;
    const config::ResourceHintsConfig& resource_hints = ctx.resource_hints;
    const std::string& sri_algorithm = ctx.sri_algorithm;
    const std::unordered_map<std::string, std::string>& sri_rel_contents =
        *ctx.sri_rel_contents;
    const std::vector<compiler::ImportRecord>& records = ast.import_records;
    // Edits whose target is a slice of an attribute value (url() fragments
    // inside style, or individual srcset candidates) are parked here while the
    // loop runs; the final flush below processes them from the tail backwards
    // so each write leaves earlier recorded offsets untouched and valid.
    std::vector<AttrUrlRewrite> style_rewrites;
    // The integrity feature does nothing when no hash algorithm was picked,
    // so the whole SRI lookup machinery is conveniently skipped in that case.
    const bool sri_enabled = !sri_algorithm.empty();
    const bool collect_origins =
        external_origins != nullptr &&
        (resource_hints.enabled &&
         (resource_hints.preconnect || resource_hints.dns_prefetch));
    for (size_t i = 0; i < records.size() && i < ast.record_origins.size(); i++) {
        const html::ImportRecordOrigin& origin = ast.record_origins[i];
        if (!records[i].source_index.IsValid()) {
            // Even a record with no usable output can still feed the origin
            // collector, as long as it is an https URL hosted elsewhere and
            // not a script/style tag (whose connections browsers handle
            // themselves without any hint assistance).
            if (collect_origins && origin.element != nullptr &&
                records[i].kind == compiler::ImportKind::kUrl &&
                origin.element->tag_name != "script" &&
                origin.element->tag_name != "style") {
                std::string origin_str =
                    ExtractExternalOrigin(records[i].path.text);
                if (!origin_str.empty() &&
                    std::find(external_origins->begin(), external_origins->end(),
                              origin_str) == external_origins->end()) {
                    external_origins->push_back(std::move(origin_str));
                }
            }
            continue; // External, data, or failed to resolve: untouched.
        }
        // An element explicitly marked with the ignore attribute keeps its
        // original reference verbatim, so the record sitting on it must be
        // left alone regardless of how it was produced.
        if (html::FindAttrValue(*origin.element, "guchho-ignore") != nullptr) {
            continue;
        }
        // <link> tags that are not stylesheets would lose their meaning if
        // rewritten, with one exception: a preload carrying an "imagesrcset"
        // whose candidate is itself a bundled asset.
        if (origin.element->tag_name == "link" && !html::IsStylesheetLink(*origin.element) &&
            html::FindAttrValue(*origin.element, "imagesrcset") == nullptr) {
            continue;
        }
        // Scripts consult the richer per-script table first: an import-only
        // facade record there points at the actual chunk it re-exports from,
        // while every other resource type falls back to the generic
        // record-to-rel-path mapping built during analysis.
        std::string resolved_rel;
        if (origin.element->tag_name == "script" &&
            i < script_info.size() && !script_info[i].output_rel_path.empty()) {
            resolved_rel = script_info[i].output_rel_path;
        } else {
            auto rel_iter = rel_paths.find(records[i].source_index.GetIndex());
            if (rel_iter == rel_paths.end()) {
                continue;
            }
            resolved_rel = rel_iter->second;
        }
        // PublicPath, when active, wins outright and produces an
        // absolute or CDN-style reference; otherwise the URL is derived
        // relative to this HTML file's own output location.
        std::string url;
        if (IsPublicPathConfigured(public_path)) {
            url = ApplyPublicPath(public_path, resolved_rel);
        } else {
            url = MakeRelativePath(html_output_rel_path, resolved_rel);
            if (url.rfind("../", 0) != 0 && url != ".." && url != "." &&
                !url.starts_with('/')) {
                url = "./" + url;
            }
        }

        html::Node* element = const_cast<html::Node*>(origin.element);
        if (origin.value_length != 0) {
            // The reference is embedded inside a larger value (a style url()
            // or a srcset candidate), so only the recorded slice is replaced;
            // the variants and spacing around it survive untouched.
            style_rewrites.push_back(AttrUrlRewrite{
                element, origin.attr_name, origin.value_offset,
                origin.value_length, std::move(url)});
        } else {
            html::SetAttrValue(*element, origin.attr_name, url);
        }

        // Module scripts must be fetched with "same-origin" credentials, which
        // translates into an empty crossorigin attribute; without it browsers
        // may refuse to preload the script via a modulepreload link.
        if (element->tag_name == "script" &&
            (html::IsModuleScript(*element) ||
             (i < script_info.size() && script_info[i].is_module))) {
            html::AddAttributeIfMissing(*element, "crossorigin", "");
        }

        // hash the emitted bytes of local script/stylesheet outputs
        // and pin both integrity and crossorigin onto their elements. Outputs
        // absent from the contents table (inlined runs, unresolved or external
        // records) are simply ignored.
        if (sri_enabled &&
            (element->tag_name == "script" || html::IsStylesheetLink(*element))) {
            auto sri_iter =
                sri_rel_contents.find(HashLookupKey(resolved_rel));
            if (sri_iter != sri_rel_contents.end()) {
                std::string integrity =
                    ComputeSRI(sri_iter->second, sri_algorithm);
                if (!integrity.empty()) {
                    html::AddAttributeIfMissing(*element, "integrity", integrity);
                    html::AddAttributeIfMissing(*element, "crossorigin", "");
                }
            }
        }
    }

    // Flush the deferred slice edits in descending offset order: replacing a
    // later-valued portion first guarantees the offsets of earlier (smaller)
    // ones remain correct, since none of the later writes change their span.
    std::sort(style_rewrites.begin(), style_rewrites.end(),
              [](const AttrUrlRewrite& a, const AttrUrlRewrite& b) {
                  return a.offset > b.offset;
              });
    for (const AttrUrlRewrite& rw : style_rewrites) {
        for (html::Attribute& attr : rw.element->attrs) {
            if (attr.name == rw.attr_name) {
                attr.value.replace(rw.offset, rw.old_len, rw.replacement);
                break;
            }
        }
    }
}

// Helper: perform %KEY% substitution on a single string in place. Every
// %-delimited token whose inner key is found in define_map is replaced by the
// mapped value; unknown keys, lone percent signs, and empty definitions are
// handled permissively (unknown keys are left untouched so no content is
// silently eaten, and an empty mapped value simply yields an empty
// replacement). Matching is case-sensitive, and because '%' is a single-byte
// ASCII character the byte-oriented scan below can never split a multi-byte
// UTF-8 character.
//
//   input:  text "__THEME__&__NOPE__", define_map {__THEME__: "dark"}
//   output: "dark&__NOPE__"
static void ReplaceEnvVariablesInString(
    std::string& text,
    const std::unordered_map<std::string, std::string>& define_map) {
    if (define_map.empty()) {
        return;
    }
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        if (text[i] == '%') {
            size_t close = text.find('%', i + 1);
            if (close != std::string::npos) {
                std::string_view key(text.data() + i + 1, close - i - 1);
                auto entry = define_map.find(std::string(key));
                if (entry != define_map.end()) {
                    result.append(entry->second);
                    i = close + 1;
                    continue;
                }
            }
        }
        result.push_back(text[i]);
        i++;
    }
    text = std::move(result);
}

// Helper: apply %KEY% substitution recursively across the subtree rooted at
// "node". Attribute values and text nodes are rewritten; the contents of
// <script>/<style> elements are deliberately skipped because those payloads
// travel through the JS/CSS pipelines and are not subject to the HTML-level
// define map, and <template> contents are visited through their separate
// fragment so shadow-like markup inside templates is covered as well.
static void ReplaceEnvVariables(
    html::Node& node,
    const std::unordered_map<std::string, std::string>& define_map) {
    if (html::IsElementNode(node)) {
        for (html::Attribute& attr : node.attrs) {
            ReplaceEnvVariablesInString(attr.value, define_map);
        }
        if (node.tag_name == "script" || node.tag_name == "style") {
            return;
        }
        if (html::IsTemplateElement(node)) {
            ReplaceEnvVariables(html::GetTemplateContent(node), define_map);
            return;
        }
        for (auto& child : node.child_nodes) {
            ReplaceEnvVariables(*child, define_map);
        }
        return;
    }
    if (html::IsTextNode(node)) {
        ReplaceEnvVariablesInString(node.value, define_map);
        return;
    }
    // Non-text, non-element nodes (document, fragment, comment, doctype) hold
    // no replaceable text, but recursion still descends into their children
    // (and their template fragments) so nothing is left unvisited.
    for (auto& child : node.child_nodes) {
        ReplaceEnvVariables(*child, define_map);
    }
}

// run the document-wide environment-variable substitution. The
// define map supplied through the context dictates which %KEY% spellings are
// recognized; this stage runs first so rewritten references are computed from
// already-substituted values.
void PassEnvSubstitution(html::AST& ast, const HTMLOutputContext& ctx) {
    const std::unordered_map<std::string, std::string>& define_map =
        *ctx.define_map;
    ReplaceEnvVariables(*ast.node, define_map);
}

// (injection): splice the bundled inline runs into the document tree.
// Every inline run identifies one chunk and the list of element members that
// must share it; the first member receives the (escaped) full chunk text and
// the remaining members are emptied so the code exists exactly once. Chunks
// missing from the lookup produce an empty string, which still satisfies the
// placement contract while emitting nothing.
//
//   input:  js run {chunk c1, members [<script>, <script>]}
//   output: first member inlined with c1's escaped text, second member emptied
void PassInlineRuns(const HTMLOutputContext& ctx) {
    const graph::HtmlInlineInfo& inline_info = *ctx.inline_info;
    const std::unordered_map<uint32_t, std::string>& chunk_texts =
        *ctx.chunk_texts;
    auto inject = [&](const std::vector<graph::HtmlInlineSegment>& segments) {
        for (const graph::HtmlInlineSegment& segment : segments) {
            const auto text_iter = chunk_texts.find(segment.chunk_source_index);
            std::string code;
            if (text_iter != chunk_texts.end()) {
                code = EscapeChunkForInlineHTML(text_iter->second);
            }
            for (size_t i = 0; i < segment.members.size(); i++) {
                if (segment.members[i] == nullptr) {
                    continue;
                }
                html::Node* member = const_cast<html::Node*>(segment.members[i]);
                // The bundle lands in the run's head; the follower members are
                // hollowed out so the payload is rendered exactly once.
                InjectElementText(*member, i == 0 ? code : "");
            }
        }
    };
    inject(inline_info.js_segments);
    inject(inline_info.css_segments);
}

// (head links): create the <link> tags that keep external module
// scripts and their split output "warm" in the browser. Every module <script>
// on the page that resolved to bundled output contributes a modulepreload
// link for each of its dependency chunks (so the browser starts fetching the
// whole graph up front) and, when that script's entry also emitted a CSS
// file, a companion stylesheet link referencing it. Both kinds are emitted
// with deduplication, so a chunk shared by several scripts is only preloaded
// once. All generated links are prepended to <head>, creating the <head> if
// the document does not have one.
//
//   input:  <script type="module" src="..." data-chunks="assets/c1.js">,
//           entry.css mapping to assets/entry.css
//   output: <head><link rel="modulepreload" ...><link rel="stylesheet" ...>
//           ...</head>
void PassInjectHeadLinks(html::AST& ast, const HTMLOutputContext& ctx) {
    html::Node& root = *ast.node;
    const std::vector<HTMLScriptInfo>& script_info = *ctx.script_info;
    const std::string& html_output_rel_path = ctx.html_output_rel_path;
    const std::string& public_path = ctx.public_path;
    if (script_info.empty()) {
        return;
    }

    // Dupes are checked per link kind; the same asset legitimately appears
    // once as a modulepreload and once as a stylesheet companion.
    std::vector<std::string> preload_hrefs;
    std::vector<std::string> stylesheet_hrefs;
    std::vector<std::unique_ptr<html::Node>> links_to_add;

    const size_t record_count = ast.import_records.size();
    const size_t info_count = std::min(script_info.size(), record_count);
    for (size_t i = 0; i < info_count; i++) {
        const HTMLScriptInfo& info = script_info[i];
        if (!info.is_module) {
            continue;
        }
        // Each dependency chunk that this module needs is advertised with a
        // modulepreload link so the browser can speculatively fetch it.
        for (const std::string& dep : info.dependency_preloads) {
            std::string url;
            if (IsPublicPathConfigured(public_path)) {
                url = ApplyPublicPath(public_path, dep);
            } else {
                url = ToPrefixedRelPath(
                    MakeRelativePath(html_output_rel_path, dep));
            }
            if (std::find(preload_hrefs.begin(), preload_hrefs.end(), url) !=
                preload_hrefs.end()) {
                continue;
            }
            preload_hrefs.push_back(url);
            links_to_add.push_back(CreateLink("modulepreload", url, true));
        }
        // If the module's entry also produced a stylesheet, that stylesheet
        // is linked in here as well so the browser applies it alongside the
        // script rather than waiting for a manual import.
        if (!info.associated_css_rel_path.empty()) {
            std::string url;
            if (IsPublicPathConfigured(public_path)) {
                url = ApplyPublicPath(public_path, info.associated_css_rel_path);
            } else {
                url = ToPrefixedRelPath(
                    MakeRelativePath(html_output_rel_path,
                                    info.associated_css_rel_path));
            }
            if (std::find(stylesheet_hrefs.begin(), stylesheet_hrefs.end(), url) ==
                stylesheet_hrefs.end()) {
                stylesheet_hrefs.push_back(url);
                links_to_add.push_back(CreateLink("stylesheet", url, false));
            }
        }
    }

    if (links_to_add.empty()) {
        return;
    }

    // Place every generated link as the first child of <head> (mirror Guchho's
    // general head-prepend behavior), manufacturing a <head> when absent.
    html::Node* head = FindOrCreateHead(root);
    // Prepending each element before the current front child, iterating in
    // reverse, preserves the original build order in the final document.
    for (auto it = links_to_add.rbegin(); it != links_to_add.rend(); ++it) {
        if (!head->child_nodes.empty()) {
            html::InsertBefore(*head, std::move(*it), *head->child_nodes.front());
        } else {
            html::AppendChild(*head, std::move(*it));
        }
    }
}

// ASCII-only lowercase helpers: the single-character form maps A..Z to a..z
// with no locale involvement (so behavior is identical on every host machine)
// and no heap allocation for the common one-byte case.
static char AsciiLowerChar(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

static std::string AsciiLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = AsciiLowerChar(c);
    }
    return out;
}

// helper: turn a font URL that was written relative to its containing
// stylesheet into an output-base-relative path. The stylesheet's own directory
// is the starting point; "." segments are dropped, ".." walks up one level
// (and stops at the top), and a URL beginning with "/" replaces the anchor
// entirely because it is already root-relative. A leading "../" chain popping
// past the output base yields "." as a defensive final value.
//
//   input:  css_output_rel_path "assets/styles/main.css",
//           font_url "../fonts/demo.woff2"
//   output: "assets/fonts/demo.woff2"
static std::string ResolveCssAssetRelPath(const std::string& css_output_rel_path,
                                           std::string_view font_url) {
    std::vector<std::string> css_parts = helpers::SplitPathSegments(css_output_rel_path);
    if (!css_parts.empty()) {
        css_parts.pop_back(); // The stylesheet file itself; keep its directory.
    }
    if (!font_url.empty() && font_url[0] == '/') {
        css_parts.clear();
    }
    for (const std::string& seg : helpers::SplitPathSegments(font_url)) {
        if (seg == "..") {
            if (!css_parts.empty()) {
                css_parts.pop_back();
            }
        } else if (seg != "/") {
            css_parts.push_back(seg);
        }
    }
    std::string result;
    for (size_t i = 0; i < css_parts.size(); i++) {
        if (i != 0) {
            result += '/';
        }
        result += css_parts[i];
    }
    return result.empty() ? "." : result;
}

// helper: locate the locally hosted fonts that a stylesheet declares.
// Each @font-face rule is inspected for its src: declaration, and the first
// url(...) payload in that declaration is kept if it names a local file
// (data: URLs are excluded) whose extension is one of woff2/woff/ttf/otf --
// eot is deliberately ignored because it almost always requires a "?#iefix"
// fragment that would not map to a real asset file. Matching is done
// case-insensitively on a lowercased mirror of the source while the reported
// URL text is copied from the original casing-preserving buffer, so emitted
// URLs keep the author's original spelling.
//
//   input:  "@font-face { src: url('./fonts/hero.woff2') format('woff2'); }"
//   output: "./fonts/hero.woff2"
static std::vector<std::string> ExtractFontFaceUrls(const std::string& css) {
    std::vector<std::string> fonts;
    std::string lowered = AsciiLower(css);
    size_t pos = 0;
    while (true) {
        const size_t kw = lowered.find("@font-face", pos);
        if (kw == std::string::npos) {
            break;
        }
        const size_t brace = lowered.find('{', kw);
        if (brace == std::string::npos) {
            break;
        }
        size_t depth = 1;
        size_t end = brace + 1;
        while (end < css.size() && depth > 0) {
            if (lowered[end] == '{') {
                depth++;
            } else if (lowered[end] == '}') {
                depth--;
            }
            end++;
        }
        const std::string_view block = std::string_view(lowered).substr(
            brace + 1, end - brace - 2);
        // Both views are byte-identical in structure, so the keyword scan can
        // safely run against the lowercased mirror while URL extraction reads
        // the same positions from the pristine source buffer.
        const std::string_view css_block = std::string_view(css).substr(
            brace + 1, end - brace - 2);
        // Look for the rule's "src:" descriptor and, inside it, the very first
        // url( opening; the payload that follows is the candidate font file.
        const size_t src = block.find("src");
        if (src != std::string_view::npos) {
            const size_t url_kw = block.find("url(", src);
            if (url_kw != std::string_view::npos) {
                size_t i = url_kw + 4;
                if (i < block.size() && (block[i] == '"' || block[i] == '\'')) {
                    const char quote = block[i];
                    i++;
                    const size_t quote_end = block.find(quote, i);
                    if (quote_end != std::string_view::npos) {
                        fonts.emplace_back(css_block.substr(i, quote_end - i));
                    }
                } else {
                    const size_t paren = block.find(')', i);
                    if (paren != std::string_view::npos) {
                        size_t j = i;
                        while (j < paren && (block[j] == ' ' || block[j] == '\t' ||
                                             block[j] == '\n' || block[j] == '\r')) {
                            j++;
                        }
                        size_t k = paren;
                        while (k > j && (block[k - 1] == ' ' || block[k - 1] == '\t' ||
                                         block[k - 1] == '\n' ||
                                         block[k - 1] == '\r')) {
                            k--;
                        }
                        fonts.emplace_back(css_block.substr(j, k - j));
                    }
                }
            }
        }
        pos = end;
    }
    // Only true local font files with a recognized extension survive the cut;
    // data: URLs and unknown/fragmented extensions fall away here.
    std::vector<std::string> result;
    for (const std::string& url : fonts) {
        if (helpers::IsDataURL(url)) {
            continue;
        }
        const size_t query = url.find_first_of("?#");
        const std::string_view path = std::string_view(url).substr(
            0, query == std::string::npos ? url.size() : query);
        const size_t slash = path.rfind('/');
        const std::string_view name = path.substr(slash == std::string_view::npos
                                                       ? 0
                                                       : slash + 1);
        const size_t dot = name.rfind('.');
        if (dot == std::string_view::npos) {
            continue;
        }
        const std::string ext = AsciiLower(name.substr(dot + 1));
        if (ext != "woff2" && ext != "woff" && ext != "ttf" && ext != "otf") {
            continue;
        }
        result.push_back(url);
    }
    return result;
}

// emit the resource-hint <link> tags for a page. Two batches are
// generated based on what the config enables. The first batch produces
// preconnect and/or dns-prefetch links for each distinct external origin that
// the rewrite pass discovered, letting the browser open outbound connections
// before those resources are actually fetched. The second batch produces
// preload (or prefetch) links for every @font-face font declared by the CSS
// chunks, computing each font's final URL, its MIME type from the extension,
// and the required `as`/`crossorigin` attributes. All built links are
// prepended to <head>, in reverse build order, so the final document order
// matches the construction order; a missing <head> is created on demand.
//
//   input:  external_origins ["https://fonts.guchho.example"],
//           css fonts ["./fonts/hero.woff2"], config {preconnect, fonts+preload}
//   output: <head><link rel="preconnect" ...><link rel="preload" as="font"
//           type="font/woff2" ...> ...</head>
void PassResourceHints(html::AST& ast, const HTMLOutputContext& ctx,
                        const std::vector<std::string>& external_origins) {
    html::Node& root = *ast.node;
    const std::string& html_output_rel_path = ctx.html_output_rel_path;
    const std::string& public_path = ctx.public_path;
    const config::ResourceHintsConfig& config = ctx.resource_hints;
    const std::vector<HTMLCssContent>& css_contents = *ctx.css_contents;
    if (!config.enabled) {
        return;
    }

    std::vector<std::string> seen_hrefs;
    std::vector<std::unique_ptr<html::Node>> links_to_add;

    // First group: one preconnect/dns-prefetch pair per distinct fed origin.
    if (config.preconnect || config.dns_prefetch) {
        for (const std::string& origin : external_origins) {
            if (config.preconnect) {
                links_to_add.push_back(CreateLink("preconnect", origin, true));
            }
            if (config.dns_prefetch) {
                links_to_add.push_back(CreateLink("dns-prefetch", origin, false));
            }
        }
    }

    // Second group: a preload/prefetch hint for every @font-face local font.
    if (config.fonts && (config.preload || config.prefetch)) {
        const std::string rel = config.preload ? "preload" : "prefetch";
        for (const HTMLCssContent& css : css_contents) {
            for (const std::string& font_url :
                 ExtractFontFaceUrls(css.contents)) {
                const std::string asset_rel = ResolveCssAssetRelPath(
                    css.css_output_rel_path, font_url);
                std::string href;
                if (IsPublicPathConfigured(public_path)) {
                    href = ApplyPublicPath(public_path, asset_rel);
                } else {
                    href = ToPrefixedRelPath(
                        MakeRelativePath(html_output_rel_path, asset_rel));
                }
                if (std::find(seen_hrefs.begin(), seen_hrefs.end(), href) !=
                    seen_hrefs.end()) {
                    continue;
                }
                seen_hrefs.push_back(href);

                const size_t query = href.find_first_of("?#");
                const std::string_view path = std::string_view(href).substr(
                    0, query == std::string::npos ? href.size() : query);
                const size_t slash = path.rfind('/');
                const std::string_view name = path.substr(
                    slash == std::string_view::npos ? 0 : slash + 1);
                const size_t dot = name.rfind('.');
                const std::string ext = dot == std::string_view::npos
                                            ? ""
                                            : std::string(name.substr(dot + 1));

                std::vector<html::Attribute> attrs;
                auto add_attr = [&](const std::string& n, const std::string& v) {
                    html::Attribute attr;
                    attr.name = n;
                    attr.value = v;
                    attrs.push_back(std::move(attr));
                };
                add_attr("rel", rel);
                add_attr("as", "font");
                add_attr("type", "font/" + ext);
                add_attr("crossorigin", "");
                add_attr("href", href);
                links_to_add.push_back(
                    html::CreateElement("link", html::NS::kHtml,
                                        std::move(attrs)));
            }
        }
    }

    if (links_to_add.empty()) {
        return;
    }

    html::Node* head = FindOrCreateHead(root);
    for (auto it = links_to_add.rbegin(); it != links_to_add.rend(); ++it) {
        if (!head->child_nodes.empty()) {
            html::InsertBefore(*head, std::move(*it), *head->child_nodes.front());
        } else {
            html::AppendChild(*head, std::move(*it));
        }
    }
}

// Helper: delete every "guchho-ignore" attribute from the subtree rooted at
// "node". The attribute exists purely as an opt-out marker for the rewrite
// passes, so it must never be serialized; this routine removes it from
// elements, their children, and template fragments, skipping descent into
// script/style payloads the way the marker-consumption scan does.
static void StripIgnoreAttribute(html::Node& node) {
    if (html::IsElementNode(node)) {
        node.attrs.erase(std::remove_if(node.attrs.begin(), node.attrs.end(),
                                        [](const html::Attribute& attr) {
                                            return attr.name == "guchho-ignore";
                                        }),
                         node.attrs.end());
        if (node.tag_name == "script" || node.tag_name == "style") {
            return;
        }
        if (html::IsTemplateElement(node)) {
            StripIgnoreAttribute(html::GetTemplateContent(node));
            return;
        }
    }
    for (auto& child : node.child_nodes) {
        StripIgnoreAttribute(*child);
    }
}

// (cleanup): scrub the now-redundant "guchho-ignore" attributes from
// the finished tree. This runs after every pass that consults the marker, so
// the document that reaches the printer is free of the attribute entirely.
void PassStripGuchhoIgnore(html::AST& ast, const HTMLOutputContext&) {
    StripIgnoreAttribute(*ast.node);
}

// helper: attach a nonce attribute of the given value to every CSP-
// relevant element in the subtree under "node" - script/style elements and
// the CSP-relevant link variants (as recognized by html::LinkRelIsCspRelevant).
// Elements that already carry a nonce keep their own value, since overwriting
// a hand-authored nonce would break the very policy this pass is meant to
// support. Template content is handled through its fragment.
static void InjectCspNonceAttributes(html::Node& node, const std::string& nonce) {
    if (html::IsElementNode(node)) {
        bool is_nonce_target =
            node.tag_name == "script" || node.tag_name == "style" ||
            html::LinkRelIsCspRelevant(node);
        if (is_nonce_target && html::FindAttrValue(node, "nonce") == nullptr) {
            html::Attribute nonce_attr;
            nonce_attr.name = "nonce";
            nonce_attr.value = nonce;
            node.attrs.push_back(std::move(nonce_attr));
        }
        if (html::IsTemplateElement(node)) {
            InjectCspNonceAttributes(html::GetTemplateContent(node), nonce);
            return;
        }
    }
    for (auto& child : node.child_nodes) {
        InjectCspNonceAttributes(*child, nonce);
    }
}

// add CSP-nonce support to the document. When the page does not
// already declare the marker meta tag, a <meta property="csp-nonce"
// nonce="..."> element is prepended inside <head> (created on demand), and
// then every script/style/CSP-link element receives the nonce attribute via
// InjectCspNonceAttributes. The pass never synthesizes a policy header - it
// only annotates elements with the nonce so an externally supplied policy can
// trust them. An empty nonce disables the stage outright.
static void InjectCspNonce(html::Node& root, const std::string& nonce) {
    if (nonce.empty()) {
        return;
    }

    if (!html::HasCspNonceMeta(root)) {
        html::Node* head = FindOrCreateHead(root);
        auto meta = html::CreateElement("meta");
        html::Attribute property;
        property.name = "property";
        property.value = "csp-nonce";
        meta->attrs.push_back(std::move(property));
        html::Attribute nonce_attr;
        nonce_attr.name = "nonce";
        nonce_attr.value = nonce;
        meta->attrs.push_back(std::move(nonce_attr));
        if (head->child_nodes.empty()) {
            html::AppendChild(*head, std::move(meta));
        } else {
            html::InsertBefore(*head, std::move(meta), *head->child_nodes.front());
        }
    }

    InjectCspNonceAttributes(root, nonce);
}

// pipeline entry: invoke the nonce-injection helpers against the
// document root. Scheduled after rewriting/injection so that both original
// and generated script/style/link elements receive the nonce.
void PassCspNonce(html::AST& ast, const HTMLOutputContext& ctx) {
    InjectCspNonce(*ast.node, ctx.csp_nonce);
}

// guarantee that every <script type="importmap"> precedes the first
// module script / modulepreload link in the document. Browsers begin
// speculative module fetching as soon as they see the first module marker, so
// an import map that appears any later would race the module resolution and
// effectively be ignored. The reordering only relocates maps; their contents
// and relative order are preserved, and maps that already sit ahead of the
// pivot are left untouched. Returns whether any move happened, and - when a
// destination range pointer is provided - records the tag byte range of the
// first relocated map there (used by the reporting stage). The underlying
// analysis (finding the pivot and the maps) is performed by html::ScanImportMaps.
//
//   input:  <script type="module" src="app.js"></script>
//           <script type="importmap">{...}</script>
//   output: map moved before the module script; function returns true
bool PassImportMapReorder(html::AST& ast, const HTMLOutputContext& ctx) {
    html::Node& root = *ast.node;
    const logger::Source* html_source = &ctx.html_file->source;
    logger::Range* first_moved_range = ctx.import_map_reordered_range;
    html::ImportMapScan scan = html::ScanImportMaps(root);
    if (scan.pivot == nullptr || scan.maps.empty() ||
        scan.pivot_preorder < 0) {
        return false;
    }
    // No work needed when every map already sits above the pivot.
    if (scan.map_preorder.back() < scan.pivot_preorder) {
        return false;
    }
    html::Node* pivot_parent = html::GetParentNode(*scan.pivot);
    if (pivot_parent == nullptr) {
        return false;
    }
    // Relocate each trailing map immediately before the pivot; anchoring them
    // to the pivot itself (rather than to whichever map was moved previously)
    // preserves the maps' mutual ordering through successive insertions.
    for (size_t i = 0; i < scan.maps.size(); i++) {
        if (scan.map_preorder[i] >= scan.pivot_preorder) {
            if (first_moved_range != nullptr && first_moved_range->len == 0 &&
                html_source != nullptr) {
                *first_moved_range =
                    html::RangeOfTag(*html_source, *scan.maps[i]);
            }
            auto map_holder = html::DetachNode(*scan.maps[i]);
            html::InsertBefore(*pivot_parent, std::move(map_holder),
                               *scan.pivot);
        }
    }
    return true;
}

// helper: HTML-escape a plugin-supplied attribute value for safe
// embedding between quotes in a serialized tag. Ampersands, double quotes, and
// angle brackets are the only characters rewritten; everything else (single
// quotes, whitespace) is passed through byte-for-byte.
//
//   input:  `a&"b<c>d`
//   output: `a&amp;&quot;b&lt;c&gt;d`
static std::string EscapeAttributeValue(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '"':  out += "&quot;"; break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

// helper: render one plugin-provided tag descriptor into its
// serialized HTML string. Attributes are ordered by name so the output is
// deterministic regardless of the unordered collection the plugin used, and
// values are escaped via EscapeAttributeValue. Void elements with no
// children are rendered without a closing tag (and without self-closing
// syntax), matching how the browser parses them; every other element gets
// explicit open and close tags.
//
//   input:  descriptor {tag "meta", attrs {content: "x", name: "viewport"}}
//   output: `<meta content="x" name="viewport">`
static std::string HtmlTagDescriptorToString(
    const config::HtmlTagDescriptor& descriptor) {
    // HTML void elements never get a closing tag.
    static const char* void_elements[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr",
    };
    const bool is_void =
        descriptor.children.empty() &&
        std::find(std::begin(void_elements), std::end(void_elements),
                  descriptor.tag) != std::end(void_elements);

    std::string out;
    out.reserve(32 + descriptor.children.size());
    out.push_back('<');
    out += descriptor.tag;
    std::vector<std::string> attr_names;
    attr_names.reserve(descriptor.attrs.size());
    for (const auto& [name, value] : descriptor.attrs) {
        attr_names.push_back(name);
    }
    std::sort(attr_names.begin(), attr_names.end());
    for (const std::string& name : attr_names) {
        out.push_back(' ');
        out += name;
        const std::string& value = descriptor.attrs.at(name);
        if (!value.empty()) {
            out += "=\"";
            out += EscapeAttributeValue(value);
            out.push_back('"');
        }
    }
    if (is_void) {
        out.push_back('>');
        return out;
    }
    out.push_back('>');
    out += descriptor.children;
    out += "</";
    out += descriptor.tag;
    out.push_back('>');
    return out;
}

// helper: find the byte offset of the first opening "<name" tag in
// the built page string, where "name" must be followed by a tag-start
// boundary character ('>', whitespace, '/', or newline) so that a
// length-prefixed element like "<header" is never mistaken for "<head".
// Returns npos when no such standalone tag exists.
//
//   input:  html "<head><title>x</title></head>", name "head"
//   output: 0
static size_t FindOpeningTagStart(std::string_view html, std::string_view name) {
    std::string needle = "<";
    needle += name;
    size_t pos = 0;
    while ((pos = html.find(needle, pos)) != std::string::npos) {
        const size_t after = pos + needle.size();
        if (after >= html.size()) {
            return std::string::npos;
        }
        const char c = html[after];
        if (c == '>' || c == ' ' || c == '\t' || c == '/' || c == '\n') {
            return pos;
        }
        pos = after;
    }
    return std::string::npos;
}

// helper: byte offset just past the '>' that closes the first opening
// "<name>" tag, i.e. the position where prepended content must be inserted.
// Returns npos if the tag is not found (mirroring FindOpeningTagStart's
// contract).
//
//   input:  html "<head></head>", name "head"      output: 6
//   input:  html "<body></body>", name "head"      output: npos
static size_t PastOpeningTag(std::string_view html, std::string_view name) {
    const size_t start = FindOpeningTagStart(html, name);
    if (start == std::string::npos) {
        return std::string::npos;
    }
    const size_t gt = html.find('>', start);
    if (gt == std::string::npos) {
        return std::string::npos;
    }
    return gt + 1;
}

// helper: byte offset of the first "</name>" closing tag, matched
// only when it is a genuine name match (the same boundary check applies, so
// "</header>" never matches "head"). Returns npos when absent.
//
//   input:  html "<head></head>", name "head"       output: 6
//   input:  html "</head>", name "head"             output: 0
static size_t FindClosingTagStart(std::string_view html, std::string_view name) {
    std::string needle = "</";
    needle += name;
    size_t pos = 0;
    while ((pos = html.find(needle, pos)) != std::string::npos) {
        const size_t after = pos + needle.size();
        if (after >= html.size()) {
            return std::string::npos;
        }
        const char c = html[after];
        if (c == '>' || c == ' ' || c == '\t' || c == '/') {
            return pos;
        }
        pos = after;
    }
    return std::string::npos;
}

// helper: splice an already-serialized chunk of HTML ("tag_html")
// into "html" at the boundary the plugin requested. Prepending variants
// insert after the boundary's opening tag; append variants insert ahead of
// its closing tag. When the target section does not exist at all, a minimal
// wrapper (<head>...</head> or <body>...</body>) is synthesized around the
// snippet and dropped at the nearest sensible anchor (before <body>, before
// the root, or at the end). Used to honor transformIndexHtml tag injections.
//
//   input:  html "<html><head></head></html>", tag_html "<meta name='x'>",
//           where kHeadPrepend
//   output: "<html><head><meta name='x'></head></html>"
static void InjectTagIntoString(std::string& html, const std::string& tag_html,
                                 config::HtmlTagDescriptor::InjectTo where) {
    switch (where) {
        case config::HtmlTagDescriptor::kHeadPrepend: {
            const size_t pos = PastOpeningTag(html, "head");
            if (pos != std::string::npos) {
                html.insert(pos, tag_html);
                return;
            }
            [[fallthrough]];
        }
        case config::HtmlTagDescriptor::kHead: {
            const size_t pos = FindClosingTagStart(html, "head");
            if (pos != std::string::npos) {
                html.insert(pos, tag_html);
                return;
            }
            const std::string wrapped = "<head>" + tag_html + "</head>";
            const size_t body = FindOpeningTagStart(html, "body");
            if (body != std::string::npos) {
                html.insert(body, wrapped);
            } else {
                html += wrapped;
            }
            return;
        }
        case config::HtmlTagDescriptor::kBodyPrepend: {
            const size_t pos = PastOpeningTag(html, "body");
            if (pos != std::string::npos) {
                html.insert(pos, tag_html);
                return;
            }
            [[fallthrough]];
        }
        case config::HtmlTagDescriptor::kBody: {
            const size_t pos = FindClosingTagStart(html, "body");
            if (pos != std::string::npos) {
                html.insert(pos, tag_html);
                return;
            }
            const std::string wrapped = "<body>" + tag_html + "</body>";
            const size_t html_tag = PastOpeningTag(html, "html");
            if (html_tag != std::string::npos) {
                html.insert(html_tag, wrapped);
            } else {
                html += wrapped;
            }
            return;
        }
    }
}

// helper: map an injection location back to the section name ("head"
// or "body") it refers to, used when phrasing warnings about tags placed into
// the wrong section.
static std::string_view InjectLocationName(
    config::HtmlTagDescriptor::InjectTo where) {
    switch (where) {
        case config::HtmlTagDescriptor::kHead:
        case config::HtmlTagDescriptor::kHeadPrepend:
            return "head";
        case config::HtmlTagDescriptor::kBody:
        case config::HtmlTagDescriptor::kBodyPrepend:
            return "body";
    }
    return "head";
}

//: drive the registered transformIndexHtml hooks over the serialized
// page, in plugin registration order, and hand back the final HTML text. A
// hook returning a plain string replaces the whole document with that string;
// a hook returning a list of tag descriptors has each descriptor serialized
// (via HtmlTagDescriptorToString) and injected at its requested position (via
// InjectTagIntoString). A tag destined for <head> that is not one of the few
// head-appropriate tags generates a warning appended to the context's warning
// vector instead of failing the build, and an exception thrown by a hook is
// likewise captured as a warning.
//
//   input:  html "<head></head>", plugin returns tag meta {name: "theme-color"}
//   output: "<head><meta content=\"#fff\" name=\"theme-color\"></head>"
std::string PassApplyTransforms(std::string html, const HTMLOutputContext& ctx) {
    const graph::InputFile& html_file = *ctx.html_file;
    const std::vector<config::Plugin>& plugins = *ctx.plugins;
    std::vector<std::string>* warnings = ctx.transform_warnings;
    if (plugins.empty()) {
        return html;
    }
    config::HtmlTransformContext transform_ctx;
    transform_ctx.is_build = true;
    const guchho::logger::PrettyPaths& pretty = html_file.source.pretty_paths;
    transform_ctx.filename = pretty.rel.empty() ? pretty.abs : pretty.rel;

    for (const config::Plugin& plugin : plugins) {
        if (!plugin.TransformIndexHtml) {
            continue;
        }
        std::variant<std::string, std::vector<config::HtmlTagDescriptor>> result;
        try {
            result = plugin.TransformIndexHtml(html, transform_ctx);
        } catch (const std::exception& e) {
            if (warnings != nullptr) {
                warnings->push_back(
                    "transformIndexHtml for plugin \"" + plugin.Name +
                    "\" threw: " + e.what());
            }
            continue;
        } catch (...) {
            if (warnings != nullptr) {
                warnings->push_back(
                    "transformIndexHtml for plugin \"" + plugin.Name +
                    "\" threw an unknown exception");
            }
            continue;
        }
        if (std::holds_alternative<std::string>(result)) {
            html = std::move(std::get<std::string>(result));
            continue;
        }
        std::vector<config::HtmlTagDescriptor>& descriptors =
            std::get<std::vector<config::HtmlTagDescriptor>>(result);
        for (const config::HtmlTagDescriptor& descriptor : descriptors) {
            const std::string_view location = InjectLocationName(descriptor.inject_to);
            if (location == "head" && descriptor.tag != "head" &&
                descriptor.tag != "meta" && descriptor.tag != "link" &&
                descriptor.tag != "script" && descriptor.tag != "style" &&
                descriptor.tag != "base" && descriptor.tag != "title" &&
                descriptor.tag != "noscript" && descriptor.tag != "template")
            {
                if (warnings != nullptr) {
                    warnings->push_back(
                        "\"" + descriptor.tag + "\" element injected into <head> "
                        "by plugin \"" + plugin.Name + "\"");
                }
            }
            InjectTagIntoString(html, HtmlTagDescriptorToString(descriptor),
                                descriptor.inject_to);
        }
    }
    return html;
}

// apply the configured CSS loading strategy to every local
// stylesheet <link> in the document. Under the non-blocking strategy each
// link is converted into a preload: its rel becomes "preload" with as="style"
// and an onload handler that flips rel back to "stylesheet" once the sheet
// finishes loading - so rendering is not blocked by the CSS fetch while the
// eventual stylesheet still applies. Original attributes are preserved except
// rel/as/onload which are regenerated, and a <noscript> fallback link is
// placed right after the converted element with the original attributes so
// non-JavaScript clients still receive the stylesheet.
//
//   input:  <link rel="stylesheet" href="./assets/app.css">
//   output: <link rel="preload" href="./assets/app.css" as="style"
//            onload="this.rel='stylesheet'">
//           <noscript><link rel="stylesheet" href="./assets/app.css"></noscript>
void PassCSSLoadingStrategy(html::AST& ast, const HTMLOutputContext& ctx) {
    html::Node& root = *ast.node;
    config::CSSLoadingStrategy strategy = ctx.css_loading;
    if (strategy != config::CSSLoadingStrategy::kNonBlocking) {
        return;
    }
    std::vector<html::Node*> links;
    html::CollectLocalStylesheetLinks(root, links);
    for (html::Node* link : links) {
        const std::vector<html::Attribute> original_attrs = link->attrs;
        std::vector<html::Attribute> preload_attrs;
        std::string href;
        for (const html::Attribute& attr : original_attrs) {
            if (attr.name == "href") {
                href = attr.value;
            }
            // rel is replaced first; as/onload are added after the original
            // attributes so href keeps its place right after rel.
            if (attr.name != "rel" && attr.name != "as" && attr.name != "onload") {
                preload_attrs.push_back(attr);
            }
        }
        if (href.empty()) {
            continue;
        }
        html::Attribute rel;
        rel.name = "rel";
        rel.value = "preload";
        preload_attrs.insert(preload_attrs.begin(), std::move(rel));
        html::Attribute as;
        as.name = "as";
        as.value = "style";
        preload_attrs.push_back(std::move(as));
        html::Attribute onload;
        onload.name = "onload";
        onload.value = "this.rel='stylesheet'";
        preload_attrs.push_back(std::move(onload));
        link->attrs = std::move(preload_attrs);

        // The <noscript> fallback is a plain stylesheet link carrying the
        // original attributes (rel, href, integrity, crossorigin, ...).
        std::vector<html::Attribute> fallback_attrs;
        for (const html::Attribute& attr : original_attrs) {
            if (attr.name != "as" && attr.name != "onload") {
                fallback_attrs.push_back(attr);
            }
        }
        auto fallback_link =
            html::CreateElement("link", html::NS::kHtml, std::move(fallback_attrs));
        auto noscript = html::CreateElement("noscript");
        html::AppendChild(*noscript, std::move(fallback_link));

        html::Node* parent = link->parent_node;
        if (parent == nullptr) {
            continue;
        }
        auto it = std::find_if(
            parent->child_nodes.begin(), parent->child_nodes.end(),
            [link](const std::unique_ptr<html::Node>& n) {
                return n.get() == link;
            });
        if (it == parent->child_nodes.end()) {
            continue;
        }
        auto next = std::next(it);
        if (next == parent->child_nodes.end()) {
            html::AppendChild(*parent, std::move(noscript));
        } else {
            html::InsertBefore(*parent, std::move(noscript), **next);
        }
    }
}


std::string GenerateHTMLOutput(const HTMLOutputContext& ctx) {
    const graph::InputFile* html_file = ctx.html_file;
    auto* html_repr_ptr =
        std::get_if<std::shared_ptr<graph::HTMLRepr>>(&html_file->repr);
    if (html_repr_ptr == nullptr || *html_repr_ptr == nullptr) {
        return html_file->source.contents;
    }
    graph::HTMLRepr& repr = **html_repr_ptr;
    html::AST& ast = repr.ast;

    std::vector<std::string> external_origins;
    PassEnvSubstitution(ast, ctx);
    PassResourceRewrite(ast, ctx, &external_origins);
    PassInlineRuns(ctx);
    PassInjectHeadLinks(ast, ctx);
    // prepend preconnect/dns-prefetch/font-preload hints ahead of the
    // modulepreload/stylesheet links.
    PassResourceHints(ast, ctx, external_origins);
    // apply the CSS loading strategy to the local stylesheet links
    // (original and JS-imported ones injected just above).
    PassCSSLoadingStrategy(ast, ctx);
    // reorder import maps before module scripts/preloads.
    bool reordered = PassImportMapReorder(ast, ctx);
    if (ctx.import_maps_reordered != nullptr) {
        *ctx.import_maps_reordered = reordered;
    }
    // Strip the guchho-ignore markers last so the attribute is never emitted
    // and the rewriting/injection passes still see it for their guards.
    PassStripGuchhoIgnore(ast, ctx);
    // nonce injection runs after rewriting/injection so that both the
    // original and the generated script/style/link elements are annotated.
    PassCspNonce(ast, ctx);

    html::PrinterOptions options;
    options.pretty_print = ctx.pretty_print;
    options.minify = ctx.minify;
    //: run transformIndexHtml hooks last, on the serialized document.
    return PassApplyTransforms(html::Print(*ast.node, options), ctx);
}

} // namespace guchho::bundler