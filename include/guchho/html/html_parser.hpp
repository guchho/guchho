#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <string_view>
#include <vector>

#include "guchho/html/html_ast.hpp"
#include "guchho/html/html_lexer.hpp"

namespace guchho::html {

// The parser's insertion mode: the current state of the token-processing
// machine. Each mode answers "how does this token change the tree?" and
// implements one section of the parsing algorithm. Modes are entered as the
// document's structure is discovered (before html, in head, in body, in
// table, ...). Two modes are transient: kText and kInTableText buffer tokens
// while the parser re-decides what to do with them, and kInTemplate pushes a
// saved mode onto a stack so parsing can return to the enclosing context
// when a </template> closes.
enum class InsertionMode : uint8_t {
    kInitial,
    kBeforeHtml,
    kBeforeHead,
    kInHead,
    kInHeadNoScript,
    kAfterHead,
    kInBody,
    kText,
    kInTable,
    kInTableText,
    kInCaption,
    kInColumnGroup,
    kInTableBody,
    kInRow,
    kInCell,
    kInSelect,
    kInSelectInTable,
    kInTemplate,
    kAfterBody,
    kInFrameset,
    kAfterFrameset,
    kAfterAfterBody,
    kAfterAfterFrameset,
};

// Concrete tree adapter for the Guchho AST. The open element stack and the
// formatting element list are written generically against a small adapter
// contract (namespace lookup, template content, tag name, attribute list);
// this struct satisfies that contract directly on Node, so those containers
// never special-case the AST and stay reusable across trees.
struct DefaultTreeAdapter {
    using ParentNode = Node;
    using Element = Node;

    static NS GetNamespaceURI(const Node& node) { return node.namespace_uri; }

    static Node* GetTemplateContent(const Node& node) {
        return node.template_content.get();
    }

    static std::string_view GetTagName(const Node& node) { return node.tag_name; }

    static const std::vector<Attribute>& GetAttrList(const Node& node) {
        return node.attrs;
    }
};

// Configuration for a parsing run. Every field is optional and keeps a
// parser-safe default, so ParserOptions{} parses an ordinary document.
struct ParserOptions {
    // The scripting flag: if true, <noscript> content is parsed as text.
    bool scripting_enabled = true;
    // When true, every produced node carries a source-code location. Needed
    // for error reporting and downstream passes that want original offsets.
    bool source_code_location_info = false;
    // Callback invoked for every parse error, with a ParserError carrying
    // the code and location. Setting it automatically enables location
    // tracking so reported errors always have coordinates.
    std::function<void(const ParserError&)> on_parse_error;
};

// A type-erased handle to whichever token is currently being processed.
// TokenHandler callbacks each receive their own concrete token type, but
// several routines must re-dispatch an arbitrary token through the public
// entry points; AnyToken stores the active token of whichever kind and
// answers kind, location and tag queries without exposing the union.
class AnyToken {
public:
    AnyToken(CharacterToken& token)
        : type_(token.type), character_(&token), tag_(nullptr), comment_(nullptr),
          doctype_(nullptr), eof_(nullptr) {}
    AnyToken(CommentToken& token)
        : type_(TokenType::kComment), character_(nullptr), tag_(nullptr),
          comment_(&token), doctype_(nullptr), eof_(nullptr) {}
    AnyToken(DoctypeToken& token)
        : type_(TokenType::kDoctype), character_(nullptr), tag_(nullptr),
          comment_(nullptr), doctype_(&token), eof_(nullptr) {}
    AnyToken(EofToken& token)
        : type_(TokenType::kEof), character_(nullptr), tag_(nullptr),
          comment_(nullptr), doctype_(nullptr), eof_(&token) {}
    AnyToken(TagToken& token)
        : type_(token.type), character_(nullptr), tag_(&token), comment_(nullptr),
          doctype_(nullptr), eof_(nullptr) {}

    TokenType type() const { return type_; }

    // Source location of the token it wraps, or null when the token carries
    // no location. Valid only while the underlying token is alive (that is,
    // during handler processing).
    const Location* location() const {
        switch (type_) {
            case TokenType::kCharacter:
            case TokenType::kNullCharacter:
            case TokenType::kWhitespaceCharacter:
                return character_->location;
            case TokenType::kStartTag:
            case TokenType::kEndTag:
                return tag_->location;
            case TokenType::kComment:
                return comment_->location;
            case TokenType::kDoctype:
                return doctype_->location;
            case TokenType::kEof:
                return eof_->location;
            default:
                return nullptr;
        }
    }

    // Tag name of the wrapped token; only meaningful for start/end tag
    // tokens (empty for every other kind).
    std::string_view tag_name() const { return tag_ != nullptr ? tag_->tag_name : std::string_view{}; }
    TagId tag_id() const { return tag_ != nullptr ? tag_->tag_id : TagId::kUnknown; }
    bool self_closing() const { return tag_ != nullptr && tag_->self_closing; }
    bool ack_self_closing() const { return tag_ != nullptr && tag_->ack_self_closing; }
    void set_ack_self_closing(bool value) {
        if (tag_ != nullptr) {
            tag_->ack_self_closing = value;
        }
    }
    bool has_tag() const { return tag_ != nullptr; }
    TagToken& tag() const { return *tag_; }
    CharacterToken& character() const { return *character_; }
    CommentToken& comment() const { return *comment_; }
    DoctypeToken& doctype() const { return *doctype_; }
    EofToken& eof() const { return *eof_; }

private:
    TokenType type_;
    CharacterToken* character_;
    TagToken* tag_;
    CommentToken* comment_;
    DoctypeToken* doctype_;
    EofToken* eof_;
};

// ---------------------------------------------------------------------------
// Open-element stack
//
// The stack of elements currently open: those whose start tag has been seen
// but whose end tag has not yet been processed. The parser appends new
// children to the top element, so this stack is where "where do I put the
// next node?" is answered, and almost every insertion mode decision boils
// down to a scope check against it. The bottom entry is always the root
// <html> element.
//
// The stack is generic over a tree adapter ("Adapter") that must provide:
//
//     using ParentNode = <document | fragment | element | template>;
//     static NS GetNamespaceURI(const ParentNode&);
//     static ParentNode* GetTemplateContent(const ParentNode&);
//
// so it can read namespace and template content without knowing the AST.
// ---------------------------------------------------------------------------

// Elements whose end tags may be implied by the parser: while such an
// element is on top of the stack, several start tags first generate its
// end tag implicitly. The "thoroughly" variant covers the extra set used by
// the in-table and text handling.
//
// Example: with <li> on top, a following <li> start tag implicitly closes
//          the previous one (IsImplicitEndTagRequired(kLi)).
bool IsImplicitEndTagRequired(TagId tag_id);
bool IsImplicitEndTagRequiredThoroughly(TagId tag_id);

// Scoping element sets. Scope checks scan the open-element stack from the
// top and stop at the first element whose tag belongs to the relevant scope
// set (html scope, list-item scope, button scope). These predicates classify
// a tag into the HTML, HTML list, HTML button, MathML and SVG scope sets.
bool IsScopingElementHtml(TagId tag_id);
bool IsScopingElementHtmlList(TagId tag_id);
bool IsScopingElementHtmlButton(TagId tag_id);
bool IsScopingElementMathml(TagId tag_id);
bool IsScopingElementSvg(TagId tag_id);

// Table context sets. These tell the in-table insertion modes how far up the
// stack a scan may extend before an element is declared "in scope": the row,
// body and table boundaries and the set of table cells that block certain
// parsing steps.
bool IsTableRowContext(TagId tag_id);
bool IsTableBodyContext(TagId tag_id);
bool IsTableContext(TagId tag_id);
bool IsTableCell(TagId tag_id);

// Observer interface for the open-element stack. The parser implements it so
// every push/pop can run mode-specific bookkeeping (e.g. recomputing the
// current context after the top changes). "is_top" distinguishes events at
// the top of the stack from mid-stack mutations (Removes, replaces).
template <typename Adapter>
class OpenElementStack;

template <typename Adapter>
class StackHandler {
public:
    using Node = typename Adapter::ParentNode;

    virtual ~StackHandler() = default;

    // Called after "node" of tag "tid" was pushed; "is_top" is true when the
    // push happened at the top of the stack.
    virtual void OnItemPush(Node& node, TagId tid, bool is_top) = 0;

    // Called after "node" was popped; "is_top" is true when it left from the
    // top of the stack.
    virtual void OnItemPop(Node& node, bool is_top) = 0;
};

// State of the open-element stack. "items" and "tag_ids" are parallel arrays
// (same index, same element); "current" and "current_tag_id" cache the top
// entry for constant-time reads, "current_tag_id" being kUnknown when the
// top element has no recognized tag, and "tmpl_count" tracks how many
// <template> elements are open so template scope is detectable without a
// scan. "stack_top" is the index of the top entry, -1 when empty.
template <typename Adapter>
class OpenElementStack {
public:
    using Node = typename Adapter::ParentNode;

    std::vector<Node*> items;
    std::vector<TagId> tag_ids;
    Node* current = nullptr;
    int stack_top = -1;
    int tmpl_count = 0;
    TagId current_tag_id = TagId::kUnknown;

    // Creates the stack over "document"; the document node becomes the
    // initial "current" but never occupies a stack slot (it has no tag).
    // "handler" receives every later push/pop notification.
    explicit OpenElementStack(Node& document, StackHandler<Adapter>& handler)
        : handler_(&handler) {
        current = &document;
    }

    // Returns the template content fragment when the current element is a
    // <template> (its children belong to the fragment, not to the element),
    // and the current element itself otherwise.
    //
    // Example: current is a <template> -> its content fragment; current is a
    //          <div> -> the <div> node.
    Node* CurrentTmplContentOrNode() const {
        return IsInTemplate() ? Adapter::GetTemplateContent(*current) : current;
    }

    // Mutations

    // Pushes "element" of tag "tag_id" onto the top of the stack, reusing
    // existing vector slots instead of resizing when possible. Updates the
    // top-of-stack caches, bumps the template count when entering a
    // <template>, and reports the push to the handler.
    //
    // Example: stack [html] before Push(body) -> [html, body] with
    //          current == body.
    void Push(Node& element, TagId tag_id) {
        ++stack_top;

        if (stack_top < static_cast<int>(items.size())) {
            items[stack_top] = &element;
            tag_ids[stack_top] = tag_id;
        } else {
            items.push_back(&element);
            tag_ids.push_back(tag_id);
        }
        current = &element;
        current_tag_id = tag_id;

        if (IsInTemplate()) {
            ++tmpl_count;
        }

        handler_->OnItemPush(element, tag_id, true);
    }

    // Pops the current (top) element. Decrements the template count when
    // leaving a <template>, re-caches the new top (before notifying the
    // handler, so the observer sees the already-updated stack), and reports
    // the pop.
    void Pop() {
        Node* popped = current;
        if (tmpl_count > 0 && IsInTemplate()) {
            --tmpl_count;
        }

        --stack_top;
        UpdateCurrentElement();

        handler_->OnItemPop(*popped, true);
    }

    // Replaces "old_element" in position with "new_element" (used when a
    // mis-nested element is re-parented). When the replaced element was on
    // top, the cached current node is repointed at the replacement.
    void Replace(Node& old_element, Node& new_element) {
        const int idx = IndexOf(&old_element);

        items[idx] = &new_element;

        if (idx == stack_top) {
            current = &new_element;
        }
    }

    // Inserts "new_element" of tag "new_element_id" directly above
    // "reference_element", keeping the parallel arrays aligned. When the
    // insertion lands on top of the stack the caches are refreshed and a
    // push is reported for the new top.
    void InsertAfter(Node& reference_element, Node& new_element, TagId new_element_id) {
        const int insertion_idx = IndexOf(&reference_element) + 1;

        items.insert(items.begin() + insertion_idx, &new_element);
        tag_ids.insert(tag_ids.begin() + insertion_idx, new_element_id);
        ++stack_top;

        if (insertion_idx == stack_top) {
            UpdateCurrentElement();
        }

        if (current != nullptr && current_tag_id != TagId::kUnknown) {
            handler_->OnItemPush(*current, current_tag_id, insertion_idx == stack_top);
        }
    }

    // Pops elements until (and including) the topmost HTML element of the
    // given tag is removed; foreign-content entries with that tag are
    // skipped. When no such element is found the stack is emptied to the
    // root slot.
    //
    // Example: stack [html, body, p] -> PopUntilTagNamePopped(kP) yields
    //          [html, body].
    void PopUntilTagNamePopped(TagId tag_id) {
        int target_idx = stack_top + 1;

        do {
            target_idx = LastIndexOf(tag_id, target_idx - 1);
        } while (target_idx > 0 && Adapter::GetNamespaceURI(*items[target_idx]) != NS::kHtml);

        ShortenToLength(std::max(target_idx, 0));
    }

    // Pops elements down to length "idx" (the element at "idx" stays on the
    // stack). Each pop is reported; the final one flags "is_top" so
    // observers can act on the outcome. This is the shared engine behind the
    // PopUntil*, PopAllUpTo* and ClearBackTo* helpers.
    void ShortenToLength(int idx) {
        while (stack_top >= idx) {
            Node* popped = current;

            if (tmpl_count > 0 && IsInTemplate()) {
                tmpl_count -= 1;
            }

            --stack_top;
            UpdateCurrentElement();

            handler_->OnItemPop(*popped, stack_top < idx);
        }
    }

    // Pops until the given "element" is removed from the stack (or the stack
    // is reduced to the root slot).
    void PopUntilElementPopped(Node& element) {
        const int idx = IndexOf(&element);
        ShortenToLength(std::max(idx, 0));
    }

    // Pops elements until a numbered header (h1..h6) is removed. Falls back
    // to emptying the stack when no header is open.
    void PopUntilNumberedHeaderPopped() {
        PopUntilTagNamesPopped(IsNumberedHeader);
    }

    // Pops elements until a table cell is removed; a no-op when no cell is
    // in the stack.
    void PopUntilTableCellPopped() {
        PopUntilTagNamesPopped(IsTableCell);
    }

    // Pops everything above the root <html> element, resetting the template
    // count (the parser guarantees the root is always the bottom entry, so
    // the stack is simply shortened to length one).
    void PopAllUpToHtmlElement() {
        tmpl_count = 0;
        ShortenToLength(1);
    }

    // Clears the stack back to (but not including) the nearest HTML element
    // of the table context set, re-establishing the table-structure context.
    void ClearBackToTableContext() {
        ClearBackTo(IsTableContext);
    }

    // Like ClearBackToTableContext but stops at a table body boundary
    // (<tbody>/<thead>/<tfoot>).
    void ClearBackToTableBodyContext() {
        ClearBackTo(IsTableBodyContext);
    }

    // Like ClearBackToTableContext but stops at a table row boundary (<tr>).
    void ClearBackToTableRowContext() {
        ClearBackTo(IsTableRowContext);
    }

    // Removes "element" from the stack wherever it sits. A top-of-stack
    // removal reuses Pop() (reported with is_top); a mid-stack removal
    // erases both parallel entries, re-caches the top, and reports a
    // non-top pop.
    void Remove(Node& element) {
        const int idx = IndexOf(&element);

        if (idx >= 0) {
            if (idx == stack_top) {
                Pop();
            } else {
                items.erase(items.begin() + idx);
                tag_ids.erase(tag_ids.begin() + idx);
                --stack_top;
                UpdateCurrentElement();
                handler_->OnItemPop(element, false);
            }
        }
    }

    // Search

    // Returns the <body> element when it is properly nested -- the expected
    // second entry of the stack at index 1 -- and null otherwise, in which
    // case the body never appeared explicitly in the input.
    Node* TryPeekProperlyNestedBodyElement() const {
        return stack_top >= 1 && tag_ids[1] == TagId::kBody ? items[1] : nullptr;
    }

    // True when "element" is anywhere on the stack.
    bool Contains(const Node& element) const {
        return IndexOf(&element) > -1;
    }

    // The entry directly below "element", i.e. its would-be parent under the
    // stack ordering, or null when "element" is the bottom entry. Drives the
    // adoption-agency bookkeeping and foster-parenting scans.
    Node* GetCommonAncestor(const Node& element) const {
        const int element_idx = IndexOf(&element) - 1;
        return element_idx >= 0 ? items[element_idx] : nullptr;
    }

    // True when the stack holds only the root <html> element.
    bool IsRootHtmlElementCurrent() const {
        return stack_top == 0 && tag_ids[0] == TagId::kHtml;
    }

    // Element in scope

    // True when an element of tag "tag_name" is in scope: scanning from the
    // top, the tag is seen before any HTML scoping boundary. The list-item
    // and button variants use their own (tighter) scoping sets so list and
    // form-button mis-nesting is handled exactly per the rules.
    //
    // Example: stack [html, body, li] -> HasInButtonScope(kLi) is true
    //          (the li is seen before any scoping boundary); stack
    //          [html, body] -> HasInButtonScope(kP) is false (scanning hits
    //          <html>, a button-scoping boundary, without seeing <p>).
    bool HasInScope(TagId tag_name) const {
        return HasInDynamicScope(tag_name, IsScopingElementHtml);
    }

    bool HasInListItemScope(TagId tag_name) const {
        return HasInDynamicScope(tag_name, IsScopingElementHtmlList);
    }

    bool HasInButtonScope(TagId tag_name) const {
        return HasInDynamicScope(tag_name, IsScopingElementHtmlButton);
    }

    // True when a numbered header (h1..h6) is in scope under the HTML
    // scoping rules. Returns true when the scan is exhausted before any
    // boundary is crossed (the algorithm's default).
    bool HasNumberedHeaderInScope() const {
        for (int i = stack_top; i >= 0; i--) {
            const TagId tn = tag_ids[i];

            switch (Adapter::GetNamespaceURI(*items[i])) {
                case NS::kHtml: {
                    if (IsNumberedHeader(tn)) return true;
                    if (IsScopingElementHtml(tn)) return false;
                    break;
                }
                case NS::kSvg: {
                    if (IsScopingElementSvg(tn)) return false;
                    break;
                }
                case NS::kMathml: {
                    if (IsScopingElementMathml(tn)) return false;
                    break;
                }
                default:
                    break;
            }
        }

        return true;
    }

    // Table-scope test: scans only HTML elements and stops at the first
    // <table> or <html>. Seeing a match first returns true, hitting a
    // <table>/<html> first returns false, and exhausting the stack returns
    // true (the spec's default for tokens that would be in scope otherwise).
    bool HasInTableScope(TagId tag_name) const {
        for (int i = stack_top; i >= 0; i--) {
            if (Adapter::GetNamespaceURI(*items[i]) != NS::kHtml) {
                continue;
            }

            const TagId tn = tag_ids[i];
            if (tn == tag_name) {
                return true;
            }
            if (tn == TagId::kTable || tn == TagId::kHtml) {
                return false;
            }
        }

        return true;
    }

    // True when a table body context (<tbody>/<thead>/<tfoot>) is in table
    // scope: the scan stops at the first <table> or <html> (false) or at a
    // matching body row (true), and defaults to true when the stack is
    // exhausted first.
    bool HasTableBodyContextInTableScope() const {
        for (int i = stack_top; i >= 0; i--) {
            if (Adapter::GetNamespaceURI(*items[i]) != NS::kHtml) {
                continue;
            }

            switch (tag_ids[i]) {
                case TagId::kTbody:
                case TagId::kThead:
                case TagId::kTfoot: {
                    return true;
                }
                case TagId::kTable:
                case TagId::kHtml: {
                    return false;
                }
                default:
                    break;
            }
        }

        return true;
    }

    // Select-scope test: true when a matching element is seen before any
    // non-<option>/<optgroup> HTML element, so those two never terminate the
    // scan. Falls back to true when the stack is exhausted.
    bool HasInSelectScope(TagId tag_name) const {
        for (int i = stack_top; i >= 0; i--) {
            if (Adapter::GetNamespaceURI(*items[i]) != NS::kHtml) {
                continue;
            }

            const TagId tn = tag_ids[i];
            if (tn == tag_name) {
                return true;
            }
            if (tn != TagId::kOption && tn != TagId::kOptgroup) {
                return false;
            }
        }

        return true;
    }

    // Implied end tags

    // Pops every implied-end-tag element currently on top (li, dt, dd,
    // option, ...) so a following block opener can close them implicitly.
    //
    // Example: top = [html, body, li, dd] -> GenerateImpliedEndTags pops dd
    //          then li, leaving [html, body].
    void GenerateImpliedEndTags() {
        while (current_tag_id != TagId::kUnknown && IsImplicitEndTagRequired(current_tag_id)) {
            Pop();
        }
    }

    // Like GenerateImpliedEndTags but also pops the elements of the
    // "thorough" set (adding p and the table-cell implied tags), used where
    // the rules require a more complete cleanup before re-processing a
    // token.
    void GenerateImpliedEndTagsThoroughly() {
        while (current_tag_id != TagId::kUnknown &&
               IsImplicitEndTagRequiredThoroughly(current_tag_id)) {
            Pop();
        }
    }

    // Pops implied-end-tag elements except the explicitly excluded one: a
    // start tag may close its siblings while leaving itself open.
    //
    // Example: in a cell with [html, ..., td, p] on top and exclusion kP,
    //          the p stays and any implied-end-tag elements above it pop.
    void GenerateImpliedEndTagsWithExclusion(TagId exclusion_id) {
        while (
            current_tag_id != TagId::kUnknown && current_tag_id != exclusion_id &&
            IsImplicitEndTagRequiredThoroughly(current_tag_id)) {
            Pop();
        }
    }

private:
    StackHandler<Adapter>* handler_;

    // Index of the topmost entry equal to "element", or -1 when absent.
    int IndexOf(const Node* element) const {
        for (int i = stack_top; i >= 0; i--) {
            if (items[i] == element) {
                return i;
            }
        }
        return -1;
    }

    // Backward index search for "tag_id" starting at "from" (clamped to the
    // vector's bounds), or -1 when not found.
    int LastIndexOf(TagId tag_id, int from) const {
        from = std::min(from, static_cast<int>(tag_ids.size()) - 1);
        for (int i = from; i >= 0; i--) {
            if (tag_ids[i] == tag_id) {
                return i;
            }
        }
        return -1;
    }

    // True when the current element is a HTML <template>, i.e. parsing is
    // currently inside a template's content.
    bool IsInTemplate() const {
        return current_tag_id == TagId::kTemplate && current != nullptr &&
               Adapter::GetNamespaceURI(*current) == NS::kHtml;
    }

    // Re-derives "current" and "current_tag_id" from the top of the stack,
    // clearing both when the stack is empty.
    void UpdateCurrentElement() {
        current = stack_top >= 0 ? items[stack_top] : nullptr;
        current_tag_id = stack_top >= 0 ? tag_ids[stack_top] : TagId::kUnknown;
    }

    // Pops elements until the topmost HTML element whose tag passes "match"
    // is removed (falling back to emptying the stack when none matches).
    void PopUntilTagNamesPopped(bool (*match)(TagId)) {
        const int idx = IndexOfTagNames(match, NS::kHtml);
        ShortenToLength(std::max(idx, 0));
    }

    // Topmost index whose tag ID matches "match" within namespace "ns", or
    // -1 when no such element exists.
    int IndexOfTagNames(bool (*match)(TagId), NS ns) const {
        for (int i = stack_top; i >= 0; i--) {
            if (match(tag_ids[i]) && Adapter::GetNamespaceURI(*items[i]) == ns) {
                return i;
            }
        }
        return -1;
    }

    // Shortens the stack to just above the nearest HTML element matching
    // "match"; with no match the stack is emptied to the root slot.
    void ClearBackTo(bool (*match)(TagId)) {
        const int idx = IndexOfTagNames(match, NS::kHtml);
        ShortenToLength(idx + 1);
    }

    // Shared scope walker used by every HasIn*Scope variant: scans upward
    // and reports whether "tag_name" appears before a boundary element is
    // crossed. Boundaries are chosen per namespace -- the HTML set comes from
    // "html_scope", MathML and SVG use their own scoping sets.
    bool HasInDynamicScope(TagId tag_name, bool (*html_scope)(TagId)) const {
        for (int i = stack_top; i >= 0; i--) {
            const TagId tn = tag_ids[i];

            switch (Adapter::GetNamespaceURI(*items[i])) {
                case NS::kHtml: {
                    if (tn == tag_name) return true;
                    if (html_scope(tn)) return false;
                    break;
                }
                case NS::kSvg: {
                    if (IsScopingElementSvg(tn)) return false;
                    break;
                }
                case NS::kMathml: {
                    if (IsScopingElementMathml(tn)) return false;
                    break;
                }
                default:
                    break;
            }
        }

        return true;
    }
};

// ---------------------------------------------------------------------------
// List of active formatting elements
//
// The formatting elements currently open: b, i, strong, em, a and similar
// tags whose formatting must be re-created when mis-nested content forces
// the tree to be rearranged (the adoption agency algorithm). Entries are
// kept newest-first in a doubly-linked list; list nodes keep their addresses
// stable across insertions, which matters because the "bookmark" used while
// re-inserting elements is a raw pointer into the list.
//
// The list is generic over a tree adapter ("Adapter") that must provide:
//
//     using Element = <element | template>;
//     static std::string_view GetTagName(const Element&);
//     static NS GetNamespaceURI(const Element&);
//     static const std::vector<Attribute>& GetAttrList(const Element&);
//
// so identical-element detection (the Noah Ark condition) can read tag,
// namespace and attributes without knowing the AST.
// ---------------------------------------------------------------------------

// Cap for the Noah Ark condition: at most this many formatting elements with
// the same tag and attributes may be active before older duplicates start
// being dropped when a new one is added.
inline constexpr int kNoahArkCapacity = 3;

// What a list entry is: a marker (a context boundary that stops scope scans
// and reconstruction) or a real formatting element.
enum class EntryType : uint8_t {
    kMarker,
    kElement,
};

// A single entry of the list. "element" and "token" are only meaningful when
// "type" is kElement; marker entries leave both null. The token is retained
// so a re-created element can reproduce the original attributes exactly.
template <typename Adapter>
struct FormattingEntry {
    EntryType type;
    typename Adapter::Element* element = nullptr;
    const TagToken* token = nullptr;
};

// The list of active formatting elements. "entries" holds the newest entry
// at the front; "bookmark" points at the entry the adoption agency is
// currently working from (null when idle). Two rules govern every operation:
// markers are never touched by element-level mutations, and lookups stop at
// the first marker encountered.
template <typename Adapter>
class FormattingElementList {
public:
    using Node = typename Adapter::Element;
    using Entry = FormattingEntry<Adapter>;

    // Newest entries first: the front of the list is the top.
    std::list<Entry> entries;

    // Entry the adoption agency algorithm re-inserts around; may be null.
    Entry* bookmark = nullptr;

    explicit FormattingElementList() = default;

    // Mutations

    // Pushes a marker entry onto the front, sealing off the elements below
    // it from later reconstruction and scope scans.
    void InsertMarker() { entries.insert(entries.begin(), Entry{EntryType::kMarker, nullptr, nullptr}); }

    // Records "element" (with its tag "token") as an active formatting
    // element, enforcing the Noah Ark condition first so the list cannot
    // grow unboundedly with identical entries.
    void PushElement(Node& element, const TagToken& token) {
        EnsureNoahArkCondition(element);

        entries.insert(entries.begin(), Entry{EntryType::kElement, &element, &token});
    }

    // Inserts "element" directly after the current bookmark. When the
    // bookmark entry was already removed from the list, falls back to
    // inserting at the front -- the same outcome the adoption agency expects
    // when its mark vanished mid-pass.
    void InsertElementAfterBookmark(Node& element, const TagToken& token) {
        auto it = FindEntry(bookmark);

        if (it == entries.end()) {
            it = entries.begin();
        }

        entries.insert(it, Entry{EntryType::kElement, &element, &token});
    }

    // Removes "entry" from the list. A missing entry is silently tolerated:
    // the adoption agency may already have dropped it in an earlier pass.
    void RemoveEntry(Entry& entry) {
        auto it = FindEntry(&entry);

        if (it == entries.end()) {
            return;
        }

        entries.erase(it);
    }

    // Clears every entry above the last marker, leaving that marker itself
    // in place; a list with no marker is emptied entirely. Done when a table
    // context boundary is reopened so stale formatting cannot leak across
    // tables.
    //
    // Example: entries [b, i, marker, em] -> [marker]; entries [b, i] -> [].
    void ClearToLastMarker() {
        auto it = entries.begin();
        while (it != entries.end() && it->type != EntryType::kMarker) {
            ++it;
        }

        if (it == entries.end()) {
            entries.clear();
        } else {
            ++it;
            entries.erase(entries.begin(), it);
        }
    }

    // Search

    // The newest entry whose element's tag equals "tag_name", scanning until
    // the first marker. Returns null both when the scan hits a marker first
    // and when nothing matches, so a marker always stops the search.
    Entry* GetElementEntryInScopeWithTagName(std::string_view tag_name) {
        for (Entry& entry : entries) {
            if (entry.type == EntryType::kMarker ||
                Adapter::GetTagName(*entry.element) == tag_name) {
                return entry.type == EntryType::kElement ? &entry : nullptr;
            }
        }
        return nullptr;
    }

    // The entry whose element pointer equals "element", or null when that
    // element is not listed. Used to locate an existing formatting element
    // for removal or re-insertion.
    Entry* GetElementEntry(const Node* element) {
        for (Entry& entry : entries) {
            if (entry.type == EntryType::kElement && entry.element == element) {
                return &entry;
            }
        }
        return nullptr;
    }

private:
    // Iterator of the entry matching the given pointer, or end() when it is
    // no longer in the list.
    typename std::list<Entry>::iterator FindEntry(Entry* entry) {
        auto it = entries.begin();
        while (it != entries.end() && &*it != entry) {
            ++it;
        }
        return it;
    }

    // Collects candidate entries that could collide with "new_element" under
    // the Noah Ark rule: same tag and namespace and an attribute list of the
    // same length. This is a cheap pre-filter; exhaustive attribute equality
    // is left to the caller. Scanning stops at the first marker.
    std::list<typename std::list<Entry>::iterator> GetNoahArkConditionCandidates(
        const Node& new_element,
        const std::vector<Attribute>& ne_attrs) {
        std::list<typename std::list<Entry>::iterator> candidates;

        const size_t ne_attrs_length = ne_attrs.size();
        const std::string_view ne_tag_name = Adapter::GetTagName(new_element);
        const NS ne_namespace_uri = Adapter::GetNamespaceURI(new_element);

        for (auto it = entries.begin(); it != entries.end(); ++it) {
            const Entry& entry = *it;

            if (entry.type == EntryType::kMarker) {
                break;
            }

            const Node* element = entry.element;

            if (
                Adapter::GetTagName(*element) == ne_tag_name &&
                Adapter::GetNamespaceURI(*element) == ne_namespace_uri) {
                const std::vector<Attribute>& element_attrs = Adapter::GetAttrList(*element);

                if (element_attrs.size() == ne_attrs_length) {
                    candidates.push_back(it);
                }
            }
        }

        return candidates;
    }

    // Enforces the Noah Ark condition before "new_element" is added. As long
    // as fewer than three duplicate-looking candidates exist the list simply
    // grows, but when a third element matching the new one would take the
    // list over capacity, the bottommost such entry is erased so at most two
    // identical formatting elements remain active.
    void EnsureNoahArkCondition(const Node& new_element) {
        if (entries.size() < static_cast<size_t>(kNoahArkCapacity)) return;

        const std::vector<Attribute>& ne_attrs = Adapter::GetAttrList(new_element);
        const auto candidates = GetNoahArkConditionCandidates(new_element, ne_attrs);

        if (candidates.size() < static_cast<size_t>(kNoahArkCapacity)) return;

        int valid_candidates = 0;

        // Walk the candidate list bottom-up and drop the first entry proven
        // to be attribute-for-attribute identical to the newcomer.
        for (const auto& candidate_it : candidates) {
            const std::vector<Attribute>& attrs =
                Adapter::GetAttrList(*candidate_it->element);

            bool all_match = !attrs.empty();
            for (const Attribute& c_attr : attrs) {
                bool found = false;
                for (const Attribute& ne_attr : ne_attrs) {
                    if (ne_attr.name == c_attr.name && ne_attr.value == c_attr.value) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    all_match = false;
                    break;
                }
            }

            if (all_match && ++valid_candidates >= kNoahArkCapacity) {
                entries.erase(candidate_it);
                break;
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Foreign content (MathML/SVG)
//
// Elements inside <math> or <svg> are parsed under foreign-content rules:
// tag and attribute names are adjusted to the foreign vocabulary, integration
// points mark where HTML parsing resumes, and some start tags make the parser
// leave foreign content entirely. The helpers below implement those
// adjustments and the exit/integration-point tests.
// ---------------------------------------------------------------------------

// True when "start_tag_token" begins an element that exits foreign content
// back into HTML parsing: an HTML tag used inside <svg> or <math> that has no
// foreign counterpart (e.g. an HTML element such as <p> nested in foreign
// content).
bool CausesExit(const TagToken& start_tag_token);

// Rewrites the token in place to the normalized foreign spelling, so both
// tree construction and output see the adjusted names. Each routine targets
// one vocabulary: MathML attribute mappings (camelCase and xlink links),
// SVG attribute mappings (e.g. xlink:href), the xml:* attribute canonical
// spellings, and the SVG camelCase tag replacements (e.g. "linearGradient").
void AdjustTokenMathMLAttrs(TagToken& token);
void AdjustTokenSVGAttrs(TagToken& token);
void AdjustTokenXMLAttrs(TagToken& token);
void AdjustTokenSVGTagName(TagToken& token);

// True when tag "tn" in namespace "ns" (with its "attrs") is a foreign-
// content integration point: a boundary inside <math>/<svg> where HTML
// parsing rules resume. Passing kHtml for "foreign_ns" checks both the
// MathML integration points (with or without the text-integration class) and
// the HTML integration points.
bool IsIntegrationPoint(TagId tn, NS ns, const std::vector<Attribute>& attrs, NS foreign_ns = NS::kHtml);

// The parser: a TokenHandler (which consumes tokens from the lexer) and, at
// the same time, the observer for the open-element stack it owns. It owns
// the tokenizer and the document it fills in, drives the insertion-mode
// machine, and exposes the public entry points ParseDocument and
// GetFragmentParser. The state the mode-handler free functions read and
// write lives here as public members so those functions can operate without
// friend accessor plumbing.
class Parser : public TokenHandler, public StackHandler<DefaultTreeAdapter> {
public:
    // Configured options; notice the constructor may force
    // source_code_location_info on when an error handler is present.
    ParserOptions options;

    // The active tokenizer feeding tokens, and the document node being built
    // (for fragment parsing, a synthetic document that will hold the
    // fragment children).
    std::unique_ptr<Tokenizer> tokenizer;
    std::unique_ptr<Node> document;

    // True once EOF is processed; the parser then ignores further input.
    bool stopped = false;

    // The current insertion mode, plus the mode saved when entering an
    // overlay mode (kText, kInTableText, ...) so it can be restored on exit.
    InsertionMode insertion_mode = InsertionMode::kInitial;
    InsertionMode original_insertion_mode = InsertionMode::kInitial;

    // Fragment parsing only: the context element the fragment is parsed
    // within (non-owning) and its tag ID. They select the initial mode,
    // namespace handling and the "adjusted current node".
    Node* fragment_context = nullptr;
    TagId fragment_context_id = TagId::kUnknown;

    // The parsed <head> element (so missing elements can be injected) and
    // the <form> element currently in effect, or null when none.
    Node* head_element = nullptr;
    Node* form_element = nullptr;

    // Core parsing state: the open-element stack, the active-formatting
    // elements list, and whether the current insertion point is outside the
    // HTML namespace (inside foreign content).
    std::unique_ptr<OpenElementStack<DefaultTreeAdapter>> open_elements;
    std::unique_ptr<FormattingElementList<DefaultTreeAdapter>> active_formatting_elements;
    bool current_not_in_html = false;

    // Modes saved for nested <template> elements, kept from the left: the
    // topmost template's saved mode is at the front.
    std::deque<InsertionMode> tmpl_insertion_mode_stack;

    // Character tokens buffered while a mode decides the fate of text (e.g.
    // in table-text mode); the flag records whether any was non-whitespace,
    // which changes how the buffer is flushed on an end tag or EOF.
    std::vector<CharacterToken> pending_character_tokens;
    bool has_non_whitespace_pending_character_token = false;

    // Whether a <frameset> is still legal in this document (frameset-ok
    // flag): rendered framesets are only allowed when no body content was
    // emitted yet.
    bool frameset_ok = true;

    // True when the next character token may be an ignorable leading line
    // feed (used after a markup line break came through the lexer).
    bool skip_next_new_line = false;

    // When true, table-region insertions that would otherwise place a node
    // inside the table must be routed through foster parenting (see below).
    bool foster_parenting_enabled = false;

    // Optional callback invoked when a </script> end tag is processed,
    // letting the embedder read or strip the finished script body.
    std::function<void(Node&)> script_handler;

    // Creates a parser. "options" tunes behavior; "document_node" supplies a
    // pre-built document to fill in (a fresh one is created when null); and
    // "fragment_context" switches to fragment parsing within that element
    // (kept as a raw pointer -- the parser never takes ownership; see
    // owned_fragment_context_storage for the default-context case).
    // An on_parse_error option turns on location tracking automatically.
    explicit Parser(const ParserOptions& options = {},
                    std::unique_ptr<Node> document_node = nullptr,
                    Node* fragment_context = nullptr);

    // A parser owns the tokenizer, document and stacks, so it is neither
    // copyable nor copy-assignable.
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;

    // API

    // Entry point: parses "html" as a whole document and returns its AST.
    // Parse errors are routed to options.on_parse_error when set. This is
    // the normal way to obtain a complete document tree.
    //
    // Example: ParseDocument("<!DOCTYPE html><p>hi</p>") returns a document
    //          node whose <html> child holds <head> and <body>, with the
    //          <p> inside <body>.
    static std::unique_ptr<Node> ParseDocument(std::string_view html,
                                               const ParserOptions& options = {});

    // Creates a parser configured for fragment parsing within
    // "fragment_context"; a <template> element is synthesized as the context
    // when none is given, which parses the input "forgivingly". Feed the
    // source with Write and collect the result with GetFragment. The caller
    // owns the returned parser.
    static std::unique_ptr<Parser> GetFragmentParser(
        Node* fragment_context = nullptr, const ParserOptions& options = {});

    // Feeds a chunk of source into the parser. For whole-document parsing,
    // feed everything and read the tree from "document"; for fragment
    // parsing, call GetFragment afterwards to harvest the built nodes.
    // Chunks may be split at any byte -- the tokenizer buffers partial input
    // internally between calls.
    void Write(std::string_view html);

    // Finishes a fragment parse and returns the built tree, moving the
    // children out of the synthetic document into a document-fragment node.
    // The context element itself is not part of the result, but the
    // fragment is parsed with the context's rules and ancestor form
    // association.
    //
    // Example: after Write("<b>x</b>") with a <div> context, GetFragment
    //          returns a fragment containing the <b> element.
    std::unique_ptr<Node> GetFragment();

    // Errors

    // Reports a parse error with code "code" at the token's location, or (with
    // before_token) at the very start of the token. Errors only reach the
    // client when options.on_parse_error is set; reporting is what forces
    // location tracking on.
    void ReportError(const AnyToken& token, Err code, bool before_token = false);

    // Stack events ("StackHandler")

    // Fired after an element is pushed: refreshes the parser's context modes
    // when the new element became the current node.
    void OnItemPush(Node& node, TagId tid, bool is_top) override;

    // Fired after an element is popped: closes source locations on the
    // abandoned element, then re-derives context modes from the new top
    // (falling back to the fragment context at the bottom of a fragment
    // parse).
    void OnItemPop(Node& node, bool is_top) override;

    // Token processing ("TokenHandler")

    // Token callbacks from the tokenizer, in lexer order. Each dispatches to
    // the insertion-mode handlers (the free functions at the bottom of this
    // header); character tokens distinguish their three flavors.
    void OnComment(CommentToken& token) override;
    void OnDoctype(DoctypeToken& token) override;
    void OnStartTag(TagToken& token) override;
    void OnEndTag(TagToken& token) override;
    void OnEof(EofToken& token) override;
    void OnCharacter(CharacterToken& token) override;
    void OnNullCharacter(CharacterToken& token) override;
    void OnWhitespaceCharacter(CharacterToken& token) override;

    // Re-dispatches a token of any kind through the public per-kind entry
    // points. Used by mode handlers that must reprocess a token after the
    // insertion mode has changed underneath it.
    void ProcessToken(const AnyToken& token);

    // Runs the start-tag processing rule directly, bypassing the
    // self-closing (trailing solidus) flag check that OnStartTag performs,
    // for call sites that already handled that decision.
    void ProcessStartTag(TagToken& token);

    // Integration points

    // Element overload of the integration-point test: true when a foreign
    // element with tag "tid" and "element"'s attributes is an integration
    // point, in which case HTML parsing rules resume inside it.
    bool IsIntegrationPoint(TagId tid, const Node& element, NS foreign_ns = NS::kHtml);

    // Active formatting elements reconstruction

    // Re-creates, in document order, the active formatting elements that the
    // open-element stack no longer contains (they were implicitly closed or
    // removed), inserting fresh clone elements back into the tree until the
    // nearest still-listed formatting element is open again. Runs before
    // most in-body insertions so formatting like <b> survives mis-nesting.
    void ReconstructActiveFormattingElements();

    // Close elements

    // Closes the current table cell: generates implied end tags, pops the
    // cell and switches the insertion mode back to the saved row mode. Used
    // for </td>/</th> and for stray closing-cell tokens.
    void CloseTableCell();

    // Closes the current <p> when one is in button scope, then re-processes
    // whoever triggered it (so two <p> tags collapse into one).
    void ClosePElement();

    // Insertion modes

    // Recomputes "insertion_mode" from the open-element stack by scanning
    // upward from the current element until a deciding element (or the root)
    // fixes the mode. Called after structural changes make the mode stale.
    void ResetInsertionMode();

    // Recomputes the insertion mode as if the element at "select_idx" were
    // the stack top: yields kInSelect, or kInSelectInTable when an element
    // that treats select specially surrounds it. Used by the select-scope
    // logic.
    void ResetInsertionModeForSelect(int select_idx);

    // Foster parenting

    // True when tag "tn" tends to trigger foster parenting (table structural
    // or cell tags that must not nest directly), per the foster-parenting
    // rules.
    bool IsElementCausesFosterParenting(TagId tn);

    // True when the current insertion point would place a node inside a
    // <table> that is missing its required structure, so the node must be
    // foster-parented instead. Drives FindFosterParentingLocation.
    bool ShouldFosterParentOnInsertion();

    // Where a foster-parented node ends up: "parent" is the real insertion
    // parent and "before_element" (when non-null) the element to insert
    // before.
    struct FosterParentingLocation {
        Node* parent;
        Node* before_element;
    };

    // Computes the foster-parenting target for the current element: the last
    // <template> ancestor's content when one is open, otherwise the last
    // open table element's parent with the table's previous sibling as the
    // before-node.
    FosterParentingLocation FindFosterParentingLocation();

    // Inserts "element" at its foster-parenting location, consuming its
    // ownership (before the table's previous sibling, or appended to the
    // table's parent).
    void FosterParentElement(std::unique_ptr<Node> element);

    // Special elements

    // True when "element" (of tag "id") is a "special" element: one that
    // acts as a formatting/reconstruction boundary (div, table, ul, body,
    // ...), so generic formatting logic must not tunnel through it.
    bool IsSpecialElement(const Node& element, TagId id);

    // Tree mutation

    // Applies the doctype carried by "token" to the document and records its
    // location, adjusting the document mode (quirks / limited-quirks /
    // no-quirks) from the public and system identifiers.
    void SetDocumentType(const DoctypeToken& token);

    // Attaches a freshly created "element" to the tree, taking ownership: it
    // is either foster-parented (when required) or appended to the current
    // template content or current element, and its source location is set
    // when location tracking is on.
    void AttachElementToTree(std::unique_ptr<Node> element, const Location* location);

    // Creates an element from "token" in namespace "namespace_uri" and
    // attaches it to the tree without opening it on the stack.
    void AppendElement(const TagToken& token, NS namespace_uri);

    // Creates an element from "token" in "namespace_uri", attaches it to the
    // tree, and pushes it onto the open-element stack, so it becomes the
    // current insertion point.
    void InsertElement(const TagToken& token, NS namespace_uri);

    // Inserts a fake element (no real start tag behind it) with the given
    // "tag_name" and ID, used to synthesize implied nodes like the root
    // <html> or a missing <body>.
    void InsertFakeElement(std::string_view tag_name, TagId tag_id);

    // Inserts a <template> element: creates it with a fresh document-fragment
    // as its content, attaches it, and opens it on the stack (the template's
    // children will be placed into the fragment).
    void InsertTemplate(const TagToken& token);

    // Inserts a fake root <html> element as a child of the current node and
    // opens it, used to bootstrap both whole-document and fragment parses.
    void InsertFakeRootElement();

    // Appends a comment node "token" to "parent", recording the location
    // when tracking is on.
    void AppendCommentNode(const CommentToken& token, Node& parent);

    // Inserts the characters of "token" as (or into) a text node at the
    // insertion point, honoring foster parenting when active, and extends or
    // sets the text node's source location.
    void InsertCharacters(const CharacterToken& token);

    // Moves every child of "donor" to "recipient" (used to relocate a
    // mis-nested element's content after re-parenting).
    void AdoptNodes(Node& donor, Node& recipient);

    // Extends "element"'s location span with "closing_token"'s location; for
    // a matching end tag the end-tag location is also recorded. Used to give
    // implicitly-closed elements their true end position.
    void SetEndLocation(Node& element, const AnyToken& closing_token);

    // Text parsing switches

    // Switches to text (RCData/RAWTEXT) parsing for the given element:
    // inserts "token" as an element, remembers the current mode in
    // "original_insertion_mode", and changes the tokenizer state so the
    // element's content is consumed verbatim until its end tag.
    void SwitchToTextParsing(TagToken& token, State next_tokenizer_state);

    // Switches to plaintext parsing, where everything until EOF is text.
    void SwitchToPlaintextParsing();

    // Fragment parsing helpers

    // The "adjusted current node": the fragment context when the stack still
    // holds only the fake root, otherwise the open-element stack's current
    // node. Decides namespace handling near the start of a fragment.
    Node* GetAdjustedCurrentElement();

    // Sets up the tokenizer's initial state for fragment parsing: contexts
    // like <title>/<textarea> put it in RCData, raw-text elements
    // (<style>, <xmp>, <iframe>, ...) in RAWTEXT, and empty contexts in the
    // standard data state.
    void InitTokenizerForFragmentParsing();

    // Context modes

    // Recomputes "current_not_in_html" and the tokenizer's
    // "in_foreign_node" flag from the current node and tag, so the lexer
    // lexes foreign content correctly.
    void SetContextModes(Node* current, TagId tid);

    // True when "token" must be processed by the foreign-content start-tag
    // handler rather than the HTML one (the current node is foreign and the
    // token either causes exit or needs attribute adjustments).
    bool ShouldProcessStartTagTokenInForeignContent(TagToken& token);

    // Start/end tag processing split at foreign-content boundary

    // The two halves of start/end-tag processing: the HTML-rules path used
    // when not inside foreign content, and the foreign-content path used
    // when the adjusted current node is foreign.
    void StartTagOutsideForeignContent(TagToken& token);
    void EndTagOutsideForeignContent(TagToken& token);

    // Copies "token" into the formatting-token arena (per-parser deep
    // copies) and returns a stable pointer to the copy, so the
    // active-formatting-elements list can retain a token whose original
    // storage the tokenizer will reuse. The arena lives for the parser's
    // whole lifetime.
    const TagToken* CopyFormattingToken(const TagToken& token);

    // The most recent start or end tag token, kept while its handlers run so
    // pop events can close locations against it; points into the tokenizer's
    // current token storage.
    TagToken* current_tag_token = nullptr;

    // Owns the default <template> fragment context synthesized by
    // GetFragmentParser when the caller passed none, keeping the context
    // alive for the parser's lifetime.
    std::unique_ptr<Node> owned_fragment_context_storage;

    // Finishes parsing when EOF arrives: sets "stopped", then walks elements
    // still on the open-element stack assigning their end locations (skipping
    // <html>/<body>, whose end positions are handled separately).
    void StopParsing(EofToken& token);

    // Holds elements created but not yet attached to the tree. The adoption
    // agency algorithm re-creates an element while its replacement is being
    // built, and these are parked here until the tree is ready for them.
    std::vector<std::unique_ptr<Node>> orphan_nodes_;

private:
    // Deep copy storage behind CopyFormattingToken; never shrinks during the
    // parse.
    std::vector<std::unique_ptr<TagToken>> formatting_token_arena_;
};

// ---------------------------------------------------------------------------
// Insertion-mode handlers
//
// The parsing algorithm implemented as one handler per insertion mode (and
// per token kind within a mode). Each receives the parser and the offending
// token, updates parser state, mutates the tree, and occasionally re-dispatches
// the token through ProcessToken after the mode has changed.
// ---------------------------------------------------------------------------

// Outer and inner loop caps for the adoption agency algorithm: roughly, at
// most this many outer passes and this many inner format-element hunts per
// closing tag. Bounding them keeps pathological mis-nesting from looping
// forever.
inline constexpr int kAaOuterLoopIter = 8;
inline constexpr int kAaInnerLoopIter = 3;

// True when "tag_id" is a table-structure tag (<table>, <tbody>, <tfoot>,
// <thead>, <tr>), i.e. one that is never allowed to nest arbitrarily inside
// a table.
inline bool IsTableStructureTag(TagId tag_id) {
    switch (tag_id) {
        case TagId::kTable:
        case TagId::kTbody:
        case TagId::kTfoot:
        case TagId::kThead:
        case TagId::kTr:
            return true;
        default:
            return false;
    }
}

// Generic token handlers: the dispatch target for every token kind while the
// parser is in the corresponding insertion mode. Several modes (initial,
// before html, before head, ...) route most tokens to a common "anything
// else" path; these functions implements those rules including the mode
// transitions that an unrecognized token triggers.
void TokenInInitialMode(Parser& p, AnyToken token);
void TokenBeforeHtml(Parser& p, AnyToken token);
void TokenBeforeHead(Parser& p, AnyToken token);
void TokenInHead(Parser& p, AnyToken token);
void TokenInHeadNoScript(Parser& p, AnyToken token);
void TokenAfterHead(Parser& p, AnyToken token);
void TokenInColumnGroup(Parser& p, AnyToken token);
void TokenAfterBody(Parser& p, AnyToken token);
void TokenAfterAfterBody(Parser& p, AnyToken token);
void TokenInTableText(Parser& p, AnyToken token);

// True when the token is a start tag for an <input> element carrying
// type="hidden", which may trigger the <form>-pointer bookkeeping in body
// mode.
bool IsHiddenInput(const TagToken& token);

// Character-token handlers. Whitespace and regular characters are treated
// differently in body and table modes: whitespace is mostly dropped while
// data text is inserted (or buffered, in table-text mode, then flushed).
void WhitespaceCharacterInBody(Parser& p, CharacterToken& token);
void CharacterInBody(Parser& p, CharacterToken& token);
void CharacterInTable(Parser& p, CharacterToken& token);
void CharacterInTableText(Parser& p, CharacterToken& token);
void WhitespaceCharacterInTableText(Parser& p, CharacterToken& token);

// Comment and doctype handlers. Comments append to the token's target (the
// document, the root <html>, or the current node); the doctype handler
// applies the document's quirks setting during the initial mode.
void AppendComment(Parser& p, CommentToken& token);
void AppendCommentToRootHtmlElement(Parser& p, CommentToken& token);
void AppendCommentToDocument(Parser& p, CommentToken& token);
void DoctypeInInitialMode(Parser& p, DoctypeToken& token);

// EOF handlers: what "the input ended here" means in each mode, always
// unwinding to a state where parsing can finish cleanly.
void EofInBody(Parser& p, EofToken& token);
void EofInText(Parser& p, EofToken& token);
void EofInTemplate(Parser& p, EofToken& token);

// Start-tag handlers per insertion mode: the "start tag" rule for each mode,
// from the point where the element becomes current to where its content
// begins. One per mode; modes without a specific rule fall through to the
// in-body handler.
void StartTagBeforeHtml(Parser& p, TagToken& token);
void StartTagBeforeHead(Parser& p, TagToken& token);
void StartTagInHead(Parser& p, TagToken& token);
void StartTagInHeadNoScript(Parser& p, TagToken& token);
void StartTagAfterHead(Parser& p, TagToken& token);
void StartTagInBody(Parser& p, TagToken& token);
void StartTagInTable(Parser& p, TagToken& token);
void StartTagInCaption(Parser& p, TagToken& token);
void StartTagInColumnGroup(Parser& p, TagToken& token);
void StartTagInTableBody(Parser& p, TagToken& token);
void StartTagInRow(Parser& p, TagToken& token);
void StartTagInCell(Parser& p, TagToken& token);
void StartTagInSelect(Parser& p, TagToken& token);
void StartTagInSelectInTable(Parser& p, TagToken& token);
void StartTagInTemplate(Parser& p, TagToken& token);
void StartTagAfterBody(Parser& p, TagToken& token);
void StartTagInFrameset(Parser& p, TagToken& token);
void StartTagAfterFrameset(Parser& p, TagToken& token);
void StartTagAfterAfterBody(Parser& p, TagToken& token);
void StartTagAfterAfterFrameset(Parser& p, TagToken& token);

// End-tag handlers per insertion mode: the "end tag" rule for each mode,
// closing the matching element, re-processing mis-matched end tags, or
// falling through to the in-body handler.
void EndTagBeforeHtml(Parser& p, TagToken& token);
void EndTagBeforeHead(Parser& p, TagToken& token);
void EndTagInHead(Parser& p, TagToken& token);
void EndTagInHeadNoScript(Parser& p, TagToken& token);
void EndTagAfterHead(Parser& p, TagToken& token);
void EndTagInBody(Parser& p, TagToken& token);
void EndTagInText(Parser& p, TagToken& token);
void EndTagInTable(Parser& p, TagToken& token);
void EndTagInCaption(Parser& p, TagToken& token);
void EndTagInColumnGroup(Parser& p, TagToken& token);
void EndTagInTableBody(Parser& p, TagToken& token);
void EndTagInRow(Parser& p, TagToken& token);
void EndTagInCell(Parser& p, TagToken& token);
void EndTagInSelect(Parser& p, TagToken& token);
void EndTagInSelectInTable(Parser& p, TagToken& token);
void EndTagInTemplate(Parser& p, TagToken& token);
void EndTagAfterBody(Parser& p, TagToken& token);
void EndTagInFrameset(Parser& p, TagToken& token);
void EndTagAfterFrameset(Parser& p, TagToken& token);

// In-body helpers: the generic end-tag rule (search the stack, close common
// elements, act on adoption-agency needs) and the adoption agency itself,
// which re-parents mis-nested formatting elements like <b><i> inside
// mismatched blocks.
void GenericEndTagInBody(Parser& p, TagToken& token);
void CallAdoptionAgency(Parser& p, TagToken& token);

// Shared helpers used across mode groups: the </template> end-tag rule (also
// reachable from head) and the blanket "anything else" in-body dispatcher
// shared by several modes.
void TemplateEndTagInHead(Parser& p, TagToken& token);
void ModeInBody(Parser& p, AnyToken token);

// Foreign content

// Character, null-character, start-tag and end-tag handlers active while the
// adjusted current node is inside <math>/<svg>: they apply the adjustment
// routines above and follow the foreign-content parsing rules instead of the
// HTML ones.
void CharacterInForeignContent(Parser& p, CharacterToken& token);
void NullCharacterInForeignContent(Parser& p, CharacterToken& token);
void StartTagInForeignContent(Parser& p, TagToken& token);
void EndTagInForeignContent(Parser& p, TagToken& token);

}