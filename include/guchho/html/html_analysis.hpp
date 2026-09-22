#pragma once

#include <string_view>
#include <vector>

#include "guchho/html/html_ast.hpp"

namespace guchho::html {

// True when "element" is a <link> whose "rel" token list contains
// "stylesheet" (so it is a CSS bundle entry). The token list is matched
// case-insensitively per the HTML spec, and every token is checked, so a
// rel like "alternate stylesheet" still counts. All other <link> elements
// (favicon, preload, modulepreload, ...) are deliberately excluded and are
// never treated as bundling sub-entries.
//
// Example: <link rel="stylesheet" href="theme.css"> -> true
//          <link rel="alternate stylesheet" href="alt.css"> -> true
//          <link rel="preload" href="theme.css" as="style"> -> false
bool IsStylesheetLink(const Node& element);

// True when "element" is a <script> whose "type" attribute value is exactly
// "module", indicating an ES module <script>. A missing or empty "type", or
// a MIME-like value (for example "text/javascript"), is not a module script;
// only the explicit modern "module" keyword counts. Classic scripts are
// handled separately by the caller.
//
// Example: <script type="module" src="app.js"></script> -> true
//          <script src="app.js"></script> -> false
//          <script type="text/javascript">... </script> -> false
bool IsModuleScript(const Node& element);

// Finds the first <head> element in "root"'s tree (searching recursively,
// since <head> is normally nested inside <html>), or nullptr if absent.
// Traversal is depth-first, so nesting order -- <html><head>...</head><body>
// -- is respected and the canonical document head is always the first match.
// The non-const overload returns a mutable pointer so callers can rewrite
// the head in place.
//
// Example: document node whose first child <html> contains <head> with a
//          <title> -> that <head> element; a fragment with no head -> nullptr
const Node* FindHead(const Node& root);
Node* FindHead(Node& root);

// Locates the <body> element in "root"'s tree (recursively, depth-first), or
// nullptr if absent. This returns the actual <body> element node so the
// caller can append or prepend children; a document whose <body> was
// implicitly created by the parser will still be found.
//
// Example: <html><head>.. </head><body>.. </body></html> -> the <body> node
const Node* FindBody(const Node& root);
Node* FindBody(Node& root);

// True when "element" is a <link> whose "rel" token list contains one of the
// CSP-relevant kinds ("stylesheet", "modulepreload", or "preload"). These
// are the ressource kinds a Content-Security-Policy style-src or
// script-src-directive commonly needs approved. The check mirrors
// IsStylesheetLink but widens the accepted token set; unrelated rel values
// (icon, manifest, dns-prefetch) are ignored.
//
// Example: <link rel="modulepreload" href="x.js"> -> true
//          <link rel="icon" href="favicon.ico"> -> false
bool LinkRelIsCspRelevant(const Node& element);

// True when the document rooted at "root" already declares
// <meta property="csp-nonce">, in which case a duplicate is never injected
// by the nonce instrumentation. The meta tag may live anywhere in the tree;
// this makes the check idempotent across multiple passes over the same
// document.
//
// Example: <meta property="csp-nonce" content="abc123"> present -> true
//          document without that meta tag -> false
bool HasCspNonceMeta(const Node& root);

// True when "element" must follow every import map in document order: a
// <script type="module"> or a <link rel="modulepreload">. These are the
// elements whose module resolution can depend on import maps, so when
// injecting import maps the insertion point must sit before this element
// (and after all earlier import maps).
//
// Example: <script type="module" src="m.js"> -> true
//          <link rel="modulepreload" href="p.js"> -> true
//          <div> -> false
bool IsImportMapPivot(const Node& element);

// Result of scanning a document for import maps and their required location.
//
//   pivot         the first node in preorder order that must come after all
//                 import maps (<script type="module"> or
//                 <link rel="modulepreload">), or nullptr when no such node
//                 exists (import maps can then go anywhere legal).
//   pivot_preorder the preorder index of "pivot" in the same numbering used
//                 for "map_preorder", or -1 when "pivot" is nullptr.
//   maps          every valid import map node (<script type="importmap">) in
//                 document order, excluding malformed ones.
//   map_preorder  the preorder index of each import map in "maps", aligned
//                 one-to-one so callers can reason about relative order.
//
// Example: document with <script type="importmap"> then
//          <script type="module"> -> maps[0] is the import map, pivot is the
//          module script, and pivot_preorder > map_preorder[0].
struct ImportMapScan {
    Node* pivot = nullptr;
    int pivot_preorder = -1;
    std::vector<Node*> maps;
    std::vector<int> map_preorder;
};

// Scans "root" (normally a full document) for import maps and returns an
// ImportMapScan describing them. Import maps are <script type="importmap">
// elements; malformed ones are skipped. Every node is visited in document
// (preorder) order so relative placement of maps and the module pivot is
// captured exactly -- the pivot chosen is the first module-or-preload node
// after the maps. The tree is not modified.
//
// Example: <head><script type="importmap">.. </script></head>
//          <body><script type="module">.. </script></body> -> scan.maps has
//          one entry, scan.pivot is the module script in <body>.
ImportMapScan ScanImportMaps(Node& root);

// True when "href" refers to a local, bundleable resource rather than a
// remote or non-resource URL: it must not start with a scheme ("http:",
// "https:", "file:", ...) nor with "//" (protocol-relative), and must not be
// a fragment or mailto-style link. Anything that could be fetched across the
// network is left untouched by the bundler.
//
// Example: "./css/app.css" and "/js/bundle.js" -> true
//          "https://cdn.example.com/x.js" and "//cdn.example.com/x.js"
//          and "#top" -> false
bool HrefIsLocal(std::string_view href);

// Recursively walks "node" and appends every local stylesheet <link>
// (stylesheet link whose href is local per HrefIsLocal) to "out" in document
// order. Nested containers are descended into fully; links inside templates
// or shadow content are included as well since they still carry real CSS.
// The tree is only read. "out" is never cleared, so callers may accumulate
// across multiple roots.
//
// Example: <body><div><link rel="stylesheet" href="a.css"></div></body> ->
//          out ends with the a.css link node appended.
void CollectLocalStylesheetLinks(Node& node, std::vector<Node*>& out);

}