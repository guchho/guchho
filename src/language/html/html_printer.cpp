// HTML serialization for Guchho.
//
// Walks a parsed HTML tree (see "guchho/html/html_ast.hpp" and
// "guchho/html/html_parser.hpp") back into text. Two output modes are
// supported:
//
//   * minify  - a single compact line; text whitespace is collapsed where the
//               browser would ignore it, quotes are dropped where legal, and
//               non-conditional comments are removed.
//   * pretty  - indented, readable markup; block children go on their own
//               lines while code and literal text content stays intact.
//
// Every function in the anonymous namespace below is a pure scalar walk: no
// output state is kept between calls, so the same source element can be
// serialized any number of times with identical results.

#include "guchho/html/html_printer.hpp"

#include <algorithm>
#include <array>
#include <string_view>


namespace guchho::html {

namespace {

// HTML "void" elements. The HTML syntax forbids closing tags and content for
// these tags, so the serializer returns immediately after their start tag and
// never looks for children (the parser does not attach any).
constexpr std::array<std::string_view, 18> kVoidElements = {
    "area",   "base",  "basefont", "bgsound", "br",    "col",
    "embed",  "frame", "hr",       "img",     "input", "keygen",
    "link",   "meta",  "param",    "source",  "track", "wbr",
};

// True when "node" is an HTML element whose tag is void (see kVoidElements).
// A void element printed at the top level of a document produces nothing, so
// "Print" also consults this predicate.
bool IsVoidElement(const Node& node) {
    return IsElementNode(node) && node.namespace_uri == NS::kHtml &&
           std::find(kVoidElements.begin(), kVoidElements.end(), node.tag_name) !=
               kVoidElements.end();
}

// Escapes run-of-the-mill text content for safe inclusion in the output.
// Replaces "&" with "&amp;", "<" with "&lt;", ">" with "&gt;" and the
// no-break space U+00A0 (in its two-byte UTF-8 form) with "&nbsp;".
//
// Example: EscapeText("a < b & c")     -> "a &lt; b &amp; c"
// Example: EscapeText("x c2 a0 y")     -> "x&nbsp;y"   (bytes 0xc2 0xa0)
//
// The no-break space is matched on its raw bytes so byte-oriented input needs
// no decoding. A leading 0xc2 not followed by 0xa0 (a real "Â") is copied
// through unchanged.
std::string EscapeText(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        switch (text[i]) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case static_cast<char>(0xc2):
                if (i + 1 < text.size() &&
                    static_cast<unsigned char>(text[i + 1]) == 0xa0) {
                    out += "&nbsp;";
                    i++;
                } else {
                    out += text[i];
                }
                break;
            default:
                out += text[i];
                break;
        }
    }
    return out;
}

// Escapes an attribute value for placement between double quotes. Handles
// "&", '"' and U+00A0. "<" and ">" are deliberately left alone: a quoted
// attribute value does not require them to be escaped.
//
// Example: EscapeAttribute("a=\"b\"&c") -> "a=&quot;b&quot;&amp;c"
std::string EscapeAttribute(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        switch (text[i]) {
            case '&':
                out += "&amp;";
                break;
            case '"':
                out += "&quot;";
                break;
            case static_cast<char>(0xc2):
                if (i + 1 < text.size() &&
                    static_cast<unsigned char>(text[i + 1]) == 0xa0) {
                    out += "&nbsp;";
                    i++;
                } else {
                    out += text[i];
                }
                break;
            default:
                out += text[i];
                break;
        }
    }
    return out;
}

// Decides whether an attribute value may be serialized without surrounding
// quotes in minify mode. Unquoted values are legal only when the value is
// non-empty and contains none of the characters the HTML tokenizer treats as
// a value delimiter: whitespace, both quote characters,  "=", "<", ">",
// backtick and NUL.
//
// Example: CanDropAttributeQuotes("foo") -> true
// Example: CanDropAttributeQuotes("a b") -> false
// Example: CanDropAttributeQuotes("")    -> false (empty must stay "=\"\"")
bool CanDropAttributeQuotes(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    for (char c : value) {
        if (c == ' ' || c == '"' || c == '\'' || c == '`' || c == '=' ||
            c == '<' || c == '>' || c == '\t' || c == '\n' || c == '\r' ||
            c == '\f' || c == '\v' || c == '\0') {
            return false;
        }
    }
    return true;
}

// Renders the full attribute list of an element into a single
// leading-space-separated string ready to follow the tag name.
//
// Namespace prefixes are emitted with their fixed XML spellings ("xml:",
// "xmlns:", "xlink:"); the bare xmlns name stays "xmlns". Any other
// namespaced attribute falls back to "prefix:name".
//
// In minify mode an empty value drops its "=\"\"", and a value that passes
// CanDropAttributeQuotes is written unquoted.
//
// Example (pretty): <img src="app.js" alt="" disabled> yields
//   " src=\"app.js\" alt=\"\" disabled=\"\""
// Example (minify): the same element yields
//   " src=app.js alt disabled"
std::string SerializeAttributes(const Node& element,
                                const PrinterOptions& options) {
    std::string html;
    for (const Attribute& attr : element.attrs) {
        html += ' ';

        if (attr.has_prefix) {
            switch (attr.ns) {
                case NS::kXml: {
                    html += "xml:";
                    break;
                }
                case NS::kXmlns: {
                    if (attr.name != "xmlns") {
                        html += "xmlns:";
                    }
                    break;
                }
                case NS::kXlink: {
                    html += "xlink:";
                    break;
                }
                default: {
                    html += attr.prefix;
                    html += ':';
                    break;
                }
            }
        }

        html += attr.name;
        if (attr.value.empty()) {
            if (!options.minify) {
                html += "=\"\"";
            }
        } else if (options.minify &&
                   CanDropAttributeQuotes(attr.value)) {
            html += '=';
            html += attr.value;
        } else {
            html += "=\"";
            html += EscapeAttribute(attr.value);
            html += '"';
        }
    }

    return html;
}

std::string SerializeNode(const Node& node, const PrinterOptions& options,
                          int depth);

// Builds the indentation prefix for a nesting level. Pretty output uses two
// spaces per level, matching the tree depth while printing.
//
// Example: Indent(2) -> "    "
std::string Indent(int depth) {
    return std::string(static_cast<size_t>(depth) * 2, ' ');
}

// Resolves the container whose children should actually be serialized for an
// element. <template> holds its markup in an inert content fragment, so its
// children come from GetTemplateContent; every other element returns itself.
//
// Example: for <template><p>x</p></template>, SerializationContainer yields
//          the template's content fragment (child: <p>), not the template.
const Node& SerializationContainer(const Node& parent_node) {
    if (IsTemplateElement(parent_node) && parent_node.namespace_uri == NS::kHtml) {
        return GetTemplateContent(parent_node);
    }
    return parent_node;
}

// True for elements whose text is embedded, whitespace-insensitive code
// (JavaScript in <script>, CSS in <style>). Their lines may be re-indented in
// pretty mode without changing meaning.
bool IsCodePreformattedElement(const Node& node) {
    if (!IsElementNode(node)) {
        return false;
    }
    const std::string_view tn = node.tag_name;
    return tn == "script" || tn == "style";
}

// True for elements whose whitespace is semantically meaningful and must be
// preserved verbatim: <pre>, <textarea> and <xmp>. Rewriting their
// whitespace would change the rendered output.
bool IsLiteralPreformattedElement(const Node& node) {
    if (!IsElementNode(node)) {
        return false;
    }
    const std::string_view tn = node.tag_name;
    return tn == "pre" || tn == "textarea" || tn == "xmp";
}

// True for every element whose text must never be re-indented while pretty
// printing: both code-like (<script>, <style>) and literal (<pre>,
// <textarea>, <xmp>) content. The two predicates are combined here so every
// serializer checks a single helper.
bool IsPreformattedElement(const Node& node) {
    return IsCodePreformattedElement(node) || IsLiteralPreformattedElement(node);
}

// True for HTML elements whose text is never whitespace-collapsed during
// minification: every preformatted element plus <listing>. <title> is
// deliberately absent because browsers collapse its displayed whitespace
// anyway, so compact output matches what the user sees.
bool IsMinifyProtectedElement(const Node& node) {
    if (!IsElementNode(node) || node.namespace_uri != NS::kHtml) {
        return false;
    }
    return IsPreformattedElement(node) || node.tag_name == "listing";
}

// True when the tag name belongs to the HTML elements whose default display
// is block-level. Used by the minifier to decide whether a text node's
// leading/trailing whitespace is render-insignificant (and therefore safe to
// trim) because it sits next to a block boundary.
bool IsBlockLevelLike(std::string_view tag_name) {
    static const char* kTags[] = {
        "address","article","aside","blockquote","body","caption","center",
        "dd","details","dialog","dir","div","dl","dt","fieldset","figcaption",
        "figure","footer","form","h1","h2","h3","h4","h5","h6","head","header",
        "hgroup","html","li","main","menu","nav","ol","p","pre","section",
        "summary","table","tbody","td","tfoot","th","thead","tr","ul",
    };
    for (const char* t : kTags) {
        if (tag_name == t) {
            return true;
        }
    }
    return false;
}

// Reports whether the node, or any of its ancestors, carries an inline
// "style" attribute whose "white-space" declaration is a pre-like keyword.
// pre, pre-wrap, pre-line and break-spaces all start with "pre" and preserve
// whitespace in the browser, so matching the start of the first property
// token is sufficient; "white-space: nowrap" stays excluded.
//
// Example: <div style="white-space: pre"><p>  x</p></div> -> true for the
//          <p> and its text, so their whitespace is preserved verbatim.
bool PreservesWhitespaceByStyle(const Node& node) {
    for (const Node* n = node.parent_node; n != nullptr;
         n = n->parent_node) {
        if (!IsElementNode(*n) || n->namespace_uri != NS::kHtml) {
            continue;
        }
        for (const Attribute& attr : n->attrs) {
            if (attr.name != "style") {
                continue;
            }
            const std::string_view sv(attr.value);
            size_t pos = sv.find("white-space");
            if (pos == std::string_view::npos) {
                continue;
            }
            pos += 11;
            if (pos < sv.size() && sv[pos] == ':') {
                ++pos;
            }
            while (pos < sv.size() &&
                   (sv[pos] == ' ' || sv[pos] == '\t')) {
                ++pos;
            }
            size_t tok_start = pos;
            while (pos < sv.size() && sv[pos] != ';' &&
                   sv[pos] != ' ' && sv[pos] != '\t') {
                ++pos;
            }
            std::string_view tok = sv.substr(tok_start, pos - tok_start);
            if (tok.find("pre") != std::string_view::npos) {
                return true;
            }
        }
    }
    return false;
}

// True for the ASCII whitespace set: space, tab, LF, CR, FF and VT.
bool IsAsciiWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

// Collapses any run of ASCII whitespace to a single space. With
// trim_leading/trim_trailing set, the whitespace at that edge is dropped
// entirely rather than replaced. The output is empty only when the input is
// pure whitespace and both edges are trimmed.
//
// Example: MinifyText("a   b \n  c", false, true)  -> "a b c"
// Example: MinifyText("  a  ",      true,  true)   -> "a"
// Example: MinifyText(" \n\t",      true,  true)   -> ""
std::string MinifyText(std::string_view raw, bool trim_leading,
                       bool trim_trailing) {
    std::string out;
    out.reserve(raw.size());
    bool pending_space = false;
    bool first_non_ws = true;
    for (char c : raw) {
        if (IsAsciiWhitespace(c)) {
            pending_space = true;
            continue;
        }
        if (first_non_ws && trim_leading) {
            pending_space = false;
        }
        if (pending_space) {
            out.push_back(' ');
        }
        pending_space = false;
        first_non_ws = false;
        out.push_back(c);
    }
    if (pending_space && !trim_trailing) {
        if (!out.empty() || !trim_leading) {
            out.push_back(' ');
        }
    }
    return out;
}

// Serializes a text node in minify mode with careful whitespace handling.
// Decisions are made in this order:
//   1. Raw-text parents (<script>, <style>, <xmp>, ...) pass the value
//      straight through — nothing may be altered.
//   2. Whitespace-protected parents and white-space:pre ancestors keep the
//      value verbatim, only escaped.
//   3. Pure-whitespace text directly under the document/document-fragment,
//      <html> or <head> is dropped — browsers render none of it.
//   4. Otherwise the value is collapsed with MinifyText. Leading/trailing
//      whitespace is trimmed when the adjacent sibling is a block-level
//      element or boundary (or the parent is block-like and this is the
//      only child), because that whitespace is never visible.
//
// Example: "hello   world" with whitespace on both sides inside a <p>
//          between two block neighbors -> "hello world"
std::string SerializeTextNodeMinified(const Node& node,
                                      const PrinterOptions& options) {
    const Node* parent = node.parent_node;

    if (parent != nullptr && IsElementNode(*parent) &&
        parent->namespace_uri == NS::kHtml &&
        HasUnescapedText(parent->tag_name, options.scripting_enabled)) {
        return node.value;
    }

    if ((parent != nullptr && IsMinifyProtectedElement(*parent)) ||
        PreservesWhitespaceByStyle(node)) {
        return EscapeText(node.value);
    }

    {
        const bool whitespace_only =
            node.value.find_first_not_of(" \t\n\r\f\v") ==
            std::string::npos;
        if (whitespace_only && parent != nullptr &&
            !IsElementNode(*parent)) {
            return "";
        }
        if (whitespace_only && parent != nullptr &&
            IsElementNode(*parent) && parent->namespace_uri == NS::kHtml &&
            (parent->tag_name == "head" || parent->tag_name == "html")) {
            return "";
        }
    }

    bool trim_leading = false;
    bool trim_trailing = false;
    if (parent != nullptr) {
        const Node* container =
            (IsTemplateElement(*parent) && parent->namespace_uri == NS::kHtml)
                ? &GetTemplateContent(*parent)
                : parent;
        const Node* prev = nullptr;
        const Node* next = nullptr;
        size_t idx = 0;
        size_t node_idx = 0;
        bool found = false;
        for (const auto& child : container->child_nodes) {
            if (child.get() == &node) {
                node_idx = idx;
                found = true;
            }
            if (found) {
                if (idx == node_idx + 1) {
                    next = child.get();
                }
            } else {
                prev = child.get();
            }
            ++idx;
        }

        auto node_is_block_or_boundary = [](const Node* n) -> bool {
            if (n == nullptr) {
                return true;
            }
            if (IsElementNode(*n) && n->namespace_uri == NS::kHtml) {
                return IsBlockLevelLike(n->tag_name);
            }
            return true;
        };

        if (node_is_block_or_boundary(prev)) {
            trim_leading = true;
        }
        if (node_is_block_or_boundary(next)) {
            trim_trailing = true;
        }
        if (IsBlockLevelLike(parent->tag_name) && prev == nullptr &&
            next == nullptr) {
            trim_leading = true;
            trim_trailing = true;
        }
    }

    const std::string collapsed = MinifyText(node.value, trim_leading,
                                             trim_trailing);
    if (collapsed.empty()) {
        return "";
    }
    return EscapeText(collapsed);
}

// True when the element's serialized children are all text nodes (no nested
// elements, comments or doctypes). Pretty mode renders such elements on a
// single line.
//
// Example: a <title> with the single text child "Docs" -> true, printed as
//          "<title>Docs</title>" instead of a multi-line block.
bool HasOnlyTextChildren(const Node& node, [[maybe_unused]] const PrinterOptions& options) {
    for (const auto& child : SerializationContainer(node).child_nodes) {
        if (!IsTextNode(*child)) {
            return false;
        }
    }
    return true;
}

// Pretty-prints the children of a <script>/<style> element. The code is
// serialized once, then every physical line is indented one level deeper
// than the element so the embedded JS/CSS stays readable and visually
// grouped. The first line always receives the indent (even for empty
// content), and any trailing blank line left behind is stripped so the
// closing tag is not preceded by blank space.
//
// Example: at depth 0, code text "init()\n" -> "  init()" (pad = two spaces,
//          trailing newline removed).
std::string SerializeCodeChildrenPretty(const Node& parent_node,
                                        const PrinterOptions& options,
                                        int depth) {
    std::string content;
    for (const auto& child : SerializationContainer(parent_node).child_nodes) {
        content += SerializeNode(*child, options, depth);
    }
    const std::string pad = Indent(depth + 1);
    std::string out;
    size_t start = 0;
    while (start < content.size()) {
        size_t nl = content.find('\n', start);
        std::string_view line(content.data() + start,
                              (nl == std::string::npos ? content.size() : nl) - start);
        out += pad;
        out += line;
        if (nl == std::string::npos) {
            break;
        }
        out += '\n';
        start = nl + 1;
    }
    while (!out.empty()) {
        size_t last_nl = out.find_last_of('\n');
        std::string_view tail = last_nl == std::string::npos
                                    ? std::string_view(out)
                                    : std::string_view(out).substr(last_nl + 1);
        if (tail.find_first_not_of(" \t\r\n") != std::string_view::npos) {
            break;
        }
        if (last_nl == std::string::npos) {
            out.clear();
            break;
        }
        out.erase(last_nl);
    }
    return out;
}

// Pretty-prints the children of a container. Each block child (element,
// comment, doctype) starts on its own line indented to child_depth; eligible
// text is emitted inline right after the previous child, while
// whitespace-only text is dropped. skip_leading_newline suppresses the
// newline before the very first child so document output does not begin with
// a blank line.
//
// Example: <p>A<b>B</b>C</p> -> "<p>A<b>B</b>C</p>" on one line, whereas a
//          <div> with two <p> children spans two indented lines.
std::string SerializeChildrenPretty(const Node& parent_node,
                                    const PrinterOptions& options, int child_depth,
                                    bool skip_leading_newline) {
    std::string html;
    bool first = skip_leading_newline;
    for (const auto& child : SerializationContainer(parent_node).child_nodes) {
        if (IsTextNode(*child)) {
            if (child->parent_node != nullptr && IsElementNode(*child->parent_node) &&
                child->parent_node->namespace_uri == NS::kHtml &&
                HasUnescapedText(child->parent_node->tag_name,
                                 options.scripting_enabled)) {
                html += child->value;
            } else {
                if (child->value.find_first_not_of(" \t\r\n") == std::string::npos) {
                    continue;
                }
                html += EscapeText(child->value);
            }
            first = false;
            continue;
        }
        if (!first) {
            html += '\n';
        }
        html += Indent(child_depth);
        html += SerializeNode(*child, options, child_depth);
        first = false;
    }
    return html;
}

// Emits the children of a node. In pretty mode, preformatted elements either
// re-indent their embedded code or keep their literal whitespace verbatim,
// while ordinary containers go through SerializeChildrenPretty. In every
// other case (including all minify output) the children are simply
// concatenated with no separators.
std::string SerializeChildNodes(const Node& parent_node,
                                const PrinterOptions& options, int depth) {
    std::string html;
    if (options.pretty_print) {
        if (IsPreformattedElement(parent_node)) {
            if (IsCodePreformattedElement(parent_node)) {
                return SerializeCodeChildrenPretty(parent_node, options, depth);
            }
            for (const auto& child : SerializationContainer(parent_node).child_nodes) {
                html += SerializeNode(*child, options, depth);
            }
            return html;
        }
        return SerializeChildrenPretty(parent_node, options, depth, false);
    }
    for (const auto& child : SerializationContainer(parent_node).child_nodes) {
        html += SerializeNode(*child, options, depth);
    }
    return html;
}

// Serializes a single element: its start tag (with attributes) and, unless
// it is void or empty, its children and end tag. Pretty mode picks one of
// three layouts:
//   - code elements: opening tag on its own line, content indented one
//     level, closing tag aligned with the opening tag;
//   - text-only elements: everything on a single line;
//   - everything else: each child on its own indented line with the closing
//     tag aligned with the opening tag.
// Minify concatenates children without line breaks.
//
// Example: pretty <ul><li>a</li></ul> at depth 0 ->
//          "<ul>\n  <li>a</li>\n</ul>"
std::string SerializeElement(const Node& node, const PrinterOptions& options,
                             int depth) {
    const std::string tn(node.tag_name);

    std::string html = "<" + tn + SerializeAttributes(node, options) + ">";

    if (IsVoidElement(node)) {
        return html;
    }

    const Node& container = SerializationContainer(node);
    if (options.pretty_print) {
        if (container.child_nodes.empty()) {
            return html + "</" + tn + ">";
        }
        if (IsCodePreformattedElement(node)) {
            html += '\n';
            html += SerializeChildNodes(node, options, depth);
            html += '\n';
            html += Indent(depth);
            html += "</" + tn + ">";
            return html;
        }
        if (HasOnlyTextChildren(node, options)) {
            html += SerializeChildNodes(node, options, depth + 1);
            html += "</" + tn + ">";
            return html;
        }
        html += SerializeChildNodes(node, options, depth + 1);
        html += '\n';
        html += Indent(depth);
        html += "</" + tn + ">";
        return html;
    }

    html += SerializeChildNodes(node, options, depth);
    html += "</" + tn + ">";
    return html;
}

// Serializes a text node in non-minify mode. Raw-text parents (for example
// <script> when scripting is enabled) pass the value through unescaped; all
// other text is escaped.
//
// Example: the text "1 < 2" inside <p> -> "1 &lt; 2"
std::string SerializeTextNode(const Node& node, const PrinterOptions& options) {
    const Node* const parent = node.parent_node;

    if (parent != nullptr && IsElementNode(*parent) &&
        parent->namespace_uri == NS::kHtml &&
        HasUnescapedText(parent->tag_name, options.scripting_enabled)) {
        return node.value;
    }

    return EscapeText(node.value);
}

// Top-level dispatcher: routes a single node to the serializer matching its
// kind. Anything that is not an element, text, comment or doctype yields the
// empty string. In minify mode, non-conditional comments are stripped (only
// "<!--[if ...]-->" / "<!--[endif]-->" conditional comments survive) and
// text nodes go through SerializeTextNodeMinified.
//
// Example: a doctype node -> "<!DOCTYPE html>"
// Example: "<!-- [if IE]>x<![endif] -->" survives minify, but the plain
//          comment "<!-- note -->" is dropped in minify and kept otherwise.
std::string SerializeNode(const Node& node, const PrinterOptions& options,
                          int depth) {
    if (!IsElementNode(node) && !IsTextNode(node) && !IsCommentNode(node) &&
        !IsDocumentTypeNode(node)) {
        return "";
    }

    if (IsTextNode(node)) {
        if (options.minify) {
            return SerializeTextNodeMinified(node, options);
        }
        return SerializeTextNode(node, options);
    }
    if (IsCommentNode(node)) {
        if (options.minify) {
            const std::string_view d = node.data;
            if (d.find("[if") == std::string_view::npos &&
                d.find("[endif]") == std::string_view::npos) {
                return "";
            }
        }
        return "<!--" + node.data + "-->";
    }
    if (IsDocumentTypeNode(node)) {
        return "<!DOCTYPE " + node.doctype_name + ">";
    }
    return SerializeElement(node, options, depth);
}

}

// Entry point for serializing a whole document or fragment. A node that is
// itself void yields no output at all. In pretty mode the root's children
// are laid out at depth 0 without a leading blank line; in minify mode they
// are concatenated.
std::string Print(const Node& parent_node, const PrinterOptions& options) {
    if (IsVoidElement(parent_node)) {
        return "";
    }

    if (options.pretty_print) {
        return SerializeChildrenPretty(parent_node, options, 0, true);
    }
    return SerializeChildNodes(parent_node, options, 0);
}

// Serializes a single node including its own tags, in contrast to "Print"
// which serializes only children. Equivalent to SerializeNode at depth 0.
//
// Example: PrintOuter of a <div> element with children emits
//          "<div>...</div>" wholesale.
std::string PrintOuter(const Node& node, const PrinterOptions& options) {
    return SerializeNode(node, options, 0);
}

}