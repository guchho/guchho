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

// Parses "input" as a whole document, collecting parse errors by default so
// doctype/quirks behavior can be asserted alongside the printed output.
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

// Depth-first first match on tag name; the root itself may match.
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

// ---------------------------------------------------------------------------
// Elements and text
// ---------------------------------------------------------------------------

TEST(HtmlPrinter, ElementWithAttributesAndText)
{
    auto c = Parse("<p class=\"c\">hi &amp; bye</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(html::PrintOuter(*p), "<p class=\"c\">hi &amp; bye</p>");
}

TEST(HtmlPrinter, PrintYieldsInnerHtmlOnly)
{
    auto c = Parse("<div><p>x</p></div>");
    const html::Node* div = FindTag(*c.document, "div");
    ASSERT_TRUE(div != nullptr);
    EXPECT_EQ(html::Print(*div), "<p>x</p>");
}

TEST(HtmlPrinter, EscapesLessThanAndGreaterThan)
{
    auto c = Parse("<p>1 < 2 > 3</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(html::PrintOuter(*p), "<p>1 &lt; 2 &gt; 3</p>");
}

TEST(HtmlPrinter, NoBreakSpaceEscaped)
{
    auto text = html::CreateTextNode("a \xC2\xA0 b");
    EXPECT_EQ(html::PrintOuter(*text), "a &nbsp; b");
}

TEST(HtmlPrinter, AttributeValueEscaped)
{
    auto c = Parse("<p title=\"a &quot; x &#38; y\">z</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(html::PrintOuter(*p), "<p title=\"a &quot; x &amp; y\">z</p>");
}

TEST(HtmlPrinter, NestedMarkup)
{
    auto c = Parse("<ul><li>a</li><li>b</li></ul>");
    const html::Node* ul = FindTag(*c.document, "ul");
    ASSERT_TRUE(ul != nullptr);
    EXPECT_EQ(html::PrintOuter(*ul), "<ul><li>a</li><li>b</li></ul>");
}

TEST(HtmlPrinter, VoidElementPrintsNoClosingTag)
{
    auto c = Parse("<p>a<br>b</p>");
    const html::Node* br = FindTag(*c.document, "br");
    ASSERT_TRUE(br != nullptr);
    EXPECT_EQ(html::PrintOuter(*br), "<br>");
}

TEST(HtmlPrinter, VoidElementAsPrintInputPrintsNothing)
{
    auto c = Parse("<br>");
    const html::Node* br = FindTag(*c.document, "br");
    ASSERT_TRUE(br != nullptr);
    EXPECT_EQ(html::Print(*br), "");
}

TEST(HtmlPrinter, CommentPrintedVerbatim)
{
    auto comment = html::CreateCommentNode(" hello ");
    EXPECT_EQ(html::PrintOuter(*comment), "<!-- hello -->");
}

TEST(HtmlPrinter, DoctypePrinted)
{
    auto doc = html::CreateDocument();
    html::SetDocumentType(*doc, "html", "", "");
    EXPECT_EQ(html::PrintOuter(*doc->child_nodes[0]), "<!DOCTYPE html>");
}

TEST(HtmlPrinter, WholeDocumentDefaultOutput)
{
    auto c = Parse("<!DOCTYPE html><p>x</p>");
    EXPECT_TRUE(c.errors.empty());
    EXPECT_EQ(html::Print(*c.document),
              "<!DOCTYPE html><html><head></head><body><p>x</p></body></html>");
}

TEST(HtmlPrinter, DocumentOuterPrintsNothing)
{
    auto c = Parse("<p>x</p>");
    EXPECT_EQ(html::PrintOuter(*c.document), "");
}

TEST(HtmlPrinter, TemplateContentSerialized)
{
    auto c = Parse("<template><div>x</div></template>");
    const html::Node* tmpl = FindTag(*c.document, "template");
    ASSERT_TRUE(tmpl != nullptr);
    EXPECT_EQ(html::PrintOuter(*tmpl), "<template><div>x</div></template>");
    EXPECT_EQ(html::Print(*tmpl), "<div>x</div>");
}

// ---------------------------------------------------------------------------
// Raw text and the scripting flag
// ---------------------------------------------------------------------------

TEST(HtmlPrinter, ScriptContentUnescaped)
{
    auto c = Parse("<script>if (a < b && c > d) {}</script>");
    const html::Node* script = FindTag(*c.document, "script");
    ASSERT_TRUE(script != nullptr);
    EXPECT_EQ(html::PrintOuter(*script),
              "<script>if (a < b && c > d) {}</script>");
}

TEST(HtmlPrinter, StyleContentUnescaped)
{
    auto c = Parse("<style>a > b { content: \"<\"; }</style>");
    const html::Node* style = FindTag(*c.document, "style");
    ASSERT_TRUE(style != nullptr);
    EXPECT_EQ(html::PrintOuter(*style),
              "<style>a > b { content: \"<\"; }</style>");
}

TEST(HtmlPrinter, WhitespaceVerbatimInPre)
{
    auto c = Parse("<pre>a   b\n c</pre>");
    const html::Node* pre = FindTag(*c.document, "pre");
    ASSERT_TRUE(pre != nullptr);
    EXPECT_EQ(html::PrintOuter(*pre), "<pre>a   b\n c</pre>");
}

TEST(HtmlPrinter, ScriptingEnabledNoscriptIsRawText)
{
    html::ParserOptions options;
    options.scripting_enabled = true;
    auto c = Parse("<!DOCTYPE html><body><noscript>a < b</noscript></body>",
                   options);
    const html::Node* noscript = FindTag(*c.document, "noscript");
    ASSERT_TRUE(noscript != nullptr);
    EXPECT_EQ(html::PrintOuter(*noscript), "<noscript>a < b</noscript>");
}

TEST(HtmlPrinter, ScriptingDisabledNoscriptIsEscaped)
{
    html::ParserOptions options;
    options.scripting_enabled = false;
    auto c = Parse("<!DOCTYPE html><body><noscript>a < b</noscript></body>",
                   options);
    const html::Node* noscript = FindTag(*c.document, "noscript");
    ASSERT_TRUE(noscript != nullptr);
    html::PrinterOptions printer_options;
    printer_options.scripting_enabled = false;
    EXPECT_EQ(html::PrintOuter(*noscript, printer_options),
              "<noscript>a &lt; b</noscript>");
}

// ---------------------------------------------------------------------------
// Attribute serialization
// ---------------------------------------------------------------------------

TEST(HtmlPrinter, NameSpacedAttributePrefixes)
{
    auto svg = html::CreateElement("svg", html::NS::kSvg);

    html::Attribute href;
    href.name = "href";
    href.prefix = "xlink";
    href.has_prefix = true;
    href.ns = html::NS::kXlink;
    href.value = "#x";
    svg->attrs.push_back(href);

    html::Attribute lang;
    lang.name = "lang";
    lang.prefix = "xml";
    lang.has_prefix = true;
    lang.ns = html::NS::kXml;
    lang.value = "en";
    svg->attrs.push_back(lang);

    html::Attribute xmlns_xlink;
    xmlns_xlink.name = "xlink";
    xmlns_xlink.prefix = "xmlns";
    xmlns_xlink.has_prefix = true;
    xmlns_xlink.ns = html::NS::kXmlns;
    xmlns_xlink.value = "http://www.w3.org/1999/xlink";
    svg->attrs.push_back(xmlns_xlink);

    html::Attribute xmlns;
    xmlns.name = "xmlns";
    xmlns.prefix = "xmlns";
    xmlns.has_prefix = true;
    xmlns.ns = html::NS::kXmlns;
    xmlns.value = "http://www.w3.org/2000/svg";
    svg->attrs.push_back(xmlns);

    EXPECT_EQ(html::PrintOuter(*svg),
              "<svg xlink:href=\"#x\" xml:lang=\"en\" "
              "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
              "xmlns=\"http://www.w3.org/2000/svg\"></svg>");
}

TEST(HtmlPrinter, UnknownPrefixAttributeFallsBackToPrefixColon)
{
    auto el = html::CreateElement("custom");
    html::Attribute attr;
    attr.name = "bar";
    attr.prefix = "foo";
    attr.has_prefix = true;
    attr.ns = html::NS::kHtml;
    attr.value = "v";
    el->attrs.push_back(attr);
    EXPECT_EQ(html::PrintOuter(*el), "<custom foo:bar=\"v\"></custom>");
}

TEST(HtmlPrinter, EmptyAttributeValueKeptAsEmptyQuoted)
{
    auto c = Parse("<input disabled>");
    const html::Node* input = FindTag(*c.document, "input");
    ASSERT_TRUE(input != nullptr);
    EXPECT_EQ(html::PrintOuter(*input), "<input disabled=\"\">");
}

// ---------------------------------------------------------------------------
// Minify
// ---------------------------------------------------------------------------

TEST(HtmlPrinterMinify, DropsSafeAttributeQuotes)
{
    auto c = Parse("<img src=\"app.js\">");
    const html::Node* img = FindTag(*c.document, "img");
    ASSERT_TRUE(img != nullptr);
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::PrintOuter(*img, options), "<img src=app.js>");
}

TEST(HtmlPrinterMinify, KeepsUnsafeAttributeQuotes)
{
    auto c = Parse("<img src=\"a b.png\">");
    const html::Node* img = FindTag(*c.document, "img");
    ASSERT_TRUE(img != nullptr);
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::PrintOuter(*img, options), "<img src=\"a b.png\">");
}

TEST(HtmlPrinterMinify, EqualsInValueForcesQuotes)
{
    auto c = Parse("<div title=\"x=y\">z</div>");
    const html::Node* div = FindTag(*c.document, "div");
    ASSERT_TRUE(div != nullptr);
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::PrintOuter(*div, options), "<div title=\"x=y\">z</div>");
}

TEST(HtmlPrinterMinify, EmptyAttributeValueKeptAsBareName)
{
    auto c = Parse("<input disabled>");
    const html::Node* input = FindTag(*c.document, "input");
    ASSERT_TRUE(input != nullptr);
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::PrintOuter(*input, options), "<input disabled>");
}

TEST(HtmlPrinterMinify, WhitespaceCollapsedInText)
{
    auto c = Parse("<p>a   b\n\t c</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::PrintOuter(*p, options), "<p>a b c</p>");
}

TEST(HtmlPrinterMinify, BlockOnlyTextTrimmed)
{
    auto c = Parse("<p>  hi  </p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::PrintOuter(*p, options), "<p>hi</p>");
}

TEST(HtmlPrinterMinify, HeadWhitespaceDropped)
{
    auto c = Parse("<!DOCTYPE html><head>   </head><body><p>z</p></body></html>");
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::Print(*c.document, options),
              "<!DOCTYPE html><html><head></head><body><p>z</p></body></html>");
}

TEST(HtmlPrinterMinify, PreContentPreservedVerbatim)
{
    auto c = Parse("<pre>a   b\n c</pre>");
    const html::Node* pre = FindTag(*c.document, "pre");
    ASSERT_TRUE(pre != nullptr);
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::PrintOuter(*pre, options), "<pre>a   b\n c</pre>");
}

TEST(HtmlPrinterMinify, TextareaContentPreservedVerbatim)
{
    auto c = Parse("<textarea>a   b</textarea>");
    const html::Node* textarea = FindTag(*c.document, "textarea");
    ASSERT_TRUE(textarea != nullptr);
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::PrintOuter(*textarea, options), "<textarea>a   b</textarea>");
}

TEST(HtmlPrinterMinify, WhiteSpacePreStylePreservesText)
{
    auto c = Parse("<div style=\"white-space: pre\"><p>  x</p></div>");
    const html::Node* div = FindTag(*c.document, "div");
    ASSERT_TRUE(div != nullptr);
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::PrintOuter(*div, options),
              "<div style=\"white-space: pre\"><p>  x</p></div>");
}

TEST(HtmlPrinterMinify, NonConditionalCommentsDropped)
{
    auto c = Parse("<p>x</p><!-- note -->");
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::Print(*c.document, options),
              "<html><head></head><body><p>x</p></body></html>");
}

TEST(HtmlPrinterMinify, ConditionalCommentsKept)
{
    auto c = Parse("<!--[if IE]>x<![endif]--><p>y</p>");
    html::PrinterOptions options;
    options.minify = true;
    const std::string printed = html::Print(*c.document, options);
    EXPECT_EQ(printed.find("<!--[if IE]>x<![endif]-->"), size_t{0});
    EXPECT_NE(printed.find("<p>y</p>"), std::string::npos);
}

TEST(HtmlPrinterMinify, WhitespaceBetweenBlocksCollapsed)
{
    auto c = Parse("<!DOCTYPE html><body><p>x</p>   <p>y</p></body>");
    html::PrinterOptions options;
    options.minify = true;
    EXPECT_EQ(html::Print(*c.document, options),
              "<!DOCTYPE html><html><head></head><body><p>x</p><p>y</p></body></html>");
}

// ---------------------------------------------------------------------------
// Pretty printing
// ---------------------------------------------------------------------------

TEST(HtmlPrinterPretty, DocumentLaidOutWithIndent)
{
    auto c = Parse("<!DOCTYPE html><html><head></head><body><p>x</p></body></html>");
    html::PrinterOptions options;
    options.pretty_print = true;
    const std::string expected =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "  <head></head>\n"
        "  <body>\n"
        "    <p>x</p>\n"
        "  </body>\n"
        "</html>";
    EXPECT_EQ(html::Print(*c.document, options), expected);
}

TEST(HtmlPrinterPretty, TextOnlyElementStaysOnOneLine)
{
    auto c = Parse("<p>hello</p>");
    const html::Node* p = FindTag(*c.document, "p");
    ASSERT_TRUE(p != nullptr);
    html::PrinterOptions options;
    options.pretty_print = true;
    EXPECT_EQ(html::PrintOuter(*p, options), "<p>hello</p>");
}

TEST(HtmlPrinterPretty, TitleStaysOnOneLine)
{
    auto c = Parse("<title>Docs</title>");
    const html::Node* title = FindTag(*c.document, "title");
    ASSERT_TRUE(title != nullptr);
    html::PrinterOptions options;
    options.pretty_print = true;
    EXPECT_EQ(html::PrintOuter(*title, options), "<title>Docs</title>");
}

TEST(HtmlPrinterPretty, ScriptCodeReindented)
{
    auto c = Parse("<script>var a = 1;\nvar b = 2;</script>");
    const html::Node* script = FindTag(*c.document, "script");
    ASSERT_TRUE(script != nullptr);
    html::PrinterOptions options;
    options.pretty_print = true;
    EXPECT_EQ(html::PrintOuter(*script, options),
              "<script>\n  var a = 1;\n  var b = 2;\n</script>");
}

TEST(HtmlPrinterPretty, PreContentKeptVerbatim)
{
    auto c = Parse("<pre>line1\n  line2</pre>");
    const html::Node* pre = FindTag(*c.document, "pre");
    ASSERT_TRUE(pre != nullptr);
    html::PrinterOptions options;
    options.pretty_print = true;
    EXPECT_EQ(html::PrintOuter(*pre, options), "<pre>line1\n  line2</pre>");
}

TEST(HtmlPrinterPretty, NestedBlocksIndent)
{
    auto c = Parse("<div><div><p>x</p></div></div>");
    const html::Node* div = FindTag(*c.document, "div");
    ASSERT_TRUE(div != nullptr);
    html::PrinterOptions options;
    options.pretty_print = true;
    const std::string expected =
        "<div>\n"
        "  <div>\n"
        "    <p>x</p>\n"
        "  </div>\n"
        "</div>";
    EXPECT_EQ(html::PrintOuter(*div, options), expected);
}

TEST(HtmlPrinterPretty, WhitespaceOnlyTextDropped)
{
    auto c = Parse("<!DOCTYPE html><body> <p>x</p> </body></html>");
    html::PrinterOptions options;
    options.pretty_print = true;
    const std::string expected =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "  <head></head>\n"
        "  <body>\n"
        "    <p>x</p>\n"
        "  </body>\n"
        "</html>";
    EXPECT_EQ(html::Print(*c.document, options), expected);
}

// ---------------------------------------------------------------------------
// Determinism and mode independence
// ---------------------------------------------------------------------------

TEST(HtmlPrinterStability, RepeatedPrintingIsIdentical)
{
    auto c = Parse("<div class=\"c\"><p>hi</p></div>");
    const html::Node* div = FindTag(*c.document, "div");
    ASSERT_TRUE(div != nullptr);
    const std::string first = html::PrintOuter(*div);
    const std::string second = html::PrintOuter(*div);
    EXPECT_EQ(first, second);
}

TEST(HtmlPrinterStability, MinifyAndDefaultDiffer)
{
    auto c = Parse("<img src=\"app.js\" alt=\"\">");
    const html::Node* img = FindTag(*c.document, "img");
    ASSERT_TRUE(img != nullptr);
    html::PrinterOptions minified;
    minified.minify = true;
    EXPECT_EQ(html::PrintOuter(*img), "<img src=\"app.js\" alt=\"\">");
    EXPECT_EQ(html::PrintOuter(*img, minified), "<img src=app.js alt>");
}

} // namespace