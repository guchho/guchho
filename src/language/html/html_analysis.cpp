#include "guchho/html/html_analysis.hpp"

#include "guchho/helpers.hpp"
#include "guchho/html/html_bridge.hpp"

// Guchho HTML analysis helpers.
//
// The functions in this translation unit read a parsed DOM and answer
// structural questions the bundling pipeline poses before any rewrite
// happens: where the <head> and <body> elements live, which <link> elements
// carry CSS, which scripts are modules (and therefore import-map consumers),
// how CSP-relevant links are recognized, and where the document's own
// local stylesheet links can be collected for bundling. Every routine here
// is a read-only query: none of them mutate the tree, so a single document
// can be inspected many times without side effects.

namespace guchho::html {

// Finds the document's <head> element by walking "root" depth-first and
// matching on node type plus the lowercase tag name "head". The search is
// blind to position -- a head nested at any depth (normally the first child
// of <html>) is returned in preorder, meaning the canonical declaration is
// the first match. Returns nullptr when no head element exists in the tree.
//
//   root: <html><head><title>Guchho</title></head><body></body></html>
//         -> the <head> node
//   root: a <div> fragment                                 -> nullptr
//
// Edge cases: a head placed later in the tree (for example authored inside
// <body> or after a paragraph) is still returned, because document nesting
// here is not verified against the HTML content model; this routine only
// answers "where is the head", not "is this document valid".
const Node* FindHead(const Node& root) {
    if (root.type == NodeType::kElement && root.tag_name == "head") {
        return &root;
    }
    for (const auto& child : root.child_nodes) {
        if (const Node* found = FindHead(*child)) {
            return found;
        }
    }
    return nullptr;
}

// Mutable flavor of FindHead. The const search above is the single source
// of truth for the traversal; this overload casts its result back to a
// non-const pointer so callers can edit the head in place (moving <link> or
// <title> children around for bundling). Routing through a const reference
// first guarantees that the traversal itself never mutates the tree.
Node* FindHead(Node& root) {
    return const_cast<Node*>(FindHead(static_cast<const Node&>(root)));
}

// Locates the document's <body> element with the same depth-first preorder
// strategy as FindHead, but matching the tag name "body". Returning the real
// element node (rather than a flag) lets callers append or prepend children
// to it; a body created implicitly by the parser appears just like an
// explicit one and is found the same way. Returns nullptr when absent.
//
//   <html><head></head><body>content</body></html> -> the <body> node
//   <html><head></head></html>                     -> nullptr
//
// Edge cases: exactly like the head search, the first body encountered wins,
// so a document with stray extra <body> elements yields only the first one.
const Node* FindBody(const Node& root) {
    if (root.type == NodeType::kElement && root.tag_name == "body") {
        return &root;
    }
    for (const auto& child : root.child_nodes) {
        if (const Node* found = FindBody(*child)) {
            return found;
        }
    }
    return nullptr;
}

// Mutable flavor of FindBody, mirroring FindHead: the const walk does the
// work, and this overload re-exposes the located element as writable.
Node* FindBody(Node& root) {
    return const_cast<Node*>(FindBody(static_cast<const Node&>(root)));
}

// True when "element" is a <link> whose rel attribute contains the token
// "stylesheet". Matching goes through RelValueHasToken, which splits on
// ASCII whitespace and folds case, so multi-valued rel lists like
// "alternate stylesheet" still count while unrelated kinds (preload, icon,
// modulepreload) are rejected. Whether the link is self-closing or void is
// irrelevant: the outcome is purely rel-driven.
//
//   <link rel="stylesheet" href="a.css">        -> true
//   <link rel="alternate stylesheet" ...>       -> true
//   <link rel="preload" href="a.css" as="style"> -> false
bool IsStylesheetLink(const Node& element) {
    if (element.tag_name != "link") {
        return false;
    }
    const std::string* rel = FindAttrValue(element, "rel");
    return rel != nullptr && RelValueHasToken(*rel, "stylesheet");
}

// True when "element" is a <script> whose type attribute is exactly the
// literal "module". Only that modern keyword designates an ES module: a
// missing or empty type, or a MIME-like value such as "text/javascript",
// yields a classic script and returns false. The comparison is exact and
// case-sensitive, matching how the HTML spec turns module behavior on.
//
//   <script type="module" src="m.js"></script>   -> true
//   <script src="m.js"></script>                 -> false
//   <script type="text/javascript">...</script>  -> false
bool IsModuleScript(const Node& element) {
    if (element.tag_name != "script") {
        return false;
    }
    const std::string* type = FindAttrValue(element, "type");
    return type != nullptr && *type == "module";
}

// True when a <link> references a resource that a Content-Security-Policy
// commonly needs to approve: the accepted rel tokens are "stylesheet",
// "modulepreload", and "preload", i.e. the kinds matched by style-src and
// script-src style directives. Anything that is not a link, or a link whose
// rel list contains none of the three tokens, is irrelevant.
//
//   <link rel="modulepreload" href="p.js"> -> true
//   <link rel="stylesheet" href="a.css">   -> true
//   <link rel="icon" href="f.ico">         -> false
bool LinkRelIsCspRelevant(const Node& element) {
    if (element.tag_name != "link") {
        return false;
    }
    const std::string* rel = FindAttrValue(element, "rel");
    if (rel == nullptr) {
        return false;
    }
    constexpr std::string_view kCspKinds[] = {
        "stylesheet",
        "modulepreload",
        "preload",
    };
    for (std::string_view kind : kCspKinds) {
        if (RelValueHasToken(*rel, kind)) {
            return true;
        }
    }
    return false;
}

// True when "root" already contains a <meta property="csp-nonce"> element at
// any nesting depth. Because the scan short-circuits on the first match,
// this makes nonce injection idempotent: a document that already declares
// the tag never receives a duplicate. A <meta> with a different property
// value, or any non-meta element, does not count toward the result.
//
//   <head><meta property="csp-nonce" content="abc"></head> -> true
//   <head><meta property="og:title" content="abc"></head>  -> false
bool HasCspNonceMeta(const Node& root) {
    if (IsElementNode(root) && root.tag_name == "meta") {
        const std::string* property = FindAttrValue(root, "property");
        return property != nullptr && *property == "csp-nonce";
    }
    for (const auto& child : root.child_nodes) {
        if (HasCspNonceMeta(*child)) {
            return true;
        }
    }
    return false;
}

// True when "element" must appear after every import map in document order:
// either a module <script> or a <link rel="modulepreload">. These are the
// only nodes whose module resolution consults import maps, so the first such
// node defines the latest legal insertion point for injected maps. Plain
// classic scripts and any non-script element are never pivots.
//
//   <script type="module" src="m.js">         -> true
//   <link rel="modulepreload" href="p.js">    -> true
//   <script src="c.js"></script>              -> false
//   <div>                                     -> false
bool IsImportMapPivot(const Node& element) {
    if (IsModuleScript(element)) {
        return true;
    }
    if (element.tag_name != "link") {
        return false;
    }
    const std::string* rel = FindAttrValue(element, "rel");
    if (rel == nullptr) {
        return false;
    }
    return RelValueHasToken(*rel, "modulepreload");
}

// Import-map discovery is a single recursive walk that must record both the
// maps and the module-pivot position per preorder index; that recursion and
// its mutable bookkeeping live in this internal helper.
namespace {

// Visits "node" in preorder, giving each visited node an increasing integer
// "order" before descending into its children, so the assigned numbers
// faithfully reproduce document order. Template elements abort the descent:
// their content is inert markup that never participates in module loading,
// so neither maps nor pivots are ever recorded inside one. The first pivot
// encountered anywhere claims the pivot slot, while every import-map script
// appends itself and its index to the scan.
//
//   input <head><script type="importmap"></script></head>
//         <body><script type="module"></script></body>
//     -> maps = [the import map], pivot = the module script,
//        and pivot_preorder > map_preorder[0]
void ScanImportMapsInto(Node& node, ImportMapScan& scan, int& order) {
    const int at = order++;
    if (IsTemplateElement(node)) {
        return;
    }
    if (IsElementNode(node)) {
        if (scan.pivot == nullptr && IsImportMapPivot(node)) {
            scan.pivot = &node;
            scan.pivot_preorder = at;
        }
        if (IsImportMapScript(node)) {
            scan.maps.push_back(&node);
            scan.map_preorder.push_back(at);
        }
    }
    for (auto& child : node.child_nodes) {
        ScanImportMapsInto(*child, scan, order);
    }
}

}

// Builds the ImportMapScan for the document rooted at "root" by running the
// recursive walk from a fresh preorder counter. The tree is only read; the
// returned scan describes pivot and maps so the injection step can decide
// exactly where new import maps belong.
//
//   root: <head><script type="importmap"></script></head>
//         <body><script type="module"></script></body>
//     -> scan.maps[0] is the import map, scan.pivot is the module script,
//        and scan.pivot_preorder > scan.map_preorder[0]
ImportMapScan ScanImportMaps(Node& root) {
    ImportMapScan scan;
    int order = 0;
    ScanImportMapsInto(root, scan, order);
    return scan;
}

// True when a raw URL string names a local resource the bundler may fetch,
// i.e. ClassifyURL reported it as relative ("app.js", "./d/a.css", "../x?v=2")
// or root-relative ("/assets/app.js"). Anything with an explicit scheme
// (https:, data:, mailto:), a protocol-relative "//host/path", a bare
// fragment, or the empty string is left untouched because the bundler cannot
// resolve it against the page origin.
//
//   HrefIsLocal("./css/app.css")      -> true
//   HrefIsLocal("/js/app.js")         -> true
//   HrefIsLocal("https://cdn.example.com/x.js") -> false
//   HrefIsLocal("#top")               -> false
bool HrefIsLocal(std::string_view href) {
    const helpers::URLKind kind = helpers::ClassifyURL(href);
    return kind == helpers::URLKind::kRelative ||
           kind == helpers::URLKind::kRootRelative;
}

// Walks every direct child of "node" (and, recursively, all descendants)
// and appends each <link rel="stylesheet"> whose href is local per
// HrefIsLocal to "out", in document order. Children of <noscript> are
// skipped because their links have no active effect; template and shadow
// content is descended into since it still carries real CSS. "out" is never
// cleared, so callers can accumulate links across multiple roots.
//
//   <div><link rel="stylesheet" href="a.css"></div><link rel="icon"
//         href="f.ico"> -> out ends with the a.css link node only
void CollectLocalStylesheetLinks(Node& node, std::vector<Node*>& out) {
    for (auto& child : node.child_nodes) {
        Node* element = child.get();
        if (element->type != NodeType::kElement) {
            continue;
        }
        if (element->tag_name == "noscript") {
            continue;
        }
        const std::string* href = FindAttrValue(*element, "href");
        if (IsStylesheetLink(*element) && href != nullptr &&
            HrefIsLocal(*href)) {
            out.push_back(element);
        }
        CollectLocalStylesheetLinks(*element, out);
    }
}

}