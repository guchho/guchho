#include "guchho/html/html_parser.hpp"

#include <array>
#include <string_view>

#include "guchho/helpers.hpp"

// Guchho's HTML tree-building engine.
//
// This translation unit implements the token-to-tree half of HTML parsing.
// It consumes the tokenizer's character, comment, doctype, and tag tokens and
// routes each one through the standard tree-construction dispatcher: tokens
// are matched against the current insertion mode, mutated as needed, and
// turned into nodes on the DOM via a stack of open elements plus the list of
// active formatting elements. The engine reproduces the actionable subset of
// the HTML spec's tree-construction rules -- implied end tags, foster
// parenting around tables, the in-table/in-select/in-template mode families,
// foreign-content integration points, and the adoption-agency algorithm that
// repairs misnested formatting (such as <b><i></b></i>) -- so a document or
// fragment parsed here comes out structured the way a browser would build it,
// including source-code locations for every node.
//
// The two public entry points are ParseDocument (whole-document mode) and
// GetFragmentParser (fragment mode); both funnel through the same Parser
// instance, which owns the tokenizer and both bookkeeping stacks and hands the
// finished tree back to the caller.

namespace guchho::html {

using Entry = FormattingElementList<DefaultTreeAdapter>::Entry;

// Parser construction and the public document/fragment entry points.

// Assembles a parser around an optional pre-made document node and, for
// fragment parsing, the context element the fragment is anchored to. When
// "document_node" is null a fresh document is created. Parse errors are
// opt-in: handing the parser an on_parse_error callback forces source-code
// location tracking on so every reported error can point into the source.
// The tokenizer, active-formatting-element list, and open-element stack are
// created here, the insertion-mode context is seeded from the document or the
// fragment context, and the fragment context's tag id is remembered because
// with no open elements the context stands in as the "current" element.
Parser::Parser(const ParserOptions& options, std::unique_ptr<Node> document_node,
               Node* fragment_context)
    : options(options),
      document(std::move(document_node)),
      fragment_context(fragment_context) {
    if (options.on_parse_error) {
        this->options.source_code_location_info = true;
    }

    if (document == nullptr) {
        document = CreateDocument();
    }

    fragment_context_id =
        fragment_context != nullptr ? GetTagId(fragment_context->tag_name) : TagId::kUnknown;

    on_parse_error = this->options.on_parse_error;

    tokenizer = std::make_unique<Tokenizer>(*this, this->options.source_code_location_info);
    active_formatting_elements = std::make_unique<FormattingElementList<DefaultTreeAdapter>>();

    SetContextModes(fragment_context != nullptr ? fragment_context : document.get(),
                    fragment_context_id);

    open_elements = std::make_unique<OpenElementStack<DefaultTreeAdapter>>(*document, *this);
}

// Parses "html" as a whole document and returns the finished tree. This is
// the normal entry point: it creates a document-mode parser, streams the input
// through the tokenizer as UTF-16, and hands the completed document back.
//
//   ParseDocument("<p>hi</p>") -> html > head, body > p > "hi"
std::unique_ptr<Node> Parser::ParseDocument(std::string_view html,
                                            const ParserOptions& options) {
    auto parser = std::make_unique<Parser>(options);
    parser->Write(html);
    return std::move(parser->document);
}

// Creates a parser that builds a fragment instead of a document. The context
// element supplied is used to seed tokenizer state and insertion-mode
// selection; without a context, a <template> element is created and used as a
// "forgiving" default so arbitrary markup parses like content inside a
// template. The chosen context also selects the initial tokenizer state (RCDATA
// for a textarea/title context, rawtext for style/xmp/iframe/noembed/noframes/
// noscript, script-data for a script, plaintext for a plaintext), and the
// fake root element -- along with the reset insertion mode -- lets tokens
// process normally. Finally the parser looks up the closest ancestor <form>
// of the context, mirroring how fragment parsing resolves form ownership.
//
//   GetFragmentParser(&divNode) -> parser whose document subtree
//     is a <html> > <body> shell the caller then extracts via GetFragment.
std::unique_ptr<Parser> Parser::GetFragmentParser(Node* fragment_context,
                                                  const ParserOptions& options) {
    std::unique_ptr<Node> default_context;
    Node* context = fragment_context;
    if (context == nullptr) {
        default_context = CreateElement("template");
        SetTemplateContent(*default_context, CreateDocumentFragment());
        context = default_context.get();
    }

    auto parser =
        std::make_unique<Parser>(options, nullptr, context);
    parser->owned_fragment_context_storage = std::move(default_context);

    if (parser->fragment_context_id == TagId::kTemplate) {
        parser->tmpl_insertion_mode_stack.push_front(InsertionMode::kInTemplate);
    }

    parser->InitTokenizerForFragmentParsing();
    parser->InsertFakeRootElement();
    parser->ResetInsertionMode();

    for (Node* node = context; node != nullptr; node = node->parent_node) {
        if (node->tag_name == "form") {
            parser->form_element = node;
            break;
        }
    }

    return parser;
}

// Feeds a chunk of UTF-8 source into the running parser. The chunk is
// converted to UTF-16 before delivery because the tokenizer operates on
// 16-bit code units; the final "true" tells the tokenizer to close out its
// output at the end of the stream. Callers typically feed the whole input in
// one call, but incremental feeding is supported (concatenation semantics).
void Parser::Write(std::string_view html) {
    tokenizer->Write(ToUtf16(html), true);
}

// Extracts the fragment the fragment parser has built. The parser always
// grows its tree under the synthetic <html> element, so the real fragment
// content is the first child; all of that child's children are adopted into a
// fresh document fragment and returned, leaving the shell behind.
//
//   GetFragment() for GetFragmentParser around "<li>one"
//     -> document-fragment [ <li>"one" ]
std::unique_ptr<Node> Parser::GetFragment() {
    Node* root_element = GetFirstChild(*document);
    auto fragment = CreateDocumentFragment();

    AdoptNodes(*root_element, *fragment);

    return fragment;
}

// Parse-error handling.

// Sends one parse error to the registered handler. Token locations arrive as
// (line, column, byte/offset) triples; when the token has no location -- or
// when "before_token" asks for it -- the error is reported as a single point
// rather than a span, so a missing-doctype error, for example, points exactly
// at the offending token instead of covering it. All such output is suppressed
// when the caller did not install an on_parse_error callback.
void Parser::ReportError(const AnyToken& token, Err code, bool before_token) {
    if (!on_parse_error) {
        return;
    }

    const Location* const loc = token.location();
    static constexpr Location kBaseLoc{-1, -1, -1, -1, -1, -1};

    ParserError err{};
    static_cast<Location&>(err) = loc != nullptr ? *loc : static_cast<const Location&>(kBaseLoc);
    err.code = code;

    if (before_token) {
        err.end_line = err.start_line;
        err.end_col = err.start_col;
        err.end_offset = err.start_offset;
    }

    on_parse_error(err);
}

// Open-element stack events.
//
// The open-element stack notifies the parser whenever an element is pushed or
// popped. These callbacks keep the tokenizer's "am I inside foreign content"
// flag and the "next text comes from a foreign namespace" switch in sync with
// whichever element is current, and they record end-tag locations on the way
// out for elements that did get a real closing tag.

// Called when an element is pushed onto the open-element stack (or the stack
// itself is pushed from inside). Only the top of the stack matters for mode
// selection, so context modes are refreshed just for the pushed element when
// it becomes the new current element.
void Parser::OnItemPush(Node& node, TagId tid, bool is_top) {
    if (is_top && open_elements->stack_top > 0) {
        SetContextModes(&node, tid);
    }
}

// Called when an element is popped off the open-element stack. When source
// locations are enabled and the pop was caused by a real end tag, the element
// receives the end tag's position (matching end tags only -- an implicitly
// closed element keeps no end-tag position). After the pop, the tokenizer and
// context switches are re-derived from whatever element is current (or the
// fragment context when the stack has drained), because entering or leaving a
// foreign namespace changes how subsequent tokens are tokenized.
void Parser::OnItemPop(Node& node, bool is_top) {
    if (options.source_code_location_info && current_tag_token != nullptr) {
        SetEndLocation(node, AnyToken(*current_tag_token));
    }

    if (is_top) {
        Node* current = nullptr;
        TagId current_tag_id = TagId::kUnknown;

        if (open_elements->stack_top == 0 && fragment_context != nullptr) {
            current = fragment_context;
            current_tag_id = fragment_context_id;
        } else {
            current = open_elements->current;
            current_tag_id = open_elements->current_tag_id;
        }

        SetContextModes(current, current_tag_id);
    }
}

// Derives the tokenizer's cross-namespace switches from "current". Two flags
// are maintained: current_not_in_html (true when the current node is outside
// the HTML namespace, used to route tokens through the foreign-content
// handlers) and the tokenizer's in_foreign_node (true when the current node
// is a foreign element WITHOUT being an HTML/MathML integration point, since
// integration points are tokenized exactly like HTML content).
void Parser::SetContextModes(Node* current, TagId tid) {
    const bool is_html =
        current == document.get() ||
        (current != nullptr && current->namespace_uri == NS::kHtml);

    current_not_in_html = !is_html;
    tokenizer->in_foreign_node =
        !is_html && current != nullptr && tid != TagId::kUnknown &&
        !IsIntegrationPoint(tid, *current);
}

// Text parsing switches.
//
// Many elements (script, style, textarea, title, plaintext, ...) switch the
// tokenizer out of the normal tag-scanning state and into a text state. These
// helpers perform the joint mode/tokenizer switch and remember the mode to
// return to once the element's end tag arrives.

// Opens an element that will contain raw or RCDATA text (for example a
// <script> or <textarea>). The element is inserted normally, the tokenizer is
// switched to "next_tokenizer_state" (script data, rawtext, or RCDATA), and
// the current insertion mode is stashed as the mode to restore when the
// matching end tag pops the element back out.
void Parser::SwitchToTextParsing(TagToken& token, State next_tokenizer_state) {
    InsertElement(token, NS::kHtml);
    tokenizer->state = next_tokenizer_state;
    original_insertion_mode = insertion_mode;
    insertion_mode = InsertionMode::kText;
}

// Switches the parser into text mode for a <plaintext> element. Unlike the
// other text elements, plaintext consumes everything to the end of input --
// there is no end tag -- so the previous insertion mode is always "in body".
void Parser::SwitchToPlaintextParsing() {
    insertion_mode = InsertionMode::kText;
    original_insertion_mode = InsertionMode::kInBody;
    tokenizer->state = State::kPlaintext;
}

// Fragment parsing.

// Returns the element that "current" token rules should be applied to. In
// fragment parsing the stack is seeded with a fake <html> element, but when
// the stack is empty the fragment context element takes the role: it is what
// general insertion-mode rules and foreign-content checks inspect.
Node* Parser::GetAdjustedCurrentElement() {
    return open_elements->stack_top == 0 && fragment_context != nullptr
               ? fragment_context
               : open_elements->current;
}

// Seeds the tokenizer's starting state from the fragment context element.
// Certain context elements imply a text-parsing state for their contents -- a
// textarea/title context means RCDATA, style/xmp/iframe/noembed/noframes/
// noscript mean rawtext, script means script data, and plaintext means the
// document ends as plaintext. All other contexts leave the tokenizer at its
// normal default state. Only HTML-namespace contexts matter here.
void Parser::InitTokenizerForFragmentParsing() {
    if (fragment_context == nullptr || fragment_context->namespace_uri != NS::kHtml) {
        return;
    }

    switch (fragment_context_id) {
        case TagId::kTitle:
        case TagId::kTextarea: {
            tokenizer->state = State::kRcdata;
            break;
        }
        case TagId::kStyle:
        case TagId::kXmp:
        case TagId::kIframe:
        case TagId::kNoembed:
        case TagId::kNoframes:
        case TagId::kNoscript: {
            tokenizer->state = State::kRawtext;
            break;
        }
        case TagId::kScript: {
            tokenizer->state = State::kScriptData;
            break;
        }
        case TagId::kPlaintext: {
            tokenizer->state = State::kPlaintext;
            break;
        }
        default:
            break;
    }
}

// Convenience overload forwarding the namespace conflict test. Foreign
// elements are checked against "foreign_ns" (the namespace a hypothetical
// new start tag would get); the free IsIntegrationPoint decides whether an
// element of namespace "element.namespace_uri" counts as an HTML or MathML
// text integration point.
bool Parser::IsIntegrationPoint(TagId tid, const Node& element, NS foreign_ns) {
    return guchho::html::IsIntegrationPoint(tid, element.namespace_uri, element.attrs,
                                            foreign_ns);
}

// Tree mutation.
//
// These helpers thread new nodes into the DOM. Each one handles the two
// places a node may land -- as a normal child of the "current" element, or,
// inside a table, "foster parented" before/inside the table element -- and
// they are the only path by which elements, text, comments, and template
// content enter the tree.

// Stores a doctype token on the document and records its source location on
// the created doctype node. Location tracking is best-effort: it only assigns
// the span when the token carries one.
void Parser::SetDocumentType(const DoctypeToken& token) {
    guchho::html::SetDocumentType(*document, token.name, token.public_id, token.system_id);

    if (token.location != nullptr) {
        for (const auto& child : document->child_nodes) {
            if (IsDocumentTypeNode(*child)) {
                SetNodeSourceCodeLocation(*child, token.location);
                break;
            }
        }
    }
}

// Attaches "element" to the tree under the current insertion point. When
// source locations are enabled the element's start-tag location is recorded
// (both in the generic node location and in the dedicated owned start-tag
// slot). Insertion picks foster parenting when the current element is a table
// structure element and foster parenting is on; otherwise the element becomes
// a child of the current element (or of the current <template>'s content, or
// the document if the stack is empty).
void Parser::AttachElementToTree(std::unique_ptr<Node> element, const Location* location) {
    Node* const raw = element.get();

    if (options.source_code_location_info && location != nullptr) {
        SetNodeSourceCodeLocation(*raw, location);
        raw->owned_start_tag_location = std::make_unique<Location>(*location);
        raw->start_tag_location = raw->owned_start_tag_location.get();
    }

    if (ShouldFosterParentOnInsertion()) {
        FosterParentElement(std::move(element));
    } else {
        Node* parent = open_elements->CurrentTmplContentOrNode();
        AppendChild(parent != nullptr ? *parent : *document, std::move(element));
    }
}

// Creates and attaches a free element -- a leaf that never goes on the open
// element stack (void elements, meta/link, and other non-container tags).
void Parser::AppendElement(const TagToken& token, NS namespace_uri) {
    auto element = CreateElement(token.tag_name, namespace_uri, token.attrs);

    AttachElementToTree(std::move(element), token.location);
}

// Creates and attaches an element AND pushes it on the open element stack,
// making it the new current element that nested tokens attach to.
void Parser::InsertElement(const TagToken& token, NS namespace_uri) {
    auto element = CreateElement(token.tag_name, namespace_uri, token.attrs);
    Node* const raw = element.get();

    AttachElementToTree(std::move(element), token.location);
    open_elements->Push(*raw, token.tag_id);
}

// Inserts a synthetic element that has no backing source token (for example
// the auto-<head> or auto-<body> wrappers). It shares the tree-insertion and
// stack-push logic with InsertElement but there is no location to record.
void Parser::InsertFakeElement(std::string_view tag_name, TagId tag_id) {
    auto element = CreateElement(std::string(tag_name));
    Node* const raw = element.get();

    AttachElementToTree(std::move(element), nullptr);
    open_elements->Push(*raw, tag_id);
}

// Inserts a <template> element and gives it a fresh document fragment for its
// contents, so children accumulate into the template's content rather than
// the main tree.
void Parser::InsertTemplate(const TagToken& token) {
    auto tmpl = CreateElement(token.tag_name, NS::kHtml, token.attrs);
    SetTemplateContent(*tmpl, CreateDocumentFragment());

    Node* const raw = tmpl.get();
    AttachElementToTree(std::move(tmpl), token.location);
    open_elements->Push(*raw, token.tag_id);
}

// Inserts the synthetic <html> root element used by document parsing as the
// very first node, attaching it to the current element (the document) and
// pushing it so it anchors the bottom of the open-element stack.
void Parser::InsertFakeRootElement() {
    auto element = CreateElement("html");

    Node* const raw = element.get();
    AppendChild(*open_elements->current, std::move(element));
    open_elements->Push(*raw, TagId::kHtml);
}

// Appends a comment node under "parent", recording the comment token's source
// location when location tracking is enabled.
void Parser::AppendCommentNode(const CommentToken& token, Node& parent) {
    auto comment_node = CreateCommentNode(token.data);

    AppendChild(parent, std::move(comment_node));
    if (options.source_code_location_info) {
        Node& inserted = *parent.child_nodes.back();
        SetNodeSourceCodeLocation(inserted, token.location);
    }
}

// Inserts a run of character tokens into the tree. Outside of table
// structures the text merges into the current insertion point; inside table
// structures (foster parenting enabled) the run is placed at the foster
// parenting location -- before the table element or inside the previous
// sibling -- so stray text in tables does not corrupt the implicit table
// structure. Character runs that come from consecutive tokens extend an
// existing text node's location rather than creating a new node each time: a
// fresh text node takes the first token's span, and later tokens only update
// the end position, so multi-token runs carry one coherent range.
void Parser::InsertCharacters(const CharacterToken& token) {
    Node* parent = nullptr;
    Node* before_element = nullptr;

    if (ShouldFosterParentOnInsertion()) {
        const FosterParentingLocation location = FindFosterParentingLocation();
        parent = location.parent;
        before_element = location.before_element;

        if (before_element != nullptr) {
            InsertTextBefore(*parent, token.chars, *before_element);
        } else {
            InsertText(*parent, token.chars);
        }
    } else {
        parent = open_elements->CurrentTmplContentOrNode();
        Node* const target = parent != nullptr ? parent : document.get();
        InsertText(*target, token.chars);
    }

    if (token.location == nullptr || options.source_code_location_info == false) {
        return;
    }

    const size_t text_node_idx =
        before_element != nullptr
            ? static_cast<size_t>(IndexofChild(*parent, *before_element))
            : parent->child_nodes.size();
    if (text_node_idx == 0) {
        return;
    }
    Node& text_node = *parent->child_nodes[text_node_idx - 1];

    if (text_node.source_code_location != nullptr) {
        UpdateNodeSourceCodeLocation(text_node,
                                     {.start_line = std::nullopt,
                                      .start_col = std::nullopt,
                                      .start_offset = std::nullopt,
                                      .end_line = token.location->end_line,
                                      .end_col = token.location->end_col,
                                      .end_offset = token.location->end_offset});
    } else {
        SetNodeSourceCodeLocation(text_node, token.location);
    }
}

// Moves every child of "donor" onto "recipient". Used when a fragment is
// extracted (the fake <html> shell's children become the fragment) and when
// the adoption agency rebuilds an element's content.
void Parser::AdoptNodes(Node& donor, Node& recipient) {
    while (Node* child = GetFirstChild(donor)) {
        auto detached = DetachNode(*child);
        AppendChild(recipient, std::move(detached));
    }
}

// Applies a closing token's location to "element". When the closing token is
// a genuine end tag for this element, the end-tag span is captured in the
// dedicated end-tag slot AND the generic node range's end point is moved to
// match; otherwise (implicit close or EOF) only the range's end advances so
// the span still covers the element without inventing an end tag. The <=
// comparison means an implicit close (like the first <p> in "<p> <p> ...")
// never fabricates closing-tag position for the first paragraph.
void Parser::SetEndLocation(Node& element, const AnyToken& closing_token) {
    if (element.source_code_location != nullptr && closing_token.location() != nullptr) {
        const Location* const ct_loc = closing_token.location();

        if (closing_token.type() == TokenType::kEndTag &&
            element.tag_name == closing_token.tag_name()) {
            element.owned_end_tag_location = std::make_unique<Location>(*ct_loc);
            element.end_tag_location = element.owned_end_tag_location.get();
            UpdateNodeSourceCodeLocation(element,
                                         {.start_line = std::nullopt,
                                          .start_col = std::nullopt,
                                          .start_offset = std::nullopt,
                                          .end_line = ct_loc->end_line,
                                          .end_col = ct_loc->end_col,
                                          .end_offset = ct_loc->end_offset});
        } else {
            UpdateNodeSourceCodeLocation(element,
                                         {.start_line = std::nullopt,
                                          .start_col = std::nullopt,
                                          .start_offset = std::nullopt,
                                          .end_line = ct_loc->start_line,
                                          .end_col = ct_loc->start_col,
                                          .end_offset = ct_loc->start_offset});
        }
    }
}

// Token processing ("TokenHandler").
//
// These Parser methods are the tokenizer's callbacks. Each tokenizer output --
// character, comment, doctype, start/end tag, EOF -- lands in exactly one On*
// method, which resets the single-newline skip flag and then routes the token
// according to insertion mode. Character tokens additionally distinguish plain
// characters, whitespace runs, and null characters up front because those
// families are handled by different mode rules (for example whitespace is
// usually harmless while regular text forbids framesets).

// Consumes a regular character run. Inside foreign content the characters are
// sent straight to the foreign-content handler, which also marks frameset_ok
// false. Otherwise the token is dispatched by insertion mode to the matching
// insertion-mode rule; modes that accept character data either insert text,
// buffer it (the in-table-text mode), or reprocess the token after an
// automatic mode fix-up.
void Parser::OnCharacter(CharacterToken& token) {
    skip_next_new_line = false;

    if (tokenizer->in_foreign_node) {
        CharacterInForeignContent(*this, token);
        return;
    }

    switch (insertion_mode) {
        case InsertionMode::kInitial:
            TokenInInitialMode(*this, AnyToken(token));
            break;
        case InsertionMode::kBeforeHtml:
            TokenBeforeHtml(*this, AnyToken(token));
            break;
        case InsertionMode::kBeforeHead:
            TokenBeforeHead(*this, AnyToken(token));
            break;
        case InsertionMode::kInHead:
            TokenInHead(*this, AnyToken(token));
            break;
        case InsertionMode::kInHeadNoScript:
            TokenInHeadNoScript(*this, AnyToken(token));
            break;
        case InsertionMode::kAfterHead:
            TokenAfterHead(*this, AnyToken(token));
            break;
        case InsertionMode::kInBody:
        case InsertionMode::kInCaption:
        case InsertionMode::kInCell:
        case InsertionMode::kInTemplate:
            CharacterInBody(*this, token);
            break;
        case InsertionMode::kText:
        case InsertionMode::kInSelect:
        case InsertionMode::kInSelectInTable:
            InsertCharacters(token);
            break;
        case InsertionMode::kInTable:
        case InsertionMode::kInTableBody:
        case InsertionMode::kInRow:
            CharacterInTable(*this, token);
            break;
        case InsertionMode::kInTableText:
            CharacterInTableText(*this, token);
            break;
        case InsertionMode::kInColumnGroup:
            TokenInColumnGroup(*this, AnyToken(token));
            break;
        case InsertionMode::kAfterBody:
            TokenAfterBody(*this, AnyToken(token));
            break;
        case InsertionMode::kAfterAfterBody:
            TokenAfterAfterBody(*this, AnyToken(token));
            break;
        default:
            break;
    }
}

// Consumes an explicit null character (U+0000) produced by the tokenizer.
// Nulls are rejected by the HTML tokenizer but survive into certain states
// (for example inside foreign content, where they are replaced below); this
// handler differs from OnCharacter mainly in that nulls are ignored in body
// content modes while still being fed to the modes that must act on them.
void Parser::OnNullCharacter(CharacterToken& token) {
    skip_next_new_line = false;

    if (tokenizer->in_foreign_node) {
        NullCharacterInForeignContent(*this, token);
        return;
    }

    switch (insertion_mode) {
        case InsertionMode::kInitial:
            TokenInInitialMode(*this, AnyToken(token));
            break;
        case InsertionMode::kBeforeHtml:
            TokenBeforeHtml(*this, AnyToken(token));
            break;
        case InsertionMode::kBeforeHead:
            TokenBeforeHead(*this, AnyToken(token));
            break;
        case InsertionMode::kInHead:
            TokenInHead(*this, AnyToken(token));
            break;
        case InsertionMode::kInHeadNoScript:
            TokenInHeadNoScript(*this, AnyToken(token));
            break;
        case InsertionMode::kAfterHead:
            TokenAfterHead(*this, AnyToken(token));
            break;
        case InsertionMode::kText:
            InsertCharacters(token);
            break;
        case InsertionMode::kInTable:
        case InsertionMode::kInTableBody:
        case InsertionMode::kInRow:
            CharacterInTable(*this, token);
            break;
        case InsertionMode::kInColumnGroup:
            TokenInColumnGroup(*this, AnyToken(token));
            break;
        case InsertionMode::kAfterBody:
            TokenAfterBody(*this, AnyToken(token));
            break;
        case InsertionMode::kAfterAfterBody:
            TokenAfterAfterBody(*this, AnyToken(token));
            break;
        default:
            break;
    }
}

// Consumes a comment token. Outside of HTML content (current_not_in_html) a
// comment is simply appended wherever the adjusted current element is. In
// HTML content the destination depends on the insertion mode: a comment is
// parked at the nearest proper home (the document before <html>, the
// <html>/<body> element, the current element, or the document after parsing
// finished). The in-table-text mode buffers comments through its own pending
// queue instead.
void Parser::OnComment(CommentToken& token) {
    skip_next_new_line = false;

    if (current_not_in_html) {
        AppendComment(*this, token);
        return;
    }

    switch (insertion_mode) {
        case InsertionMode::kInitial:
        case InsertionMode::kBeforeHtml:
        case InsertionMode::kBeforeHead:
        case InsertionMode::kInHead:
        case InsertionMode::kInHeadNoScript:
        case InsertionMode::kAfterHead:
        case InsertionMode::kInBody:
        case InsertionMode::kInTable:
        case InsertionMode::kInCaption:
        case InsertionMode::kInColumnGroup:
        case InsertionMode::kInTableBody:
        case InsertionMode::kInRow:
        case InsertionMode::kInCell:
        case InsertionMode::kInSelect:
        case InsertionMode::kInSelectInTable:
        case InsertionMode::kInTemplate:
        case InsertionMode::kInFrameset:
        case InsertionMode::kAfterFrameset:
            AppendComment(*this, token);
            break;
        case InsertionMode::kInTableText:
            TokenInTableText(*this, AnyToken(token));
            break;
        case InsertionMode::kAfterBody:
            AppendCommentToRootHtmlElement(*this, token);
            break;
        case InsertionMode::kAfterAfterBody:
        case InsertionMode::kAfterAfterFrameset:
            AppendCommentToDocument(*this, token);
            break;
        default:
            break;
    }
}

// Consumes a doctype token. A doctype is only meaningful in the "initial"
// insertion mode, where it is processed and moves the parser to "before
// html". Anywhere in the head the doctype flag is a misplaced-token parse
// error; everywhere else it is dropped silently. The in-table-text mode
// buffers it through the pending-character queue.
void Parser::OnDoctype(DoctypeToken& token) {
    skip_next_new_line = false;

    switch (insertion_mode) {
        case InsertionMode::kInitial:
            DoctypeInInitialMode(*this, token);
            break;
        case InsertionMode::kBeforeHead:
        case InsertionMode::kInHead:
        case InsertionMode::kInHeadNoScript:
        case InsertionMode::kAfterHead:
            ReportError(AnyToken(token), Err::kMisplacedDoctype);
            break;
        case InsertionMode::kInTableText:
            TokenInTableText(*this, AnyToken(token));
            break;
        default:
            break;
    }
}

// Consumes a start tag. The token is remembered so later ops (end-location
// assignment when the element pops) can find the tag it came from, then
// processed via the foreign-content-vs-HTML split in ProcessStartTag. A start
// tag written with an unacknowledged trailing solidus is reported, since the
// trailing-slash construct is only honored for void and foreign elements.
void Parser::OnStartTag(TagToken& token) {
    skip_next_new_line = false;
    current_tag_token = &token;

    ProcessStartTag(token);

    if (token.self_closing && !token.ack_self_closing) {
        ReportError(AnyToken(token),
                    Err::kNonVoidHtmlElementStartTagWithTrailingSolidus);
    }
}

// Routes a start tag down one of the two processing paths: foreign-content
// rules when the adjusted current element demands them, otherwise the regular
// insertion-mode start-tag table. ShouldProcessStartTagTokenInForeignContent
// decides which side applies, so every start-tag entry point funnels through
// this one decision.
void Parser::ProcessStartTag(TagToken& token) {
    if (ShouldProcessStartTagTokenInForeignContent(token)) {
        StartTagInForeignContent(*this, token);
    } else {
        StartTagOutsideForeignContent(token);
    }
}

// Consumes an end tag. As with start tags, the token is remembered for
// end-location bookkeeping; end tags bypass the start-tag routing and split
// directly on foreign-content state, since an end tag may close ancestors
// across namespace boundaries and has its own foreign-content rules.
void Parser::OnEndTag(TagToken& token) {
    skip_next_new_line = false;
    current_tag_token = &token;

    if (current_not_in_html) {
        EndTagInForeignContent(*this, token);
    } else {
        EndTagOutsideForeignContent(token);
    }
}

// Consumes the end-of-input token. Most insertion modes either auto-close
// their open elements and keep parsing (the "in body" family runs EofInBody,
// which stops parsing for a well-formed document but first unwinds any open
// <template> modes), raise a parse error for text elements, or simply stop.
// The "after*" trailing modes stop immediately since the document is done.
void Parser::OnEof(EofToken& token) {
    switch (insertion_mode) {
        case InsertionMode::kInitial:
            TokenInInitialMode(*this, AnyToken(token));
            break;
        case InsertionMode::kBeforeHtml:
            TokenBeforeHtml(*this, AnyToken(token));
            break;
        case InsertionMode::kBeforeHead:
            TokenBeforeHead(*this, AnyToken(token));
            break;
        case InsertionMode::kInHead:
            TokenInHead(*this, AnyToken(token));
            break;
        case InsertionMode::kInHeadNoScript:
            TokenInHeadNoScript(*this, AnyToken(token));
            break;
        case InsertionMode::kAfterHead:
            TokenAfterHead(*this, AnyToken(token));
            break;
        case InsertionMode::kInBody:
        case InsertionMode::kInTable:
        case InsertionMode::kInCaption:
        case InsertionMode::kInColumnGroup:
        case InsertionMode::kInTableBody:
        case InsertionMode::kInRow:
        case InsertionMode::kInCell:
        case InsertionMode::kInSelect:
        case InsertionMode::kInSelectInTable:
            EofInBody(*this, token);
            break;
        case InsertionMode::kText:
            EofInText(*this, token);
            break;
        case InsertionMode::kInTableText:
            TokenInTableText(*this, AnyToken(token));
            break;
        case InsertionMode::kInTemplate:
            EofInTemplate(*this, token);
            break;
        case InsertionMode::kAfterBody:
        case InsertionMode::kInFrameset:
        case InsertionMode::kAfterFrameset:
        case InsertionMode::kAfterAfterBody:
        case InsertionMode::kAfterAfterFrameset:
            StopParsing(token);
            break;
        default:
            break;
    }
}

// Consumes a whitespace run. Before anything else, the pending single-newline
// skip is honored: a run that starts with U+000A (the LF the tokenizer
// records right after <pre> etc.) has that leading LF dropped, implementing
// the spec's "ignore the first line feed after pre/textarea/listing". The
// remaining run is then dispatched by insertion mode; body-like modes insert
// it (whitespace does not forbid a frameset), table modes buffer it, and
// whitespace inside foreign content inserts directly.
void Parser::OnWhitespaceCharacter(CharacterToken& token) {
    if (skip_next_new_line) {
        skip_next_new_line = false;

        if (!token.chars.empty() && token.chars[0] == '\n') {
            if (token.chars.size() == 1) {
                return;
            }
            token.chars.erase(0, 1);
        }
    }

    if (tokenizer->in_foreign_node) {
        InsertCharacters(token);
        return;
    }

    switch (insertion_mode) {
        case InsertionMode::kInHead:
        case InsertionMode::kInHeadNoScript:
        case InsertionMode::kAfterHead:
        case InsertionMode::kText:
        case InsertionMode::kInColumnGroup:
        case InsertionMode::kInSelect:
        case InsertionMode::kInSelectInTable:
        case InsertionMode::kInFrameset:
        case InsertionMode::kAfterFrameset:
            InsertCharacters(token);
            break;
        case InsertionMode::kInBody:
        case InsertionMode::kInCaption:
        case InsertionMode::kInCell:
        case InsertionMode::kInTemplate:
        case InsertionMode::kAfterBody:
        case InsertionMode::kAfterAfterBody:
        case InsertionMode::kAfterAfterFrameset:
            WhitespaceCharacterInBody(*this, token);
            break;
        case InsertionMode::kInTable:
        case InsertionMode::kInTableBody:
        case InsertionMode::kInRow:
            CharacterInTable(*this, token);
            break;
        case InsertionMode::kInTableText:
            WhitespaceCharacterInTableText(*this, token);
            break;
        default:
            break;
    }
}

// The single entry point the tokenizer calls for every token. It splits a
// token down to its concrete type and hands it to the matching On* method;
// unusable token types are ignored. Nearly all internal reprocessing also
// goes through here, which is what lets helper rules emit a token "again"
// simply by re-invoking ProcessToken.
void Parser::ProcessToken(const AnyToken& token) {
    switch (token.type()) {
        case TokenType::kCharacter:
            OnCharacter(token.character());
            break;
        case TokenType::kNullCharacter:
            OnNullCharacter(token.character());
            break;
        case TokenType::kWhitespaceCharacter:
            OnWhitespaceCharacter(token.character());
            break;
        case TokenType::kComment: {
            OnComment(token.comment());
            break;
        }
        case TokenType::kDoctype: {
            OnDoctype(token.doctype());
            break;
        }
        case TokenType::kStartTag: {
            TagToken& tag = token.tag();
            ProcessStartTag(tag);
            break;
        }
        case TokenType::kEndTag: {
            TagToken& tag = token.tag();
            OnEndTag(tag);
            break;
        }
        case TokenType::kEof: {
            OnEof(token.eof());
            break;
        }
        default:
            break;
    }
}

// Start-tag dispatch for HTML-side content. The switch is driven by the
// current insertion mode: token-friendly modes (head, body, tables, select,
// templates, framesets) each hand the tag to their own StartTag* rules, and
// modes that do not expect start tags at all (the "initial" mode) bounce the
// token through their Token* fallback, which performs the automatic mode
// fix-up (for example injecting <html>, <head>, or <body>) and reprocesses.
void Parser::StartTagOutsideForeignContent(TagToken& token) {
    switch (insertion_mode) {
        case InsertionMode::kInitial:
            TokenInInitialMode(*this, AnyToken(token));
            break;
        case InsertionMode::kBeforeHtml:
            StartTagBeforeHtml(*this, token);
            break;
        case InsertionMode::kBeforeHead:
            StartTagBeforeHead(*this, token);
            break;
        case InsertionMode::kInHead:
            StartTagInHead(*this, token);
            break;
        case InsertionMode::kInHeadNoScript:
            StartTagInHeadNoScript(*this, token);
            break;
        case InsertionMode::kAfterHead:
            StartTagAfterHead(*this, token);
            break;
        case InsertionMode::kInBody:
            StartTagInBody(*this, token);
            break;
        case InsertionMode::kInTable:
            StartTagInTable(*this, token);
            break;
        case InsertionMode::kInTableText:
            TokenInTableText(*this, AnyToken(token));
            break;
        case InsertionMode::kInCaption:
            StartTagInCaption(*this, token);
            break;
        case InsertionMode::kInColumnGroup:
            StartTagInColumnGroup(*this, token);
            break;
        case InsertionMode::kInTableBody:
            StartTagInTableBody(*this, token);
            break;
        case InsertionMode::kInRow:
            StartTagInRow(*this, token);
            break;
        case InsertionMode::kInCell:
            StartTagInCell(*this, token);
            break;
        case InsertionMode::kInSelect:
            StartTagInSelect(*this, token);
            break;
        case InsertionMode::kInSelectInTable:
            StartTagInSelectInTable(*this, token);
            break;
        case InsertionMode::kInTemplate:
            StartTagInTemplate(*this, token);
            break;
        case InsertionMode::kAfterBody:
            StartTagAfterBody(*this, token);
            break;
        case InsertionMode::kInFrameset:
            StartTagInFrameset(*this, token);
            break;
        case InsertionMode::kAfterFrameset:
            StartTagAfterFrameset(*this, token);
            break;
        case InsertionMode::kAfterAfterBody:
            StartTagAfterAfterBody(*this, token);
            break;
        case InsertionMode::kAfterAfterFrameset:
            StartTagAfterAfterFrameset(*this, token);
            break;
        default:
            break;
    }
}

// End-tag dispatch for HTML-side content, mirroring the start-tag table:
// each mode either has its own EndTag* rule (which inspects stack scope
// before closing anything) or bounces the token to the mode's Token* fallback
// for the automatic fix-up path.
void Parser::EndTagOutsideForeignContent(TagToken& token) {
    switch (insertion_mode) {
        case InsertionMode::kInitial:
            TokenInInitialMode(*this, AnyToken(token));
            break;
        case InsertionMode::kBeforeHtml:
            EndTagBeforeHtml(*this, token);
            break;
        case InsertionMode::kBeforeHead:
            EndTagBeforeHead(*this, token);
            break;
        case InsertionMode::kInHead:
            EndTagInHead(*this, token);
            break;
        case InsertionMode::kInHeadNoScript:
            EndTagInHeadNoScript(*this, token);
            break;
        case InsertionMode::kAfterHead:
            EndTagAfterHead(*this, token);
            break;
        case InsertionMode::kInBody:
            EndTagInBody(*this, token);
            break;
        case InsertionMode::kText:
            EndTagInText(*this, token);
            break;
        case InsertionMode::kInTable:
            EndTagInTable(*this, token);
            break;
        case InsertionMode::kInTableText:
            TokenInTableText(*this, AnyToken(token));
            break;
        case InsertionMode::kInCaption:
            EndTagInCaption(*this, token);
            break;
        case InsertionMode::kInColumnGroup:
            EndTagInColumnGroup(*this, token);
            break;
        case InsertionMode::kInTableBody:
            EndTagInTableBody(*this, token);
            break;
        case InsertionMode::kInRow:
            EndTagInRow(*this, token);
            break;
        case InsertionMode::kInCell:
            EndTagInCell(*this, token);
            break;
        case InsertionMode::kInSelect:
            EndTagInSelect(*this, token);
            break;
        case InsertionMode::kInSelectInTable:
            EndTagInSelectInTable(*this, token);
            break;
        case InsertionMode::kInTemplate:
            EndTagInTemplate(*this, token);
            break;
        case InsertionMode::kAfterBody:
            EndTagAfterBody(*this, token);
            break;
        case InsertionMode::kInFrameset:
            EndTagInFrameset(*this, token);
            break;
        case InsertionMode::kAfterFrameset:
            EndTagAfterFrameset(*this, token);
            break;
        case InsertionMode::kAfterAfterBody:
            TokenAfterAfterBody(*this, AnyToken(token));
            break;
        default:
            break;
    }
}

// Foreign-content dispatch helper.

// Decides whether a start tag must be processed by the foreign-content rules
// rather than the ordinary HTML insertion-mode table. The tag needs foreign
// handling when the tokenizer is currently inside a foreign node that is not
// an integration point. Even then a few exceptions route back to HTML: an
// <svg> start tag appearing in a <annotation-xml> MathML integration point,
// and MathML <mglyph>/<malignmark> start tags under an element that is NOT an
// HTML integration point, both stay on the HTML side. The adjusted current
// element (or the fragment context, when the stack is empty) supplies the
// element being tested.
bool Parser::ShouldProcessStartTagTokenInForeignContent(TagToken& token) {
    if (!current_not_in_html) {
        return false;
    }

    Node* current = nullptr;
    TagId current_tag_id = TagId::kUnknown;

    if (open_elements->stack_top == 0 && fragment_context != nullptr) {
        current = fragment_context;
        current_tag_id = fragment_context_id;
    } else {
        current = open_elements->current;
        current_tag_id = open_elements->current_tag_id;
    }

    if (token.tag_id == TagId::kSvg && current != nullptr &&
        current->tag_name == "annotation-xml" && current->namespace_uri == NS::kMathml) {
        return false;
    }

    return (
        tokenizer->in_foreign_node ||
        ((token.tag_id == TagId::kMglyph || token.tag_id == TagId::kMalignmark) &&
         current_tag_id != TagId::kUnknown && current != nullptr &&
         !IsIntegrationPoint(current_tag_id, *current, NS::kHtml)));
}

// Formatting-element reconstruction and close helpers.

// Brings the list of active formatting elements back in sync with the stack
// of open elements. When a new element is inserted, every formatting element
// (b, i, a, ...) that was "closed early" -- dropped from the open-element
// stack without ever being removed from the active-formatting list -- must be
// re-implanted so later text keeps its formatting. The routine scans the list
// from the newest entry backward and stops at the first marker or element
// still present on the stack; anything newer is re-created via InsertElement
// until every active formatting element is open again. Nothing happens when
// the newest entry is already open or the list is empty.
void Parser::ReconstructActiveFormattingElements() {
    auto& entries = active_formatting_elements->entries;
    const auto list_length = entries.size();

    if (list_length == 0) {
        return;
    }

    auto boundary = entries.end();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->type == EntryType::kMarker || open_elements->Contains(*it->element)) {
            boundary = it;
            break;
        }
    }

    if (boundary == entries.begin()) {
        return;
    }

    auto start =
        boundary != entries.end() ? std::prev(boundary) : std::prev(entries.end());

    for (auto it = start;; --it) {
        Entry& entry = *it;
        InsertElement(*entry.token, entry.element->namespace_uri);
        entry.element = open_elements->current;
        if (it == entries.begin()) {
            break;
        }
    }
}

// Closes a table cell: implied end tags are generated, the stack is popped
// until the cell itself is gone, and the formatting list is cleared back to
// the nearest marker (the cell pushed a marker when it opened). Play returns
// to the "in row" mode so the next row can start.
void Parser::CloseTableCell() {
    open_elements->GenerateImpliedEndTags();
    open_elements->PopUntilTableCellPopped();
    active_formatting_elements->ClearToLastMarker();
    insertion_mode = InsertionMode::kInRow;
}

// Closes a <p> element: any open end-tag-implicit elements above it are
// closed first, then the stack is popped exactly through the paragraph.
void Parser::ClosePElement() {
    open_elements->GenerateImpliedEndTagsWithExclusion(TagId::kP);
    open_elements->PopUntilTagNamePopped(TagId::kP);
}

// Insertion-mode reset.

// Recomputes the current insertion mode from the open-element stack. The
// stack is walked from the top down; the first element whose tag binds a
// mode (row -> in row, caption -> in caption, select -> in select, template
// -> whatever its own mode stack says, html -> before/after head, and so on)
// fixes the result. <td>/<th> and <head> only count when they are not the
// very bottom of the stack. If nothing on the stack designates a mode, the
// document defaults to "in body". This is invoked whenever parsing changes
// context (e.g. after closing a <table>, <select>, or <template>).
void Parser::ResetInsertionMode() {
    for (int i = open_elements->stack_top; i >= 0; i--) {
        const TagId tn = i == 0 && fragment_context != nullptr
                             ? fragment_context_id
                             : open_elements->tag_ids[static_cast<size_t>(i)];

        switch (tn) {
            case TagId::kTr:
                insertion_mode = InsertionMode::kInRow;
                return;
            case TagId::kTbody:
            case TagId::kThead:
            case TagId::kTfoot:
                insertion_mode = InsertionMode::kInTableBody;
                return;
            case TagId::kCaption:
                insertion_mode = InsertionMode::kInCaption;
                return;
            case TagId::kColgroup:
                insertion_mode = InsertionMode::kInColumnGroup;
                return;
            case TagId::kTable:
                insertion_mode = InsertionMode::kInTable;
                return;
            case TagId::kBody:
                insertion_mode = InsertionMode::kInBody;
                return;
            case TagId::kFrameset:
                insertion_mode = InsertionMode::kInFrameset;
                return;
            case TagId::kSelect:
                ResetInsertionModeForSelect(i);
                return;
            case TagId::kTemplate:
                insertion_mode = tmpl_insertion_mode_stack.front();
                return;
            case TagId::kHtml:
                insertion_mode =
                    head_element != nullptr ? InsertionMode::kAfterHead : InsertionMode::kBeforeHead;
                return;
            case TagId::kTd:
            case TagId::kTh:
                if (i > 0) {
                    insertion_mode = InsertionMode::kInCell;
                    return;
                }
                break;
            case TagId::kHead:
                if (i > 0) {
                    insertion_mode = InsertionMode::kInHead;
                    return;
                }
                break;
            default:
                break;
        }
    }

    insertion_mode = InsertionMode::kInBody;
}

// Chooses the mode for a <select> on the stack at "select_idx". A select that
// is nested inside a table -- with no intervening <template> -- must switch to
// "in select in table" so table tags inside the select are handled correctly;
// otherwise "in select" applies. The scan stops at the nearest <template>, so
// selects inside templates never inherit the table rule across the template
// boundary.
void Parser::ResetInsertionModeForSelect(int select_idx) {
    if (select_idx > 0) {
        for (int i = select_idx - 1; i > 0; i--) {
            const TagId tn = open_elements->tag_ids[static_cast<size_t>(i)];

            if (tn == TagId::kTemplate) {
                break;
            }
            if (tn == TagId::kTable) {
                insertion_mode = InsertionMode::kInSelectInTable;
                return;
            }
        }
    }

    insertion_mode = InsertionMode::kInSelect;
}

// Foster parenting.

// True for the elements that forbid in-place insertion (table and its
// structure tags); during foster parenting any mis-placed content belongs
// before the table, not inside it.
bool Parser::IsElementCausesFosterParenting(TagId tn) { return IsTableStructureTag(tn); }

// Whether the current insertion of a mis-placed element should be
// foster-parented: only when foster parenting is enabled and the current
// element is a table-structure element.
bool Parser::ShouldFosterParentOnInsertion() {
    return foster_parenting_enabled && open_elements->current_tag_id != TagId::kUnknown &&
           IsElementCausesFosterParenting(open_elements->current_tag_id);
}

// Finds where foster-parented content goes: for a table nested inside a
// template, into the template's content fragment; for a table with a parent,
// right before the table itself; otherwise at the very start of the document.
// The returned location holds the parent node plus an optional "insert
// before" sibling.
Parser::FosterParentingLocation Parser::FindFosterParentingLocation() {
    for (int i = open_elements->stack_top; i >= 0; i--) {
        Node* const open_element = open_elements->items[static_cast<size_t>(i)];

        switch (open_elements->tag_ids[static_cast<size_t>(i)]) {
            case TagId::kTemplate: {
                if (open_element->namespace_uri == NS::kHtml) {
                    return {DefaultTreeAdapter::GetTemplateContent(*open_element), nullptr};
                }
                break;
            }
            case TagId::kTable: {
                Node* const parent = open_element->parent_node;

                if (parent != nullptr) {
                    return {parent, open_element};
                }

                return {open_elements->items[static_cast<size_t>(i - 1)], nullptr};
            }
            default:
                break;
        }
    }

    return {open_elements->items[0], nullptr};
}

// Moves a node into the foster-parenting location determined above, either
// inserted before the relevant sibling or appended to the found parent.
void Parser::FosterParentElement(std::unique_ptr<Node> element) {
    const FosterParentingLocation location = FindFosterParentingLocation();

    if (location.before_element != nullptr) {
        InsertBefore(*location.parent, std::move(element), *location.before_element);
    } else {
        AppendChild(*location.parent, std::move(element));
    }
}

// Convenience wrapper that classifies an element using the free helper from
// the open-element array classification above.
bool Parser::IsSpecialElement(const Node& element, TagId id) {
    return guchho::html::IsSpecialElement(id, element.namespace_uri);
}

// Formatting-token arena.

// Retains an immutable copy of the start-tag token of an active formatting
// element. The formatting list only stores the element node, so this arena
// keeps the original token safe while the element may be recreated by the
// adoption agency.
const TagToken* Parser::CopyFormattingToken(const TagToken& token) {
    formatting_token_arena_.push_back(std::make_unique<TagToken>(token));
    return formatting_token_arena_.back().get();
}

// Stop parsing.

// Marks the parser as stopped and, when the EOF carries a source location,
// stamps end locations on the elements left on the stack. In a fragment only
// the fragment root stays; otherwise the never-popped html and body elements
// are given the EOF position (each only if it does not already have an end
// tag of its own).
void Parser::StopParsing(EofToken& token) {
    stopped = true;

    if (token.location != nullptr) {
        const int target = fragment_context != nullptr ? 0 : 2;
        for (int i = open_elements->stack_top; i >= target; i--) {
            SetEndLocation(*open_elements->items[static_cast<size_t>(i)], AnyToken(token));
        }

        if (fragment_context == nullptr && open_elements->stack_top >= 0) {
            Node& html_element = *open_elements->items[0];
            if (html_element.source_code_location != nullptr &&
                html_element.end_tag_location == nullptr) {
                SetEndLocation(html_element, AnyToken(token));

                if (open_elements->stack_top >= 1) {
                    Node& body_element = *open_elements->items[1];
                    if (body_element.source_code_location != nullptr &&
                        body_element.end_tag_location == nullptr) {
                        SetEndLocation(body_element, AnyToken(token));
                    }
                }
            }
        }
    }
}

// Insertion-mode handlers.
//
// The functions below implement the per-mode rules. Each insertion mode --
// initial, before html, in body, in table, in select, in template, the
// frameset family, and so on -- owns a small set of StartTag/EndTag/Character/
// Comment/Eof rules; the Parser's dispatch tables route tokens here, and a
// rule that cannot handle a token hands it back to a fallback (usually the
// mode's Token* helper), which performs an automatic fix-up, switches mode,
// and reprocesses the token.

// Generic token handlers.

// Appends a comment under the adjusted current element (or the document when
// the stack is empty). The generic comment destination for "in body"-style
// modes.
void AppendComment(Parser& p, CommentToken& token) {
    Node* const parent = p.open_elements->CurrentTmplContentOrNode();
    p.AppendCommentNode(token, parent != nullptr ? *parent : *p.document);
}

// Appends a comment to the root <html> element itself, used so comments that
// arrive in the "after body" mode land between </body> and </html> where they
// are allowed.
void AppendCommentToRootHtmlElement(Parser& p, CommentToken& token) {
    p.AppendCommentNode(token, *p.open_elements->items[0]);
}

// Appends a comment directly under the document node, the destination for
// comments seen after parsing completed (after </html>).
void AppendCommentToDocument(Parser& p, CommentToken& token) {
    p.AppendCommentNode(token, *p.document);
}

// The "initial" insertion mode.

// Records the document type and moves parsing forward. The quirks mode of the
// document is derived from the doctype (or forced by the token), and a
// non-conforming doctype is reported. When the token is a doctype this is the
// only mode where it sets state; afterwards parsing continues in "before
// html".
//
//   DoctypeInInitialMode("<!doctype html>") -> document mode kNoQuirks,
//     parser moves to kBeforeHtml
void DoctypeInInitialMode(Parser& p, DoctypeToken& token) {
    p.SetDocumentType(token);

    const DocumentMode mode =
        token.force_quirks ? DocumentMode::kQuirks : GetDocumentMode(token);

    if (!IsConformingDoctype(token)) {
        p.ReportError(AnyToken(token), Err::kNonConformingDoctype);
    }

    SetDocumentMode(*p.document, mode);

    p.insertion_mode = InsertionMode::kBeforeHtml;
}

// Fallback rule for the "initial" mode: any non-doctype token means the
// document has no doctype, which is reported, forces quirks mode, and drops
// the parser into "before html". The token that caused the fix-up is then
// re-processed under the new mode.
//
//   TokenInInitialMode("<html>...") -> kMissingDoctype error, kQuirks mode,
//     parser reprocesses the <html> start tag in kBeforeHtml
void TokenInInitialMode(Parser& p, AnyToken token) {
    p.ReportError(token, Err::kMissingDoctype, true);
    SetDocumentMode(*p.document, DocumentMode::kQuirks);
    p.insertion_mode = InsertionMode::kBeforeHtml;
    p.ProcessToken(token);
}

// The "before html" insertion mode.

// An explicit <html> start tag opens the html element and moves parsing to
// "before head"; anything else uses the automatic fix-up that injects a fake
// <html> element first.
void StartTagBeforeHtml(Parser& p, TagToken& token) {
    if (token.tag_id == TagId::kHtml) {
        p.InsertElement(token, NS::kHtml);
        p.insertion_mode = InsertionMode::kBeforeHead;
    } else {
        TokenBeforeHtml(p, AnyToken(token));
    }
}

// End tags that make sense before <html> (`</html>`, `</head>`, `</body>`,
// `</br>`) trigger the same automatic fix-up as any other stray token.
void EndTagBeforeHtml(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    if (tn == TagId::kHtml || tn == TagId::kHead || tn == TagId::kBody || tn == TagId::kBr) {
        TokenBeforeHtml(p, AnyToken(token));
    }
}

// Automatic fix-up for the "before html" mode: the parser synthesizes the
// <html> root element, switches to "before head", and reprocesses the token
// that forced the change.
void TokenBeforeHtml(Parser& p, AnyToken token) {
    p.InsertFakeRootElement();
    p.insertion_mode = InsertionMode::kBeforeHead;
    p.ProcessToken(token);
}

// The "before head" insertion mode.

// An explicit <html> start tag is delegated to the "in body" rule (attribute
// adoption); <head> opens the head and moves on; any other token triggers the
// automatic fix-up that injects a fake <head> first.
void StartTagBeforeHead(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kHtml: {
            StartTagInBody(p, token);
            break;
        }
        case TagId::kHead: {
            p.InsertElement(token, NS::kHtml);
            p.head_element = p.open_elements->current;
            p.insertion_mode = InsertionMode::kInHead;
            break;
        }
        default: {
            TokenBeforeHead(p, AnyToken(token));
        }
    }
}

// End tags for head/body/html/br are treated as stray content that triggers
// the head fix-up; any other end tag is an error.
void EndTagBeforeHead(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    if (tn == TagId::kHead || tn == TagId::kBody || tn == TagId::kHtml || tn == TagId::kBr) {
        TokenBeforeHead(p, AnyToken(token));
    } else {
        p.ReportError(AnyToken(token), Err::kEndTagWithoutMatchingOpenElement);
    }
}

// Automatic fix-up for the "before head" mode: a fake <head> element is
// created and remembered as heading the document, and the token that forced
// the change is reprocessed in "in head".
void TokenBeforeHead(Parser& p, AnyToken token) {
    p.InsertFakeElement("head", TagId::kHead);
    p.head_element = p.open_elements->current;
    p.insertion_mode = InsertionMode::kInHead;
    p.ProcessToken(token);
}

// The "in head" insertion mode.

// Start tags inside <head> are mostly appended without opening elements:
// base/link/meta/bgsound/basefont are leaf elements that acknowledge their
// self-closing flag. title/style/xmp/iframe/noembed/script enter text
// parsing, and <template> pushes a template context (marker + its own mode
// stack). A stray <head> is an error; anything else uses the head fix-up.
//
//   StartTagInHead("<meta charset=utf-8>") -> <meta> appended under <head>
void StartTagInHead(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kHtml: {
            StartTagInBody(p, token);
            break;
        }
        case TagId::kBase:
        case TagId::kBasefont:
        case TagId::kBgsound:
        case TagId::kLink:
        case TagId::kMeta: {
            p.AppendElement(token, NS::kHtml);
            token.ack_self_closing = true;
            break;
        }
        case TagId::kTitle: {
            p.SwitchToTextParsing(token, State::kRcdata);
            break;
        }
        case TagId::kNoscript: {
            if (p.options.scripting_enabled) {
                p.SwitchToTextParsing(token, State::kRawtext);
            } else {
                p.InsertElement(token, NS::kHtml);
                p.insertion_mode = InsertionMode::kInHeadNoScript;
            }
            break;
        }
        case TagId::kNoframes:
        case TagId::kStyle: {
            p.SwitchToTextParsing(token, State::kRawtext);
            break;
        }
        case TagId::kScript: {
            p.SwitchToTextParsing(token, State::kScriptData);
            break;
        }
        case TagId::kTemplate: {
            p.InsertTemplate(token);
            p.active_formatting_elements->InsertMarker();
            p.frameset_ok = false;
            p.insertion_mode = InsertionMode::kInTemplate;
            p.tmpl_insertion_mode_stack.push_front(InsertionMode::kInTemplate);
            break;
        }
        case TagId::kHead: {
            p.ReportError(AnyToken(token), Err::kMisplacedStartTagForHeadElement);
            break;
        }
        default: {
            TokenInHead(p, AnyToken(token));
        }
    }
}

// A </head> end tag pops the head element and moves to "after head"; body/br/
// html end tags are stray and trigger the auto fix-up; </template> closes the
// template; anything else is an unmatched-end-tag error.
void EndTagInHead(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kHead: {
            p.open_elements->Pop();
            p.insertion_mode = InsertionMode::kAfterHead;
            break;
        }
        case TagId::kBody:
        case TagId::kBr:
        case TagId::kHtml: {
            TokenInHead(p, AnyToken(token));
            break;
        }
        case TagId::kTemplate: {
            TemplateEndTagInHead(p, token);
            break;
        }
        default: {
            p.ReportError(AnyToken(token), Err::kEndTagWithoutMatchingOpenElement);
        }
    }
}

// Shared </template> handling for every mode that can receive it (in head,
// after head, in table, in select, in cell, ...). When a template is open on
// the stack its content is closed out, the template element popped, and the
// formatting list is cleared back to the template's marker; the template-mode
// stack gives up one level and insertion mode is re-derived. When no template
// is open the end tag is a plain error.
void TemplateEndTagInHead(Parser& p, TagToken& token) {
    if (p.open_elements->tmpl_count > 0) {
        p.open_elements->GenerateImpliedEndTagsThoroughly();

        if (p.open_elements->current_tag_id != TagId::kTemplate) {
            p.ReportError(AnyToken(token), Err::kClosingOfElementWithOpenChildElements);
        }

        p.open_elements->PopUntilTagNamePopped(TagId::kTemplate);
        p.active_formatting_elements->ClearToLastMarker();
        p.tmpl_insertion_mode_stack.pop_front();
        p.ResetInsertionMode();
    } else {
        p.ReportError(AnyToken(token), Err::kEndTagWithoutMatchingOpenElement);
    }
}

// Automatic fix-up for the "in head" mode: the parser closes the (synthetic
// or explicit) head, moves to "after head", and reprocesses the token.
void TokenInHead(Parser& p, AnyToken token) {
    p.open_elements->Pop();
    p.insertion_mode = InsertionMode::kAfterHead;
    p.ProcessToken(token);
}

// The "in head no script" insertion mode.

// Rules applied inside <noscript> when scripting is disabled. Most head
// elements delegate straight back to the "in head" rules; only a nested
// <noscript> is rejected, and anything else falls through to the fix-up that
// leaves the noscript context (TokenInHeadNoScript).
void StartTagInHeadNoScript(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kHtml: {
            StartTagInBody(p, token);
            break;
        }
        case TagId::kBasefont:
        case TagId::kBgsound:
        case TagId::kHead:
        case TagId::kLink:
        case TagId::kMeta:
        case TagId::kNoframes:
        case TagId::kStyle: {
            StartTagInHead(p, token);
            break;
        }
        case TagId::kNoscript: {
            p.ReportError(AnyToken(token), Err::kNestedNoscriptInHead);
            break;
        }
        default: {
            TokenInHeadNoScript(p, AnyToken(token));
        }
    }
}

// </noscript> pops the noscript element and returns to "in head"; stray <br>
// end tags and anything else leave the noscript context via the fix-up or are
// reported as unmatched.
void EndTagInHeadNoScript(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kNoscript: {
            p.open_elements->Pop();
            p.insertion_mode = InsertionMode::kInHead;
            break;
        }
        case TagId::kBr: {
            TokenInHeadNoScript(p, AnyToken(token));
            break;
        }
        default: {
            p.ReportError(AnyToken(token), Err::kEndTagWithoutMatchingOpenElement);
        }
    }
}

// Automatic fix-up for the "in head no script" mode: content that is not
// allowed inside <noscript> closes the noscript element, reports a parse
// error (an open-elements error when the offending token is EOF), and
// reprocesses the token back in "in head".
void TokenInHeadNoScript(Parser& p, AnyToken token) {
    const Err err_code = token.type() == TokenType::kEof ? Err::kOpenElementsLeftAfterEof
                                                         : Err::kDisallowedContentInNoscriptInHead;

    p.ReportError(token, err_code);
    p.open_elements->Pop();
    p.insertion_mode = InsertionMode::kInHead;
    p.ProcessToken(token);
}

// The "after head" insertion mode.

// <html> start tags delegate to the "in body" rule; <body> opens the body and
// starts "in body"; <frameset> opens a frameset document instead. Head-only
// elements seen after the head are reprimanded but still allowed: the head is
// pushed back on the stack so they open under it, then removed again. A stray
// <head> or any other token uses the auto-fix-up that injects <body>.
void StartTagAfterHead(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kHtml: {
            StartTagInBody(p, token);
            break;
        }
        case TagId::kBody: {
            p.InsertElement(token, NS::kHtml);
            p.frameset_ok = false;
            p.insertion_mode = InsertionMode::kInBody;
            break;
        }
        case TagId::kFrameset: {
            p.InsertElement(token, NS::kHtml);
            p.insertion_mode = InsertionMode::kInFrameset;
            break;
        }
        case TagId::kBase:
        case TagId::kBasefont:
        case TagId::kBgsound:
        case TagId::kLink:
        case TagId::kMeta:
        case TagId::kNoframes:
        case TagId::kScript:
        case TagId::kStyle:
        case TagId::kTemplate:
        case TagId::kTitle: {
            p.ReportError(AnyToken(token), Err::kAbandonedHeadElementChild);
            p.open_elements->Push(*p.head_element, TagId::kHead);
            StartTagInHead(p, token);
            p.open_elements->Remove(*p.head_element);
            break;
        }
        case TagId::kHead: {
            p.ReportError(AnyToken(token), Err::kMisplacedStartTagForHeadElement);
            break;
        }
        default: {
            TokenAfterHead(p, AnyToken(token));
        }
    }
}

// Body/html/br end tags pass through the auto-fix-up; </template> closes the
// template; anything else is an unmatched-end-tag error.
void EndTagAfterHead(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kBody:
        case TagId::kHtml:
        case TagId::kBr: {
            TokenAfterHead(p, AnyToken(token));
            break;
        }
        case TagId::kTemplate: {
            TemplateEndTagInHead(p, token);
            break;
        }
        default: {
            p.ReportError(AnyToken(token), Err::kEndTagWithoutMatchingOpenElement);
        }
    }
}

// Automatic fix-up for the "after head" mode: a fake <body> element is
// created and opened, and the token is processed in "in body" mode.
void TokenAfterHead(Parser& p, AnyToken token) {
    p.InsertFakeElement("body", TagId::kBody);
    p.insertion_mode = InsertionMode::kInBody;
    ModeInBody(p, token);
}

// The "in body" and "text" insertion modes.

inline constexpr std::string_view kHiddenInputType = "hidden";

// True for an <input> element whose type attribute is exactly "hidden". Hidden
// inputs are exempt from the frameset_ok clearing that ordinary inputs cause,
// because a documents made purely of hidden inputs is still a valid frameset
// document.
bool IsHiddenInput(const TagToken& token) {
    const std::string* const input_type = GetTokenAttr(token, "type");

    return input_type != nullptr && helpers::ToLowerASCII(*input_type) == kHiddenInputType;
}

// The token-type dispatcher for "in body"-style modes. Each concrete token
// type is forwarded to the matching in-body rule; used both by the "in body"
// mode itself and by the fallback paths of other modes that bounce tokens
// through body processing (for example after the automatic <body> fix-up).
void ModeInBody(Parser& p, AnyToken token) {
    switch (token.type()) {
        case TokenType::kCharacter: {
            CharacterInBody(p, token.character());
            break;
        }
        case TokenType::kWhitespaceCharacter: {
            WhitespaceCharacterInBody(p, token.character());
            break;
        }
        case TokenType::kComment: {
            AppendComment(p, token.comment());
            break;
        }
        case TokenType::kStartTag: {
            StartTagInBody(p, token.tag());
            break;
        }
        case TokenType::kEndTag: {
            EndTagInBody(p, token.tag());
            break;
        }
        case TokenType::kEof: {
            EofInBody(p, token.eof());
            break;
        }
        default:
            break;
    }
}

// Inserts whitespace into the body. Reconstruction happens first, because
// whitespace (unlike regular text) does not forbid a frameset document; the
// run ends up inside whatever formatting element is active.
//
//   WhitespaceCharacterInBody("  ") -> text node "  " under current element
void WhitespaceCharacterInBody(Parser& p, CharacterToken& token) {
    p.ReconstructActiveFormattingElements();
    p.InsertCharacters(token);
}

// Inserts regular character data into the body. Any real text (past the
// reconstruction to re-open active formatting elements) makes the document
// non-frameset, so frameset_ok is cleared.
//
//   CharacterInBody("hi") -> text node "hi", frameset_ok = false
void CharacterInBody(Parser& p, CharacterToken& token) {
    p.ReconstructActiveFormattingElements();
    p.InsertCharacters(token);
    p.frameset_ok = false;
}

// A second <html> start tag merges its attributes into the existing html
// element (first-wins for duplicates), unless we are inside a <template>.
void HtmlStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->tmpl_count == 0) {
        AdoptAttributes(*p.open_elements->items[0], token.attrs);
    }
}

// A late <body> start tag adopts its attributes onto the properly nested body
// element (outside any template), and invalidates the frameset-ok flag.
void BodyStartTagInBody(Parser& p, TagToken& token) {
    Node* const body_element = p.open_elements->TryPeekProperlyNestedBodyElement();

    if (body_element != nullptr && p.open_elements->tmpl_count == 0) {
        p.frameset_ok = false;
        AdoptAttributes(*body_element, token.attrs);
    }
}

// A <frameset> start tag only takes over when the document so far allows a
// frameset (frameset_ok) and a proper body element exists: the body is
// detached, the stack is unwound to the html element, the frameset is opened
// in its own mode, and the body children are abandoned.
void FramesetStartTagInBody(Parser& p, TagToken& token) {
    Node* const body_element = p.open_elements->TryPeekProperlyNestedBodyElement();

    if (p.frameset_ok && body_element != nullptr) {
        auto detached = DetachNode(*body_element);
        (void)detached;
        p.open_elements->PopAllUpToHtmlElement();
        p.InsertElement(token, NS::kHtml);
        p.insertion_mode = InsertionMode::kInFrameset;
    }
}

// Block-level elements (address, article, section, div, ...) close any open
// <p> first, then open themselves.
void AddressStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInButtonScope(TagId::kP)) {
        p.ClosePElement();
    }

    p.InsertElement(token, NS::kHtml);
}

// h1-h6 headings close a paragraph, and a heading directly inside another
// heading closes the inner one first (matching the browser quirk of <h1><h2>
// auto-closing the <h1>).
void NumberedHeaderStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInButtonScope(TagId::kP)) {
        p.ClosePElement();
    }

    if (p.open_elements->current_tag_id != TagId::kUnknown &&
        IsNumberedHeader(p.open_elements->current_tag_id)) {
        p.open_elements->Pop();
    }

    p.InsertElement(token, NS::kHtml);
}

// <pre>/<listing> close a paragraph and set the skip-next-newline flag so the
// implied leading U+000A (authoring convenience) is ignored before any text
// is inserted; pre-formatted content keeps frameset_ok false.
void PreStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInButtonScope(TagId::kP)) {
        p.ClosePElement();
    }

    p.InsertElement(token, NS::kHtml);
    p.skip_next_new_line = true;
    p.frameset_ok = false;
}

// A <form> start tag inserts a form element only when no form element is in
// scope (or we are inside a template); the first form in that context is
// remembered as the document's form element. Nested forms outside templates
// are silently ignored.
void FormStartTagInBody(Parser& p, TagToken& token) {
    const bool in_template = p.open_elements->tmpl_count > 0;

    if (p.form_element == nullptr || in_template) {
        if (p.open_elements->HasInButtonScope(TagId::kP)) {
            p.ClosePElement();
        }

        p.InsertElement(token, NS::kHtml);

        if (!in_template) {
            p.form_element = p.open_elements->current;
        }
    }
}

// <li>/<dd>/<dt> close any previous list item of the same kind found at the
// ends of the open-element stack (an outer list item is only closed when a
// special element other than address/div/p separates them), then drop the
// auto-closing paragraph and open the new item. Because only the innermost
// matching item is popped, deeply nested <li><ul><li>... stays independent.
void ListItemStartTagInBody(Parser& p, TagToken& token) {
    p.frameset_ok = false;

    const TagId tn = token.tag_id;

    for (int i = p.open_elements->stack_top; i >= 0; i--) {
        const TagId element_id = p.open_elements->tag_ids[static_cast<size_t>(i)];

        if ((tn == TagId::kLi && element_id == TagId::kLi) ||
            ((tn == TagId::kDd || tn == TagId::kDt) &&
             (element_id == TagId::kDd || element_id == TagId::kDt))) {
            p.open_elements->GenerateImpliedEndTagsWithExclusion(element_id);
            p.open_elements->PopUntilTagNamePopped(element_id);
            break;
        }

        if (element_id != TagId::kAddress && element_id != TagId::kDiv &&
            element_id != TagId::kP &&
            p.IsSpecialElement(*p.open_elements->items[static_cast<size_t>(i)], element_id)) {
            break;
        }
    }

    if (p.open_elements->HasInButtonScope(TagId::kP)) {
        p.ClosePElement();
    }

    p.InsertElement(token, NS::kHtml);
}

// <plaintext> closes a paragraph, opens the element, and switches the
// tokenizer to the plaintext state: every remaining character, including
// anything that looks like markup, is treated as text until the end of input.
void PlaintextStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInButtonScope(TagId::kP)) {
        p.ClosePElement();
    }

    p.InsertElement(token, NS::kHtml);
    p.tokenizer->state = State::kPlaintext;
}

// A <button> start tag first closes any earlier button in scope (generating
// implied end tags and popping through the button), so buttons never nest.
// The new button is reconstructed and opened with frameset_ok cleared.
void ButtonStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInScope(TagId::kButton)) {
        p.open_elements->GenerateImpliedEndTags();
        p.open_elements->PopUntilTagNamePopped(TagId::kButton);
    }

    p.ReconstructActiveFormattingElements();
    p.InsertElement(token, NS::kHtml);
    p.frameset_ok = false;
}

// An <a> start tag follows the "adoption agency" rules: an existing <a> entry
// in the active formatting list is removed (along with its element) before the
// new link is reconstructed, opened, and pushed onto the formatting list.
void AStartTagInBody(Parser& p, TagToken& token) {
    Entry* const active_element_entry =
        p.active_formatting_elements->GetElementEntryInScopeWithTagName("a");

    if (active_element_entry != nullptr) {
        CallAdoptionAgency(p, token);
        p.open_elements->Remove(*active_element_entry->element);
        p.active_formatting_elements->RemoveEntry(*active_element_entry);
    }

    p.ReconstructActiveFormattingElements();
    p.InsertElement(token, NS::kHtml);
    p.active_formatting_elements->PushElement(*p.open_elements->current,
                                              *p.CopyFormattingToken(token));
}

// Inline formatting elements such as <b>/<i>/<u> reconstruct the formatting
// list, open themselves, and record an entry in the active formatting list so
// later <b> can be re-associated with the open element.
void BStartTagInBody(Parser& p, TagToken& token) {
    p.ReconstructActiveFormattingElements();
    p.InsertElement(token, NS::kHtml);
    p.active_formatting_elements->PushElement(*p.open_elements->current,
                                              *p.CopyFormattingToken(token));
}

// <nobr> is a formatting element that resists nesting: when one is already in
// scope the adoption agency unwinds it first, then reconstruction and
// insertion of the new element proceed normally.
void NobrStartTagInBody(Parser& p, TagToken& token) {
    p.ReconstructActiveFormattingElements();

    if (p.open_elements->HasInScope(TagId::kNobr)) {
        CallAdoptionAgency(p, token);
        p.ReconstructActiveFormattingElements();
    }

    p.InsertElement(token, NS::kHtml);
    p.active_formatting_elements->PushElement(*p.open_elements->current,
                                              *p.CopyFormattingToken(token));
}

// <applet> (and <embed>/<object>) reconstruct formatting, open the element,
// and place a marker in the active formatting list so stray formatting inside
// the applet cannot leak out when it closes.
void AppletStartTagInBody(Parser& p, TagToken& token) {
    p.ReconstructActiveFormattingElements();
    p.InsertElement(token, NS::kHtml);
    p.active_formatting_elements->InsertMarker();
    p.frameset_ok = false;
}

// A <table> start tag closes a paragraph when not in quirks mode, opens the
// table, and moves to the "in table" insertion mode; a table also forbids
// frameset documents.
void TableStartTagInBody(Parser& p, TagToken& token) {
    if (p.document->mode != DocumentMode::kQuirks &&
        p.open_elements->HasInButtonScope(TagId::kP)) {
        p.ClosePElement();
    }

    p.InsertElement(token, NS::kHtml);
    p.frameset_ok = false;
    p.insertion_mode = InsertionMode::kInTable;
}

// Void elements with an explicit self-closing flag (area, br, img, ...): the
// element is appended and the flag is acknowledged so it is not reported as
// an error. `AckSelfClosing = true` keeps the tokenizer silent about the
// self-closing marker.
void AreaStartTagInBody(Parser& p, TagToken& token) {
    p.ReconstructActiveFormattingElements();
    p.AppendElement(token, NS::kHtml);
    p.frameset_ok = false;
    token.ack_self_closing = true;
}

// <input> appends the element without reconstruction of its block structure;
// hidden inputs are special-cased to not clear frameset_ok, since a document
// of only hidden inputs may still become a frameset document.
void InputStartTagInBody(Parser& p, TagToken& token) {
    p.ReconstructActiveFormattingElements();
    p.AppendElement(token, NS::kHtml);

    if (!IsHiddenInput(token)) {
        p.frameset_ok = false;
    }

    token.ack_self_closing = true;
}

// <param> is a void element that is simply appended after its (already open)
// owner <object>; the self-closing flag is acknowledged.
void ParamStartTagInBody(Parser& p, TagToken& token) {
    p.AppendElement(token, NS::kHtml);
    token.ack_self_closing = true;
}

// <hr> closes a paragraph if one is open, appends itself as a void element,
// clears frameset_ok, and acknowledges the self-closing flag.
void HrStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInButtonScope(TagId::kP)) {
        p.ClosePElement();
    }

    p.AppendElement(token, NS::kHtml);
    p.frameset_ok = false;
    token.ack_self_closing = true;
}

// The obsolete <image> tag is rewritten as <img> before being handled, which
// is how browsers honor the canonical spelling while accepting the legacy
// name.
void ImageStartTagInBody(Parser& p, TagToken& token) {
    token.tag_name = "img";
    token.tag_id = TagId::kImg;
    AreaStartTagInBody(p, token);
}

// <textarea> opens an element whose content is parsed as RCDATA: the parser
// stores the previous insertion mode, skips the anticipated leading newline,
// and switches to plain text-mode parsing until the matching end tag.
void TextareaStartTagInBody(Parser& p, TagToken& token) {
    p.InsertElement(token, NS::kHtml);
    p.skip_next_new_line = true;
    p.tokenizer->state = State::kRcdata;
    p.original_insertion_mode = p.insertion_mode;
    p.frameset_ok = false;
    p.insertion_mode = InsertionMode::kText;
}

// <xmp> is rawtext: close paragraph, reconstruct, then use the shared
// text-mode switch that records the original insertion mode.
void XmpStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInButtonScope(TagId::kP)) {
        p.ClosePElement();
    }

    p.ReconstructActiveFormattingElements();
    p.frameset_ok = false;
    p.SwitchToTextParsing(token, State::kRawtext);
}

// <iframe> switches to rawtext text-mode parsing (the iframe content can only
// be text) and clears frameset_ok.
void IframeStartTagInBody(Parser& p, TagToken& token) {
    p.frameset_ok = false;
    p.SwitchToTextParsing(token, State::kRawtext);
}

// <noembed> and <noframes> are always parsed as rawtext here, matching a user
// agent with plugins and frames enabled: their content is never interpreted
// as markup.
void RawTextStartTagInBody(Parser& p, TagToken& token) {
    p.SwitchToTextParsing(token, State::kRawtext);
}

// <select> reconstructs, opens the element, clears frameset_ok, and chooses
// the "in select" or "in select in table" mode depending on whether the
// current mode is one of the table modes.
void SelectStartTagInBody(Parser& p, TagToken& token) {
    p.ReconstructActiveFormattingElements();
    p.InsertElement(token, NS::kHtml);
    p.frameset_ok = false;

    p.insertion_mode =
        p.insertion_mode == InsertionMode::kInTable ||
                p.insertion_mode == InsertionMode::kInCaption ||
                p.insertion_mode == InsertionMode::kInTableBody ||
                p.insertion_mode == InsertionMode::kInRow ||
                p.insertion_mode == InsertionMode::kInCell
            ? InsertionMode::kInSelectInTable
            : InsertionMode::kInSelect;
}

// <option> elements auto-close: a preceding option on top of the stack is
// popped first, then the new one opens.
void OptgroupStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->current_tag_id == TagId::kOption) {
        p.open_elements->Pop();
    }

    p.ReconstructActiveFormattingElements();
    p.InsertElement(token, NS::kHtml);
}

// <rb> inside a <ruby> generates implied end tags first, then opens; the rb
// element becomes current and ruby content stops.
void RbStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInScope(TagId::kRuby)) {
        p.open_elements->GenerateImpliedEndTags();
    }

    p.InsertElement(token, NS::kHtml);
}

// <rt>/<rp> behave like <rb> but keep an open <rtc> from being closed early
// (GenerateImpliedEndTagsWithExclusion(TagId::kRtc)).
void RtStartTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInScope(TagId::kRuby)) {
        p.open_elements->GenerateImpliedEndTagsWithExclusion(TagId::kRtc);
    }

    p.InsertElement(token, NS::kHtml);
}

// <math> start tags reconstruct formatting, adjust the token's attribute and
// namespace properties to the MathML of the token, and append/insert in the
// MathML namespace; the self-closing flag is acknowledged.
void MathStartTagInBody(Parser& p, TagToken& token) {
    p.ReconstructActiveFormattingElements();

    AdjustTokenMathMLAttrs(token);
    AdjustTokenXMLAttrs(token);

    if (token.self_closing) {
        p.AppendElement(token, NS::kMathml);
    } else {
        p.InsertElement(token, NS::kMathml);
    }

    token.ack_self_closing = true;
}

// <svg> start tags reconstruct formatting, adjust attribute names to the SVG
// standard (viewbox -> viewBox etc.), and append/insert the element in the
// SVG namespace with the self-closing flag acknowledged.
void SvgStartTagInBody(Parser& p, TagToken& token) {
    p.ReconstructActiveFormattingElements();

    AdjustTokenSVGAttrs(token);
    AdjustTokenXMLAttrs(token);

    if (token.self_closing) {
        p.AppendElement(token, NS::kSvg);
    } else {
        p.InsertElement(token, NS::kSvg);
    }

    token.ack_self_closing = true;
}

// Fallback for any other HTML start tag in "in body": reconstruct the active
// formatting list, open the element, and clear frameset_ok (unknown elements
// make the document non-frameset).
void GenericStartTagInBody(Parser& p, TagToken& token) {
    p.ReconstructActiveFormattingElements();
    p.InsertElement(token, NS::kHtml);
}

// The start-tag dispatcher of the "in body" insertion mode. Every known start
// tag is routed to its dedicated rule; several tags that cannot legally appear
// in the body (col, td, th, tr, thead, head, and their siblings) are simply
// ignored, while anything unknown falls back to GenericStartTagInBody.
void StartTagInBody(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kI:
        case TagId::kS:
        case TagId::kB:
        case TagId::kU:
        case TagId::kEm:
        case TagId::kTt:
        case TagId::kBig:
        case TagId::kCode:
        case TagId::kFont:
        case TagId::kSmall:
        case TagId::kStrike:
        case TagId::kStrong: {
            BStartTagInBody(p, token);
            break;
        }
        case TagId::kA: {
            AStartTagInBody(p, token);
            break;
        }
        case TagId::kH1:
        case TagId::kH2:
        case TagId::kH3:
        case TagId::kH4:
        case TagId::kH5:
        case TagId::kH6: {
            NumberedHeaderStartTagInBody(p, token);
            break;
        }
        case TagId::kP:
        case TagId::kDl:
        case TagId::kOl:
        case TagId::kUl:
        case TagId::kDiv:
        case TagId::kDir:
        case TagId::kNav:
        case TagId::kMain:
        case TagId::kMenu:
        case TagId::kAside:
        case TagId::kCenter:
        case TagId::kFigure:
        case TagId::kFooter:
        case TagId::kHeader:
        case TagId::kHgroup:
        case TagId::kDialog:
        case TagId::kDetails:
        case TagId::kAddress:
        case TagId::kArticle:
        case TagId::kSearch:
        case TagId::kSection:
        case TagId::kSummary:
        case TagId::kFieldset:
        case TagId::kBlockquote:
        case TagId::kFigcaption: {
            AddressStartTagInBody(p, token);
            break;
        }
        case TagId::kLi:
        case TagId::kDd:
        case TagId::kDt: {
            ListItemStartTagInBody(p, token);
            break;
        }
        case TagId::kBr:
        case TagId::kImg:
        case TagId::kWbr:
        case TagId::kArea:
        case TagId::kEmbed:
        case TagId::kKeygen: {
            AreaStartTagInBody(p, token);
            break;
        }
        case TagId::kHr: {
            HrStartTagInBody(p, token);
            break;
        }
        case TagId::kRb:
        case TagId::kRtc: {
            RbStartTagInBody(p, token);
            break;
        }
        case TagId::kRt:
        case TagId::kRp: {
            RtStartTagInBody(p, token);
            break;
        }
        case TagId::kPre:
        case TagId::kListing: {
            PreStartTagInBody(p, token);
            break;
        }
        case TagId::kXmp: {
            XmpStartTagInBody(p, token);
            break;
        }
        case TagId::kSvg: {
            SvgStartTagInBody(p, token);
            break;
        }
        case TagId::kHtml: {
            HtmlStartTagInBody(p, token);
            break;
        }
        case TagId::kBase:
        case TagId::kLink:
        case TagId::kMeta:
        case TagId::kStyle:
        case TagId::kTitle:
        case TagId::kScript:
        case TagId::kBgsound:
        case TagId::kBasefont:
        case TagId::kTemplate: {
            StartTagInHead(p, token);
            break;
        }
        case TagId::kBody: {
            BodyStartTagInBody(p, token);
            break;
        }
        case TagId::kForm: {
            FormStartTagInBody(p, token);
            break;
        }
        case TagId::kNobr: {
            NobrStartTagInBody(p, token);
            break;
        }
        case TagId::kMath: {
            MathStartTagInBody(p, token);
            break;
        }
        case TagId::kTable: {
            TableStartTagInBody(p, token);
            break;
        }
        case TagId::kInput: {
            InputStartTagInBody(p, token);
            break;
        }
        case TagId::kParam:
        case TagId::kTrack:
        case TagId::kSource: {
            ParamStartTagInBody(p, token);
            break;
        }
        case TagId::kImage: {
            ImageStartTagInBody(p, token);
            break;
        }
        case TagId::kButton: {
            ButtonStartTagInBody(p, token);
            break;
        }
        case TagId::kApplet:
        case TagId::kObject:
        case TagId::kMarquee: {
            AppletStartTagInBody(p, token);
            break;
        }
        case TagId::kIframe: {
            IframeStartTagInBody(p, token);
            break;
        }
        case TagId::kSelect: {
            SelectStartTagInBody(p, token);
            break;
        }
        case TagId::kOption:
        case TagId::kOptgroup: {
            OptgroupStartTagInBody(p, token);
            break;
        }
        case TagId::kNoembed:
        case TagId::kNoframes: {
            RawTextStartTagInBody(p, token);
            break;
        }
        case TagId::kFrameset: {
            FramesetStartTagInBody(p, token);
            break;
        }
        case TagId::kTextarea: {
            TextareaStartTagInBody(p, token);
            break;
        }
        case TagId::kNoscript: {
            if (p.options.scripting_enabled) {
                RawTextStartTagInBody(p, token);
            } else {
                GenericStartTagInBody(p, token);
            }
            break;
        }
        case TagId::kPlaintext: {
            PlaintextStartTagInBody(p, token);
            break;
        }

        case TagId::kCol:
        case TagId::kTh:
        case TagId::kTd:
        case TagId::kTr:
        case TagId::kHead:
        case TagId::kFrame:
        case TagId::kTbody:
        case TagId::kTfoot:
        case TagId::kThead:
        case TagId::kCaption:
        case TagId::kColgroup: {
            break;
        }
        default: {
            GenericStartTagInBody(p, token);
        }
    }
}

// End-tag handlers of the "in body" insertion mode.

// </body> switches to "after body". The body element is itself never popped
// from the stack, so its end location is recorded explicitly from the token
// when source-location tracking is enabled.
void BodyEndTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInScope(TagId::kBody)) {
        p.insertion_mode = InsertionMode::kAfterBody;

        if (p.options.source_code_location_info) {
            Node* const body_element = p.open_elements->TryPeekProperlyNestedBodyElement();
            if (body_element != nullptr) {
                p.SetEndLocation(*body_element, AnyToken(token));
            }
        }
    }
}

// </html> acts like </body> and is then re-processed in "after body" mode,
// which finishes the document.
void HtmlEndTagInBody(Parser& p, TagToken& token) {
    if (p.open_elements->HasInScope(TagId::kBody)) {
        p.insertion_mode = InsertionMode::kAfterBody;
        EndTagAfterBody(p, token);
    }
}

// End tags of block elements (div, section, article, footer, ...) close the
// matching element when it is in scope, first generating implied end tags so
// nested inline content is closed out.
void AddressEndTagInBody(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    if (p.open_elements->HasInScope(tn)) {
        p.open_elements->GenerateImpliedEndTags();
        p.open_elements->PopUntilTagNamePopped(tn);
    }
}

// </form> clears the remembered form element (unless inside a template) and,
// when the form is in scope, closes it: inside templates the stack is popped
// through the form; otherwise the remembered form node is removed directly.
// The token itself is unused, which keeps the signature uniform.
void FormEndTagInBody(Parser& p, TagToken&) {
    const bool in_template = p.open_elements->tmpl_count > 0;
    Node* const form_element = p.form_element;

    if (!in_template) {
        p.form_element = nullptr;
    }

    if ((form_element != nullptr || in_template) && p.open_elements->HasInScope(TagId::kForm)) {
        p.open_elements->GenerateImpliedEndTags();

        if (in_template) {
            p.open_elements->PopUntilTagNamePopped(TagId::kForm);
        } else if (form_element != nullptr) {
            p.open_elements->Remove(*form_element);
        }
    }
}

// </p> is forgiving: if no paragraph is in button scope a fake <p> is opened
// first, then the paragraph is closed (the paragraph being closed is the one
// the close operation targets).
void PEndTagInBody(Parser& p) {
    if (!p.open_elements->HasInButtonScope(TagId::kP)) {
        p.InsertFakeElement("p", TagId::kP);
    }

    p.ClosePElement();
}

// </li> ends the nearest list item while keeping its inline children open
// until the item itself is popped.
void LiEndTagInBody(Parser& p) {
    if (p.open_elements->HasInListItemScope(TagId::kLi)) {
        p.open_elements->GenerateImpliedEndTagsWithExclusion(TagId::kLi);
        p.open_elements->PopUntilTagNamePopped(TagId::kLi);
    }
}

// </dd>/</dt> close the matching description element when in scope; inline
// descendants stay open while the item is popped.
void DdEndTagInBody(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    if (p.open_elements->HasInScope(tn)) {
        p.open_elements->GenerateImpliedEndTagsWithExclusion(tn);
        p.open_elements->PopUntilTagNamePopped(tn);
    }
}

// </h1>..</h6> close whichever heading is currently in scope, with the usual
// implied-end-tag generation before popping the heading.
void NumberedHeaderEndTagInBody(Parser& p) {
    if (p.open_elements->HasNumberedHeaderInScope()) {
        p.open_elements->GenerateImpliedEndTags();
        p.open_elements->PopUntilNumberedHeaderPopped();
    }
}

// </applet>/</object>/</marquee> close the element when in scope and clear the
// active formatting list back to the marker that the element left behind.
void AppletEndTagInBody(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    if (p.open_elements->HasInScope(tn)) {
        p.open_elements->GenerateImpliedEndTags();
        p.open_elements->PopUntilTagNamePopped(tn);
        p.active_formatting_elements->ClearToLastMarker();
    }
}

// A stray </br> end tag is forgiving: a fake <br> element is created and
// immediately popped, emitting an empty break with frameset_ok cleared.
void BrEndTagInBody(Parser& p) {
    p.ReconstructActiveFormattingElements();
    p.InsertFakeElement("br", TagId::kBr);
    p.open_elements->Pop();
    p.frameset_ok = false;
}

// Fallback end-tag handler: the stack is scanned from the top for a matching
// element by tag id (and, for unknown tags, by exact name). When found, the
// implied end tags are generated and the stack is shortened past it; any
// special element encountered first stops the search.
void GenericEndTagInBody(Parser& p, TagToken& token) {
    const std::string_view tn = token.tag_name;
    const TagId tid = token.tag_id;

    for (int i = p.open_elements->stack_top; i > 0; i--) {
        Node* const element = p.open_elements->items[static_cast<size_t>(i)];
        const TagId element_id = p.open_elements->tag_ids[static_cast<size_t>(i)];

        if (tid == element_id &&
            (tid != TagId::kUnknown || DefaultTreeAdapter::GetTagName(*element) == tn)) {
            p.open_elements->GenerateImpliedEndTagsWithExclusion(tid);
            if (p.open_elements->stack_top >= i) {
                p.open_elements->ShortenToLength(i);
            }
            break;
        }

        if (p.IsSpecialElement(*element, element_id)) {
            break;
        }
    }
}

// The end-tag dispatcher for the "in body" mode. Formatting-element end tags
// run through the adoption agency, block elements and list items/headings use
// their in-scope checks, and anything else reaches the generic handler.
void EndTagInBody(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kA:
        case TagId::kB:
        case TagId::kI:
        case TagId::kS:
        case TagId::kU:
        case TagId::kEm:
        case TagId::kTt:
        case TagId::kBig:
        case TagId::kCode:
        case TagId::kFont:
        case TagId::kNobr:
        case TagId::kSmall:
        case TagId::kStrike:
        case TagId::kStrong: {
            CallAdoptionAgency(p, token);
            break;
        }
        case TagId::kP: {
            PEndTagInBody(p);
            break;
        }
        case TagId::kDl:
        case TagId::kUl:
        case TagId::kOl:
        case TagId::kDir:
        case TagId::kDiv:
        case TagId::kNav:
        case TagId::kPre:
        case TagId::kMain:
        case TagId::kMenu:
        case TagId::kAside:
        case TagId::kButton:
        case TagId::kCenter:
        case TagId::kFigure:
        case TagId::kFooter:
        case TagId::kHeader:
        case TagId::kHgroup:
        case TagId::kDialog:
        case TagId::kAddress:
        case TagId::kArticle:
        case TagId::kDetails:
        case TagId::kSearch:
        case TagId::kSection:
        case TagId::kSummary:
        case TagId::kListing:
        case TagId::kFieldset:
        case TagId::kBlockquote:
        case TagId::kFigcaption: {
            AddressEndTagInBody(p, token);
            break;
        }
        case TagId::kLi: {
            LiEndTagInBody(p);
            break;
        }
        case TagId::kDd:
        case TagId::kDt: {
            DdEndTagInBody(p, token);
            break;
        }
        case TagId::kH1:
        case TagId::kH2:
        case TagId::kH3:
        case TagId::kH4:
        case TagId::kH5:
        case TagId::kH6: {
            NumberedHeaderEndTagInBody(p);
            break;
        }
        case TagId::kBr: {
            BrEndTagInBody(p);
            break;
        }
        case TagId::kBody: {
            BodyEndTagInBody(p, token);
            break;
        }
        case TagId::kHtml: {
            HtmlEndTagInBody(p, token);
            break;
        }
        case TagId::kForm: {
            FormEndTagInBody(p, token);
            break;
        }
        case TagId::kApplet:
        case TagId::kObject:
        case TagId::kMarquee: {
            AppletEndTagInBody(p, token);
            break;
        }
        case TagId::kTemplate: {
            TemplateEndTagInHead(p, token);
            break;
        }
        default: {
            GenericEndTagInBody(p, token);
        }
    }
}

// End-of-file in "in body": with an open template the shutdown is handled by
// the template rules (which close the remaining templates), otherwise normal
// stop-parsing closes the document.
void EofInBody(Parser& p, EofToken& token) {
    if (!p.tmpl_insertion_mode_stack.empty()) {
        EofInTemplate(p, token);
    } else {
        p.StopParsing(token);
    }
}

// The "text" insertion mode.

// </script> runs the script handler on the just-closed element before text
// mode ends; any other end tag simply pops the element and restores the
// insertion mode that text-mode parsing was entered from.
void EndTagInText(Parser& p, TagToken& token) {
    if (token.tag_id == TagId::kScript && p.script_handler) {
        p.script_handler(*p.open_elements->current);
    }

    p.open_elements->Pop();
    p.insertion_mode = p.original_insertion_mode;
}

// EOF inside a text-mode element (e.g. an unterminated <textarea>) is reported
// as an error; the element is still popped and the restored mode processes
// the EOF normally.
void EofInText(Parser& p, EofToken& token) {
    p.ReportError(AnyToken(token), Err::kEofInElementThatCanContainOnlyText);
    p.open_elements->Pop();
    p.insertion_mode = p.original_insertion_mode;
    p.OnEof(token);
}

// Insertion modes: "in table" family, "in select", "in template",
// "in frameset" family and the trailing "after*" modes.

// True for the table-related tags whose start/end inside a caption or a cell
// forces that caption/cell to close first so the table structure is reached
// again.
inline bool IsTableVoidElement(TagId tn) {
    switch (tn) {
        case TagId::kCaption:
        case TagId::kCol:
        case TagId::kColgroup:
        case TagId::kTbody:
        case TagId::kTd:
        case TagId::kTfoot:
        case TagId::kTh:
        case TagId::kThead:
        case TagId::kTr:
            return true;
        default:
            return false;
    }
}

// The "in table" insertion mode.

// Fallback for the "in table" mode: the token is handed to the "in body"
// rules while foster parenting is enabled, so any content inside the table is
// misplaced outside of it (foster-parented), preserving the table structure.
void TokenInTable(Parser& p, AnyToken token) {
    const bool saved_foster_parenting_state = p.foster_parenting_enabled;

    p.foster_parenting_enabled = true;
    ModeInBody(p, token);
    p.foster_parenting_enabled = saved_foster_parenting_state;
}

// Character data while a table-structure element is current is buffered as
// pending characters that get flushed when the structure element closes (or
// when an EOF is reached), rather than inserted directly into the table.
void CharacterInTable(Parser& p, CharacterToken& token) {
    if (p.open_elements->current_tag_id != TagId::kUnknown &&
        IsTableStructureTag(p.open_elements->current_tag_id)) {
        p.pending_character_tokens.clear();
        p.has_non_whitespace_pending_character_token = false;
        p.original_insertion_mode = p.insertion_mode;
        p.insertion_mode = InsertionMode::kInTableText;

        switch (token.type) {
            case TokenType::kCharacter: {
                CharacterInTableText(p, token);
                break;
            }
            case TokenType::kWhitespaceCharacter: {
                WhitespaceCharacterInTableText(p, token);
                break;
            }
            default:
                break;
        }
    } else {
        TokenInTable(p, AnyToken(token));
    }
}

// <caption> clears the stack back to the table, puts a marker on the active
// formatting list (so formatting cannot escape the caption), opens the caption
// and switches to "in caption".
void CaptionStartTagInTable(Parser& p, TagToken& token) {
    p.open_elements->ClearBackToTableContext();
    p.active_formatting_elements->InsertMarker();
    p.InsertElement(token, NS::kHtml);
    p.insertion_mode = InsertionMode::kInCaption;
}

// <colgroup> resets to table context and opens a column group in its mode.
void ColgroupStartTagInTable(Parser& p, TagToken& token) {
    p.open_elements->ClearBackToTableContext();
    p.InsertElement(token, NS::kHtml);
    p.insertion_mode = InsertionMode::kInColumnGroup;
}

// A bare <col> start tag inserts an implied <colgroup> so the row rules of
// the column-group mode take over immediately with the col as its child.
void ColStartTagInTable(Parser& p, TagToken& token) {
    p.open_elements->ClearBackToTableContext();
    p.InsertFakeElement("colgroup", TagId::kColgroup);
    p.insertion_mode = InsertionMode::kInColumnGroup;
    StartTagInColumnGroup(p, token);
}

// <tbody>/<tfoot>/<thead> reset to table context and open the section in
// "in table body" mode.
void TbodyStartTagInTable(Parser& p, TagToken& token) {
    p.open_elements->ClearBackToTableContext();
    p.InsertElement(token, NS::kHtml);
    p.insertion_mode = InsertionMode::kInTableBody;
}

// A bare <td>/<th>/<tr> start tag inserts an implied <tbody> and continues in
// "in table body", which then handles the cell/row.
void TdStartTagInTable(Parser& p, TagToken& token) {
    p.open_elements->ClearBackToTableContext();
    p.InsertFakeElement("tbody", TagId::kTbody);
    p.insertion_mode = InsertionMode::kInTableBody;
    StartTagInTableBody(p, token);
}

// A nested <table> start tag closes the current table, recomputes the mode,
// and re-processes the token (so the second table opens outside the first).
void TableStartTagInTable(Parser& p, TagToken& token) {
    if (p.open_elements->HasInTableScope(TagId::kTable)) {
        p.open_elements->PopUntilTagNamePopped(TagId::kTable);
        p.ResetInsertionMode();
        p.ProcessStartTag(token);
    }
}

// Hidden inputs are allowed straight into a table (they do not disturb it),
// while any other <input> is foster-parented out of the table like normal
// content.
void InputStartTagInTable(Parser& p, TagToken& token) {
    if (IsHiddenInput(token)) {
        p.AppendElement(token, NS::kHtml);
    } else {
        TokenInTable(p, AnyToken(token));
    }

    token.ack_self_closing = true;
}

// A <form> start tag in a table only inserts (and immediately re-pops) the
// form element when no form is remembered and we are outside templates; the
// element thus holds a form owner for the table's content without staying on
// the stack.
void FormStartTagInTable(Parser& p, TagToken& token) {
    if (p.form_element == nullptr && p.open_elements->tmpl_count == 0) {
        p.InsertElement(token, NS::kHtml);
        p.form_element = p.open_elements->current;
        p.open_elements->Pop();
    }
}

// The start/end-tag dispatcher of the "in table" insertion mode. Inserts that
// belong inside the table are handled here; everything else is delegated to
// the foster-parenting fallback.
void StartTagInTable(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kTd:
        case TagId::kTh:
        case TagId::kTr: {
            TdStartTagInTable(p, token);
            break;
        }
        case TagId::kStyle:
        case TagId::kScript:
        case TagId::kTemplate: {
            StartTagInHead(p, token);
            break;
        }
        case TagId::kCol: {
            ColStartTagInTable(p, token);
            break;
        }
        case TagId::kForm: {
            FormStartTagInTable(p, token);
            break;
        }
        case TagId::kTable: {
            TableStartTagInTable(p, token);
            break;
        }
        case TagId::kTbody:
        case TagId::kTfoot:
        case TagId::kThead: {
            TbodyStartTagInTable(p, token);
            break;
        }
        case TagId::kInput: {
            InputStartTagInTable(p, token);
            break;
        }
        case TagId::kCaption: {
            CaptionStartTagInTable(p, token);
            break;
        }
        case TagId::kColgroup: {
            ColgroupStartTagInTable(p, token);
            break;
        }
        default: {
            TokenInTable(p, AnyToken(token));
        }
    }
}

void EndTagInTable(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kTable: {
            if (p.open_elements->HasInTableScope(TagId::kTable)) {
                p.open_elements->PopUntilTagNamePopped(TagId::kTable);
                p.ResetInsertionMode();
            }
            break;
        }
        case TagId::kTemplate: {
            TemplateEndTagInHead(p, token);
            break;
        }
        case TagId::kBody:
        case TagId::kCaption:
        case TagId::kCol:
        case TagId::kColgroup:
        case TagId::kHtml:
        case TagId::kTbody:
        case TagId::kTd:
        case TagId::kTfoot:
        case TagId::kTh:
        case TagId::kThead:
        case TagId::kTr: {
            break;
        }
        default: {
            TokenInTable(p, AnyToken(token));
        }
    }
}

// The "in table text" insertion mode.

// Whitespace character in table text mode is appended to the pending list but
// does not mark the pending content as non-whitespace, so it can be inserted
// directly when the table structure closes.
void WhitespaceCharacterInTableText(Parser& p, CharacterToken& token) {
    p.pending_character_tokens.push_back(token);
}

// A non-whitespace character is buffered and flags the pending buffer as
// containing real text; that flag later decides whether the buffered
// characters are inserted directly or foster-parented out of the table.
void CharacterInTableText(Parser& p, CharacterToken& token) {
    p.pending_character_tokens.push_back(token);
    p.has_non_whitespace_pending_character_token = true;
}

// Flush rule of the "in table text" mode: any pending characters are inserted
// (whitespace stays in place, real text is foster-parented out of the table),
// the saved mode is restored, and the current token resumes processing there.
void TokenInTableText(Parser& p, AnyToken token) {
    size_t i = 0;

    if (p.has_non_whitespace_pending_character_token) {
        for (; i < p.pending_character_tokens.size(); i++) {
            TokenInTable(p, AnyToken(p.pending_character_tokens[i]));
        }
    } else {
        for (; i < p.pending_character_tokens.size(); i++) {
            p.InsertCharacters(p.pending_character_tokens[i]);
        }
    }

    p.insertion_mode = p.original_insertion_mode;
    p.ProcessToken(token);
}

// The "in caption" insertion mode.

// A table-structure start tag inside a caption closes the caption first and
// re-runs the "in table" rule for the token; any other start tag is handled
// with the ordinary "in body" rules so captions support inline content.
void StartTagInCaption(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    if (IsTableVoidElement(tn)) {
        if (p.open_elements->HasInTableScope(TagId::kCaption)) {
            p.open_elements->GenerateImpliedEndTags();
            p.open_elements->PopUntilTagNamePopped(TagId::kCaption);
            p.active_formatting_elements->ClearToLastMarker();
            p.insertion_mode = InsertionMode::kInTable;
            StartTagInTable(p, token);
        }
    } else {
        StartTagInBody(p, token);
    }
}

// </caption> (or </table>) closes the caption, clears the formatting marker,
// returns to "in table", and for </table> delegates to the table end-tag rule.
// Table-structure end tags are ignored; everything else is an "in body" end
// tag.
void EndTagInCaption(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    switch (tn) {
        case TagId::kCaption:
        case TagId::kTable: {
            if (p.open_elements->HasInTableScope(TagId::kCaption)) {
                p.open_elements->GenerateImpliedEndTags();
                p.open_elements->PopUntilTagNamePopped(TagId::kCaption);
                p.active_formatting_elements->ClearToLastMarker();
                p.insertion_mode = InsertionMode::kInTable;

                if (tn == TagId::kTable) {
                    EndTagInTable(p, token);
                }
            }
            break;
        }
        case TagId::kBody:
        case TagId::kCol:
        case TagId::kColgroup:
        case TagId::kHtml:
        case TagId::kTbody:
        case TagId::kTd:
        case TagId::kTfoot:
        case TagId::kTh:
        case TagId::kThead:
        case TagId::kTr: {
            break;
        }
        default: {
            EndTagInBody(p, token);
        }
    }
}

// The "in column group" insertion mode.

// <html> delegates to the body rule; <col> appends a void column and
// acknowledges its self-closing flag; <template> is handled by the head rules;
// anything else exits the column group via the auto-fix-up.
void StartTagInColumnGroup(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kHtml: {
            StartTagInBody(p, token);
            break;
        }
        case TagId::kCol: {
            p.AppendElement(token, NS::kHtml);
            token.ack_self_closing = true;
            break;
        }
        case TagId::kTemplate: {
            StartTagInHead(p, token);
            break;
        }
        default: {
            TokenInColumnGroup(p, AnyToken(token));
        }
    }
}

// </colgroup> pops the column group back to "in table"; </template> closes the
// template; </col> is ignored; anything else bounces through the fix-up.
void EndTagInColumnGroup(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kColgroup: {
            if (p.open_elements->current_tag_id == TagId::kColgroup) {
                p.open_elements->Pop();
                p.insertion_mode = InsertionMode::kInTable;
            }
            break;
        }
        case TagId::kTemplate: {
            TemplateEndTagInHead(p, token);
            break;
        }
        case TagId::kCol: {
            break;
        }
        default: {
            TokenInColumnGroup(p, AnyToken(token));
        }
    }
}

// Automatic fix-up for "in column group": when the current element is still a
// colgroup it is popped back to "in table" and the token is re-processed
// there.
void TokenInColumnGroup(Parser& p, AnyToken token) {
    if (p.open_elements->current_tag_id == TagId::kColgroup) {
        p.open_elements->Pop();
        p.insertion_mode = InsertionMode::kInTable;
        p.ProcessToken(token);
    }
}

// The "in table body" insertion mode.

// <tr> clears back to the table-body context and opens a row in "in row";
// a bare <td>/<th> inserts an implied <tr> first and continues there. A new
// section tag closes the current section and re-enters the table mode; any
// other start tag delegates back to the table dispatcher.
void StartTagInTableBody(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kTr: {
            p.open_elements->ClearBackToTableBodyContext();
            p.InsertElement(token, NS::kHtml);
            p.insertion_mode = InsertionMode::kInRow;
            break;
        }
        case TagId::kTh:
        case TagId::kTd: {
            p.open_elements->ClearBackToTableBodyContext();
            p.InsertFakeElement("tr", TagId::kTr);
            p.insertion_mode = InsertionMode::kInRow;
            StartTagInRow(p, token);
            break;
        }
        case TagId::kCaption:
        case TagId::kCol:
        case TagId::kColgroup:
        case TagId::kTbody:
        case TagId::kTfoot:
        case TagId::kThead: {
            if (p.open_elements->HasTableBodyContextInTableScope()) {
                p.open_elements->ClearBackToTableBodyContext();
                p.open_elements->Pop();
                p.insertion_mode = InsertionMode::kInTable;
                StartTagInTable(p, token);
            }
            break;
        }
        default: {
            StartTagInTable(p, token);
        }
    }
}

// </tbody>/</tfoot>/</thead> close the section when in scope and return to
// "in table"; </table> also closes the section first, then lets the table
// end-tag rule run. Forgotten table end tags are ignored; anything else is
// delegated to the table end-tag handler.
void EndTagInTableBody(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    switch (token.tag_id) {
        case TagId::kTbody:
        case TagId::kTfoot:
        case TagId::kThead: {
            if (p.open_elements->HasInTableScope(tn)) {
                p.open_elements->ClearBackToTableBodyContext();
                p.open_elements->Pop();
                p.insertion_mode = InsertionMode::kInTable;
            }
            break;
        }
        case TagId::kTable: {
            if (p.open_elements->HasTableBodyContextInTableScope()) {
                p.open_elements->ClearBackToTableBodyContext();
                p.open_elements->Pop();
                p.insertion_mode = InsertionMode::kInTable;
                EndTagInTable(p, token);
            }
            break;
        }
        case TagId::kBody:
        case TagId::kCaption:
        case TagId::kCol:
        case TagId::kColgroup:
        case TagId::kHtml:
        case TagId::kTd:
        case TagId::kTh:
        case TagId::kTr: {
            break;
        }
        default: {
            EndTagInTable(p, token);
        }
    }
}

// The "in row" insertion mode.

// <td>/<th> clear back to the row context, open the cell, and switch to
// "in cell" (with a formatting marker so formatting stays inside the cell).
// A new section or row start tag closes the current row first.
void StartTagInRow(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kTh:
        case TagId::kTd: {
            p.open_elements->ClearBackToTableRowContext();
            p.InsertElement(token, NS::kHtml);
            p.insertion_mode = InsertionMode::kInCell;
            p.active_formatting_elements->InsertMarker();
            break;
        }
        case TagId::kCaption:
        case TagId::kCol:
        case TagId::kColgroup:
        case TagId::kTbody:
        case TagId::kTfoot:
        case TagId::kThead:
        case TagId::kTr: {
            if (p.open_elements->HasInTableScope(TagId::kTr)) {
                p.open_elements->ClearBackToTableRowContext();
                p.open_elements->Pop();
                p.insertion_mode = InsertionMode::kInTableBody;
                StartTagInTableBody(p, token);
            }
            break;
        }
        default: {
            StartTagInTable(p, token);
        }
    }
}

// </tr> closes the row and returns to "in table body"; </table> and section
// end tags close the row (if present) then continue with the table-body end
// tag rules. Forgotten cell/section tags are ignored; everything else goes to
// the table end-tag handler.
void EndTagInRow(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kTr: {
            if (p.open_elements->HasInTableScope(TagId::kTr)) {
                p.open_elements->ClearBackToTableRowContext();
                p.open_elements->Pop();
                p.insertion_mode = InsertionMode::kInTableBody;
            }
            break;
        }
        case TagId::kTable: {
            if (p.open_elements->HasInTableScope(TagId::kTr)) {
                p.open_elements->ClearBackToTableRowContext();
                p.open_elements->Pop();
                p.insertion_mode = InsertionMode::kInTableBody;
                EndTagInTableBody(p, token);
            }
            break;
        }
        case TagId::kTbody:
        case TagId::kTfoot:
        case TagId::kThead: {
            if (p.open_elements->HasInTableScope(token.tag_id) ||
                p.open_elements->HasInTableScope(TagId::kTr)) {
                p.open_elements->ClearBackToTableRowContext();
                p.open_elements->Pop();
                p.insertion_mode = InsertionMode::kInTableBody;
                EndTagInTableBody(p, token);
            }
            break;
        }
        case TagId::kBody:
        case TagId::kCaption:
        case TagId::kCol:
        case TagId::kColgroup:
        case TagId::kHtml:
        case TagId::kTd:
        case TagId::kTh: {
            break;
        }
        default: {
            EndTagInTable(p, token);
        }
    }
}

// The "in cell" insertion mode.

// A table-structure start tag inside a cell closes the current cell (via
// CloseTableCell, which also advances to the next cell) and re-runs the row
// rule; other start tags use the ordinary "in body" rules so a cell supports
// regular content.
void StartTagInCell(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    if (IsTableVoidElement(tn)) {
        if (p.open_elements->HasInTableScope(TagId::kTd) ||
            p.open_elements->HasInTableScope(TagId::kTh)) {
            p.CloseTableCell();
            StartTagInRow(p, token);
        }
    } else {
        StartTagInBody(p, token);
    }
}

// </td>/</th> close the matching cell in scope, clear its formatting marker,
// and return to "in row"; end tags that imply a cell boundary (table, tbody,
// row) close the current cell first and then continue with the "in row" end
// tag rules. Forgotten structural tags are ignored; otherwise the rules fall
// back to "in body" end tags.
void EndTagInCell(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    switch (tn) {
        case TagId::kTd:
        case TagId::kTh: {
            if (p.open_elements->HasInTableScope(tn)) {
                p.open_elements->GenerateImpliedEndTags();
                p.open_elements->PopUntilTagNamePopped(tn);
                p.active_formatting_elements->ClearToLastMarker();
                p.insertion_mode = InsertionMode::kInRow;
            }
            break;
        }
        case TagId::kTable:
        case TagId::kTbody:
        case TagId::kTfoot:
        case TagId::kThead:
        case TagId::kTr: {
            if (p.open_elements->HasInTableScope(tn)) {
                p.CloseTableCell();
                EndTagInRow(p, token);
            }
            break;
        }
        case TagId::kBody:
        case TagId::kCaption:
        case TagId::kCol:
        case TagId::kColgroup:
        case TagId::kHtml: {
            break;
        }
        default: {
            EndTagInBody(p, token);
        }
    }
}

// The "in select" insertion mode.

// Inside a <select>, only <option>, <optgroup>, <hr> and script/template
// markup is honored. Options and groups auto-close each other; an input,
// textarea, keygen, or a nested select closes the current select (which then
// reprocesses non-select tags), and effectively everything else is ignored.
void StartTagInSelect(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kHtml: {
            StartTagInBody(p, token);
            break;
        }
        case TagId::kOption: {
            if (p.open_elements->current_tag_id == TagId::kOption) {
                p.open_elements->Pop();
            }

            p.InsertElement(token, NS::kHtml);
            break;
        }
        case TagId::kOptgroup: {
            if (p.open_elements->current_tag_id == TagId::kOption) {
                p.open_elements->Pop();
            }

            if (p.open_elements->current_tag_id == TagId::kOptgroup) {
                p.open_elements->Pop();
            }

            p.InsertElement(token, NS::kHtml);
            break;
        }
        case TagId::kHr: {
            if (p.open_elements->current_tag_id == TagId::kOption) {
                p.open_elements->Pop();
            }

            if (p.open_elements->current_tag_id == TagId::kOptgroup) {
                p.open_elements->Pop();
            }

            p.AppendElement(token, NS::kHtml);
            token.ack_self_closing = true;
            break;
        }
        case TagId::kInput:
        case TagId::kKeygen:
        case TagId::kTextarea:
        case TagId::kSelect: {
            if (p.open_elements->HasInSelectScope(TagId::kSelect)) {
                p.open_elements->PopUntilTagNamePopped(TagId::kSelect);
                p.ResetInsertionMode();

                if (token.tag_id != TagId::kSelect) {
                    p.ProcessStartTag(token);
                }
            }
            break;
        }
        case TagId::kScript:
        case TagId::kTemplate: {
            StartTagInHead(p, token);
            break;
        }
        default:
            break;
    }
}

// End tags in a select: </optgroup> closes an open group (after closing a
// trailing <option>), </option> closes an option, </select> pops the select
// and resets the mode, </template> closes the template, and any other end tag
// is ignored.
void EndTagInSelect(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kOptgroup: {
            if (p.open_elements->stack_top > 0 &&
                p.open_elements->current_tag_id == TagId::kOption &&
                p.open_elements->tag_ids[static_cast<size_t>(p.open_elements->stack_top - 1)] ==
                    TagId::kOptgroup) {
                p.open_elements->Pop();
            }

            if (p.open_elements->current_tag_id == TagId::kOptgroup) {
                p.open_elements->Pop();
            }
            break;
        }
        case TagId::kOption: {
            if (p.open_elements->current_tag_id == TagId::kOption) {
                p.open_elements->Pop();
            }
            break;
        }
        case TagId::kSelect: {
            if (p.open_elements->HasInSelectScope(TagId::kSelect)) {
                p.open_elements->PopUntilTagNamePopped(TagId::kSelect);
                p.ResetInsertionMode();
            }
            break;
        }
        case TagId::kTemplate: {
            TemplateEndTagInHead(p, token);
            break;
        }
        default:
            break;
    }
}

// The "in select in table" insertion mode.

// True for the table tags that are not allowed inside a select opened within a
// table; when one appears the select is closed out and the token re-processed
// under the table rules.
bool IsSelectInTableTerminationTag(TagId tn) {
    return tn == TagId::kCaption || tn == TagId::kTable || tn == TagId::kTbody ||
           tn == TagId::kTfoot || tn == TagId::kThead || tn == TagId::kTr || tn == TagId::kTd ||
           tn == TagId::kTh;
}

// A table-structure start tag ends the select so the surrounding table parsing
// can resume; any other start tag is a plain "in select" start tag.
void StartTagInSelectInTable(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    if (IsSelectInTableTerminationTag(tn)) {
        p.open_elements->PopUntilTagNamePopped(TagId::kSelect);
        p.ResetInsertionMode();
        p.ProcessStartTag(token);
    } else {
        StartTagInSelect(p, token);
    }
}

// A table-structure end tag closes the select (when the tag is in table
// scope) and re-processes the end tag under the table rules; otherwise the
// end tag is handled as an ordinary "in select" end tag.
void EndTagInSelectInTable(Parser& p, TagToken& token) {
    const TagId tn = token.tag_id;

    if (IsSelectInTableTerminationTag(tn)) {
        if (p.open_elements->HasInTableScope(tn)) {
            p.open_elements->PopUntilTagNamePopped(TagId::kSelect);
            p.ResetInsertionMode();
            p.OnEndTag(token);
        }
    } else {
        EndTagInSelect(p, token);
    }
}

// The "in template" insertion mode.

// Template content parsing: the first start tag selects the insertion mode the
// template body is parsed in (which is also stored on the template-mode
// stack). Head-only markup needs no mode change; table-related tags decide
// between the "in table" family modes; anything else falls back to "in body".
void StartTagInTemplate(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kBase:
        case TagId::kBasefont:
        case TagId::kBgsound:
        case TagId::kLink:
        case TagId::kMeta:
        case TagId::kNoframes:
        case TagId::kScript:
        case TagId::kStyle:
        case TagId::kTemplate:
        case TagId::kTitle: {
            StartTagInHead(p, token);
            break;
        }

        case TagId::kCaption:
        case TagId::kColgroup:
        case TagId::kTbody:
        case TagId::kTfoot:
        case TagId::kThead: {
            p.tmpl_insertion_mode_stack.front() = InsertionMode::kInTable;
            p.insertion_mode = InsertionMode::kInTable;
            StartTagInTable(p, token);
            break;
        }
        case TagId::kCol: {
            p.tmpl_insertion_mode_stack.front() = InsertionMode::kInColumnGroup;
            p.insertion_mode = InsertionMode::kInColumnGroup;
            StartTagInColumnGroup(p, token);
            break;
        }
        case TagId::kTr: {
            p.tmpl_insertion_mode_stack.front() = InsertionMode::kInTableBody;
            p.insertion_mode = InsertionMode::kInTableBody;
            StartTagInTableBody(p, token);
            break;
        }
        case TagId::kTd:
        case TagId::kTh: {
            p.tmpl_insertion_mode_stack.front() = InsertionMode::kInRow;
            p.insertion_mode = InsertionMode::kInRow;
            StartTagInRow(p, token);
            break;
        }
        default: {
            p.tmpl_insertion_mode_stack.front() = InsertionMode::kInBody;
            p.insertion_mode = InsertionMode::kInBody;
            StartTagInBody(p, token);
        }
    }
}

// Only </template> is meaningful in this mode; it closes the template and
// updates both the stack and the template-mode stack via the shared handler.
// Any other end tag is ignored.
void EndTagInTemplate(Parser& p, TagToken& token) {
    if (token.tag_id == TagId::kTemplate) {
        TemplateEndTagInHead(p, token);
    }
}

// EOF while a template is open: every template element is closed out (with its
// formatting marker and template-mode stack entry removed) and the mode reset
// before the EOF is finally reported through the normal path.
void EofInTemplate(Parser& p, EofToken& token) {
    if (p.open_elements->tmpl_count > 0) {
        p.open_elements->PopUntilTagNamePopped(TagId::kTemplate);
        p.active_formatting_elements->ClearToLastMarker();
        p.tmpl_insertion_mode_stack.pop_front();
        p.ResetInsertionMode();
        p.OnEof(token);
    } else {
        p.StopParsing(token);
    }
}

// The "after body" insertion mode.

// After </body>, an <html> start tag delegates to the "in body" rule; any
// other start tag bounces back into "in body" via the fix-up.
void StartTagAfterBody(Parser& p, TagToken& token) {
    if (token.tag_id == TagId::kHtml) {
        StartTagInBody(p, token);
    } else {
        TokenAfterBody(p, AnyToken(token));
    }
}

// </html> finishes the document (moving to "after after body" outside
// fragments) and records the end location of the never-popped html and body
// nodes when source-location tracking is enabled. Any other end tag reverts
// to "in body" via the fix-up.
void EndTagAfterBody(Parser& p, TagToken& token) {
    if (token.tag_id == TagId::kHtml) {
        if (p.fragment_context == nullptr) {
            p.insertion_mode = InsertionMode::kAfterAfterBody;
        }

        if (p.options.source_code_location_info && p.open_elements->tag_ids[0] == TagId::kHtml) {
            p.SetEndLocation(*p.open_elements->items[0], AnyToken(token));

            if (p.open_elements->stack_top >= 1) {
                Node& body_element = *p.open_elements->items[1];
                if (body_element.end_tag_location == nullptr) {
                    p.SetEndLocation(body_element, AnyToken(token));
                }
            }
        }
    } else {
        TokenAfterBody(p, AnyToken(token));
    }
}

// Automatic fix-up for "after body": processing resumes in "in body" mode.
void TokenAfterBody(Parser& p, AnyToken token) {
    p.insertion_mode = InsertionMode::kInBody;
    ModeInBody(p, token);
}

// The "in frameset" insertion mode.

// <html> delegates to the body rule; <frameset> opens a nested frameset;
// <frame> appends a void frame and acknowledges the self-closing flag; and
// <noframes> is handled by the head rules. Everything else in a frameset
// document is ignored.
void StartTagInFrameset(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kHtml: {
            StartTagInBody(p, token);
            break;
        }
        case TagId::kFrameset: {
            p.InsertElement(token, NS::kHtml);
            break;
        }
        case TagId::kFrame: {
            p.AppendElement(token, NS::kHtml);
            token.ack_self_closing = true;
            break;
        }
        case TagId::kNoframes: {
            StartTagInHead(p, token);
            break;
        }
        default:
            break;
    }
}

// </frameset> pops the frameset when the html root is not the current element
// and, once the last frameset is closed, moves to "after frameset".
void EndTagInFrameset(Parser& p, TagToken& token) {
    if (token.tag_id == TagId::kFrameset && !p.open_elements->IsRootHtmlElementCurrent()) {
        p.open_elements->Pop();

        if (p.fragment_context == nullptr &&
            p.open_elements->current_tag_id != TagId::kFrameset) {
            p.insertion_mode = InsertionMode::kAfterFrameset;
        }
    }
}

// The "after frameset" insertion mode.

// Only <html> and <noframes> start tags are meaningful here (handled by the
// body and head rules respectively); everything else is ignored until EOF.
void StartTagAfterFrameset(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kHtml: {
            StartTagInBody(p, token);
            break;
        }
        case TagId::kNoframes: {
            StartTagInHead(p, token);
            break;
        }
        default:
            break;
    }
}

// </html> after a frameset closes the document (moving to "after after
// frameset"); other end tags are ignored.
void EndTagAfterFrameset(Parser& p, TagToken& token) {
    if (token.tag_id == TagId::kHtml) {
        p.insertion_mode = InsertionMode::kAfterAfterFrameset;
    }
}

// The "after after body" insertion mode.

// An <html> start tag opens where the body rules left off; anything else is
// bounced back into "in body".
void StartTagAfterAfterBody(Parser& p, TagToken& token) {
    if (token.tag_id == TagId::kHtml) {
        StartTagInBody(p, token);
    } else {
        TokenAfterAfterBody(p, AnyToken(token));
    }
}

// Automatic fix-up for "after after body": the token is processed in "in
// body" mode once more.
void TokenAfterAfterBody(Parser& p, AnyToken token) {
    p.insertion_mode = InsertionMode::kInBody;
    ModeInBody(p, token);
}

// The "after after frameset" insertion mode.

// Only <html> and <noframes> start tags are honored (via the body and head
// rules); anything else is ignored.
void StartTagAfterAfterFrameset(Parser& p, TagToken& token) {
    switch (token.tag_id) {
        case TagId::kHtml: {
            StartTagInBody(p, token);
            break;
        }
        case TagId::kNoframes: {
            StartTagInHead(p, token);
            break;
        }
        default:
            break;
    }
}

// Open element stack element classification.

// True when the element normally needs no explicit end tag: it is closed
// implicitly by the following sibling (e.g. a <dd> closes an open <dt>). The
// "thorough" variant additionally includes the table-structure tags, which are
// only closed implicitly when the parser is actually leaving a table region.
bool IsImplicitEndTagRequired(TagId tag_id) {
    switch (tag_id) {
        case TagId::kDd:
        case TagId::kDt:
        case TagId::kLi:
        case TagId::kOptgroup:
        case TagId::kOption:
        case TagId::kP:
        case TagId::kRb:
        case TagId::kRp:
        case TagId::kRt:
        case TagId::kRtc:
            return true;
        default:
            return false;
    }
}

bool IsImplicitEndTagRequiredThoroughly(TagId tag_id) {
    switch (tag_id) {
        case TagId::kDd:
        case TagId::kDt:
        case TagId::kLi:
        case TagId::kOptgroup:
        case TagId::kOption:
        case TagId::kP:
        case TagId::kRb:
        case TagId::kRp:
        case TagId::kRt:
        case TagId::kRtc:
        case TagId::kCaption:
        case TagId::kColgroup:
        case TagId::kTbody:
        case TagId::kTd:
        case TagId::kTfoot:
        case TagId::kTh:
        case TagId::kThead:
        case TagId::kTr:
            return true;
        default:
            return false;
    }
}

// HTML scoping elements: these create a new scope within the element stack,
// so "has in scope" checks stop at them.
bool IsScopingElementHtml(TagId tag_id) {
    switch (tag_id) {
        case TagId::kApplet:
        case TagId::kCaption:
        case TagId::kHtml:
        case TagId::kMarquee:
        case TagId::kObject:
        case TagId::kTable:
        case TagId::kTd:
        case TagId::kTemplate:
        case TagId::kTh:
            return true;
        default:
            return false;
    }
}

bool IsScopingElementHtmlList(TagId tag_id) {
    return IsScopingElementHtml(tag_id) || tag_id == TagId::kOl || tag_id == TagId::kUl;
}

bool IsScopingElementHtmlButton(TagId tag_id) {
    return IsScopingElementHtml(tag_id) || tag_id == TagId::kButton;
}

bool IsScopingElementMathml(TagId tag_id) {
    switch (tag_id) {
        case TagId::kAnnotationXml:
        case TagId::kMi:
        case TagId::kMn:
        case TagId::kMo:
        case TagId::kMs:
        case TagId::kMtext:
            return true;
        default:
            return false;
    }
}

bool IsScopingElementSvg(TagId tag_id) {
    switch (tag_id) {
        case TagId::kDesc:
        case TagId::kForeignObject:
        case TagId::kTitle:
            return true;
        default:
            return false;
    }
}

bool IsTableRowContext(TagId tag_id) {
    switch (tag_id) {
        case TagId::kTr:
        case TagId::kTemplate:
        case TagId::kHtml:
            return true;
        default:
            return false;
    }
}

bool IsTableBodyContext(TagId tag_id) {
    switch (tag_id) {
        case TagId::kTbody:
        case TagId::kTfoot:
        case TagId::kThead:
        case TagId::kTemplate:
        case TagId::kHtml:
            return true;
        default:
            return false;
    }
}

bool IsTableContext(TagId tag_id) {
    switch (tag_id) {
        case TagId::kTable:
        case TagId::kTemplate:
        case TagId::kHtml:
            return true;
        default:
            return false;
    }
}

// True only for the <td> and <th> cell tags.
bool IsTableCell(TagId tag_id) {
    return tag_id == TagId::kTd || tag_id == TagId::kTh;
}

// Foreign content attribute/tag adjustment and integration points.

namespace {

// MIME types.
inline constexpr std::string_view kTextHtml = "text/html";
inline constexpr std::string_view kApplicationXml = "application/xhtml+xml";

// The dotted-case attribute name that SVG requires for the MathML definition
// URL, plus the SpellingError/definitionURL forms produced by adjustment.
inline constexpr std::string_view kDefinitionUrlAttr = "definitionurl";
inline constexpr std::string_view kAdjustedDefinitionUrlAttr = "definitionURL";

// One entry of the SVG case-adjustment tables: the lower-case attribute or
// tag name as written in foreign content, and the adjusted camel-case name a
// conforming SVG document expects.
struct AttrAdjustment {
    std::string_view lower;
    std::string_view adjusted;
};

// Maps the 58 SVG attribute names that are written in lower case in HTML
// foreign content (e.g. viewbox) to their official camelCase SVG spelling
// (viewBox); applied when an SVG start tag enters foreign content.
inline constexpr std::array<AttrAdjustment, 58> kSvgAttrsAdjustmentMap = {{
    {"attributename", "attributeName"},
    {"attributetype", "attributeType"},
    {"basefrequency", "baseFrequency"},
    {"baseprofile", "baseProfile"},
    {"calcmode", "calcMode"},
    {"clippathunits", "clipPathUnits"},
    {"diffuseconstant", "diffuseConstant"},
    {"edgemode", "edgeMode"},
    {"filterunits", "filterUnits"},
    {"glyphref", "glyphRef"},
    {"gradienttransform", "gradientTransform"},
    {"gradientunits", "gradientUnits"},
    {"kernelmatrix", "kernelMatrix"},
    {"kernelunitlength", "kernelUnitLength"},
    {"keypoints", "keyPoints"},
    {"keysplines", "keySplines"},
    {"keytimes", "keyTimes"},
    {"lengthadjust", "lengthAdjust"},
    {"limitingconeangle", "limitingConeAngle"},
    {"markerheight", "markerHeight"},
    {"markerunits", "markerUnits"},
    {"markerwidth", "markerWidth"},
    {"maskcontentunits", "maskContentUnits"},
    {"maskunits", "maskUnits"},
    {"numoctaves", "numOctaves"},
    {"pathlength", "pathLength"},
    {"patterncontentunits", "patternContentUnits"},
    {"patterntransform", "patternTransform"},
    {"patternunits", "patternUnits"},
    {"pointsatx", "pointsAtX"},
    {"pointsaty", "pointsAtY"},
    {"pointsatz", "pointsAtZ"},
    {"preservealpha", "preserveAlpha"},
    {"preserveaspectratio", "preserveAspectRatio"},
    {"primitiveunits", "primitiveUnits"},
    {"refx", "refX"},
    {"refy", "refY"},
    {"repeatcount", "repeatCount"},
    {"repeatdur", "repeatDur"},
    {"requiredextensions", "requiredExtensions"},
    {"requiredfeatures", "requiredFeatures"},
    {"specularconstant", "specularConstant"},
    {"specularexponent", "specularExponent"},
    {"spreadmethod", "spreadMethod"},
    {"startoffset", "startOffset"},
    {"stddeviation", "stdDeviation"},
    {"stitchtiles", "stitchTiles"},
    {"surfacescale", "surfaceScale"},
    {"systemlanguage", "systemLanguage"},
    {"tablevalues", "tableValues"},
    {"targetx", "targetX"},
    {"targety", "targetY"},
    {"textlength", "textLength"},
    {"viewbox", "viewBox"},
    {"viewtarget", "viewTarget"},
    {"xchannelselector", "xChannelSelector"},
    {"ychannelselector", "yChannelSelector"},
    {"zoomandpan", "zoomAndPan"},
}};

// Adjustments for XML-family attributes in foreign elements: the qualified
// spelling as written, the namespace prefix to apply, whether a prefix exists,
// the local name, and the target namespace (xlink, xml, or xmlns).
struct XmlAttrAdjustment {
    std::string_view qualified;
    std::string_view prefix;
    bool has_prefix;
    std::string_view name;
    NS ns;
};

// Splits the xml/xlink/xmlns attributes into their local name and namespace
// so foreign elements can store them with the proper namespace metadata.
inline constexpr std::array<XmlAttrAdjustment, 11> kXmlAttrsAdjustmentMap = {{
    {"xlink:actuate", "xlink", true, "actuate", NS::kXlink},
    {"xlink:arcrole", "xlink", true, "arcrole", NS::kXlink},
    {"xlink:href", "xlink", true, "href", NS::kXlink},
    {"xlink:role", "xlink", true, "role", NS::kXlink},
    {"xlink:show", "xlink", true, "show", NS::kXlink},
    {"xlink:title", "xlink", true, "title", NS::kXlink},
    {"xlink:type", "xlink", true, "type", NS::kXlink},
    {"xml:lang", "xml", true, "lang", NS::kXml},
    {"xml:space", "xml", true, "space", NS::kXml},
    {"xmlns", "", false, "xmlns", NS::kXmlns},
    {"xmlns:xlink", "xmlns", true, "xlink", NS::kXmlns},
}};

// Maps the 36 SVG tag names that need camelCase spelling when they appear in
// foreign content (altglyph -> altGlyph, feturbulence -> feTurbulence, ...).
inline constexpr std::array<AttrAdjustment, 36> kSvgTagNamesAdjustmentMap = {{
    {"altglyph", "altGlyph"},
    {"altglyphdef", "altGlyphDef"},
    {"altglyphitem", "altGlyphItem"},
    {"animatecolor", "animateColor"},
    {"animatemotion", "animateMotion"},
    {"animatetransform", "animateTransform"},
    {"clippath", "clipPath"},
    {"feblend", "feBlend"},
    {"fecolormatrix", "feColorMatrix"},
    {"fecomponenttransfer", "feComponentTransfer"},
    {"fecomposite", "feComposite"},
    {"feconvolvematrix", "feConvolveMatrix"},
    {"fediffuselighting", "feDiffuseLighting"},
    {"fedisplacementmap", "feDisplacementMap"},
    {"fedistantlight", "feDistantLight"},
    {"feflood", "feFlood"},
    {"fefunca", "feFuncA"},
    {"fefuncb", "feFuncB"},
    {"fefuncg", "feFuncG"},
    {"fefuncr", "feFuncR"},
    {"fegaussianblur", "feGaussianBlur"},
    {"feimage", "feImage"},
    {"femerge", "feMerge"},
    {"femergenode", "feMergeNode"},
    {"femorphology", "feMorphology"},
    {"feoffset", "feOffset"},
    {"fepointlight", "fePointLight"},
    {"fespecularlighting", "feSpecularLighting"},
    {"fespotlight", "feSpotLight"},
    {"fetile", "feTile"},
    {"feturbulence", "feTurbulence"},
    {"foreignobject", "foreignObject"},
    {"glyphref", "glyphRef"},
    {"lineargradient", "linearGradient"},
    {"radialgradient", "radialGradient"},
    {"textpath", "textPath"},
}};

// True for the HTML start tags that, when encountered inside foreign
// (MathML/SVG) content, break out of the foreign environment and are
// re-processed in the "in body" insertion mode instead.
bool CausesExitTagId(TagId tn) {
    using T = TagId;
    switch (tn) {
        case T::kB:
        case T::kBig:
        case T::kBlockquote:
        case T::kBody:
        case T::kBr:
        case T::kCenter:
        case T::kCode:
        case T::kDd:
        case T::kDiv:
        case T::kDl:
        case T::kDt:
        case T::kEm:
        case T::kEmbed:
        case T::kH1:
        case T::kH2:
        case T::kH3:
        case T::kH4:
        case T::kH5:
        case T::kH6:
        case T::kHead:
        case T::kHr:
        case T::kI:
        case T::kImg:
        case T::kLi:
        case T::kListing:
        case T::kMenu:
        case T::kMeta:
        case T::kNobr:
        case T::kOl:
        case T::kP:
        case T::kPre:
        case T::kRuby:
        case T::kS:
        case T::kSmall:
        case T::kSpan:
        case T::kStrong:
        case T::kStrike:
        case T::kSub:
        case T::kSup:
        case T::kTable:
        case T::kTt:
        case T::kU:
        case T::kUl:
        case T::kVar:
            return true;
        default:
            return false;
    }
}

// Returns the value of the first attribute whose name matches exactly, or
// nullptr when the token carries no such attribute. Used during foreign
// token adjustment lookups.
const std::string* FindAttr(const TagToken& token, std::string_view name) {
    for (const Attribute& attr : token.attrs) {
        if (attr.name == name) {
            return &attr.value;
        }
    }
    return nullptr;
}

}

// True when a start tag forces the parser out of foreign content. <font> is
// special: it only breaks out when it carries any of the color/size/face
// attributes; all other tags on the exit list break out unconditionally.
bool CausesExit(const TagToken& start_tag_token) {
    TagId tn = start_tag_token.tag_id;
    if (tn == TagId::kFont) {
        return FindAttr(start_tag_token, attrs::kColor) != nullptr || FindAttr(start_tag_token, attrs::kSize) != nullptr ||
            FindAttr(start_tag_token, attrs::kFace) != nullptr;
    }
    return CausesExitTagId(tn);
}

// Rewrites the legacy lower-case MathML "definitionurl" attribute to the
// official "definitionURL" spelling.
void AdjustTokenMathMLAttrs(TagToken& token) {
    for (Attribute& attr : token.attrs) {
        if (attr.name == kDefinitionUrlAttr) {
            attr.name = std::string(kAdjustedDefinitionUrlAttr);
            break;
        }
    }
}

// Rewrites SVG attributes that use lower-case HTML spelling to their official
// camelCase SVG names using the adjustment table.
void AdjustTokenSVGAttrs(TagToken& token) {
    for (Attribute& attr : token.attrs) {
        for (const AttrAdjustment& adjustment : kSvgAttrsAdjustmentMap) {
            if (attr.name == adjustment.lower) {
                attr.name = std::string(adjustment.adjusted);
                break;
            }
        }
    }
}

// Repairs xml/xlink/xmlns attributes in foreign content: their prefix, local
// name, and namespace are populated from the adjustment table.
void AdjustTokenXMLAttrs(TagToken& token) {
    for (Attribute& attr : token.attrs) {
        for (const XmlAttrAdjustment& adjustment : kXmlAttrsAdjustmentMap) {
            if (attr.name == adjustment.qualified) {
                attr.prefix = std::string(adjustment.prefix);
                attr.has_prefix = adjustment.has_prefix;
                attr.name = std::string(adjustment.name);
                attr.ns = adjustment.ns;
                break;
            }
        }
    }
}

// Rewrites SVG element names to camelCase as needed (altglyph -> altGlyph)
// and re-resolves the tag id afterwards.
void AdjustTokenSVGTagName(TagToken& token) {
    for (const AttrAdjustment& adjustment : kSvgTagNamesAdjustmentMap) {
        if (token.tag_name == adjustment.lower) {
            token.tag_name = std::string(adjustment.adjusted);
            token.tag_id = GetTagId(token.tag_name);
            break;
        }
    }
}

namespace {

// MathML text integration points: at these elements foreign content resumes
// and character data is processed as HTML text.
bool IsMathMLTextIntegrationPoint(TagId tn, NS ns) {
    return ns == NS::kMathml &&
        (tn == TagId::kMi || tn == TagId::kMo || tn == TagId::kMn || tn == TagId::kMs || tn == TagId::kMtext);
}

// HTML integration points accept HTML children: a MathML annotation-xml
// whose encoding attribute names an HTML or XML MIME type, or the SVG
// foreignObject/desc/title elements.
bool IsHtmlIntegrationPoint(TagId tn, NS ns, const std::vector<Attribute>& attrs) {
    if (ns == NS::kMathml && tn == TagId::kAnnotationXml) {
        for (const Attribute& attr : attrs) {
            if (attr.name == attrs::kEncoding) {
                std::string value;
                value.reserve(attr.value.size());
                for (char c : attr.value) {
                    value += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
                }
                return value == kTextHtml || value == kApplicationXml;
            }
        }
        return false;
    }

    return ns == NS::kSvg && (tn == TagId::kForeignObject || tn == TagId::kDesc || tn == TagId::kTitle);
}

}

// Whether an element in the foreign namespace "ns" is an integration point
// with respect to the namespace the parser is currently in (foreign_ns):
// MathML text integration points, or HTML integration points reached from an
// HTML context.
bool IsIntegrationPoint(TagId tn, NS ns, const std::vector<Attribute>& attrs, NS foreign_ns) {
    return ((foreign_ns == NS::kHtml) && IsHtmlIntegrationPoint(tn, ns, attrs)) ||
        ((foreign_ns == NS::kMathml) && IsMathMLTextIntegrationPoint(tn, ns));
}

// Foreign content token handling.

// The UTF-8 encoding of the U+FFFD replacement character, which substitutes
// for the null bytes that are illegal inside foreign content.
inline constexpr std::string_view kReplacementCharacterUtf8 = "\xEF\xBF\xBD";

// Null characters in foreign content are replaced by the Unicode replacement
// character before being inserted.
void NullCharacterInForeignContent(Parser& p, CharacterToken& token) {
    token.chars = std::string(kReplacementCharacterUtf8);
    p.InsertCharacters(token);
}

// Ordinary character data in foreign content is inserted directly and makes
// the document non-frameset.
void CharacterInForeignContent(Parser& p, CharacterToken& token) {
    p.InsertCharacters(token);
    p.frameset_ok = false;
}

namespace {

// Pops foreign elements until the current element is an HTML element or an
// integration point, i.e. until processing can resume in HTML rules.
void PopUntilHtmlOrIntegrationPoint(Parser& p) {
    while (p.open_elements->current != nullptr &&
           p.open_elements->current->namespace_uri != NS::kHtml &&
           p.open_elements->current_tag_id != TagId::kUnknown &&
           !p.IsIntegrationPoint(p.open_elements->current_tag_id,
                                 *p.open_elements->current)) {
        p.open_elements->Pop();
    }
}

}

// A foreign-content start tag: if the tag forces an exit, the foreign stack is
// unwound to HTML and the token is processed with the normal HTML start-tag
// rules. Otherwise the token is adjusted for the current namespace (MathML or
// SVG), its XML attributes are repaired, and the element opens in the current
// foreign namespace, acknowledging any self-closing flag.
void StartTagInForeignContent(Parser& p, TagToken& token) {
    if (CausesExit(token)) {
        PopUntilHtmlOrIntegrationPoint(p);

        p.StartTagOutsideForeignContent(token);
    } else {
        Node* const current = p.GetAdjustedCurrentElement();
        const NS current_ns = current->namespace_uri;

        if (current_ns == NS::kMathml) {
            AdjustTokenMathMLAttrs(token);
        } else if (current_ns == NS::kSvg) {
            AdjustTokenSVGTagName(token);
            AdjustTokenSVGAttrs(token);
        }

        AdjustTokenXMLAttrs(token);

        if (token.self_closing) {
            p.AppendElement(token, current_ns);
        } else {
            p.InsertElement(token, current_ns);
        }

        token.ack_self_closing = true;
    }
}

// A foreign-content end tag: </p> and </br> first unwind the foreign stack to
// HTML, then delegate to the normal HTML end-tag rules. Any other end tag is
// matched against the foreign element stack; the stack is shortened past the
// first equal-name element (whose spelling is also copied back onto the token
// so the end location matches the element).
void EndTagInForeignContent(Parser& p, TagToken& token) {
    if (token.tag_id == TagId::kP || token.tag_id == TagId::kBr) {
        PopUntilHtmlOrIntegrationPoint(p);

        p.EndTagOutsideForeignContent(token);

        return;
    }

    for (int i = p.open_elements->stack_top; i > 0; i--) {
        Node* const element = p.open_elements->items[static_cast<size_t>(i)];

        if (element->namespace_uri == NS::kHtml) {
            p.EndTagOutsideForeignContent(token);
            break;
        }

        const std::string_view tag_name = element->tag_name;

        if (helpers::ToLowerASCII(tag_name) == token.tag_name) {
            token.tag_name = std::string(tag_name);
            p.open_elements->ShortenToLength(i);
            break;
        }
    }
}

// Adoption agency algorithm.

// Steps 3-8 of the adoption agency: look up the formatting element entry for
// the token's tag. The entry is discarded when its element is no longer on
// the open-element stack or is not in scope; when there is no entry at all
// the token is treated as a stray generic end tag.
Entry* aaObtainFormattingElementEntry(Parser& p, TagToken& token) {
    Entry* formatting_element_entry =
        p.active_formatting_elements->GetElementEntryInScopeWithTagName(token.tag_name);

    if (formatting_element_entry != nullptr) {
        if (!p.open_elements->Contains(*formatting_element_entry->element)) {
            p.active_formatting_elements->RemoveEntry(*formatting_element_entry);
            formatting_element_entry = nullptr;
        } else if (!p.open_elements->HasInScope(token.tag_id)) {
            formatting_element_entry = nullptr;
        }
    } else {
        GenericEndTagInBody(p, token);
    }

    return formatting_element_entry;
}

// Steps 9 and 10 of the adoption agency: search the open-element stack for
// the furthest special element below the formatting element (the deepest
// block that contains the formatting element). When there is none, the
// formatting element is unwound from both stacks entirely and the algorithm
// stops.
Node* aaObtainFurthestBlock(Parser& p, Entry& formatting_element_entry) {
    Node* furthest_block = nullptr;
    int idx = p.open_elements->stack_top;

    for (; idx >= 0; idx--) {
        Node* const element = p.open_elements->items[static_cast<size_t>(idx)];

        if (element == formatting_element_entry.element) {
            break;
        }

        if (p.IsSpecialElement(*element, p.open_elements->tag_ids[static_cast<size_t>(idx)])) {
            furthest_block = element;
        }
    }

    if (furthest_block == nullptr) {
        p.open_elements->ShortenToLength(std::max(idx, 0));
        p.active_formatting_elements->RemoveEntry(formatting_element_entry);
    }

    return furthest_block;
}

// Step 13.7 of the adoption agency: rebuild the formatting element from its
// saved token. The freshly created node replaces the old element on the stack
// (the detached node is held in orphan_nodes_ until it is reattached by the
// caller), and the active-formatting entry is made to point at the new node.
Node& aaRecreateElementFromEntry(Parser& p, Entry& element_entry) {
    const NS ns = element_entry.element->namespace_uri;
    auto new_element =
        CreateElement(std::string(element_entry.token->tag_name), ns, element_entry.token->attrs);
    Node& new_element_ref = *new_element;

    p.orphan_nodes_.push_back(std::move(new_element));

    p.open_elements->Replace(*element_entry.element, new_element_ref);
    element_entry.element = &new_element_ref;

    return new_element_ref;
}

// Step 13 of the adoption agency (inner loop): walk from the common ancestor
// of the formatting element and the furthest block down to the formatting
// element, recreating every formatting element found (or, on overflow or a
// missing entry, removing it from the stack). The last recreated node is
// returned and becomes the new parent for the moved content.
Node& aaInnerLoop(Parser& p, Node& furthest_block, Node& formatting_element) {
    Node* last_element = &furthest_block;
    Node* next_element = p.open_elements->GetCommonAncestor(furthest_block);

    int i = 0;
    for (Node* element = next_element; element != &formatting_element;
         i++, element = next_element) {
        next_element = p.open_elements->GetCommonAncestor(*element);

        Entry* const element_entry = p.active_formatting_elements->GetElementEntry(element);
        const bool counter_overflow = element_entry != nullptr && i >= kAaInnerLoopIter;
        const bool should_remove_from_open_elements =
            element_entry == nullptr || counter_overflow;

        if (should_remove_from_open_elements) {
            if (counter_overflow) {
                p.active_formatting_elements->RemoveEntry(*element_entry);
            }

            p.open_elements->Remove(*element);
        } else {
            Node& new_element = aaRecreateElementFromEntry(p, *element_entry);

            if (last_element == &furthest_block) {
                p.active_formatting_elements->bookmark = element_entry;
            }

            auto detached_last = DetachNode(*last_element);
            AppendChild(new_element, std::move(detached_last));
            last_element = &new_element;
        }
    }

    return *last_element;
}

// Step 14 of the adoption agency: attach the reconstructed content back into
// the tree at the common ancestor. When that ancestor fosters parenting the
// node is foster-parented; otherwise it is appended to the ancestor (or to a
// template's content fragment).
void aaInsertLastNodeInCommonAncestor(Parser& p, Node& common_ancestor,
                                      std::unique_ptr<Node> last_element) {
    const TagId tid = GetTagId(common_ancestor.tag_name);

    if (p.IsElementCausesFosterParenting(tid)) {
        p.FosterParentElement(std::move(last_element));
        return;
    }

    Node* target = &common_ancestor;

    if (tid == TagId::kTemplate && common_ancestor.namespace_uri == NS::kHtml) {
        target = DefaultTreeAdapter::GetTemplateContent(common_ancestor);
    }

    AppendChild(*target, std::move(last_element));
}

// Steps 15-19 of the adoption agency: rebuild the formatting element from its
// saved token, move the furthest block's children into it (adopting their
// location/formatting info), replace the old formatting element on both the
// formatting list (inserting the clone right after the bookmark) and the
// open-element stack (right after the furthest block).
void aaReplaceFormattingElement(Parser& p, Node& furthest_block,
                                Entry& formatting_element_entry) {
    const NS ns = formatting_element_entry.element->namespace_uri;
    const TagToken& token = *formatting_element_entry.token;
    auto new_element_up = CreateElement(std::string(token.tag_name), ns, token.attrs);
    Node& new_element = *new_element_up;

    p.orphan_nodes_.push_back(std::move(new_element_up));

    p.AdoptNodes(furthest_block, new_element);
    AppendChild(furthest_block, std::move(p.orphan_nodes_.back()));
    p.orphan_nodes_.pop_back();

    p.active_formatting_elements->InsertElementAfterBookmark(new_element, token);
    p.active_formatting_elements->RemoveEntry(formatting_element_entry);

    p.open_elements->Remove(*formatting_element_entry.element);
    p.open_elements->InsertAfter(furthest_block, new_element, token.tag_id);
}

}

// The adoption-agency driver. It runs the whole algorithm as an outer loop of
// at most kAaOuterLoopIter passes, one per formatting element still active:
// each pass locates the formatting entry and the furthest block, replays the
// inner loop over the common ancestor, and finally replaces the formatting
// element in place. The loop terminates early whenever no formatting entry or
// furthest block is found.
namespace guchho::html {

void CallAdoptionAgency(Parser& p, TagToken& token) {
    for (int i = 0; i < kAaOuterLoopIter; i++) {
        Entry* const formatting_element_entry = aaObtainFormattingElementEntry(p, token);

        if (formatting_element_entry == nullptr) {
            break;
        }

        Node* const furthest_block = aaObtainFurthestBlock(p, *formatting_element_entry);

        if (furthest_block == nullptr) {
            break;
        }

        p.active_formatting_elements->bookmark = formatting_element_entry;

        Node& last_element = aaInnerLoop(p, *furthest_block, *formatting_element_entry->element);
        Node* const common_ancestor =
            p.open_elements->GetCommonAncestor(*formatting_element_entry->element);

        auto detached_last = DetachNode(last_element);
        if (common_ancestor != nullptr) {
            aaInsertLastNodeInCommonAncestor(p, *common_ancestor, std::move(detached_last));
        }
        aaReplaceFormattingElement(p, *furthest_block, *formatting_element_entry);
    }
}

}
