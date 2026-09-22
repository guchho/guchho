#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "guchho/compiler.hpp"
#include "guchho/html/html_ast.hpp"
#include "guchho/logger.hpp"

namespace guchho::html {

struct BridgeOptions {
    // Parse as a document fragment instead of a whole document.
    bool parse_fragment = false;
    // Populate "AST::import_records" from resource-bearing elements
    // (<script src>, <link href>, <img src>, ...).
    bool collect_import_records = true;
    // Collect content-bearing <script>/<style> elements into
    // "AST::inline_scripts"/"AST::inline_styles".
    bool collect_inline_code = true;

    bool operator==(const BridgeOptions&) const = default;
};

// Locates the attribute whose value produced an import record. "record_origins"
// parallels "AST::import_records" (record i originates from
// "record_origins[i]"). The element pointer stays valid for as long as the AST
// tree lives, and "attr_name" points into that element's attribute storage.
struct ImportRecordOrigin {
    const Node* element = nullptr;
    std::string_view attr_name;

    // For attributes whose value holds more than one resource URL (e.g. a
    // "style" attribute with several url()/image-set() references), the byte
    // range [value_offset, value_offset + value_length) within the attribute
    // value that should be replaced. A zero length means the whole attribute
    // value is the URL (the existing behavior for src/href/data/etc.).
    uint32_t value_offset = 0;
    uint32_t value_length = 0;
};

struct AST {
    std::unique_ptr<Node> node;

    // Resource references, in document order. Ranges cover the element's
    // start tag.
    std::vector<compiler::ImportRecord> import_records;

    // For each entry in "import_records", which element/attribute produced it.
    // Used by the bundler to rewrite the URL after linking.
    std::vector<ImportRecordOrigin> record_origins;

    // <script> elements without a "src" attribute (their text children are
    // the code) and <style> elements. These point into "node"'s tree, so the
    // AST must re-point them whenever the tree is copied or moved.
    std::vector<const Node*> inline_scripts;
    std::vector<const Node*> inline_styles;

    AST() = default;
    AST(AST&&) = default;
    AST& operator=(AST&&) = default;
    // Deep-copies the tree and re-points "record_origins", "inline_scripts",
    // and "inline_styles" at the clone.
    AST(const AST& other);
    AST& operator=(const AST& other);
};

// Parses "source.contents" as HTML. Parse errors are reported through "log"
// (as errors; they carry no MsgID so that they cannot be downgraded), while
// bridge-level diagnostics use kHTML_* IDs.
AST Parse(logger::Log& log, const logger::Source& source,
          const BridgeOptions& options = {});

// Converts the start tag of "element" into a byte range in "source"'s
// UTF-8 contents, for use with a logger::LineColumnTracker bound to that
// source. Import records already point at this same range; this lets the
// bundler attach a code frame to diagnostics (warnings such as the
// import-map reorder) that are not tied to an import record (Phase 12).
logger::Range RangeOfTag(const logger::Source& source, const Node& element);

// Returns true when "element" is a <script type="importmap">. Import maps are
// data islands: their JSON body is never bundled as inline JavaScript, and
// they produce no import records (Phase 4). Browsers interpret them natively;
// the bundler only reorders them before the module scripts.
inline bool IsImportMapScript(const Node& element) {
    if (element.tag_name != "script") {
        return false;
    }
    for (const Attribute& attr : element.attrs) {
        if (attr.name == "type" && attr.value == "importmap") {
            return true;
        }
    }
    return false;
}

} // namespace guchho::html
