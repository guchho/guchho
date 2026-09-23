#include "test/guchho_test.hpp"

#include "guchho/html/html_ast.hpp"
#include "guchho/html/html_parser.hpp"
#include "guchho/html/html_printer.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace html = guchho::html;

namespace {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

struct ParseOutcome {
    std::unique_ptr<html::Node> document;
    std::vector<html::ParserError> errors;
};

// Parses "input" as a whole document. By default an error handler is installed
// so every parse error is collected; pass "collect_errors = false" to parse
// without one (the only way to observe the no-location default).
ParseOutcome Parse(std::string_view input,
                   html::ParserOptions options = {},
                   bool collect_errors = true) {
    ParseOutcome outcome;
    if (collect_errors && !options.on_parse_error) {
        options.on_parse_error = [&outcome](const html::ParserError& error) {
            outcome.errors.push_back(error);
        };
    }
    outcome.document = html::Parser::ParseDocument(input, options);
    return outcome;
}

std::unique_ptr<html::Node> ParseFragment(html::Node* context,
                                          std::string_view input,
                                          html::ParserOptions options = {}) {
    auto parser = html::Parser::GetFragmentParser(context, options);
    parser->Write(input);
    return parser->GetFragment();
}

const html::Node* Child(const html::Node& parent, size_t index) {
    if (index >= parent.child_nodes.size()) {
        return nullptr;
    }
    return parent.child_nodes[index].get();
}

size_t ChildCount(const html::Node& parent) { return parent.child_nodes.size(); }

// Concatenated text of every descendant text node.
std::string TextContent(const html::Node& node) {
    if (html::IsTextNode(node)) {
        return node.value;
    }
    std::string out;
    for (const auto& child : node.child_nodes) {
        out += TextContent(*child);
    }
    return out;
}

// Depth-first first match on tag name; the root itself may match. Descends
// into a <template>'s content fragment as well as its visible children.
const html::Node* FindTag(const html::Node& root, std::string_view tag) {
    if (html::IsElementNode(root) && root.tag_name == tag) {
        return &root;
    }
    for (const auto& child : root.child_nodes) {
        if (const html::Node* found = FindTag(*child, tag); found != nullptr) {
            return found;
        }
    }
    if (html::IsTemplateElement(root) && root.template_content != nullptr) {
        return FindTag(*root.template_content, tag);
    }
    return nullptr;
}

// All depth-first matches on tag name, in document order.
void CollectTags(const html::Node& root, std::string_view tag,
                 std::vector<const html::Node*>& out) {
    if (html::IsElementNode(root) && root.tag_name == tag) {
        out.push_back(&root);
    }
    for (const auto& child : root.child_nodes) {
        CollectTags(*child, tag, out);
    }
    if (html::IsTemplateElement(root) && root.template_content != nullptr) {
        CollectTags(*root.template_content, tag, out);
    }
}

std::vector<const html::Node*> FindAllTags(const html::Node& root,
                                           std::string_view tag) {
    std::vector<const html::Node*> out;
    CollectTags(root, tag, out);
    return out;
}

size_t CountError(const std::vector<html::ParserError>& errors, html::Err code) {
    size_t n = 0;
    for (const html::ParserError& error : errors) {
        if (error.code == code) {
            ++n;
        }
    }
    return n;
}

const std::string* Attr(const html::Node& element, std::string_view name) {
    return html::FindAttrValue(element, name);
}

// ---------------------------------------------------------------------------
// Whole-document structure
// ---------------------------------------------------------------------------

TEST(HtmlParserDocument, ConformingDocumentSkeleton)
{
    auto c = Parse("<!DOCTYPE html><html lang=\"en\"><head><title>T</title></head>"
                   "<body><p>x</p></body></html>");

    ASSERT_TRUE(c.document != nullptr);
    EXPECT_EQ(c.document->type, html::NodeType::kDocument);
    EXPECT_EQ(c.document->mode, html::DocumentMode::kNoQuirks);
    EXPECT_TRUE(c.errors.empty());

    ASSERT_EQ(ChildCount(*c.document), size_t{2});
    EXPECT_EQ(Child(*c.document, 0)->type, html::NodeType::kDocumentType);
    EXPECT_EQ(Child(*c.document, 0)->doctype_name, "html");

    const html::Node* html_el = Child(*c.document, 1);
    ASSERT_TRUE(html_el != nullptr);
    EXPECT_EQ(html_el->tag_name, "html");
    EXPECT_EQ(html_el->namespace_uri, html::NS::kHtml);
    ASSERT_TRUE(Attr(*html_el, "lang") != nullptr);
    EXPECT_EQ(*Attr(*html_el, "lang"), "en");

    ASSERT_EQ(ChildCount(*html_el), size_t{2});
    const html::Node* head = Child(*html_el, 0);
    const html::Node* body = Child(*html_el, 1);
    EXPECT_EQ(head->tag_name, "head");
    EXPECT_EQ(body->tag_name, "body");

    const html::Node* title = FindTag(*head, "title");
    ASSERT_TRUE(title != nullptr);
    EXPECT_EQ(TextContent(*title), "T");

    const html::Node* p = FindTag(*body, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(TextContent(*p), "x");
}

TEST(HtmlParserDocument, ImpliesHtmlHeadAndBody)
{
    auto c = Parse("hello");

    ASSERT_TRUE(c.document != nullptr);
    EXPECT_EQ(c.document->mode, html::DocumentMode::kQuirks);

    const html::Node* html_el = FindTag(*c.document, "html");
    ASSERT_TRUE(html_el != nullptr);
    const html::Node* head = FindTag(*html_el, "head");
    const html::Node* body = FindTag(*html_el, "body");
    ASSERT_TRUE(head != nullptr);
    ASSERT_TRUE(body != nullptr);
    EXPECT_EQ(ChildCount(*head), size_t{0});
    EXPECT_EQ(TextContent(*body), "hello");

    EXPECT_EQ(CountError(c.errors, html::Err::kMissingDoctype), size_t{1});
}

TEST(HtmlParserDocument, ExplicitDoctypeSuppressesQuirks)
{
    auto c = Parse("<!DOCTYPE html><p>x</p>");
    EXPECT_EQ(c.document->mode, html::DocumentMode::kNoQuirks);
    EXPECT_EQ(CountError(c.errors, html::Err::kMissingDoctype), size_t{0});
}

TEST(HtmlParserDocument, LegacyPublicDoctypeForcesQuirks)
{
    auto c = Parse("<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">"
                   "<p>x</p>");
    EXPECT_EQ(c.document->mode, html::DocumentMode::kQuirks);
    EXPECT_EQ(CountError(c.errors, html::Err::kNonConformingDoctype), size_t{1});
}

TEST(HtmlParserDocument, CommentBeforeHtmlLandsOnDocument)
{
    auto c = Parse("<!-- lead --><p>x</p>");
    const html::Node* comment = Child(*c.document, 0);
    ASSERT_TRUE(comment != nullptr);
    EXPECT_EQ(comment->type, html::NodeType::kComment);
    EXPECT_EQ(comment->data, " lead ");
}

TEST(HtmlParserDocument, CommentAfterHtmlLandsOnDocument)
{
    auto c = Parse("<html>trail</html><!-- tail -->");
    const html::Node* html_el = FindTag(*c.document, "html");
    ASSERT_TRUE(html_el != nullptr);
    EXPECT_EQ(c.document->child_nodes.back()->type, html::NodeType::kComment);
    EXPECT_EQ(c.document->child_nodes.back()->data, " tail ");
}

TEST(HtmlParserDocument, HeadTextFallsThroughToBody)
{
    auto c = Parse("<head>stuff");
    const html::Node* head = FindTag(*c.document, "head");
    ASSERT_TRUE(head != nullptr);
    EXPECT_EQ(ChildCount(*head), size_t{0});
    const html::Node* body = FindTag(*c.document, "body");
    ASSERT_TRUE(body != nullptr);
    EXPECT_EQ(TextContent(*body), "stuff");
}

// ---------------------------------------------------------------------------
// Head content
// ---------------------------------------------------------------------------

TEST(HtmlParserHead, MetaAppendedToHead)
{
    auto c = Parse("<!DOCTYPE html><meta charset=\"utf-8\">");
    const html::Node* head = FindTag(*c.document, "head");
    ASSERT_TRUE(head != nullptr);
    const html::Node* meta = FindTag(*head, "meta");
    ASSERT_TRUE(meta != nullptr);
    ASSERT_TRUE(Attr(*meta, "charset") != nullptr);
    EXPECT_EQ(*Attr(*meta, "charset"), "utf-8");
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlParserHead, LinkAndStyleSiblings)
{
    auto c = Parse("<link rel=\"stylesheet\"><style>a{}</style>");
    const html::Node* head = FindTag(*c.document, "head");
    ASSERT_TRUE(head != nullptr);
    const html::Node* link = Child(*head, 0);
    const html::Node* style = Child(*head, 1);
    ASSERT_TRUE(link != nullptr);
    ASSERT_TRUE(style != nullptr);
    EXPECT_EQ(link->tag_name, "link");
    EXPECT_EQ(style->tag_name, "style");
    EXPECT_EQ(TextContent(*style), "a{}");
}

TEST(HtmlParserHead, TitleIsRcdataCharactersDecoded)
{
    auto c = Parse("<title>a &amp; b</title>");
    const html::Node* title = FindTag(*c.document, "title");
    ASSERT_TRUE(title != nullptr);
    EXPECT_EQ(TextContent(*title), "a & b");
}

TEST(HtmlParserHead, ScriptContentIsRawtext)
{
    auto c = Parse("<script>if (a < b && c > d) { alert(\"&amp;\"); }</script>");
    const html::Node* script = FindTag(*c.document, "script");
    ASSERT_TRUE(script != nullptr);
    EXPECT_EQ(TextContent(*script), "if (a < b && c > d) { alert(\"&amp;\"); }");
}

TEST(HtmlParserHead, StyleContentIsRawtext)
{
    auto c = Parse("<style>a > b { content: \"<\"; }</style>");
    const html::Node* style = FindTag(*c.document, "style");
    ASSERT_TRUE(style != nullptr);
    EXPECT_EQ(TextContent(*style), "a > b { content: \"<\"; }");
}

TEST(HtmlParserHead, BaseAndMetaAreVoid)
{
    auto c = Parse("<base href=\"/\"><meta name=\"a\" content=\"b\">");
    const html::Node* head = FindTag(*c.document, "head");
    ASSERT_TRUE(head != nullptr);
    ASSERT_EQ(ChildCount(*head), size_t{2});
    EXPECT_EQ(Child(*head, 0)->tag_name, "base");
    EXPECT_EQ(Child(*head, 1)->tag_name, "meta");
    EXPECT_EQ(ChildCount(*Child(*head, 0)), size_t{0});
}

TEST(HtmlParserHead, TemplateStaysInHead)
{
    auto c = Parse("<!DOCTYPE html><template><div>x</div></template>");
    const html::Node* head = FindTag(*c.document, "head");
    ASSERT_TRUE(head != nullptr);
    const html::Node* tmpl = FindTag(*head, "template");
    ASSERT_TRUE(tmpl != nullptr);
    EXPECT_TRUE(c.errors.empty());
}

// ---------------------------------------------------------------------------
// Body: text and basic blocks
// ---------------------------------------------------------------------------

TEST(HtmlParserBody, TextBecomesSingleTextNode)
{
    auto c = Parse("<p>Hello world</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    ASSERT_EQ(ChildCount(*p), size_t{1});
    EXPECT_EQ(Child(*p, 0)->type, html::NodeType::kText);
    EXPECT_EQ(Child(*p, 0)->value, "Hello world");
}

TEST(HtmlParserBody, CharacterReferencesDecodedInText)
{
    auto c = Parse("<!DOCTYPE html><p>a &amp; b &#x1F600; c</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(TextContent(*p), "a & b " + std::string("\xF0\x9F\x98\x80") + " c");
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlParserBody, UnclosedElementFlushesAtEof)
{
    auto c = Parse("<div>x");
    EXPECT_EQ(CountError(c.errors, html::Err::kMissingDoctype), size_t{1});
    const html::Node* div = FindTag(*c.document, "div");
    ASSERT_TRUE(div != nullptr);
    EXPECT_EQ(TextContent(*div), "x");
}

TEST(HtmlParserBody, VoidBrHasNoChildren)
{
    auto c = Parse("<!DOCTYPE html><p>a<br>b</p>");
    const html::Node* br = FindTag(*c.document, "br");
    ASSERT_TRUE(br != nullptr);
    EXPECT_EQ(br->namespace_uri, html::NS::kHtml);
    EXPECT_EQ(ChildCount(*br), size_t{0});
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlParserBody, ImgAttributesPreserved)
{
    auto c = Parse("<img src=\"a.png\" alt=\"pic\" width=\"10\">");
    const html::Node* img = FindTag(*c.document, "img");
    ASSERT_TRUE(img != nullptr);
    ASSERT_TRUE(Attr(*img, "src") != nullptr);
    ASSERT_TRUE(Attr(*img, "alt") != nullptr);
    ASSERT_TRUE(Attr(*img, "width") != nullptr);
    EXPECT_EQ(*Attr(*img, "src"), "a.png");
    EXPECT_EQ(*Attr(*img, "alt"), "pic");
    EXPECT_EQ(*Attr(*img, "width"), "10");
    EXPECT_EQ(ChildCount(*img), size_t{0});
}

TEST(HtmlParserBody, SelfClosingNonVoidReportsError)
{
    auto c = Parse("<div/>x");
    EXPECT_EQ(CountError(c.errors, html::Err::kNonVoidHtmlElementStartTagWithTrailingSolidus),
              size_t{1});
    const html::Node* div = FindTag(*c.document, "div");
    ASSERT_TRUE(div != nullptr);
    EXPECT_EQ(TextContent(*div), "x");
}

TEST(HtmlParserBody, SelfClosingVoidIsSilent)
{
    auto c = Parse("<br/>");
    EXPECT_EQ(CountError(c.errors, html::Err::kNonVoidHtmlElementStartTagWithTrailingSolidus),
              size_t{0});
    const html::Node* br = FindTag(*c.document, "br");
    ASSERT_TRUE(br != nullptr);
}

TEST(HtmlParserBody, ImageRewrittenToImg)
{
    auto c = Parse("<image src=\"a.png\">");
    EXPECT_TRUE(FindTag(*c.document, "image") == nullptr);
    const html::Node* img = FindTag(*c.document, "img");
    ASSERT_TRUE(img != nullptr);
}

TEST(HtmlParserBody, EmptyAttributeValuePreserved)
{
    auto c = Parse("<input disabled>");
    const html::Node* input = FindTag(*c.document, "input");
    ASSERT_TRUE(input != nullptr);
    const std::string* value = Attr(*input, "disabled");
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "");
}

// ---------------------------------------------------------------------------
// Auto-closing and nesting
// ---------------------------------------------------------------------------

TEST(HtmlParserBody, ConsecutiveParagraphsAutoClose)
{
    auto c = Parse("<p>one<p>two");
    const std::vector<const html::Node*> paragraphs = FindAllTags(*c.document, "p");
    ASSERT_EQ(paragraphs.size(), size_t{2});
    EXPECT_EQ(TextContent(*paragraphs[0]), "one");
    EXPECT_EQ(TextContent(*paragraphs[1]), "two");
    EXPECT_EQ(paragraphs[0]->child_nodes.size(), size_t{1});
    EXPECT_EQ(paragraphs[1]->child_nodes.size(), size_t{1});
}

TEST(HtmlParserBody, StrayParagraphEndClosesFinally)
{
    auto c = Parse("<p>one<p>two</p></p>");
    const std::vector<const html::Node*> paragraphs = FindAllTags(*c.document, "p");
    ASSERT_EQ(paragraphs.size(), size_t{3});
    EXPECT_EQ(TextContent(*paragraphs[0]), "one");
    EXPECT_EQ(TextContent(*paragraphs[1]), "two");
    EXPECT_EQ(TextContent(*paragraphs[2]), "");
}

TEST(HtmlParserBody, DivClosesOpenParagraph)
{
    auto c = Parse("<p>x<div>y");
    const html::Node* body = FindTag(*c.document, "body");
    ASSERT_TRUE(body != nullptr);
    ASSERT_EQ(ChildCount(*body), size_t{2});
    EXPECT_EQ(Child(*body, 0)->tag_name, "p");
    EXPECT_EQ(Child(*body, 1)->tag_name, "div");
    EXPECT_EQ(TextContent(*Child(*body, 0)), "x");
    EXPECT_EQ(TextContent(*Child(*body, 1)), "y");
}

TEST(HtmlParserBody, HeadingsCloseEachOther)
{
    auto c = Parse("<h1>a<h2>b");
    const std::vector<const html::Node*> h1 = FindAllTags(*c.document, "h1");
    const std::vector<const html::Node*> h2 = FindAllTags(*c.document, "h2");
    ASSERT_EQ(h1.size(), size_t{1});
    ASSERT_EQ(h2.size(), size_t{1});
    EXPECT_EQ(TextContent(*h1[0]), "a");
    EXPECT_EQ(TextContent(*h2[0]), "b");
    EXPECT_EQ(h1[0]->child_nodes.size(), size_t{1});
    EXPECT_EQ(h2[0]->child_nodes.size(), size_t{1});
}

TEST(HtmlParserBody, PreSkipsLeadingNewline)
{
    auto c = Parse("<pre>\nline1\nline2</pre>");
    const html::Node* pre = FindTag(*c.document, "pre");
    ASSERT_TRUE(pre != nullptr);
    EXPECT_EQ(TextContent(*pre), "line1\nline2");
}

TEST(HtmlParserBody, PreKeepsNewlinesWithoutLeadingOne)
{
    auto c = Parse("<pre>line1\nline2</pre>");
    const html::Node* pre = FindTag(*c.document, "pre");
    ASSERT_TRUE(pre != nullptr);
    EXPECT_EQ(TextContent(*pre), "line1\nline2");
}

TEST(HtmlParserBody, TextareaIsRcdata)
{
    auto c = Parse("<textarea>a &amp; b</textarea>");
    const html::Node* textarea = FindTag(*c.document, "textarea");
    ASSERT_TRUE(textarea != nullptr);
    EXPECT_EQ(TextContent(*textarea), "a & b");
}

TEST(HtmlParserBody, PlaintextConsumesEverything)
{
    auto c = Parse("<plaintext>a<br>b &amp; c");
    const html::Node* plaintext = FindTag(*c.document, "plaintext");
    ASSERT_TRUE(plaintext != nullptr);
    EXPECT_EQ(TextContent(*plaintext), "a<br>b &amp; c");
}

TEST(HtmlParserBody, FormattingNesting)
{
    auto c = Parse("<p><b>bold<i> both</i></b></p>");
    const html::Node* b = FindTag(*c.document, "b");
    ASSERT_TRUE(b != nullptr);
    const html::Node* i = FindTag(*b, "i");
    ASSERT_TRUE(i != nullptr);
    EXPECT_EQ(TextContent(*b), "bold both");
    EXPECT_EQ(Child(*b, 0)->type, html::NodeType::kText);
    EXPECT_EQ(Child(*b, 0)->value, "bold");
    EXPECT_EQ(Child(*b, 1)->tag_name, "i");
}

TEST(HtmlParserBody, DivNestedInSpan)
{
    auto c = Parse("<span><div>x</div></span>");
    const html::Node* span = FindTag(*c.document, "span");
    ASSERT_TRUE(span != nullptr);
    const html::Node* div = FindTag(*span, "div");
    ASSERT_TRUE(div != nullptr);
    EXPECT_EQ(TextContent(*div), "x");
}

TEST(HtmlParserBody, CommentsInsideBody)
{
    auto c = Parse("<p>a<!-- hidden -->b</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    ASSERT_EQ(ChildCount(*p), size_t{3});
    EXPECT_EQ(Child(*p, 0)->value, "a");
    EXPECT_EQ(Child(*p, 1)->type, html::NodeType::kComment);
    EXPECT_EQ(Child(*p, 1)->data, " hidden ");
    EXPECT_EQ(Child(*p, 2)->value, "b");
}

// ---------------------------------------------------------------------------
// Lists, definition lists, selects, forms
// ---------------------------------------------------------------------------

TEST(HtmlParserBody, ListItemsAutoClose)
{
    auto c = Parse("<ul><li>a<li>b</ul>");
    const html::Node* ul = FindTag(*c.document, "ul");
    ASSERT_TRUE(ul != nullptr);
    ASSERT_EQ(ChildCount(*ul), size_t{2});
    EXPECT_EQ(Child(*ul, 0)->tag_name, "li");
    EXPECT_EQ(Child(*ul, 1)->tag_name, "li");
    EXPECT_EQ(TextContent(*Child(*ul, 0)), "a");
    EXPECT_EQ(TextContent(*Child(*ul, 1)), "b");
}

TEST(HtmlParserBody, NestedListsStayNested)
{
    auto c = Parse("<ul><li>a<ul><li>b</ul></li></ul>");
    const std::vector<const html::Node*> uls = FindAllTags(*c.document, "ul");
    ASSERT_EQ(uls.size(), size_t{2});
    const html::Node* outer = uls[0];
    const html::Node* inner = uls[1];
    ASSERT_EQ(ChildCount(*outer), size_t{1});
    const html::Node* outer_li = Child(*outer, 0);
    ASSERT_TRUE(outer_li != nullptr);
    EXPECT_EQ(outer_li->tag_name, "li");
    EXPECT_EQ(Child(*outer_li, 0)->type, html::NodeType::kText);
    EXPECT_EQ(Child(*outer_li, 0)->value, "a");
    EXPECT_EQ(Child(*outer_li, 1)->tag_name, "ul");
    EXPECT_EQ(TextContent(*inner), "b");
}

TEST(HtmlParserBody, DlDdDtAutoClose)
{
    auto c = Parse("<dl><dt>term<dd>definition<dt>t2<dd>d2</dl>");
    const html::Node* dl = FindTag(*c.document, "dl");
    ASSERT_TRUE(dl != nullptr);
    const std::vector<const html::Node*> dt = FindAllTags(*dl, "dt");
    const std::vector<const html::Node*> dd = FindAllTags(*dl, "dd");
    ASSERT_EQ(dt.size(), size_t{2});
    ASSERT_EQ(dd.size(), size_t{2});
    EXPECT_EQ(TextContent(*dt[0]), "term");
    EXPECT_EQ(TextContent(*dd[0]), "definition");
    EXPECT_EQ(TextContent(*dt[1]), "t2");
    EXPECT_EQ(TextContent(*dd[1]), "d2");
}

TEST(HtmlParserBody, OptionsAutoClose)
{
    auto c = Parse("<select><option>a<option>b</select>");
    const html::Node* select = FindTag(*c.document, "select");
    ASSERT_TRUE(select != nullptr);
    const std::vector<const html::Node*> options = FindAllTags(*select, "option");
    ASSERT_EQ(options.size(), size_t{2});
    EXPECT_EQ(TextContent(*options[0]), "a");
    EXPECT_EQ(TextContent(*options[1]), "b");
}

TEST(HtmlParserBody, NestedFormIgnored)
{
    auto c = Parse("<form><form><input></form>");
    const std::vector<const html::Node*> forms = FindAllTags(*c.document, "form");
    ASSERT_EQ(forms.size(), size_t{1});
    const html::Node* input = FindTag(*c.document, "input");
    ASSERT_TRUE(input != nullptr);
    EXPECT_EQ(forms[0]->child_nodes.front().get(), input);
}

TEST(HtmlParserBody, ButtonsDoNotNest)
{
    auto c = Parse("<button>a<button>b");
    const std::vector<const html::Node*> buttons = FindAllTags(*c.document, "button");
    ASSERT_EQ(buttons.size(), size_t{2});
    EXPECT_EQ(TextContent(*buttons[0]), "a");
    EXPECT_EQ(TextContent(*buttons[1]), "b");
    EXPECT_EQ(buttons[0]->child_nodes.size(), size_t{1});
}

// ---------------------------------------------------------------------------
// Formatting elements and the adoption agency
// ---------------------------------------------------------------------------

TEST(HtmlParserFormatting, SimpleUnclosedFormattingFlushedAtEof)
{
    auto c = Parse("<b>x");
    const html::Node* b = FindTag(*c.document, "b");
    ASSERT_TRUE(b != nullptr);
    EXPECT_EQ(TextContent(*b), "x");
    EXPECT_FALSE(b->child_nodes.empty());
}

TEST(HtmlParserFormatting, MisnestedRepairedByAdoptionAgency)
{
    auto c = Parse("<b><i>x</b>y</i>");
    const html::Node* body = FindTag(*c.document, "body");
    ASSERT_TRUE(body != nullptr);
    ASSERT_EQ(ChildCount(*body), size_t{2});

    const html::Node* outer_b = Child(*body, 0);
    const html::Node* outer_i = Child(*body, 1);
    EXPECT_EQ(outer_b->tag_name, "b");
    EXPECT_EQ(outer_i->tag_name, "i");

    const html::Node* b_i = Child(*outer_b, 0);
    EXPECT_EQ(b_i->tag_name, "i");
    EXPECT_EQ(TextContent(*b_i), "x");
    EXPECT_EQ(ChildCount(*b_i), size_t{1});

    EXPECT_EQ(TextContent(*outer_i), "y");
    EXPECT_EQ(ChildCount(*outer_i), size_t{1});
}

TEST(HtmlParserFormatting, NestedFormattingRenderedSequential)
{
    auto c = Parse("<p>1<b>2<i>3</i>4</b>5</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(TextContent(*p), "12345");
    const html::Node* b = FindTag(*p, "b");
    ASSERT_TRUE(b != nullptr);
    const html::Node* i = FindTag(*b, "i");
    ASSERT_TRUE(i != nullptr);
    EXPECT_EQ(Child(*b, 0)->type, html::NodeType::kText);
    EXPECT_EQ(Child(*b, 0)->value, "2");
    EXPECT_EQ(Child(*b, 1)->tag_name, "i");
}

// ---------------------------------------------------------------------------
// Tables and foster parenting
// ---------------------------------------------------------------------------

TEST(HtmlParserTable, InfersTbody)
{
    auto c = Parse("<table><tr><td>x</td></tr></table>");
    const html::Node* table = FindTag(*c.document, "table");
    ASSERT_TRUE(table != nullptr);
    ASSERT_EQ(ChildCount(*table), size_t{1});
    const html::Node* tbody = Child(*table, 0);
    EXPECT_EQ(tbody->tag_name, "tbody");
    ASSERT_EQ(ChildCount(*tbody), size_t{1});
    const html::Node* tr = Child(*tbody, 0);
    EXPECT_EQ(tr->tag_name, "tr");
    ASSERT_EQ(ChildCount(*tr), size_t{1});
    const html::Node* td = Child(*tr, 0);
    EXPECT_EQ(td->tag_name, "td");
    EXPECT_EQ(TextContent(*td), "x");
}

TEST(HtmlParserTable, ExplicitTbodyUsed)
{
    auto c = Parse("<!DOCTYPE html><table><tbody><tr><th>h</th></tr></tbody></table>");
    const html::Node* table = FindTag(*c.document, "table");
    ASSERT_TRUE(table != nullptr);
    const html::Node* tbody = FindTag(*table, "tbody");
    ASSERT_TRUE(tbody != nullptr);
    const html::Node* tr = FindTag(*tbody, "tr");
    ASSERT_TRUE(tr != nullptr);
    const html::Node* th = FindTag(*tr, "th");
    ASSERT_TRUE(th != nullptr);
    EXPECT_EQ(TextContent(*th), "h");
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlParserTable, StrayTextIsFosterParented)
{
    auto c = Parse("<table>stray<tr><td>x</td></tr></table>");
    const html::Node* body = FindTag(*c.document, "body");
    ASSERT_TRUE(body != nullptr);
    const html::Node* table = FindTag(*c.document, "table");
    ASSERT_TRUE(table != nullptr);

    ASSERT_EQ(ChildCount(*body), size_t{2});
    EXPECT_EQ(Child(*body, 0)->type, html::NodeType::kText);
    EXPECT_EQ(Child(*body, 0)->value, "stray");
    EXPECT_EQ(Child(*body, 1), table);
}

TEST(HtmlParserTable, StrayBlockFosterParentedBeforeTable)
{
    auto c = Parse("<table><div>x</div><tr><td>y</td></tr></table>");
    const html::Node* body = FindTag(*c.document, "body");
    ASSERT_TRUE(body != nullptr);
    const html::Node* table = FindTag(*c.document, "table");
    ASSERT_TRUE(table != nullptr);

    const html::Node* div = Child(*body, 0);
    ASSERT_TRUE(div != nullptr);
    EXPECT_EQ(div->tag_name, "div");
    EXPECT_EQ(TextContent(*div), "x");
    EXPECT_EQ(Child(*body, 1), table);
}

TEST(HtmlParserTable, CaptionRecognized)
{
    auto c = Parse("<table><caption>sum</caption><tr><td>y</td></tr></table>");
    const html::Node* table = FindTag(*c.document, "table");
    ASSERT_TRUE(table != nullptr);
    const html::Node* caption = FindTag(*table, "caption");
    ASSERT_TRUE(caption != nullptr);
    EXPECT_EQ(TextContent(*caption), "sum");
}

TEST(HtmlParserTable, ColgroupAndCol)
{
    auto c = Parse("<table><colgroup><col></colgroup><tr><td>z</td></tr></table>");
    const html::Node* table = FindTag(*c.document, "table");
    ASSERT_TRUE(table != nullptr);
    const html::Node* colgroup = FindTag(*table, "colgroup");
    ASSERT_TRUE(colgroup != nullptr);
    const html::Node* col = FindTag(*colgroup, "col");
    ASSERT_TRUE(col != nullptr);
}

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

TEST(HtmlParserTemplate, ContentParsedIntoOwnedFragment)
{
    auto c = Parse("<template><div>x</div></template>");
    const html::Node* tmpl = FindTag(*c.document, "template");
    ASSERT_TRUE(tmpl != nullptr);
    ASSERT_TRUE(html::IsTemplateElement(*tmpl));
    ASSERT_TRUE(tmpl->template_content != nullptr);
    EXPECT_EQ(tmpl->template_content->type, html::NodeType::kDocumentFragment);
    const html::Node* div = FindTag(*tmpl->template_content, "div");
    ASSERT_TRUE(div != nullptr);
    EXPECT_EQ(TextContent(*div), "x");
    EXPECT_EQ(ChildCount(*tmpl), size_t{0});
}

TEST(HtmlParserTemplate, NestedTemplates)
{
    auto c = Parse("<template><template><p>deep</p></template></template>");
    const std::vector<const html::Node*> templates = FindAllTags(*c.document, "template");
    ASSERT_EQ(templates.size(), size_t{2});
    EXPECT_TRUE(html::IsTemplateElement(*templates[0]));
    EXPECT_TRUE(html::IsTemplateElement(*templates[1]));
    const html::Node* p = FindTag(*templates[1]->template_content, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(TextContent(*p), "deep");
}

// ---------------------------------------------------------------------------
// Foreign content (MathML / SVG)
// ---------------------------------------------------------------------------

TEST(HtmlParserForeign, SvgElementInSvgNamespace)
{
    auto c = Parse("<svg><circle cx=\"1\" cy=\"2\" r=\"3\"></circle></svg>");
    const html::Node* svg = FindTag(*c.document, "svg");
    ASSERT_TRUE(svg != nullptr);
    EXPECT_EQ(svg->namespace_uri, html::NS::kSvg);
    const html::Node* circle = FindTag(*svg, "circle");
    ASSERT_TRUE(circle != nullptr);
    EXPECT_EQ(circle->namespace_uri, html::NS::kSvg);
    ASSERT_TRUE(Attr(*circle, "cx") != nullptr);
    EXPECT_EQ(*Attr(*circle, "cy"), "2");
}

TEST(HtmlParserForeign, SvgAttributesAdjustedToCamelCase)
{
    auto c = Parse("<svg viewbox=\"0 0 10 10\"><line x1=\"0\" x2=\"10\"></line></svg>");
    const html::Node* svg = FindTag(*c.document, "svg");
    ASSERT_TRUE(svg != nullptr);
    EXPECT_TRUE(Attr(*svg, "viewbox") == nullptr);
    const std::string* viewbox = Attr(*svg, "viewBox");
    ASSERT_TRUE(viewbox != nullptr);
    EXPECT_EQ(*viewbox, "0 0 10 10");
}

TEST(HtmlParserForeign, MathElementInMathmlNamespace)
{
    auto c = Parse("<math><mi>x</mi></math>");
    const html::Node* math = FindTag(*c.document, "math");
    ASSERT_TRUE(math != nullptr);
    EXPECT_EQ(math->namespace_uri, html::NS::kMathml);
    const html::Node* mi = FindTag(*math, "mi");
    ASSERT_TRUE(mi != nullptr);
    EXPECT_EQ(mi->namespace_uri, html::NS::kMathml);
    EXPECT_EQ(TextContent(*mi), "x");
}

TEST(HtmlParserForeign, DefinitionUrlAdjusted)
{
    auto c = Parse("<math><mo definitionurl=\"#x\">*</mo></math>");
    const html::Node* mo = FindTag(*c.document, "mo");
    ASSERT_TRUE(mo != nullptr);
    EXPECT_TRUE(Attr(*mo, "definitionurl") == nullptr);
    const std::string* name = Attr(*mo, "definitionURL");
    ASSERT_TRUE(name != nullptr);
    EXPECT_EQ(*name, "#x");
}

TEST(HtmlParserForeign, HtmlStartTagExitsForeignContent)
{
    auto c = Parse("<svg><p>x</p></svg>");
    const html::Node* svg = FindTag(*c.document, "svg");
    ASSERT_TRUE(svg != nullptr);
    EXPECT_TRUE(FindTag(*svg, "p") == nullptr);
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(p->namespace_uri, html::NS::kHtml);
    EXPECT_EQ(TextContent(*p), "x");
}

TEST(HtmlParserForeign, SvgTitleBecomesIntegrationPoint)
{
    auto c = Parse("<svg><title>the title</title></svg>");
    const html::Node* title = FindTag(*c.document, "title");
    ASSERT_TRUE(title != nullptr);
    EXPECT_EQ(title->namespace_uri, html::NS::kSvg);
    EXPECT_EQ(TextContent(*title), "the title");
}

TEST(HtmlParserForeign, SvgSelfClosingVoidAcknowledged)
{
    auto c = Parse("<svg><rect width=\"1\"/></svg>");
    EXPECT_EQ(CountError(c.errors, html::Err::kNonVoidHtmlElementStartTagWithTrailingSolidus),
              size_t{0});
    const html::Node* rect = FindTag(*c.document, "rect");
    ASSERT_TRUE(rect != nullptr);
    EXPECT_EQ(rect->namespace_uri, html::NS::kSvg);
}

// ---------------------------------------------------------------------------
// Framesets
// ---------------------------------------------------------------------------

TEST(HtmlParserFrameset, EmptyFramesetDocument)
{
    auto c = Parse("<!DOCTYPE html><html><head></head><frameset><frame></frameset>");
    EXPECT_EQ(c.document->mode, html::DocumentMode::kNoQuirks);
    const html::Node* frameset = FindTag(*c.document, "frameset");
    ASSERT_TRUE(frameset != nullptr);
    const html::Node* frame = FindTag(*frameset, "frame");
    ASSERT_TRUE(frame != nullptr);
}

TEST(HtmlParserFrameset, BodyContentSuppressesFrameset)
{
    auto c = Parse("<body>x<frameset>");
    const html::Node* frameset = FindTag(*c.document, "frameset");
    EXPECT_TRUE(frameset == nullptr);
    const html::Node* body = FindTag(*c.document, "body");
    ASSERT_TRUE(body != nullptr);
    EXPECT_EQ(TextContent(*body), "x");
}

// ---------------------------------------------------------------------------
// Fragment parsing
// ---------------------------------------------------------------------------

TEST(HtmlParserFragment, DivContextBuildsFragment)
{
    auto context = html::CreateElement("div");
    auto fragment = ParseFragment(context.get(), "<b>x</b>");
    ASSERT_TRUE(fragment != nullptr);
    EXPECT_EQ(fragment->type, html::NodeType::kDocumentFragment);
    ASSERT_EQ(ChildCount(*fragment), size_t{1});
    const html::Node* b = Child(*fragment, 0);
    EXPECT_EQ(b->tag_name, "b");
    EXPECT_EQ(TextContent(*b), "x");
}

TEST(HtmlParserFragment, DefaultTemplateContextIsForgiving)
{
    auto fragment = ParseFragment(nullptr, "<p>x</p><br>");
    ASSERT_TRUE(fragment != nullptr);
    ASSERT_EQ(ChildCount(*fragment), size_t{2});
    EXPECT_EQ(Child(*fragment, 0)->tag_name, "p");
    EXPECT_EQ(Child(*fragment, 1)->tag_name, "br");
    EXPECT_EQ(TextContent(*Child(*fragment, 0)), "x");
}

TEST(HtmlParserFragment, TitleContextIsRcdata)
{
    auto context = html::CreateElement("title");
    auto fragment = ParseFragment(context.get(), "a &amp; b");
    ASSERT_TRUE(fragment != nullptr);
    ASSERT_EQ(ChildCount(*fragment), size_t{1});
    EXPECT_EQ(Child(*fragment, 0)->type, html::NodeType::kText);
    EXPECT_EQ(Child(*fragment, 0)->value, "a & b");
}

TEST(HtmlParserFragment, StyleContextIsRawtext)
{
    auto context = html::CreateElement("style");
    auto fragment = ParseFragment(context.get(), "<b>not a tag &amp; still text");
    ASSERT_TRUE(fragment != nullptr);
    ASSERT_EQ(ChildCount(*fragment), size_t{1});
    EXPECT_EQ(Child(*fragment, 0)->type, html::NodeType::kText);
    EXPECT_EQ(Child(*fragment, 0)->value, "<b>not a tag &amp; still text");
}

TEST(HtmlParserFragment, ScriptContextIsScriptData)
{
    auto context = html::CreateElement("script");
    auto fragment = ParseFragment(context.get(), "var x = 1;");
    ASSERT_TRUE(fragment != nullptr);
    ASSERT_EQ(ChildCount(*fragment), size_t{1});
    EXPECT_EQ(Child(*fragment, 0)->type, html::NodeType::kText);
    EXPECT_EQ(Child(*fragment, 0)->value, "var x = 1;");
}

TEST(HtmlParserFragment, FormContextAssociatesFormPointer)
{
    auto context = html::CreateElement("form");
    auto fragment = ParseFragment(context.get(), "<input>");
    ASSERT_TRUE(fragment != nullptr);
    ASSERT_EQ(ChildCount(*fragment), size_t{1});
    EXPECT_EQ(Child(*fragment, 0)->tag_name, "input");
}

// ---------------------------------------------------------------------------
// Script handler
// ---------------------------------------------------------------------------

TEST(HtmlParserScriptHandler, FiresOnClosingScript)
{
    bool called = false;
    const html::Node* seen = nullptr;
    html::Parser parser;
    parser.script_handler = [&](html::Node& node) {
        called = true;
        seen = &node;
    };
    parser.Write("<script>var x = 1;</script>");

    EXPECT_TRUE(called);
    ASSERT_TRUE(seen != nullptr);
    EXPECT_EQ(seen->tag_name, "script");
    EXPECT_EQ(TextContent(*seen), "var x = 1;");
}

TEST(HtmlParserScriptHandler, DoesNotFireWithoutScript)
{
    bool called = false;
    html::Parser parser;
    parser.script_handler = [&](html::Node&) { called = true; };
    parser.Write("<p>no script</p>");
    EXPECT_FALSE(called);
}

// ---------------------------------------------------------------------------
// Scripting flag (noscript)
// ---------------------------------------------------------------------------

TEST(HtmlParserNoscript, ScriptingEnabledParsesAsRawtext)
{
    html::ParserOptions options;
    options.scripting_enabled = true;
    auto c = Parse("<noscript><img src=\"a.png\"></noscript>", options);
    const html::Node* noscript = FindTag(*c.document, "noscript");
    ASSERT_TRUE(noscript != nullptr);
    EXPECT_TRUE(FindTag(*noscript, "img") == nullptr);
    EXPECT_EQ(ChildCount(*noscript), size_t{1});
    EXPECT_EQ(Child(*noscript, 0)->type, html::NodeType::kText);
}

TEST(HtmlParserNoscript, ScriptingDisabledParsesAsMarkup)
{
    html::ParserOptions options;
    options.scripting_enabled = false;
    auto c = Parse("<noscript><img src=\"a.png\"></noscript>", options);
    const html::Node* noscript = FindTag(*c.document, "noscript");
    ASSERT_TRUE(noscript != nullptr);
    EXPECT_EQ(ChildCount(*noscript), size_t{0});
    const html::Node* img = FindTag(*c.document, "img");
    ASSERT_TRUE(img != nullptr);
    ASSERT_TRUE(Attr(*img, "src") != nullptr);
    EXPECT_EQ(*Attr(*img, "src"), "a.png");
}

TEST(HtmlParserNoscript, DisallowedContentInHeadNoscriptReported)
{
    html::ParserOptions options;
    options.scripting_enabled = false;
    auto c = Parse("<head><noscript><p>x</p></noscript>", options);
    EXPECT_EQ(CountError(c.errors, html::Err::kDisallowedContentInNoscriptInHead),
              size_t{1});
    const html::Node* noscript = FindTag(*c.document, "noscript");
    ASSERT_TRUE(noscript != nullptr);
    EXPECT_EQ(ChildCount(*noscript), size_t{0});
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(TextContent(*p), "x");
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

TEST(HtmlParserErrors, StrayEndTagReported)
{
    auto c = Parse("<!DOCTYPE html><head></head></div><p>a</p>");
    EXPECT_EQ(CountError(c.errors, html::Err::kEndTagWithoutMatchingOpenElement),
              size_t{1});
    const html::Node* body = FindTag(*c.document, "body");
    ASSERT_TRUE(body != nullptr);
    ASSERT_EQ(ChildCount(*body), size_t{1});
    EXPECT_EQ(Child(*body, 0)->tag_name, "p");
    EXPECT_EQ(TextContent(*Child(*body, 0)), "a");
}

TEST(HtmlParserErrors, ClosingElementWithOpenChildren)
{
    auto c = Parse("<!DOCTYPE html><template><div>x</template>");
    EXPECT_EQ(CountError(c.errors, html::Err::kClosingOfElementWithOpenChildElements),
              size_t{1});
}

TEST(HtmlParserErrors, EndTagInHeadWithoutOpenElement)
{
    auto c = Parse("<head><div></div></head>");
    EXPECT_EQ(CountError(c.errors, html::Err::kMisplacedStartTagForHeadElement), size_t{0});
}

TEST(HtmlParserErrors, MisplacedDoctypeReported)
{
    auto c = Parse("<!DOCTYPE html><head><!DOCTYPE html></head><p>x</p>");
    EXPECT_EQ(CountError(c.errors, html::Err::kMisplacedDoctype), size_t{1});
}

TEST(HtmlParserErrors, UnfinishedTextElementReported)
{
    auto c = Parse("<textarea>unterminated");
    EXPECT_EQ(CountError(c.errors, html::Err::kEofInElementThatCanContainOnlyText),
              size_t{1});
}

TEST(HtmlParserErrors, ErrorsCarryLocations)
{
    auto c = Parse("<!DOCTYPE html><head></head></div>");
    bool found_end_tag = false;
    for (const html::ParserError& error : c.errors) {
        if (error.code == html::Err::kEndTagWithoutMatchingOpenElement) {
            found_end_tag = true;
            EXPECT_GT(error.start_offset, 0);
            EXPECT_GE(error.end_offset, error.start_offset);
            EXPECT_GE(error.start_line, 1);
            EXPECT_GE(error.start_col, 1);
        }
    }
    EXPECT_TRUE(found_end_tag);
}

TEST(HtmlParserErrors, NoErrorsReportedWithoutHandler)
{
    auto c = Parse("<p>a</p>", {}, /*collect_errors=*/false);
    EXPECT_EQ(c.errors.size(), size_t{0});
}

// ---------------------------------------------------------------------------
// Source locations
// ---------------------------------------------------------------------------

TEST(HtmlParserLocation, RecordedWhenRequested)
{
    html::ParserOptions options;
    options.source_code_location_info = true;
    auto c = Parse("<div><p>x</p></div>", options, /*collect_errors=*/false);

    const html::Node* div = FindTag(*c.document, "div");
    ASSERT_TRUE(div != nullptr);
    ASSERT_TRUE(div->source_code_location != nullptr);
    EXPECT_EQ(div->source_code_location->start_line, 1);
    EXPECT_EQ(div->source_code_location->start_col, 1);
    EXPECT_EQ(div->source_code_location->start_offset, 0);

    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    ASSERT_TRUE(p->source_code_location != nullptr);
    EXPECT_EQ(p->source_code_location->start_line, 1);
    EXPECT_EQ(p->source_code_location->start_col, 6);

    ASSERT_TRUE(div->child_nodes.size() == size_t{1});
    EXPECT_TRUE(html::IsElementNode(*div->child_nodes[0]));
    EXPECT_EQ(div->child_nodes[0]->tag_name, "p");
    ASSERT_TRUE(div->child_nodes[0]->source_code_location != nullptr);

    ASSERT_TRUE(div->child_nodes[0]->child_nodes.size() == size_t{1});
    EXPECT_TRUE(html::IsTextNode(*div->child_nodes[0]->child_nodes[0]));
    EXPECT_EQ(div->child_nodes[0]->child_nodes[0]->value, "x");
    ASSERT_TRUE(div->child_nodes[0]->child_nodes[0]->source_code_location != nullptr);
}

TEST(HtmlParserLocation, OffByDefault)
{
    auto c = Parse("<div><p>x</p></div>", {}, /*collect_errors=*/false);
    const html::Node* div = FindTag(*c.document, "div");
    ASSERT_TRUE(div != nullptr);
    EXPECT_TRUE(div->source_code_location == nullptr);
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_TRUE(p->source_code_location == nullptr);
}

TEST(HtmlParserLocation, ErrorHandlerEnablesLocations)
{
    auto c = Parse("<!DOCTYPE html><p>x</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_TRUE(p->source_code_location != nullptr);
}

TEST(HtmlParserLocation, EndTagLocationRecordedForExplicitClose)
{
    html::ParserOptions options;
    options.source_code_location_info = true;
    auto c = Parse("<div>a</div>", options, /*collect_errors=*/false);
    const html::Node* div = FindTag(*c.document, "div");
    ASSERT_TRUE(div != nullptr);
    ASSERT_TRUE(div->end_tag_location != nullptr);
    EXPECT_EQ(div->end_tag_location->start_offset, 6);
}

// ---------------------------------------------------------------------------
// Streaming (Parser::Write -- whole-document feed)
// ---------------------------------------------------------------------------

TEST(HtmlParserStreaming, SingleWriteAssemblesDocument)
{
    html::ParserOptions options;
    html::Parser parser(options);
    parser.Write("<div class=\"x\">hi</div>");
    ASSERT_TRUE(parser.document != nullptr);

    const html::Node* div = FindTag(*parser.document, "div");
    ASSERT_TRUE(div != nullptr);
    const std::string* class_value = Attr(*div, "class");
    ASSERT_TRUE(class_value != nullptr);
    EXPECT_EQ(*class_value, "x");
    EXPECT_EQ(TextContent(*div), "hi");
}

TEST(HtmlParserStreaming, WrittenDocumentMatchesParseDocument)
{
    html::ParserOptions options;
    html::Parser parser(options);
    parser.Write("<!DOCTYPE html><p>one<p>two</p></html>");
    ASSERT_TRUE(parser.document != nullptr);

    auto whole = Parse("<!DOCTYPE html><p>one<p>two</p></html>");
    ASSERT_TRUE(whole.document != nullptr);
    const std::string written = html::Print(*parser.document);
    const std::string parsed = html::Print(*whole.document);
    EXPECT_EQ(written, parsed);
}

// ---------------------------------------------------------------------------
// Serialization round trips
// ---------------------------------------------------------------------------

TEST(HtmlParserRoundTrip, ElementWithAttributesAndText)
{
    auto c = Parse("<p class=\"c\">hi &amp; bye</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(html::PrintOuter(*p), "<p class=\"c\">hi &amp; bye</p>");
}

TEST(HtmlParserRoundTrip, NestedMarkup)
{
    auto c = Parse("<ul><li>a</li><li>b</li></ul>");
    const html::Node* ul = FindTag(*c.document, "ul");
    ASSERT_TRUE(ul != nullptr);
    EXPECT_EQ(html::PrintOuter(*ul), "<ul><li>a</li><li>b</li></ul>");
}

TEST(HtmlParserRoundTrip, RawtextContentUnescaped)
{
    auto c = Parse("<script>a < b</script>");
    const html::Node* script = FindTag(*c.document, "script");
    ASSERT_TRUE(script != nullptr);
    EXPECT_EQ(html::PrintOuter(*script), "<script>a < b</script>");
}

// ---------------------------------------------------------------------------
// Parser helpers (implied end tags, scoping, table contexts)
// ---------------------------------------------------------------------------

TEST(HtmlParserHelpers, ImpliedEndTagSets)
{
    EXPECT_TRUE(html::IsImplicitEndTagRequired(html::TagId::kLi));
    EXPECT_TRUE(html::IsImplicitEndTagRequired(html::TagId::kDd));
    EXPECT_TRUE(html::IsImplicitEndTagRequired(html::TagId::kDt));
    EXPECT_TRUE(html::IsImplicitEndTagRequired(html::TagId::kP));
    EXPECT_TRUE(html::IsImplicitEndTagRequired(html::TagId::kOption));
    EXPECT_TRUE(html::IsImplicitEndTagRequired(html::TagId::kOptgroup));
    EXPECT_FALSE(html::IsImplicitEndTagRequired(html::TagId::kDiv));
    EXPECT_FALSE(html::IsImplicitEndTagRequired(html::TagId::kUnknown));

    EXPECT_TRUE(html::IsImplicitEndTagRequiredThoroughly(html::TagId::kTd));
    EXPECT_TRUE(html::IsImplicitEndTagRequiredThoroughly(html::TagId::kTr));
    EXPECT_TRUE(html::IsImplicitEndTagRequiredThoroughly(html::TagId::kCaption));
    EXPECT_TRUE(html::IsImplicitEndTagRequiredThoroughly(html::TagId::kLi));
    EXPECT_FALSE(html::IsImplicitEndTagRequiredThoroughly(html::TagId::kSpan));
}

TEST(HtmlParserHelpers, ScopingElementPredicates)
{
    using html::TagId;
    EXPECT_TRUE(html::IsScopingElementHtml(TagId::kHtml));
    EXPECT_TRUE(html::IsScopingElementHtml(TagId::kTable));
    EXPECT_TRUE(html::IsScopingElementHtml(TagId::kTd));
    EXPECT_TRUE(html::IsScopingElementHtml(TagId::kTemplate));
    EXPECT_FALSE(html::IsScopingElementHtml(TagId::kBody));

    EXPECT_TRUE(html::IsScopingElementHtmlList(TagId::kUl));
    EXPECT_TRUE(html::IsScopingElementHtmlList(TagId::kOl));
    EXPECT_TRUE(html::IsScopingElementHtmlButton(TagId::kButton));
    EXPECT_FALSE(html::IsScopingElementHtmlButton(TagId::kUl));

    EXPECT_TRUE(html::IsScopingElementMathml(TagId::kMi));
    EXPECT_TRUE(html::IsScopingElementSvg(TagId::kForeignObject));
}

TEST(HtmlParserHelpers, TableContextPredicates)
{
    using html::TagId;
    EXPECT_TRUE(html::IsTableRowContext(TagId::kTr));
    EXPECT_FALSE(html::IsTableRowContext(TagId::kTbody));
    EXPECT_TRUE(html::IsTableBodyContext(TagId::kTbody));
    EXPECT_TRUE(html::IsTableBodyContext(TagId::kThead));
    EXPECT_TRUE(html::IsTableContext(TagId::kTable));
    EXPECT_TRUE(html::IsTableCell(TagId::kTd));
    EXPECT_TRUE(html::IsTableCell(TagId::kTh));
    EXPECT_FALSE(html::IsTableCell(TagId::kTr));
}

TEST(HtmlParserHelpers, IsTableStructureTag)
{
    EXPECT_TRUE(html::IsTableStructureTag(html::TagId::kTable));
    EXPECT_TRUE(html::IsTableStructureTag(html::TagId::kTbody));
    EXPECT_TRUE(html::IsTableStructureTag(html::TagId::kTfoot));
    EXPECT_TRUE(html::IsTableStructureTag(html::TagId::kThead));
    EXPECT_TRUE(html::IsTableStructureTag(html::TagId::kTr));
    EXPECT_FALSE(html::IsTableStructureTag(html::TagId::kTd));
}

TEST(HtmlParserHelpers, ForeignContentAdjustments)
{
    html::TagToken token;
    token.tag_name = "lineargradient";
    token.tag_id = html::GetTagId("lineargradient");
    html::AdjustTokenSVGTagName(token);
    EXPECT_EQ(token.tag_name, "linearGradient");

    token.tag_name = "line";
    html::Attribute attr;
    attr.name = "gradientunits";
    attr.value = "objectBoundingBox";
    token.attrs.push_back(attr);
    html::AdjustTokenSVGAttrs(token);
    EXPECT_EQ(token.attrs[0].name, "gradientUnits");

    html::TagToken math_token;
    math_token.tag_name = "mo";
    html::Attribute math_attr;
    math_attr.name = "definitionurl";
    math_attr.value = "#x";
    math_token.attrs.push_back(math_attr);
    html::AdjustTokenMathMLAttrs(math_token);
    EXPECT_EQ(math_token.attrs[0].name, "definitionURL");
}

// ---------------------------------------------------------------------------
// Open-element stack (direct unit coverage)
// ---------------------------------------------------------------------------

namespace {

// Records push/pop events so tests can assert the stack's bookkeeping
// notifications fire at the right times.
class RecordingStackHandler : public html::StackHandler<html::DefaultTreeAdapter> {
public:
    using Node = html::DefaultTreeAdapter::ParentNode;

    void OnItemPush(Node& node, html::TagId, bool is_top) override {
        history.push_back("push:" + node.tag_name + (is_top ? ":top" : ""));
    }

    void OnItemPop(Node& node, bool is_top) override {
        history.push_back("pop:" + node.tag_name + (is_top ? ":top" : ""));
    }

    std::vector<std::string> history;
};

} // namespace

TEST(HtmlParserStack, PushPopAndCurrentTracking)
{
    auto document = html::CreateDocument();
    RecordingStackHandler handler;
    html::OpenElementStack<html::DefaultTreeAdapter> stack(*document, handler);

    auto html_el = html::CreateElement("html");
    auto body = html::CreateElement("body");
    auto p = html::CreateElement("p");

    EXPECT_EQ(stack.current, document.get());
    EXPECT_EQ(stack.stack_top, -1);

    stack.Push(*html_el, html::TagId::kHtml);
    EXPECT_EQ(stack.current, html_el.get());
    EXPECT_EQ(stack.current_tag_id, html::TagId::kHtml);
    EXPECT_EQ(stack.stack_top, 0);

    stack.Push(*body, html::TagId::kBody);
    EXPECT_EQ(stack.current, body.get());

    stack.Push(*p, html::TagId::kP);
    EXPECT_EQ(stack.current, p.get());
    EXPECT_EQ(stack.stack_top, 2);

    stack.Pop();
    EXPECT_EQ(stack.current, body.get());
    EXPECT_EQ(stack.stack_top, 1);

    stack.Pop();
    stack.Pop();
    EXPECT_EQ(stack.current, nullptr);
    EXPECT_EQ(stack.stack_top, -1);

    ASSERT_EQ(handler.history.size(), size_t{6});
    EXPECT_EQ(handler.history[0], "push:html:top");
    EXPECT_EQ(handler.history[1], "push:body:top");
    EXPECT_EQ(handler.history[2], "push:p:top");
    EXPECT_EQ(handler.history[3], "pop:p:top");
    EXPECT_EQ(handler.history[4], "pop:body:top");
    EXPECT_EQ(handler.history[5], "pop:html:top");
}

TEST(HtmlParserStack, ScopeChecks)
{
    auto document = html::CreateDocument();
    RecordingStackHandler handler;
    html::OpenElementStack<html::DefaultTreeAdapter> stack(*document, handler);

    auto html_el = html::CreateElement("html");
    auto body = html::CreateElement("body");
    auto li = html::CreateElement("li");
    auto ul = html::CreateElement("ul");
    auto div = html::CreateElement("div");

    stack.Push(*html_el, html::TagId::kHtml);
    stack.Push(*body, html::TagId::kBody);
    stack.Push(*div, html::TagId::kDiv);
    stack.Push(*li, html::TagId::kLi);

    EXPECT_TRUE(stack.HasInScope(html::TagId::kLi));
    EXPECT_TRUE(stack.HasInListItemScope(html::TagId::kLi));
    EXPECT_TRUE(stack.HasInButtonScope(html::TagId::kLi));

    stack.Push(*ul, html::TagId::kUl);
    EXPECT_TRUE(stack.HasInListItemScope(html::TagId::kUl));
    EXPECT_FALSE(stack.HasInListItemScope(html::TagId::kLi));
    EXPECT_TRUE(stack.HasInButtonScope(html::TagId::kUl));
}

TEST(HtmlParserStack, GenerateImpliedEndTags)
{
    auto document = html::CreateDocument();
    RecordingStackHandler handler;
    html::OpenElementStack<html::DefaultTreeAdapter> stack(*document, handler);

    auto html_el = html::CreateElement("html");
    auto body = html::CreateElement("body");
    auto li = html::CreateElement("li");
    auto dd = html::CreateElement("dd");
    auto div = html::CreateElement("div");

    stack.Push(*html_el, html::TagId::kHtml);
    stack.Push(*body, html::TagId::kBody);
    stack.Push(*div, html::TagId::kDiv);
    stack.Push(*li, html::TagId::kLi);
    stack.Push(*dd, html::TagId::kDd);

    stack.GenerateImpliedEndTags();
    EXPECT_EQ(stack.current, div.get());

    stack.GenerateImpliedEndTags();
    EXPECT_EQ(stack.current_tag_id, html::TagId::kDiv);
}

TEST(HtmlParserStack, PopUntilTagNamePopped)
{
    auto document = html::CreateDocument();
    RecordingStackHandler handler;
    html::OpenElementStack<html::DefaultTreeAdapter> stack(*document, handler);

    auto html_el = html::CreateElement("html");
    auto body = html::CreateElement("body");
    auto p = html::CreateElement("p");
    auto span = html::CreateElement("span");

    stack.Push(*html_el, html::TagId::kHtml);
    stack.Push(*body, html::TagId::kBody);
    stack.Push(*p, html::TagId::kP);
    stack.Push(*span, html::TagId::kSpan);

    stack.PopUntilTagNamePopped(html::TagId::kP);
    EXPECT_EQ(stack.current, body.get());
    EXPECT_EQ(stack.stack_top, 1);
}

TEST(HtmlParserStack, ReplaceAndInsertAfter)
{
    auto document = html::CreateDocument();
    RecordingStackHandler handler;
    html::OpenElementStack<html::DefaultTreeAdapter> stack(*document, handler);

    auto html_el = html::CreateElement("html");
    auto html_el2 = html::CreateElement("html");
    auto body = html::CreateElement("body");
    auto footer = html::CreateElement("footer");

    stack.Push(*html_el, html::TagId::kHtml);
    stack.Replace(*html_el, *html_el2);
    EXPECT_EQ(stack.items[0], html_el2.get());

    stack.InsertAfter(*html_el2, *body, html::TagId::kBody);
    EXPECT_EQ(stack.current, body.get());
    EXPECT_EQ(stack.stack_top, 1);

    stack.InsertAfter(*body, *footer, html::TagId::kFooter);
    EXPECT_EQ(stack.current, footer.get());
    EXPECT_EQ(stack.stack_top, 2);

    stack.Remove(*footer);
    EXPECT_EQ(stack.current, body.get());
}

TEST(HtmlParserStack, MemberQueries)
{
    auto document = html::CreateDocument();
    RecordingStackHandler handler;
    html::OpenElementStack<html::DefaultTreeAdapter> stack(*document, handler);

    auto html_el = html::CreateElement("html");
    auto body = html::CreateElement("body");
    auto table = html::CreateElement("table");

    stack.Push(*html_el, html::TagId::kHtml);
    EXPECT_TRUE(stack.Contains(*html_el));
    EXPECT_TRUE(stack.IsRootHtmlElementCurrent());
    EXPECT_FALSE(stack.Contains(*body));

    stack.Push(*body, html::TagId::kBody);
    EXPECT_EQ(stack.GetCommonAncestor(*html_el), nullptr);
    EXPECT_EQ(stack.GetCommonAncestor(*body), html_el.get());

    stack.Push(*table, html::TagId::kTable);
    stack.ClearBackToTableContext();
    EXPECT_EQ(stack.current, table.get());
    EXPECT_EQ(stack.stack_top, 2);
}

// ---------------------------------------------------------------------------
// Formatting element list (direct unit coverage)
// ---------------------------------------------------------------------------

TEST(HtmlParserFormattingList, MarkerStopsScopeSearch)
{
    using List = html::FormattingElementList<html::DefaultTreeAdapter>;
    List list;

    auto b_el = html::CreateElement("b");
    html::TagToken token;
    token.tag_name = "b";
    list.PushElement(*b_el, token);

    EXPECT_EQ(list.GetElementEntryInScopeWithTagName("b"), &*list.entries.begin());

    list.InsertMarker();
    auto i_el = html::CreateElement("i");
    html::TagToken i_token;
    i_token.tag_name = "i";
    list.PushElement(*i_el, i_token);

    EXPECT_TRUE(list.GetElementEntryInScopeWithTagName("b") == nullptr);
    EXPECT_TRUE(list.GetElementEntryInScopeWithTagName("i") != nullptr);
}

TEST(HtmlParserFormattingList, ClearToLastMarker)
{
    using List = html::FormattingElementList<html::DefaultTreeAdapter>;
    List list;

    auto i_el = html::CreateElement("i");
    html::TagToken i_token;
    i_token.tag_name = "i";
    list.PushElement(*i_el, i_token);

    list.InsertMarker();
    EXPECT_EQ(list.entries.size(), size_t{2});

    list.ClearToLastMarker();
    ASSERT_EQ(list.entries.size(), size_t{1});
    EXPECT_EQ(list.entries.front().type, html::EntryType::kElement);
    EXPECT_EQ(list.entries.front().element, i_el.get());
}

TEST(HtmlParserFormattingList, NoahArkConditionDropsOldestDuplicate)
{
    using List = html::FormattingElementList<html::DefaultTreeAdapter>;
    List list;

    html::Attribute class_attr;
    class_attr.name = "class";
    class_attr.value = "x";

    auto make = [&]() {
        return html::CreateElement("b", html::NS::kHtml, {class_attr});
    };
    auto token_for = [&](const html::Node& element) {
        html::TagToken token;
        token.tag_name = element.tag_name;
        return token;
    };

    auto b1 = make();
    auto b2 = make();
    auto b3 = make();
    auto b4 = make();

    list.PushElement(*b1, token_for(*b1));
    list.PushElement(*b2, token_for(*b2));
    list.PushElement(*b3, token_for(*b3));
    ASSERT_EQ(list.entries.size(), size_t{3});

    EXPECT_TRUE(list.GetElementEntry(b1.get()) != nullptr);

    list.PushElement(*b4, token_for(*b4));
    ASSERT_EQ(list.entries.size(), size_t{3});

    EXPECT_TRUE(list.GetElementEntry(b1.get()) == nullptr);
    EXPECT_TRUE(list.GetElementEntry(b2.get()) != nullptr);
    EXPECT_TRUE(list.GetElementEntry(b3.get()) != nullptr);
    EXPECT_TRUE(list.GetElementEntry(b4.get()) != nullptr);
}

TEST(HtmlParserFormattingList, RemoveEntryToleratesMissing)
{
    using List = html::FormattingElementList<html::DefaultTreeAdapter>;
    List list;

    auto b_el = html::CreateElement("b");
    html::TagToken token;
    token.tag_name = "b";
    list.PushElement(*b_el, token);

    html::FormattingEntry<html::DefaultTreeAdapter> bogus{};
    list.RemoveEntry(bogus);
    ASSERT_EQ(list.entries.size(), size_t{1});
}

// ---------------------------------------------------------------------------
// Insertion mode constants and helper entry points
// ---------------------------------------------------------------------------

TEST(HtmlParserMode, InsertionModeEnumerationStable)
{
    using html::InsertionMode;
    EXPECT_LT(static_cast<int>(InsertionMode::kInitial),
              static_cast<int>(InsertionMode::kBeforeHtml));
    EXPECT_LT(static_cast<int>(InsertionMode::kBeforeHtml),
              static_cast<int>(InsertionMode::kBeforeHead));
    EXPECT_LT(static_cast<int>(InsertionMode::kInHead),
              static_cast<int>(InsertionMode::kInBody));
}

} // namespace