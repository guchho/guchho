// The Guchho HTML abstract syntax tree.
//
// A plain, deliberately minimal tree for parsed HTML documents. Parents own
// their children: "child_nodes" holds each child by value in a unique_ptr, so
// node lifetimes are hierarchical and destroying a document frees the whole
// tree. Two kinds of link point back up: "parent_node" and any pointers the
// parser handed out are non-owning, raw references. A node's address is
// stable across tree edits -- moving a node to a new parent only re-anchors
// the unique_ptr, never the pointee -- so callers may freely cache Node*
// across mutations.
//
// A node's concrete meaning is carried by its NodeType; only the fields
// relevant to that type are populated (see the per-group notes inside
// Node). Location bookkeeping keeps two parallel sets of pointers: the
// public, nullable lookup pointers and the owned storage that keeps the
// locations alive.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "guchho/html/html_helpers.hpp"
#include "guchho/html/html_lexer.hpp"

namespace guchho::html {

// The kind of a tree node. Exactly one kind applies per node; the kind is
// fixed at creation and never changes for the lifetime of the node. It is the
// primary selector used by IsElementNode, IsTextNode, and friends when code
// must branch on what a node holds.
enum class NodeType : uint8_t {
    kDocument,
    kDocumentFragment,
    kElement,
    kText,
    kComment,
    kDocumentType,
};

// Stable, standard node names for the non-element node kinds. Every node of a
// given kind reports the same string, which lets generic routines recognize a
// node's kind without touching a NodeType trait table. Elements instead use
// their tag name (see Node::node_name).
inline constexpr std::string_view kDocumentNodeName = "#document";
inline constexpr std::string_view kDocumentFragmentNodeName = "#document-fragment";
inline constexpr std::string_view kTextNodeName = "#text";
inline constexpr std::string_view kCommentNodeName = "#comment";
inline constexpr std::string_view kDocumentTypeNodeName = "#documentType";

// A single node of the HTML tree.
//
// Only the field groups matching "type" make sense: a text node uses
// "value", a comment uses "data", a doctype uses the doctype fields, and an
// element uses tag/namespace/attributes plus, for <template>, the owned
// "template_content" fragment. For elements, "node_name" always equals
// "tag_name", so reading either is equivalent. Every Node instance created
// by the tree adapter carries a unique "node_id"; a freshly constructed Node
// (as in tests) keeps it at zero, meaning "not created by the adapter".
//
// Source locations are stored as raw pointers for read-only access plus an
// owned unique_ptr that actually retains the value. The lexer keeps its own
// pool of Locations that is only valid while the lexer lives, so any
// location a node exposes must be a value copy owned by that node; the
// owned_* members guarantee the public pointers stay valid for as long as
// the node does.
struct Node {
    NodeType type;

    // The name of the node: "#document", "#text", ... or the tag name for
    // elements.
    std::string node_name;

    // kDocument.
    DocumentMode mode = DocumentMode::kNoQuirks;

    // kElement.
    // NOTE: for elements, "node_name" always equals "tag_name".
    std::string tag_name;
    NS namespace_uri = NS::kHtml;
    std::vector<Attribute> attrs;

    // kElement where tag_name == "template": the content fragment owned by
    // the template element.
    std::unique_ptr<Node> template_content;

    // kText.
    std::string value;

    // kComment.
    std::string data;

    // kDocumentType.
    std::string doctype_name;
    std::string public_id;
    std::string system_id;

    // Stable identity, unique for every Node instance created by the tree
    // adapter (and re-assigned fresh by CloneNode). It never leaves the AST:
    // the serializer does not read it, so output is unaffected. The bridge
    // uses it to re-point AST metadata (record origins, inline element lists)
    // at the clone after a deep copy, replacing the former pointer walk.
    // Zero means "not created by the adapter" (e.g. a hand-built Node in
    // tests).
    uint32_t node_id = 0;

    // Tree structure. Null for a detached or root node.
    Node* parent_node = nullptr;
    std::vector<std::unique_ptr<Node>> child_nodes;

    // Source code location info; null when disabled or not set yet.
    const Location* source_code_location = nullptr;

    // kElement: location of the start tag and of a matching end tag; null
    // when absent.
    const Location* start_tag_location = nullptr;
    const Location* end_tag_location = nullptr;

    // Owned storage for locations copied from the lexer's pool. The pool
    // dies with the lexer, so every location reachable from a node must be a
    // value copy owned by the node.
    std::unique_ptr<Location> owned_location;
    std::unique_ptr<Location> owned_start_tag_location;
    std::unique_ptr<Location> owned_end_tag_location;
};

// Partial location update: a six-field (line, column, byte offset) span where
// any combination of fields may be left unset. Fields present overwrite the
// node's current location; fields missing keep the existing values, so an
// update can adjust just the start or just the end of a span. Applied by
// UpdateNodeSourceCodeLocation.
//
// Example: start_line = 7 only -> the node's start line becomes 7 while its
//          start column/offset and all end fields stay as they were.
struct LocationUpdate {
    std::optional<int32_t> start_line;
    std::optional<int32_t> start_col;
    std::optional<int32_t> start_offset;
    std::optional<int32_t> end_line;
    std::optional<int32_t> end_col;
    std::optional<int32_t> end_offset;
};

// Node construction

// Creates an empty document node: the root of an HTML tree, with a docmode
// (defaulting to no-quirks) that the parser then adjusts while inserting
// content, and no children yet. Documents may hold at most one doctype child
// (see SetDocumentType).
std::unique_ptr<Node> CreateDocument();
std::unique_ptr<Node> CreateDocumentFragment();
std::unique_ptr<Node> CreateElement(std::string tag_name,
                                    NS namespace_uri = NS::kHtml,
                                    std::vector<Attribute> attrs = {});

// Deep-clones "node" and its whole subtree, re-pointing parent pointers and
// owned locations at the clone. The clone is independent: later edits to
// either tree never leak into the other. Every new node receives a fresh,
// unused "node_id" so identity remains unique across the whole process.
// When "clone_map" is non-null, each clone is recorded as
// "source node_id -> clone" (including nested <template> content), keyed by
// the source node's id so callers can resolve an original element to its
// clone directly with no extra tree traversal.
//
// Example: CloneNode of <div><span>x</span></div> yields a separate
//          <div><span>x</span></div>; clone_map (if given) maps the source
//          div's id to the clone div and each span likewise.
std::unique_ptr<Node> CloneNode(
    const Node& node, Node* parent = nullptr,
    std::unordered_map<uint32_t, Node*>* clone_map = nullptr);
std::unique_ptr<Node> CreateCommentNode(std::string data);
std::unique_ptr<Node> CreateTextNode(std::string value);

// Tree mutation

void AppendChild(Node& parent_node, std::unique_ptr<Node> new_node);
// Returns the index of "node" in "parent_node.child_nodes", or -1 if "node"
// is not a direct child. Useful before removal or before inserting a sibling
// in a known spot.
//
// Example: parent with children [a, b, c] -> IndexofChild(parent, b) == 1
//          IndexofChild(parent, z) == -1
int32_t IndexofChild(const Node& parent_node, const Node& node);
// Inserts "new_node" directly before "reference_node" in
// "parent_node.child_nodes"; both nodes must already be children of
// "parent_node" (either through InsertBefore or AppendChild). Because
// ownership is held by the parent, "new_node" is taken as a unique_ptr; a
// caller wanting to relocate an existing child must DetachNode it first, or
// the insert would create a duplicate owner.
//
// Example: InsertBefore(parent, n, child_b) turns children [a, b] into
//          [a, n, b].
void InsertBefore(Node& parent_node, std::unique_ptr<Node> new_node,
                  const Node& reference_node);

void SetTemplateContent(Node& template_element, std::unique_ptr<Node> content);
Node& GetTemplateContent(const Node& template_element);

// Sets (or replaces) the doctype child of "document". Only the copied-out
// names are retained; if "document" already had a doctype child it is
// replaced rather than duplicated.
void SetDocumentType(Node& document, std::string name, std::string public_id,
                     std::string system_id);

void SetDocumentMode(Node& document, DocumentMode mode);
DocumentMode GetDocumentMode(const Node& document);

// Removes "node" from its parent without destroying it; ownership moves to
// the returned holder. The node's parent pointer is cleared, leaving it
// detached and ready to be re-appended elsewhere.
//
// Example: DetachNode of a <span> in <div>[text, <span>] yields the span and
//          leaves <div> holding only [text].
[[nodiscard]] std::unique_ptr<Node> DetachNode(Node& node);

void InsertText(Node& parent_node, std::string_view text);
void InsertTextBefore(Node& parent_node, std::string_view text,
                      const Node& reference_node);

// Appends attributes that are not already present on "recipient". Existing
// attributes are untouched (kept in place, value intact); only the missing
// ones from "attrs" are added, in the order they appear.
//
// Example: recipient has [id] and attrs is [id, class, name] ->
//          recipient ends with [id, class, name].
void AdoptAttributes(Node& recipient, const std::vector<Attribute>& attrs);

// Tree traversing

Node* GetFirstChild(Node& node);
const Node* GetFirstChild(const Node& node);

inline std::vector<std::unique_ptr<Node>>& GetChildNodes(Node& node) {
    return node.child_nodes;
}

inline const std::vector<std::unique_ptr<Node>>& GetChildNodes(const Node& node) {
    return node.child_nodes;
}

inline Node* GetParentNode(Node& node) { return node.parent_node; }

inline const std::vector<Attribute>& GetAttrList(const Node& element) {
    return element.attrs;
}

// Returns the value of "element"'s attribute named "name", or nullptr if the
// element has no such attribute. Shared by the bridge and the HTML output
// pass so attribute lookup stays in one place.
//
// Example: <a href="x" ...> -> FindAttrValue(..., "href") returns pointer to
//          "x"; FindAttrValue(..., "rel") returns nullptr.
inline const std::string* FindAttrValue(const Node& element,
                                        std::string_view name) {
    for (const Attribute& attr : element.attrs) {
        if (attr.name == name) {
            return &attr.value;
        }
    }
    return nullptr;
}

// Returns true when "element" has an attribute named "name" (regardless of
// its value). Doubles as a cheap presence test that avoids a null-value
// comparison.
//
// Example: <input required> -> HasAttribute(..., "required") == true even
//          though the attribute has an empty value.
inline bool HasAttribute(const Node& element, std::string_view name) {
    return FindAttrValue(element, name) != nullptr;
}

// Sets (or adds) the attribute named "name" on "element" to "new_value".
// Existing attributes keep their position; a missing one is appended
// (mirroring how the bundler builds injected attributes).
//
// Example: SetAttrValue(<a href>, "href", "b") rewrites href to "b";
//          SetAttrValue(<a>, "target", "_blank") appends target.
inline void SetAttrValue(Node& element, std::string_view name,
                         std::string new_value) {
    for (Attribute& attr : element.attrs) {
        if (attr.name == name) {
            attr.value = std::move(new_value);
            return;
        }
    }
    Attribute attr;
    attr.name = std::string(name);
    attr.value = std::move(new_value);
    element.attrs.push_back(std::move(attr));
}

// Adds the attribute named "name" to "element" with value "value", unless an
// attribute with that name already exists (in which case it is left
// untouched), mirroring how the bundler injects opt-in attributes like
// `crossorigin`.
inline void AddAttributeIfMissing(Node& element, std::string_view name,
                                  std::string value) {
    if (!HasAttribute(element, name)) {
        SetAttrValue(element, name, std::move(value));
    }
}

// True when "attribute_value"'s space-delimited token list contains "token"
// (case-sensitive), e.g. "<link rel='stylesheet'>". Use this instead of
// hand-rolling per-site token scanners for "rel" values.
//
// Example: RelValueHasToken("alternate stylesheet", "stylesheet") == true;
//          RelValueHasToken("preload", "stylesheet") == false.
inline bool RelValueHasToken(std::string_view attribute_value,
                             std::string_view token) {
    size_t start = 0;
    while (start <= attribute_value.size()) {
        size_t end = attribute_value.find(' ', start);
        if (end == std::string_view::npos) {
            end = attribute_value.size();
        }
        if (attribute_value.substr(start, end - start) == token) {
            return true;
        }
        if (end == attribute_value.size()) {
            break;
        }
        start = end + 1;
    }
    return false;
}

inline std::string_view GetTagName(const Node& element) { return element.tag_name; }

inline NS GetNamespaceURI(const Node& element) { return element.namespace_uri; }

inline const std::string& GetTextNodeContent(const Node& text_node) {
    return text_node.value;
}

inline const std::string& GetCommentNodeContent(const Node& comment_node) {
    return comment_node.data;
}

inline const std::string& GetDocumentTypeNodeName(const Node& doctype_node) {
    return doctype_node.doctype_name;
}

inline const std::string& GetDocumentTypeNodePublicId(const Node& doctype_node) {
    return doctype_node.public_id;
}

inline const std::string& GetDocumentTypeNodeSystemId(const Node& doctype_node) {
    return doctype_node.system_id;
}

// Node types

inline bool IsElementNode(const Node& node) { return node.type == NodeType::kElement; }
inline bool IsTextNode(const Node& node) { return node.type == NodeType::kText; }
inline bool IsCommentNode(const Node& node) { return node.type == NodeType::kComment; }
inline bool IsDocumentTypeNode(const Node& node) {
    return node.type == NodeType::kDocumentType;
}

// True when "node" is a <template> element. Templates are special in the
// printer: their content lives in the owned "template_content" fragment, so
// caller code must consult GetTemplateContent rather than "child_nodes".
inline bool IsTemplateElement(const Node& node) {
    return IsElementNode(node) && node.tag_name == "template";
}

// Source code location

// Publishes "location" as the node's source location. A null pointer clears
// the location and drops the owned copy; otherwise the value is copied into
// "owned_location" before the public pointer is set, so the exposed
// "source_code_location" stays valid after the lexer's pool is gone.
inline void SetNodeSourceCodeLocation(Node& node, const Location* location) {
    if (location == nullptr) {
        node.source_code_location = nullptr;
        return;
    }

    node.owned_location = std::make_unique<Location>(*location);
    node.source_code_location = node.owned_location.get();
}

inline const Location* GetNodeSourceCodeLocation(const Node& node) {
    return node.source_code_location;
}

// Merges a partial "update" into the node's source location, keeping any
// fields the update leaves unset. Only nodes that already have a location are
// extended: nodes with none are left untouched.
//
// Example: node located at 1:2 (offset 5) merged with end_line={3},
//          end_col={1} -> location start unchanged, end becomes 3:1.
void UpdateNodeSourceCodeLocation(Node& node, const LocationUpdate& update);

}