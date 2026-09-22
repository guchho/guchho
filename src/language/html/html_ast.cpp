#include "guchho/html/html_ast.hpp"

#include <atomic>
#include <cstdint>

// Guchho DOM AST construction and mutation.
//
// This translation unit contains the builders and mutators that bring the
// in-memory Node tree to life: factory functions that stamp out well-formed
// document, fragment, element, comment, and text nodes; a deep clone routine
// used when the bundler forks a sub-tree; and the small set of structural
// operations (append, insert-before, detach, text merging, attribute
// adoption) that tree construction and bundling rewrites rely on. Node
// identities come from a process-wide atomic counter, so every live AST in
// the process -- originals and their clones alike -- can be told apart.

namespace guchho::html {

namespace {

// Source of "node_id" values. Assigning from a single process-wide source
// guarantees that ids are unique across every live AST (an original and its
// clones coexist in one process), so a clone can never be confused with an
// original when the bridge re-points AST metadata. Ids never reach the
// serializer or any output, so their per-process values do not affect the
// determinism of emitted files. The counter starts at 1 so zero stays free as
// an unambiguous "unset" sentinel.
std::atomic<uint32_t> g_next_node_id{1};

// Hands out the next node identifier. The relaxed memory order is safe: each
// caller only needs its own distinct value, and cross-thread ordering of the
// IDs themselves carries no meaning for the AST.
uint32_t NextNodeId() {
    return g_next_node_id.fetch_add(1, std::memory_order_relaxed);
}

}

// Assembles a brand-new document root: a node of type kDocument carrying the
// canonical "#document" name and the default no-quirks rendering mode. Every
// later body of the document is attached as its child.
//
//   CreateDocument() -> node { type: kDocument, node_name: "#document",
//                              mode: kNoQuirks, child_nodes: [] }
std::unique_ptr<Node> CreateDocument() {
    auto node = std::make_unique<Node>();
    node->node_id = NextNodeId();
    node->type = NodeType::kDocument;
    node->node_name = std::string(kDocumentNodeName);
    node->mode = DocumentMode::kNoQuirks;
    return node;
}

// Assembles a detached document fragment: a container that can hold any set
// of nodes before they are spliced into a real document. Fragments never
// select a quirks mode, so mode stays at its default.
//
//   CreateDocumentFragment() -> node { type: kDocumentFragment,
//                                      node_name: "#document-fragment" }
std::unique_ptr<Node> CreateDocumentFragment() {
    auto node = std::make_unique<Node>();
    node->node_id = NextNodeId();
    node->type = NodeType::kDocumentFragment;
    node->node_name = std::string(kDocumentFragmentNodeName);
    return node;
}

// Assembles an element node from its tag name, namespace, and attribute
// list. Because elements keep their lowercase name duplicated in both
// node_name and tag_name, consumers can always read the tag through either
// field without a type dispatch.
//
//   CreateElement("div", NS::kHtml, [{id:"hero"}])
//     -> node { type: kElement, node_name: "div", tag_name: "div",
//               namespace_uri: kHtml, attrs: [id="hero"] }
std::unique_ptr<Node> CreateElement(std::string tag_name, NS namespace_uri,
                                    std::vector<Attribute> attrs) {    auto node = std::make_unique<Node>();
    node->node_id = NextNodeId();
    node->type = NodeType::kElement;
    node->node_name = tag_name;
    node->tag_name = std::move(tag_name);
    node->namespace_uri = namespace_uri;
    node->attrs = std::move(attrs);
    return node;
}

// Assembles a comment node wrapping "data" (the raw text between <!-- and
// -->). The "#comment" node name marks the payload as a comment, which the
// serializer emits without further processing.
//
//   CreateCommentNode(" TODO ") -> node { type: kComment,
//                                         node_name: "#comment",
//                                         data: " TODO " }
std::unique_ptr<Node> CreateCommentNode(std::string data) {
    auto node = std::make_unique<Node>();
    node->node_id = NextNodeId();
    node->type = NodeType::kComment;
    node->node_name = std::string(kCommentNodeName);
    node->data = std::move(data);
    return node;
}

// Assembles a text node holding "value" (character data that sits between
// tags). Adjacent text runs are merged by InsertText before this factory is
// reached, so plain content rarely produces more than one text node per
// span.
//
//   CreateTextNode("hello") -> node { type: kText,
//                                     node_name: "#text",
//                                     value: "hello" }
std::unique_ptr<Node> CreateTextNode(std::string value) {
    auto node = std::make_unique<Node>();
    node->node_id = NextNodeId();
    node->type = NodeType::kText;
    node->node_name = std::string(kTextNodeName);
    node->value = std::move(value);
    return node;
}

// Deep-copies "node" and everything reachable from it. The clone receives a
// fresh node_id (see NextNodeId) so originals and clones share no identity;
// if a clone_map is supplied, it records source_id -> clone so the bridge can
// later resolve an original element straight to its copied counterpart. The
// clone is wired to "parent", then template_content and every child are
// cloned recursively with the same map and the clone as their parent, keeping
// the copied tree's parent pointers and inner connectivity consistent.
// Location info is duplicated through owned copies so the clone never aliases
// the source's location storage.
//
//   CloneNode(<div id="a">text</div>, parent, map)
//     -> a fresh div with its own id, attrs, child text, and locations;
//        map[original.div.node_id] points at the clone
std::unique_ptr<Node> CloneNode(const Node& node, Node* parent,
                                std::unordered_map<uint32_t, Node*>* clone_map) {
    auto clone = std::make_unique<Node>();
    clone->node_id = NextNodeId();
    if (clone_map != nullptr) {
        clone_map->emplace(node.node_id, clone.get());
    }
    clone->type = node.type;
    clone->node_name = node.node_name;
    clone->mode = node.mode;
    clone->tag_name = node.tag_name;
    clone->namespace_uri = node.namespace_uri;
    clone->attrs = node.attrs;
    clone->value = node.value;
    clone->data = node.data;
    clone->doctype_name = node.doctype_name;
    clone->public_id = node.public_id;
    clone->system_id = node.system_id;
    clone->parent_node = parent;

    if (node.owned_location != nullptr) {
        clone->owned_location = std::make_unique<Location>(*node.owned_location);
        clone->source_code_location = clone->owned_location.get();
    }
    if (node.owned_start_tag_location != nullptr) {
        clone->owned_start_tag_location =
            std::make_unique<Location>(*node.owned_start_tag_location);
        clone->start_tag_location = clone->owned_start_tag_location.get();
    }
    if (node.owned_end_tag_location != nullptr) {
        clone->owned_end_tag_location =
            std::make_unique<Location>(*node.owned_end_tag_location);
        clone->end_tag_location = clone->owned_end_tag_location.get();
    }

    if (node.template_content != nullptr) {
        clone->template_content =
            CloneNode(*node.template_content, clone.get(), clone_map);
    }
    clone->child_nodes.reserve(node.child_nodes.size());
    for (const auto& child : node.child_nodes) {
        clone->child_nodes.push_back(CloneNode(*child, clone.get(), clone_map));
    }
    return clone;
}

// Tree mutation
//
// The mutators below perform the structural edits the tree builder performs
// as it parses markup and the bundler performs as it reorders resources.
// They all keep parent pointers in sync with child lists, so accidental
// orphaned or double-parented nodes are the exception rather than the norm.

// Makes "new_node" the last child of "parent_node", rewiring the child's
// parent pointer first. Ownership transfers to the parent's child list, so
// the caller must not use the handle afterwards.
//
//   AppendChild(document, text("hi"))
//     -> document.child_nodes = [..., text("hi")], text.parent_node = doc
void AppendChild(Node& parent_node, std::unique_ptr<Node> new_node) {
    new_node->parent_node = &parent_node;
    parent_node.child_nodes.push_back(std::move(new_node));
}

// Finds the position of "node" among "parent_node" children by pointer
// identity, or -1 when the node is not a direct child. Used as the basis for
// insertion and detach operations that need a stable index.
int32_t IndexofChild(const Node& parent_node, const Node& node) {
    for (size_t i = 0; i < parent_node.child_nodes.size(); i++) {
        if (parent_node.child_nodes[i].get() == &node) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// Returns the first child of "node", or nullptr for a childless node. The
// const overload exposes a read-only view, the mutable one lets callers edit
// the child in place.
Node* GetFirstChild(Node& node) {
    return node.child_nodes.empty() ? nullptr : node.child_nodes.front().get();
}

// Read-only counterpart of GetFirstChild(Node&), returning the same first
// child (or nullptr) without permitting mutation through the pointer.
const Node* GetFirstChild(const Node& node) {
    return node.child_nodes.empty() ? nullptr : node.child_nodes.front().get();
}

// Inserts "new_node" immediately before "reference_node" as a child of
// "parent_node". The reference must be a direct child; a reference that is
// not one yields an index of -1 (IndexofChild), which underflows to the
// front of the child list after the cast, so callers should pre-check.
// The parent pointer is updated and ownership is transferred into the list,
// so the caller drops its handle afterwards.
void InsertBefore(Node& parent_node, std::unique_ptr<Node> new_node,
                  const Node& reference_node) {
    const auto insertion_idx = static_cast<size_t>(IndexofChild(parent_node, reference_node));

    new_node->parent_node = &parent_node;
    parent_node.child_nodes.insert(parent_node.child_nodes.begin() +
                                       static_cast<ptrdiff_t>(insertion_idx),
                                   std::move(new_node));
}

// Attaches "content" (a document-fragment placeholder) as the inert body of
// a <template> element. Template content is stored separately from normal
// children so it can be produced or cloned without affecting layout.
void SetTemplateContent(Node& template_element, std::unique_ptr<Node> content) {
    template_element.template_content = std::move(content);
}

// Reads back the inert fragment stored on a <template> element. The pointer
// is asserted non-null by the caller's contract: templates created by the
// parser always carry a content fragment.
Node& GetTemplateContent(const Node& template_element) {
    return *template_element.template_content;
}

// Establishes the document's document-type info. When "document" already
// holds a doctype node, its name/public/system identifiers are overwritten
// in place (keeping the node's identity and position); otherwise a fresh
// kDocumentType node is appended as a child. This lets callers both amend a
// parsed doctype and inject one into a doctype-less document.
//
//   SetDocumentType(doc, "html", "", "") on a doc without doctype
//     -> appends node { type: kDocumentType, doctype_name: "html" }
void SetDocumentType(Node& document, std::string name, std::string public_id,
                     std::string system_id) {
    for (const auto& child : document.child_nodes) {
        if (IsDocumentTypeNode(*child)) {
            child->doctype_name = name;
            child->public_id = public_id;
            child->system_id = system_id;
            return;
        }
    }

    auto node = std::make_unique<Node>();
    node->node_id = NextNodeId();
    node->type = NodeType::kDocumentType;
    node->node_name = std::string(kDocumentTypeNodeName);
    node->doctype_name = std::move(name);
    node->public_id = std::move(public_id);
    node->system_id = std::move(system_id);
    AppendChild(document, std::move(node));
}

// Stores the rendering mode selected for "document" (no-quirks, quirks, or
// limited-quirks) on the document node; tree walkers read it back with
// GetDocumentMode instead of recomputing from the doctype.
void SetDocumentMode(Node& document, DocumentMode mode) { document.mode = mode; }

// Returns the rendering mode previously stored by SetDocumentMode, defaulting
// to kNoQuirks on nodes that never had one assigned (for example document
// fragments).
DocumentMode GetDocumentMode(const Node& document) { return document.mode; }

// Removes "node" from its parent's child list, detaches its parent pointer,
// and returns the owning unique_ptr so the caller can reinsert or destroy it.
// Returns nullptr when the node is already parentless, which is also the
// safe answer for a node whose parent's list no longer contains it.
std::unique_ptr<Node> DetachNode(Node& node) {
    if (node.parent_node == nullptr) {
        return nullptr;
    }

    Node& parent = *node.parent_node;
    for (auto it = parent.child_nodes.begin(); it != parent.child_nodes.end(); ++it) {
        if (it->get() == &node) {
            auto owned = std::move(*it);
            parent.child_nodes.erase(it);
            node.parent_node = nullptr;
            return owned;
        }
    }

    return nullptr;
}

// Appends "text" as child "parent_node". Runs of text are coalesced: when
// the last existing child is a text node, "text" extends that node's value
// in place so markup like "a" then "b" produces one "#text" node rather than
// two siblings; otherwise a fresh text node is created and appended.
//
//   children [<i>…</i>, #text "he"], InsertText "llo"
//     -> single #text "hello"; otherwise a new #text child
void InsertText(Node& parent_node, std::string_view text) {
    if (!parent_node.child_nodes.empty()) {
        Node& prev_node = *parent_node.child_nodes.back();
        if (IsTextNode(prev_node)) {
            prev_node.value += text;
            return;
        }
    }

    AppendChild(parent_node, CreateTextNode(std::string(text)));
}

// Inserts "text" into "parent_node" just before "reference_node", again
// merging into an immediately preceding text sibling when one exists so
// adjacent character data stays in a single node. With no preceding text
// sibling, a new text node is placed before the reference via InsertBefore.
//
//   children [#text "a"], reference <b>, InsertTextBefore "x"
//     -> #text "ax", then <b>
void InsertTextBefore(Node& parent_node, std::string_view text,
                      const Node& reference_node) {
    const auto reference_idx = IndexofChild(parent_node, reference_node);

    if (reference_idx > 0) {
        Node& prev_node = *parent_node.child_nodes[static_cast<size_t>(reference_idx - 1)];
        if (IsTextNode(prev_node)) {
            prev_node.value += text;
            return;
        }
    }

    InsertBefore(parent_node, CreateTextNode(std::string(text)), reference_node);
}

// Copies an attribute vector into "recipient" without producing duplicates:
// an attribute whose name already exists on the recipient is skipped (the
// recipient's existing value wins); every other attribute is appended
// verbatim. Used when elements absorb attributes from a malformed or
// surrogate parent during tree construction.
//
//   recipient [id="a"], adopt [class="x", id="b"]
//     -> recipient [id="a", class="x"]
void AdoptAttributes(Node& recipient, const std::vector<Attribute>& attrs) {
    for (const Attribute& attr : attrs) {
        bool found = false;
        for (const Attribute& recipient_attr : recipient.attrs) {
            if (recipient_attr.name == attr.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            recipient.attrs.push_back(attr);
        }
    }
}

// Source code location
//
// Location bookkeeping is deliberately lazy: a node only acquires owned
// location storage when a caller first asks to update it, keeping memory flat
// for large documents that never need source spans.

// Merges the optional fields of "update" into "node"'s source location,
// leaving any unset dimension untouched. If the node has no location yet, an
// owned Location is allocated on first use and the public pointer resolved to
// it, so subsequent updates mutate the same storage.
void UpdateNodeSourceCodeLocation(Node& node, const LocationUpdate& update) {
    if (node.source_code_location == nullptr) {
        node.owned_location = std::make_unique<Location>();
        node.source_code_location = node.owned_location.get();
    }

    Location& location = const_cast<Location&>(*node.source_code_location);

    if (update.start_line.has_value()) location.start_line = *update.start_line;
    if (update.start_col.has_value()) location.start_col = *update.start_col;
    if (update.start_offset.has_value()) location.start_offset = *update.start_offset;
    if (update.end_line.has_value()) location.end_line = *update.end_line;
    if (update.end_col.has_value()) location.end_col = *update.end_col;
    if (update.end_offset.has_value()) location.end_offset = *update.end_offset;
}

}
