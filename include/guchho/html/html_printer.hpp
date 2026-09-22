#pragma once
#include <string>
#include "guchho/html/html_ast.hpp"

namespace guchho::html {

// Options controlling how Print / PrintOuter serialize a tree.
struct PrinterOptions {

    // Whether scripting is enabled, matching the flag the parser used. Raw
    // text elements (<script>, <style>, <xmp>, <iframe>, <noembed>,
    // <noframes>, <plaintext>) always pass their content through unescaped,
    // whichever way this is set -- their content is CDATA-like and must never
    // be re-encoded. The flag only matters for <noscript>: with scripting on,
    // its content is raw text (unedited); with scripting off it is ordinary
    // markup whose text nodes are escaped normally.
    //
    // Example: <noscript>a < b</noscript> prints "a < b" when true and is
    //          escaped like normal content when false.
    bool scripting_enabled = true;

    // Pretty printing: children are written on their own indented lines (two
    // spaces per nesting level), code elements re-indent their embedded
    // content, and <title>-like elements with only text stay on one line.
    // Whitespace-only text nodes between blocks are dropped so the output is
    // clean rather than faithful. Off by default (single-line output).
    bool pretty_print = false;

    // Minification: collapses inter-element whitespace, trims text against
    // block boundaries, drops non-conditional comments, strips unquoteable
    // attribute quotes, and omits empty attribute value pairs. Content of
    // raw-text and preformatted elements is preserved verbatim. Off by
    // default.
    bool minify = false;
};

// Serializes an AST node's children to an HTML string. For a document node
// this is the whole document; for an element it is the node's inner HTML
// (the element's own tags are not part of the result). A void element as the
// input produces the empty string, since it can have no children.
//
// Example: Print of a <body> holding <p>hi</p> returns "<p>hi</p>" without
//          the <body> tags.
std::string Print(const Node& parent_node,
                  const PrinterOptions& options = {});

// Serializes an AST node to an HTML string including the node itself: start
// tag (with attributes), children, and end tag, exactly as the element
// appears in source. Equivalent to Print wrapped around one element.
//
// Example: PrintOuter of a <div id="x">text</div> returns
//          "<div id=\"x\">text</div>".
std::string PrintOuter(const Node& node, const PrinterOptions& options = {});

}