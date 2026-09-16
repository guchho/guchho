#include "test/guchho_test.hpp"

#include "guchho/css/css_parser.hpp"
#include "guchho/css/css_printer.hpp"
#include "guchho/compiler.hpp"
#include "guchho/sourcemap.hpp"

#include <string>
#include <utility>
#include <vector>

namespace css = guchho::css;
namespace compiler = guchho::compiler;
namespace config = guchho::config;
namespace sourcemap = guchho::sourcemap;

using guchho::logger::DeferLogKind;
using guchho::logger::Log;
using guchho::logger::NewDeferLog;
using guchho::logger::Source;

namespace {

css::PrintResult PrintCss(std::string contents, const css::PrinterOptions& print_options,
                          const css::ParserOptions& parse_options = {}) {
    Source source;
    source.contents = std::move(contents);
    Log log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    css::AST ast = css::Parse(log, source, parse_options);

    compiler::SymbolMap symbols = compiler::NewSymbolMap(1);
    symbols.symbols_for_source[0] = std::move(ast.symbols);

    return css::Print(ast, symbols, print_options);
}

css::PrinterOptions MinifyPrintOptions() {
    css::PrinterOptions options;
    options.minify_whitespace = true;
    return options;
}

css::ParserOptions MinifyParseOptions() {
    css::ParserOptions options;
    options.minify_whitespace = true;
    return options;
}

} // namespace

// ---------------------------------------------------------------------------
// Basic rules
// ---------------------------------------------------------------------------

TEST(CssPrinter, EmptyStylesheet)
{
    css::PrintResult result = PrintCss("", {});
    EXPECT_EQ(result.css, "");
    EXPECT_TRUE(result.extracted_legal_comments.empty());
    EXPECT_TRUE(result.json_metadata_imports.empty());
}

TEST(CssPrinter, ClassSelector)
{
    css::PrintResult result = PrintCss(".a { color: red; }", {});
    EXPECT_EQ(result.css, ".a {\n  color: red;\n}\n");
}

TEST(CssPrinter, IdSelector)
{
    css::PrintResult result = PrintCss("#a { color: red; }", {});
    EXPECT_EQ(result.css, "#a {\n  color: red;\n}\n");
}

TEST(CssPrinter, ElementSelector)
{
    css::PrintResult result = PrintCss("a { color: red; }", {});
    EXPECT_EQ(result.css, "a {\n  color: red;\n}\n");
}

TEST(CssPrinter, TypeSelectorWithClass)
{
    css::PrintResult result = PrintCss("a.b { color: red; }", {});
    EXPECT_EQ(result.css, "a.b {\n  color: red;\n}\n");
}

TEST(CssPrinter, MultipleDeclarations)
{
    css::PrintResult result = PrintCss("a { color: red; background: blue; }", {});
    EXPECT_EQ(result.css, "a {\n  color: red;\n  background: blue;\n}\n");
}

TEST(CssPrinter, MissingTrailingSemicolon)
{
    css::PrintResult result = PrintCss("a { color: red }", {});
    EXPECT_EQ(result.css, "a {\n  color: red;\n}\n");
}

TEST(CssPrinter, EmptyRuleBody)
{
    css::PrintResult result = PrintCss("a { }", {});
    EXPECT_EQ(result.css, "a {\n}\n");
}

TEST(CssPrinter, TwoRules)
{
    css::PrintResult result = PrintCss(".a { color: red; }\n.b { color: blue; }", {});
    EXPECT_EQ(result.css, ".a {\n  color: red;\n}\n.b {\n  color: blue;\n}\n");
}

// ---------------------------------------------------------------------------
// Combinators and selector lists
// ---------------------------------------------------------------------------

TEST(CssPrinter, DescendantCombinator)
{
    css::PrintResult result = PrintCss("a b { color: red; }", {});
    EXPECT_EQ(result.css, "a b {\n  color: red;\n}\n");
}

TEST(CssPrinter, ChildCombinator)
{
    css::PrintResult result = PrintCss("a > b { color: red; }", {});
    EXPECT_EQ(result.css, "a > b {\n  color: red;\n}\n");
}

TEST(CssPrinter, AdjacentSiblingCombinator)
{
    css::PrintResult result = PrintCss("a + b { color: red; }", {});
    EXPECT_EQ(result.css, "a + b {\n  color: red;\n}\n");
}

TEST(CssPrinter, GeneralSiblingCombinator)
{
    css::PrintResult result = PrintCss("a ~ b { color: red; }", {});
    EXPECT_EQ(result.css, "a ~ b {\n  color: red;\n}\n");
}

TEST(CssPrinter, SelectorList)
{
    css::PrintResult result = PrintCss("a, b { color: red; }", {});
    EXPECT_EQ(result.css, "a,\nb {\n  color: red;\n}\n");
}

TEST(CssPrinter, AttributeSelector)
{
    css::PrintResult result = PrintCss("[data-x=\"y\"] { color: red; }", {});
    EXPECT_EQ(result.css, "[data-x=y] {\n  color: red;\n}\n");
}

TEST(CssPrinter, AttributeSelectorQuotedValue)
{
    css::PrintResult result = PrintCss("[data-x=\"a b\"] { color: red; }", {});
    EXPECT_EQ(result.css, "[data-x=\"a b\"] {\n  color: red;\n}\n");
}

TEST(CssPrinter, AttributeSelectorModifier)
{
    css::PrintResult result = PrintCss("[data-x=\"y\" i] { color: red; }", {});
    EXPECT_EQ(result.css, "[data-x=y i] {\n  color: red;\n}\n");
}

TEST(CssPrinter, PseudoClass)
{
    css::PrintResult result = PrintCss("a:hover { color: red; }", {});
    EXPECT_EQ(result.css, "a:hover {\n  color: red;\n}\n");
}

TEST(CssPrinter, PseudoElement)
{
    css::PrintResult result = PrintCss("a::before { content: \"x\"; }", {});
    EXPECT_EQ(result.css, "a::before {\n  content: \"x\";\n}\n");
}

TEST(CssPrinter, PseudoNot)
{
    css::PrintResult result = PrintCss("a:not(.b) { color: red; }", {});
    EXPECT_EQ(result.css, "a:not(.b) {\n  color: red;\n}\n");
}

TEST(CssPrinter, PseudoIsWithSelectorList)
{
    css::PrintResult result = PrintCss("a:is(.b, .c) { color: red; }", {});
    EXPECT_EQ(result.css, "a:is(.b, .c) {\n  color: red;\n}\n");
}

TEST(CssPrinter, PseudoNthChild)
{
    css::PrintResult result = PrintCss("a:nth-child(2n+1) { color: red; }", {});
    EXPECT_EQ(result.css, "a:nth-child(2n+1) {\n  color: red;\n}\n");
}

TEST(CssPrinter, NamespacedTypeSelector)
{
    css::PrintResult result = PrintCss("svg|circle { fill: red; }", {});
    EXPECT_EQ(result.css, "svg|circle {\n  fill: red;\n}\n");
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

TEST(CssPrinter, Dimension)
{
    css::PrintResult result = PrintCss("a { width: 10px; }", {});
    EXPECT_EQ(result.css, "a {\n  width: 10px;\n}\n");
}

TEST(CssPrinter, SpaceSeparatedValues)
{
    css::PrintResult result = PrintCss("a { padding: 1px 2px; }", {});
    EXPECT_EQ(result.css, "a {\n  padding: 1px 2px;\n}\n");
}

TEST(CssPrinter, StringValue)
{
    css::PrintResult result = PrintCss("a { content: \"x\"; }", {});
    EXPECT_EQ(result.css, "a {\n  content: \"x\";\n}\n");
}

TEST(CssPrinter, UrlValue)
{
    css::PrintResult result = PrintCss("a { background: url(\"b.png\"); }", {});
    EXPECT_EQ(result.css, "a {\n  background: url(b.png);\n}\n");
}

TEST(CssPrinter, HexColor)
{
    css::PrintResult result = PrintCss("a { color: #fff; }", {});
    EXPECT_EQ(result.css, "a {\n  color: #fff;\n}\n");
}

TEST(CssPrinter, Important)
{
    css::PrintResult result = PrintCss("a { color: red !important; }", {});
    EXPECT_EQ(result.css, "a {\n  color: red !important;\n}\n");
}

TEST(CssPrinter, CustomProperty)
{
    css::PrintResult result = PrintCss("a { --foo: bar; }", {});
    EXPECT_EQ(result.css, "a {\n  --foo: bar;\n}\n");
}

TEST(CssPrinter, DebugTransitionTokens)
{
    Source source;
    source.contents = "a { transition: a, b, c; }";
    Log log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    css::AST ast = css::Parse(log, source, {});
    for (const css::Rule& rule : ast.rules) {
        if (auto* r = dynamic_cast<css::RSelector*>(rule.data.get())) {
            for (const css::Rule& inner : r->rules) {
                if (auto* d = dynamic_cast<css::RDeclaration*>(inner.data.get())) {
                    for (const css::Token& t : d->value) {
                        std::string ws = t.whitespace == css::WhitespaceFlags::kWhitespaceBefore ? "B" : ".";
                    }
                }
            }
        }
    }
}

TEST(CssPrinter, DeclarationCommaList)
{
    css::PrintResult result = PrintCss("a { transition: a, b, c; }", {});
    EXPECT_EQ(result.css, "a {\n  transition:\n    a,\n    b,\n    c;\n}\n");
}

TEST(CssPrinter, GradientFunction)
{
    css::PrintResult result = PrintCss("a { background: linear-gradient(red, green, blue); }", {});
    EXPECT_EQ(result.css, "a {\n  background:\n    linear-gradient(\n      red,\n      green,\n      blue);\n}\n");
}

// ---------------------------------------------------------------------------
// At-rules
// ---------------------------------------------------------------------------

TEST(CssPrinter, AtCharset)
{
    css::PrintResult result = PrintCss("@charset \"UTF-8\";", {});
    EXPECT_EQ(result.css, "@charset \"UTF-8\";\n");
}

TEST(CssPrinter, AtImportString)
{
    css::PrintResult result = PrintCss("@import \"a.css\";", {});
    EXPECT_EQ(result.css, "@import \"a.css\";\n");
}

TEST(CssPrinter, AtImportUrl)
{
    css::PrintResult result = PrintCss("@import url(\"a.css\");", {});
    EXPECT_EQ(result.css, "@import \"a.css\";\n");
}

TEST(CssPrinter, AtImportWithMediaQuery)
{
    css::PrintResult result = PrintCss("@import \"a.css\" screen and (min-width: 100px);", {});
    EXPECT_EQ(result.css, "@import \"a.css\" screen and (min-width: 100px);\n");
}

TEST(CssPrinter, AtImportWithLayerAndSupports)
{
    css::PrintResult result = PrintCss("@import \"a.css\" layer(base) supports(display: grid);", {});
    EXPECT_EQ(result.css, "@import \"a.css\" layer(base) supports(display: grid);\n");
}

TEST(CssPrinter, AtKeyframes)
{
    css::PrintResult result = PrintCss("@keyframes foo { from { opacity: 0; } to { opacity: 1; } }", {});
    EXPECT_EQ(result.css, "@keyframes foo {\n  from {\n    opacity: 0;\n  }\n  to {\n    opacity: 1;\n  }\n}\n");
}

TEST(CssPrinter, AtFontFace)
{
    css::PrintResult result = PrintCss("@font-face { font-family: \"x\"; }", {});
    EXPECT_EQ(result.css, "@font-face {\n  font-family: \"x\";\n}\n");
}

TEST(CssPrinter, AtSupports)
{
    css::PrintResult result = PrintCss("@supports (display: grid) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@supports (display: grid) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtMediaType)
{
    css::PrintResult result = PrintCss("@media screen { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@media screen {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtMediaNotType)
{
    css::PrintResult result = PrintCss("@media not screen { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@media not screen {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtMediaOnlyType)
{
    css::PrintResult result = PrintCss("@media only screen { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@media only screen {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtMediaNotCondition)
{
    css::PrintResult result = PrintCss("@media not (color) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@media not (color) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtMediaPlainFeature)
{
    css::PrintResult result = PrintCss("@media (min-width: 100px) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@media (min-width: 100px) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtMediaAnd)
{
    css::PrintResult result = PrintCss("@media (width: 100px) and (height: 200px) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@media (width: 100px) and (height: 200px) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtMediaOr)
{
    css::PrintResult result = PrintCss("@media (width: 100px) or (height: 200px) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@media (width: 100px) or (height: 200px) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtMediaRange)
{
    css::PrintResult result = PrintCss("@media (width >= 100px) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@media (width >= 100px) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtMediaFullRange)
{
    css::PrintResult result = PrintCss("@media (400px <= width < 700px) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@media (400px <= width < 700px) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtLayerStatement)
{
    css::PrintResult result = PrintCss("@layer a, b;", {});
    EXPECT_EQ(result.css, "@layer a, b;\n");
}

TEST(CssPrinter, AtLayerDottedName)
{
    css::PrintResult result = PrintCss("@layer a.b.c;", {});
    EXPECT_EQ(result.css, "@layer a.b.c;\n");
}

TEST(CssPrinter, AtLayerBlock)
{
    css::PrintResult result = PrintCss("@layer a { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@layer a {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtScope)
{
    css::PrintResult result = PrintCss("@scope (.a) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@scope (.a) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtScopeTo)
{
    css::PrintResult result = PrintCss("@scope (.a) to (.b) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@scope (.a) to (.b) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, AtContainer)
{
    css::PrintResult result = PrintCss("@container sidebar (min-width: 100px) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@container sidebar (min-width: 100px) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, UnknownAtRuleStatement)
{
    css::PrintResult result = PrintCss("@foo bar;", {});
    EXPECT_EQ(result.css, "@foo bar;\n");
}

TEST(CssPrinter, UnknownAtRuleBlock)
{
    css::PrintResult result = PrintCss("@foo bar { baz: qux; }", {});
    EXPECT_EQ(result.css, "@foo bar { baz: qux; }\n");
}

// ---------------------------------------------------------------------------
// Comments
// ---------------------------------------------------------------------------

TEST(CssPrinter, InlineLegalComment)
{
    css::PrintResult result = PrintCss("/*! hi */\n.a { color: red; }", {});
    EXPECT_EQ(result.css, "/*! hi */\n.a {\n  color: red;\n}\n");
    EXPECT_TRUE(result.extracted_legal_comments.empty());
}

TEST(CssPrinter, CommentInsideRule)
{
    // Comments inside a declaration list are not represented in the AST, so
    // they are dropped by the parse -> print round trip.
    css::PrintResult result = PrintCss("a { /*! hi */ color: red; }", {});
    EXPECT_EQ(result.css, "a {\n  color: red;\n}\n");
}

TEST(CssPrinter, MultiLineCommentTopLevel)
{
    css::PrintResult result = PrintCss("/*! hello\n  world */\na { color: red; }", {});
    EXPECT_EQ(result.css, "/*! hello\n  world */\na {\n  color: red;\n}\n");
}

TEST(CssPrinter, MultiLineCommentReindented)
{
    // Build the AST by hand to place an "RComment" rule inside a rule body,
    // which is what exercises comment re-indentation in the printer.
    css::AST ast;

    css::Rule comment_rule;
    auto r_comment = std::make_shared<css::RComment>();
    r_comment->text = "/*! hello\nworld */";
    comment_rule.data = r_comment;

    css::Rule decl_rule;
    auto r_decl = std::make_shared<css::RDeclaration>();
    r_decl->key_text = "color";
    css::Token t;
    t.kind = css::TokenType::kIdent;
    t.text = "red";
    t.whitespace = css::WhitespaceFlags::kWhitespaceBefore;
    r_decl->value.push_back(std::move(t));
    decl_rule.data = r_decl;

    css::Rule sel_rule;
    auto r_sel = std::make_shared<css::RSelector>();
    css::CompoundSelector compound;
    compound.type_selector = std::make_shared<css::NamespacedName>();
    compound.type_selector->name.text = "a";
    compound.type_selector->name.kind = css::TokenType::kIdent;
    css::ComplexSelector complex;
    complex.selectors.push_back(std::move(compound));
    r_sel->selectors.push_back(std::move(complex));
    r_sel->rules.push_back(std::move(comment_rule));
    r_sel->rules.push_back(std::move(decl_rule));
    sel_rule.data = r_sel;

    ast.rules.push_back(std::move(sel_rule));
    compiler::SymbolMap symbols = compiler::NewSymbolMap(1);

    css::PrintResult result = css::Print(ast, symbols, {});
    EXPECT_EQ(result.css, "a {\n  /*! hello\n  world */\n  color: red;\n}\n");
}

TEST(CssPrinter, LegalCommentsNone)
{
    css::PrinterOptions options;
    options.legal_comments = config::LegalComments::kNone;
    css::PrintResult result = PrintCss("/*! hi */\n.a { color: red; }", options);
    EXPECT_EQ(result.css, ".a {\n  color: red;\n}\n");
    EXPECT_TRUE(result.extracted_legal_comments.empty());
}

TEST(CssPrinter, LegalCommentsExtracted)
{
    css::PrinterOptions options;
    options.legal_comments = config::LegalComments::kEndOfFile;
    css::PrintResult result = PrintCss("/*! hi */\n.a { color: red; }", options);
    EXPECT_EQ(result.css, ".a {\n  color: red;\n}\n");
    ASSERT_EQ(result.extracted_legal_comments.size(), 1u);
    EXPECT_EQ(result.extracted_legal_comments[0], "/*! hi */");
}

TEST(CssPrinter, LegalCommentsDeduplicated)
{
    css::PrinterOptions options;
    options.legal_comments = config::LegalComments::kEndOfFile;
    css::PrintResult result = PrintCss("/*! hi */\n.a { color: red; }\n/*! hi */\n.b { color: blue; }", options);
    EXPECT_EQ(result.extracted_legal_comments.size(), 1u);
}

// ---------------------------------------------------------------------------
// Minification
// ---------------------------------------------------------------------------

TEST(CssPrinter, MinifySimple)
{
    css::PrintResult result = PrintCss("a { color: red; }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "a{color:red}");
}

TEST(CssPrinter, MinifyMultipleDeclarations)
{
    css::PrintResult result = PrintCss("a { color: red; background: blue; }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "a{color:red;background:blue}");
}

TEST(CssPrinter, MinifyImportant)
{
    css::PrintResult result = PrintCss("a { color: red !important; }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "a{color:red!important}");
}

TEST(CssPrinter, MinifySelectorList)
{
    css::PrintResult result = PrintCss("a, b { color: red; }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "a,b{color:red}");
}

TEST(CssPrinter, MinifyEmptyRule)
{
    css::PrintResult result = PrintCss("a { }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "a{}");
}

TEST(CssPrinter, MinifyKeyframes)
{
    css::PrintResult result = PrintCss("@keyframes foo { from { opacity: 0; } to { opacity: 1; } }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "@keyframes foo{from{opacity:0}to{opacity:1}}");
}

TEST(CssPrinter, MinifyMedia)
{
    css::PrintResult result = PrintCss("@media screen { a { color: red; } }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "@media screen{a{color:red}}");
}

TEST(CssPrinter, MinifyImport)
{
    css::PrintResult result = PrintCss("@import \"a.css\" screen;", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "@import\"a.css\"screen;");
}

TEST(CssPrinter, MinifyDimension)
{
    css::PrintResult result = PrintCss("a { width: 10px; }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "a{width:10px}");
}

// ---------------------------------------------------------------------------
// Escaping
// ---------------------------------------------------------------------------

TEST(CssPrinter, EscapeIdentifier)
{
    css::PrintResult result = PrintCss(".foo\\:bar { color: red; }", {});
    EXPECT_EQ(result.css, ".foo\\:bar {\n  color: red;\n}\n");
}

TEST(CssPrinter, ASCIIOnlyString)
{
    css::PrinterOptions options;
    options.ascii_only = true;
    css::PrintResult result = PrintCss("a { content: \"\xCF\x80\"; }", options);
    EXPECT_EQ(result.css, "a {\n  content: \"\\3c0\";\n}\n");
}

TEST(CssPrinter, ASCIIOnlyIdent)
{
    css::PrinterOptions options;
    options.ascii_only = true;
    css::PrintResult result = PrintCss(".\\791  { color: red; }", options);
    EXPECT_EQ(result.css, ".\\791  {\n  color: red;\n}\n");
}

// ---------------------------------------------------------------------------
// Line limits
// ---------------------------------------------------------------------------

TEST(CssPrinter, LineLimitWrapString)
{
    css::PrinterOptions options;
    options.line_limit = 40;
    css::PrintResult result = PrintCss("a { content: \"00000000000000000000000000000000000000000000000000000000\"; }", options);
    EXPECT_EQ(result.css, "a {\n  content: \"0000000000000000000000000000\\\n0000000000000000000000000000\";\n}\n");
}

// ---------------------------------------------------------------------------
// Symbols and renaming
// ---------------------------------------------------------------------------

TEST(CssPrinter, RenameLocalSymbol)
{
    css::PrinterOptions options;
    options.local_names[compiler::Ref{0, 0}] = "b";
    css::PrintResult result = PrintCss(".a { color: red; }", options);
    EXPECT_EQ(result.css, ".b {\n  color: red;\n}\n");
}

// ---------------------------------------------------------------------------
// Source maps
// ---------------------------------------------------------------------------

TEST(CssPrinter, NoSourceMapByDefault)
{
    css::PrintResult result = PrintCss(".a { color: red; }", {});
    EXPECT_TRUE(result.source_map_chunk.buffer.data.empty());
}

TEST(CssPrinter, SourceMapChunkGenerated)
{
    std::string contents = ".a { color: red; }";
    css::PrinterOptions options;
    options.source_map = config::SourceMap::kExternalWithoutComment;
    options.add_source_mappings = true;
    options.line_offset_tables = sourcemap::GenerateLineOffsetTables(contents, 1);
    css::PrintResult result = PrintCss(contents, options);
    EXPECT_FALSE(result.source_map_chunk.buffer.data.empty());
    EXPECT_FALSE(result.source_map_chunk.should_ignore);
}

// ---------------------------------------------------------------------------
// Metafile
// ---------------------------------------------------------------------------

TEST(CssPrinter, NoMetafileByDefault)
{
    css::PrintResult result = PrintCss("@import \"a.css\";", {});
    EXPECT_TRUE(result.json_metadata_imports.empty());
}

TEST(CssPrinter, MetafileImport)
{
    css::PrinterOptions options;
    options.needs_metafile = true;
    css::PrintResult result = PrintCss("@import \"a.css\";", options);
    ASSERT_EQ(result.json_metadata_imports.size(), 1u);
    EXPECT_EQ(result.json_metadata_imports[0], "\n        {\n          \"path\": \"a.css\",\n          \"kind\": \"import-rule\",\n          \"external\": true\n        }");
}

TEST(CssPrinter, MetafileUrlToken)
{
    css::PrinterOptions options;
    options.needs_metafile = true;
    css::PrintResult result = PrintCss("a { background: url(b.png); }", options);
    ASSERT_EQ(result.json_metadata_imports.size(), 1u);
    EXPECT_EQ(result.json_metadata_imports[0], "\n        {\n          \"path\": \"b.png\",\n          \"kind\": \"url-token\",\n          \"external\": true\n        }");
}

TEST(CssPrinter, MetafileMinified)
{
    css::PrinterOptions options;
    options.needs_metafile = true;
    options.metafile_format = config::MetafileFormat::kMinified;
    css::PrintResult result = PrintCss("@import \"a.css\";", options);
    ASSERT_EQ(result.json_metadata_imports.size(), 1u);
    EXPECT_EQ(result.json_metadata_imports[0], "{\"path\":\"a.css\",\"kind\":\"import-rule\",\"external\":true}");
}

// ---------------------------------------------------------------------------
// PrintResult fields
// ---------------------------------------------------------------------------

TEST(CssPrinter, PrintResultAllFields)
{
    // Exercise every field of css::PrintResult at once: the CSS text, the
    // extracted legal comments, the metafile import records, and the source
    // map chunk.
    std::string contents = "/*! hi */\n@import \"a.css\";\n.a { color: red; }";

    css::PrinterOptions options;
    options.legal_comments = config::LegalComments::kEndOfFile;
    options.needs_metafile = true;
    options.source_map = config::SourceMap::kExternalWithoutComment;
    options.add_source_mappings = true;
    options.line_offset_tables = sourcemap::GenerateLineOffsetTables(contents, 1);

    css::PrintResult result = PrintCss(contents, options);

    EXPECT_EQ(result.css, "@import \"a.css\";\n.a {\n  color: red;\n}\n");

    ASSERT_EQ(result.extracted_legal_comments.size(), 1u);
    EXPECT_EQ(result.extracted_legal_comments[0], "/*! hi */");

    ASSERT_EQ(result.json_metadata_imports.size(), 1u);
    EXPECT_EQ(result.json_metadata_imports[0], "\n        {\n          \"path\": \"a.css\",\n          \"kind\": \"import-rule\",\n          \"external\": true\n        }");

    EXPECT_FALSE(result.source_map_chunk.buffer.data.empty());
    EXPECT_FALSE(result.source_map_chunk.should_ignore);
}

// ---------------------------------------------------------------------------
// Professional round-trip cases (expected output validated against esbuild)
// ---------------------------------------------------------------------------

TEST(CssPrinter, NestedRule)
{
    css::PrintResult result = PrintCss("a { b { color: red; } }", {});
    EXPECT_EQ(result.css, "a {\n  b {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, DeepNesting)
{
    css::PrintResult result = PrintCss("a { b { c { color: red } } }", {});
    EXPECT_EQ(result.css, "a {\n  b {\n    c {\n      color: red;\n    }\n  }\n}\n");
}

TEST(CssPrinter, CombinatorChain)
{
    css::PrintResult result = PrintCss("a > b + c ~ d { color: red; }", {});
    EXPECT_EQ(result.css, "a > b + c ~ d {\n  color: red;\n}\n");
}

TEST(CssPrinter, PseudoElementAfterClass)
{
    css::PrintResult result = PrintCss("a:hover::before { content: \"x\"; }", {});
    EXPECT_EQ(result.css, "a:hover::before {\n  content: \"x\";\n}\n");
}

TEST(CssPrinter, AttributeMatcherAsIdent)
{
    css::PrintResult result = PrintCss("a[href^=\"https\"]::after { color: red; }", {});
    EXPECT_EQ(result.css, "a[href^=https]::after {\n  color: red;\n}\n");
}

TEST(CssPrinter, AttributePresence)
{
    css::PrintResult result = PrintCss("a[disabled] { color: red; }", {});
    EXPECT_EQ(result.css, "a[disabled] {\n  color: red;\n}\n");
}

TEST(CssPrinter, MediaQueryCommaList)
{
    css::PrintResult result = PrintCss("@media screen and (min-width: 100px), print { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@media screen and (min-width: 100px), print {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, SupportsSelectorFunction)
{
    css::PrintResult result = PrintCss("@supports selector(:has(a)) { a { color: red; } }", {});
    EXPECT_EQ(result.css, "@supports selector(:has(a)) {\n  a {\n    color: red;\n  }\n}\n");
}

TEST(CssPrinter, ImportUrlToQuotedString)
{
    css::PrintResult result = PrintCss("@import url(\"a b.css\");", {});
    EXPECT_EQ(result.css, "@import \"a b.css\";\n");
}

TEST(CssPrinter, UrlTokenEscapesSpace)
{
    css::PrintResult result = PrintCss("a { background: url(\"a b.png\"); }", {});
    EXPECT_EQ(result.css, "a {\n  background: url(a\\ b.png);\n}\n");
}

TEST(CssPrinter, CalcFunctionRoundtrip)
{
    css::PrintResult result = PrintCss("a { width: calc(100% - 20px); }", {});
    EXPECT_EQ(result.css, "a {\n  width: calc(100% - 20px);\n}\n");
}

TEST(CssPrinter, FloatDimension)
{
    css::PrintResult result = PrintCss("a { font-size: 1.5em; }", {});
    EXPECT_EQ(result.css, "a {\n  font-size: 1.5em;\n}\n");
}

TEST(CssPrinter, TwoArgumentFunctionStaysSingleLine)
{
    css::PrintResult result = PrintCss("a { transform: translate(10px, 20px); }", {});
    EXPECT_EQ(result.css, "a {\n  transform: translate(10px, 20px);\n}\n");
}

TEST(CssPrinter, BoxShadowTwoItemsStaysSingleLine)
{
    css::PrintResult result = PrintCss("a { box-shadow: 0 0 1px #000, 1px 1px 2px rgba(0, 0, 0, 0.5); }", {});
    EXPECT_EQ(result.css, "a {\n  box-shadow: 0 0 1px #000, 1px 1px 2px rgba(0, 0, 0, 0.5);\n}\n");
}

TEST(CssPrinter, ShorthandDeclarationGroup)
{
    css::PrintResult result = PrintCss("a { margin: 0; padding: 1px 2px 3px 4px; border: 1px solid red; }", {});
    EXPECT_EQ(result.css, "a {\n  margin: 0;\n  padding: 1px 2px 3px 4px;\n  border: 1px solid red;\n}\n");
}

TEST(CssPrinter, KeyframesPercentageSelectors)
{
    css::PrintResult result = PrintCss("@keyframes foo { 0% { opacity: 0; } 100% { opacity: 1; } }", {});
    EXPECT_EQ(result.css, "@keyframes foo {\n  0% {\n    opacity: 0;\n  }\n  100% {\n    opacity: 1;\n  }\n}\n");
}

TEST(CssPrinter, RgbFunctionSingleLine)
{
    css::PrintResult result = PrintCss("a { color: rgb(1, 2, 3); }", {});
    EXPECT_EQ(result.css, "a {\n  color: rgb(1, 2, 3);\n}\n");
}

// Minified variants, also validated against esbuild

TEST(CssPrinter, MinifyNestedRule)
{
    css::PrintResult result = PrintCss("a { b { color: red; } }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "a{b{color:red}}");
}

TEST(CssPrinter, MinifyKeyframesPercentageSelectors)
{
    css::ParserOptions parse_options = MinifyParseOptions();
    parse_options.minify_syntax = true;
    css::PrintResult result = PrintCss("@keyframes foo { 0% { opacity: 0 } 100% { opacity: 1 } }", MinifyPrintOptions(), parse_options);
    EXPECT_EQ(result.css, "@keyframes foo{0%{opacity:0}to{opacity:1}}");
}

TEST(CssPrinter, MinifyMediaQueryCommaList)
{
    css::PrintResult result = PrintCss("@media screen and (min-width: 100px), print { a, b { color: red } }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "@media screen and (min-width:100px),print{a,b{color:red}}");
}

TEST(CssPrinter, MinifyUrlTokenEscapesSpace)
{
    css::PrintResult result = PrintCss("a { background: url(\"a b.png\"); }", MinifyPrintOptions(), MinifyParseOptions());
    EXPECT_EQ(result.css, "a{background:url(a\\ b.png)}");
}