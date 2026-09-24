// Tests for the CSS linking pass (linker_css.cpp).

#include "test/guchho_test.hpp"

#include "guchho/compiler.hpp"
#include "guchho/css/css_ast.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/graph.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/linker.hpp"
#include "guchho/logger.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace g = ::guchho::graph;
namespace css = ::guchho::css;
namespace cm = ::guchho::compiler;
namespace js = ::guchho::javascript;
namespace lnk = ::guchho::linker;

namespace {

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

g::LinkerGraph MakeLinkerGraph(uint32_t source_count)
{
    g::LinkerGraph graph;
    graph.files.resize(source_count);
    return graph;
}

void SetJS(g::LinkerGraph& graph, uint32_t source_index, std::shared_ptr<g::JSRepr> repr)
{
    graph.files[source_index].input_file.repr = std::move(repr);
}

void SetCSS(g::LinkerGraph& graph, uint32_t source_index, std::shared_ptr<g::CSSRepr> repr)
{
    graph.files[source_index].input_file.repr = std::move(repr);
}

std::shared_ptr<g::JSRepr> MakeJSRepr()
{
    return std::make_shared<g::JSRepr>();
}

std::shared_ptr<g::CSSRepr> MakeCSSRepr()
{
    return std::make_shared<g::CSSRepr>();
}

// Appends a live part to the JS AST that statically imports "target".
void AddPartWithImport(g::JSRepr& repr, uint32_t target)
{
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(target);
    record.kind = cm::ImportKind::kStmt;
    repr.ast.import_records.push_back(record);

    js::Part part;
    part.is_live = true;
    part.import_record_indices.push_back(static_cast<uint32_t>(repr.ast.import_records.size() - 1));
    repr.ast.parts.push_back(std::move(part));
}

css::Token IdentToken(std::string text)
{
    css::Token token;
    token.kind = css::TokenType::kIdent;
    token.text = std::move(text);
    return token;
}

// Builds an ImportConditions from ident-token lists. Media queries stay empty;
// the conditions differ only by layer names and supports tokens.
css::ImportConditions MakeConditions(
    std::vector<std::string> layer_texts = {},
    std::vector<std::string> supports_texts = {})
{
    css::ImportConditions conditions;
    for (auto& text : layer_texts) {
        conditions.layers.push_back(IdentToken(text));
    }
    for (auto& text : supports_texts) {
        conditions.supports.push_back(IdentToken(text));
    }
    return conditions;
}

css::Rule MakeNamedRule(const char* at_token)
{
    auto known = std::make_shared<css::RKnownAt>();
    known->at_token = at_token;
    css::Rule rule;
    rule.data = known;
    return rule;
}

css::Rule MakeImportRule(uint32_t record_index)
{
    auto import = std::make_shared<css::RAtImport>();
    import->import_record_index = record_index;
    css::Rule rule;
    rule.data = import;
    return rule;
}

css::RKnownAt* AsKnownAt(css::Rule& rule)
{
    return dynamic_cast<css::RKnownAt*>(rule.data.get());
}

} // namespace

// ---------------------------------------------------------------------------
// LinkerContext::ImportConditionsAreEqual
// ---------------------------------------------------------------------------

TEST(LinkerCSS, ImportConditionsAreEqualEmptyLists)
{
    lnk::LinkerContext c;
    EXPECT_TRUE(c.ImportConditionsAreEqual({}, {}));
}

TEST(LinkerCSS, ImportConditionsAreEqualIdenticalConditions)
{
    auto a = std::vector<css::ImportConditions>{MakeConditions({"theme", "base"}, {"display"})};
    auto b = std::vector<css::ImportConditions>{MakeConditions({"theme", "base"}, {"display"})};
    lnk::LinkerContext c;
    EXPECT_TRUE(c.ImportConditionsAreEqual(a, b));
}

TEST(LinkerCSS, ImportConditionsAreEqualIgnoresWhitespace)
{
    lnk::LinkerContext c;

    css::ImportConditions cond_a;
    cond_a.layers = {IdentToken("theme")};
    css::ImportConditions cond_b;
    css::Token with_ws = IdentToken("theme");
    with_ws.whitespace = css::WhitespaceFlags::kWhitespaceBefore;
    cond_b.layers = {with_ws};

    EXPECT_TRUE(c.ImportConditionsAreEqual({cond_a}, {cond_b}));
}

TEST(LinkerCSS, ImportConditionsAreEqualDetectsLayerDifference)
{
    auto a = std::vector<css::ImportConditions>{MakeConditions({"theme"})};
    auto b = std::vector<css::ImportConditions>{MakeConditions({"base"})};
    lnk::LinkerContext c;
    EXPECT_FALSE(c.ImportConditionsAreEqual(a, b));
}

TEST(LinkerCSS, ImportConditionsAreEqualDetectsLayerCountDifference)
{
    auto a = std::vector<css::ImportConditions>{MakeConditions({"theme"})};
    auto b = std::vector<css::ImportConditions>{MakeConditions({"theme", "base"})};
    lnk::LinkerContext c;
    EXPECT_FALSE(c.ImportConditionsAreEqual(a, b));
}

TEST(LinkerCSS, ImportConditionsAreEqualDetectsSupportsDifference)
{
    auto a = std::vector<css::ImportConditions>{MakeConditions({"theme"}, {"display:grid"})};
    auto b = std::vector<css::ImportConditions>{MakeConditions({"theme"}, {"display:flex"})};
    lnk::LinkerContext c;
    EXPECT_FALSE(c.ImportConditionsAreEqual(a, b));
}

TEST(LinkerCSS, ImportConditionsAreEqualDetectsListLengthDifference)
{
    auto a = std::vector<css::ImportConditions>{
        MakeConditions({"theme"}),
        MakeConditions({"base"}),
    };
    auto b = std::vector<css::ImportConditions>{MakeConditions({"theme"})};
    lnk::LinkerContext c;
    EXPECT_FALSE(c.ImportConditionsAreEqual(a, b));
}

// ---------------------------------------------------------------------------
// LinkerContext::IsConditionalImportRedundant
// ---------------------------------------------------------------------------

TEST(LinkerCSS, IsConditionalImportRedundantEmptyLists)
{
    lnk::LinkerContext c;
    EXPECT_TRUE(c.IsConditionalImportRedundant({}, {}));
}

TEST(LinkerCSS, IsConditionalImportRedundantIdenticalQualifiersCovered)
{
    lnk::LinkerContext c;
    auto earlier = std::vector<css::ImportConditions>{MakeConditions({"theme"}, {"display"})};
    auto later = std::vector<css::ImportConditions>{MakeConditions({"theme"}, {"display"})};
    EXPECT_TRUE(c.IsConditionalImportRedundant(earlier, later));
}

TEST(LinkerCSS, IsConditionalImportRedundantLaterDropsSupports)
{
    lnk::LinkerContext c;
    auto earlier = std::vector<css::ImportConditions>{MakeConditions({"theme"}, {"display"})};
    auto later = std::vector<css::ImportConditions>{MakeConditions({"theme"})};
    EXPECT_TRUE(c.IsConditionalImportRedundant(earlier, later));
}

TEST(LinkerCSS, IsConditionalImportRedundantLaterAddsSupports)
{
    lnk::LinkerContext c;
    auto earlier = std::vector<css::ImportConditions>{MakeConditions({"theme"})};
    auto later = std::vector<css::ImportConditions>{MakeConditions({"theme"}, {"display"})};
    EXPECT_FALSE(c.IsConditionalImportRedundant(earlier, later));
}

TEST(LinkerCSS, IsConditionalImportRedundantLayerMismatch)
{
    lnk::LinkerContext c;
    auto earlier = std::vector<css::ImportConditions>{MakeConditions({"theme"})};
    auto later = std::vector<css::ImportConditions>{MakeConditions({"base"})};
    EXPECT_FALSE(c.IsConditionalImportRedundant(earlier, later));
}

TEST(LinkerCSS, IsConditionalImportRedundantLaterListLonger)
{
    lnk::LinkerContext c;
    auto earlier = std::vector<css::ImportConditions>{MakeConditions({"theme"})};
    auto later = std::vector<css::ImportConditions>{
        MakeConditions({"theme"}),
        MakeConditions({"base"}),
    };
    EXPECT_FALSE(c.IsConditionalImportRedundant(earlier, later));
}

TEST(LinkerCSS, IsConditionalImportRedundantMatchesEarlierPrefix)
{
    lnk::LinkerContext c;
    auto earlier = std::vector<css::ImportConditions>{
        MakeConditions({"theme"}),
        MakeConditions({"base"}, {"display"}),
    };
    auto later = std::vector<css::ImportConditions>{MakeConditions({"theme"})};
    EXPECT_TRUE(c.IsConditionalImportRedundant(earlier, later));
}

// ---------------------------------------------------------------------------
// LinkerContext::FindImportedCSSFilesInJSOrder
// ---------------------------------------------------------------------------

TEST(LinkerCSS, FindImportedCSSFilesInJSOrderDependencyBeforeEntry)
{
    g::LinkerGraph graph = MakeLinkerGraph(4);

    auto entry = MakeJSRepr();
    entry->css_source_index = cm::Index32::Make(6);
    AddPartWithImport(*entry, 2);
    SetJS(graph, 1, entry);

    auto dep = MakeJSRepr();
    dep->css_source_index = cm::Index32::Make(5);
    SetJS(graph, 2, dep);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    EXPECT_EQ(c.FindImportedCSSFilesInJSOrder(1), (std::vector<uint32_t>{5, 6}));
}

TEST(LinkerCSS, FindImportedCSSFilesInJSOrderAppendsTransitively)
{
    g::LinkerGraph graph = MakeLinkerGraph(4);

    auto entry = MakeJSRepr();
    entry->css_source_index = cm::Index32::Make(6);
    AddPartWithImport(*entry, 2);
    SetJS(graph, 1, entry);

    auto dep = MakeJSRepr();
    dep->css_source_index = cm::Index32::Make(5);
    AddPartWithImport(*dep, 3);
    SetJS(graph, 2, dep);

    auto deep = MakeJSRepr();
    deep->css_source_index = cm::Index32::Make(8);
    SetJS(graph, 3, deep);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    // Postorder: the deepest dependency's CSS comes first.
    EXPECT_EQ(c.FindImportedCSSFilesInJSOrder(1), (std::vector<uint32_t>{8, 5, 6}));
}

TEST(LinkerCSS, FindImportedCSSFilesInJSOrderIgnoresDirectCSSImports)
{
    g::LinkerGraph graph = MakeLinkerGraph(4);

    auto entry = MakeJSRepr();
    entry->css_source_index = cm::Index32::Make(6);
    AddPartWithImport(*entry, 2);
    SetJS(graph, 1, entry);

    auto dep = MakeJSRepr();
    dep->css_source_index = cm::Index32::Make(5);
    SetJS(graph, 2, dep);

    // A file imported directly from JS that is itself a stylesheet: the JS
    // order only records the stub's css_source_index, never the raw CSS file.
    SetCSS(graph, 3, MakeCSSRepr());

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    EXPECT_EQ(c.FindImportedCSSFilesInJSOrder(1), (std::vector<uint32_t>{5, 6}));
}

TEST(LinkerCSS, FindImportedCSSFilesInJSOrderSkipsWholeImportCycle)
{
    g::LinkerGraph graph = MakeLinkerGraph(4);

    auto entry = MakeJSRepr();
    entry->css_source_index = cm::Index32::Make(6);
    AddPartWithImport(*entry, 2);
    SetJS(graph, 1, entry);

    // dep imports back into entry; the cycle must not loop forever.
    auto dep = MakeJSRepr();
    dep->css_source_index = cm::Index32::Make(5);
    AddPartWithImport(*dep, 1);
    SetJS(graph, 2, dep);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    EXPECT_EQ(c.FindImportedCSSFilesInJSOrder(1), (std::vector<uint32_t>{5, 6}));
}

// ---------------------------------------------------------------------------
// LinkerContext::FindImportedFilesInCSSOrder
// ---------------------------------------------------------------------------

TEST(LinkerCSS, FindImportedFilesInCSSOrderEmitsDependenciesFirst)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);

    auto dep = MakeCSSRepr();
    SetCSS(graph, 1, dep);

    auto entry = MakeCSSRepr();
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(1);
    record.kind = cm::ImportKind::kAt;
    entry->ast.import_records.push_back(record);
    entry->ast.rules.push_back(MakeImportRule(0));
    SetCSS(graph, 2, entry);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    auto order = c.FindImportedFilesInCSSOrder({2});
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0].kind, lnk::CssImportKind::kSourceIndex);
    EXPECT_EQ(order[0].source_index, 1u);
    EXPECT_EQ(order[1].kind, lnk::CssImportKind::kSourceIndex);
    EXPECT_EQ(order[1].source_index, 2u);
}

TEST(LinkerCSS, FindImportedFilesInCSSOrderHoistsExternalImports)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);

    auto entry = MakeCSSRepr();
    cm::ImportRecord record;
    record.path.text = "https://example.com/x.css";
    record.kind = cm::ImportKind::kAt;
    entry->ast.import_records.push_back(record);
    entry->ast.rules.push_back(MakeImportRule(0));
    SetCSS(graph, 2, entry);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    auto order = c.FindImportedFilesInCSSOrder({2});
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0].kind, lnk::CssImportKind::kExternalPath);
    EXPECT_EQ(order[0].external_path.text, "https://example.com/x.css");
    EXPECT_EQ(order[1].kind, lnk::CssImportKind::kSourceIndex);
    EXPECT_EQ(order[1].source_index, 2u);
}

TEST(LinkerCSS, FindImportedFilesInCSSOrderHoistsLeadingLayersWithExternal)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);

    auto entry = MakeCSSRepr();
    entry->ast.layers_pre_import = {{"theme"}};
    cm::ImportRecord record;
    record.path.text = "https://example.com/x.css";
    record.kind = cm::ImportKind::kAt;
    entry->ast.import_records.push_back(record);
    entry->ast.rules.push_back(MakeImportRule(0));
    SetCSS(graph, 2, entry);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    auto order = c.FindImportedFilesInCSSOrder({2});
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0].kind, lnk::CssImportKind::kLayers);
    EXPECT_EQ(order[0].layers, (std::vector<std::vector<std::string>>{{"theme"}}));
    EXPECT_EQ(order[1].kind, lnk::CssImportKind::kExternalPath);
    EXPECT_EQ(order[2].kind, lnk::CssImportKind::kSourceIndex);
    EXPECT_EQ(order[2].source_index, 2u);
}

TEST(LinkerCSS, FindImportedFilesInCSSOrderSkipsEmptyLoaderExternal)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);

    auto entry = MakeCSSRepr();
    cm::ImportRecord record;
    record.path.text = "empty.css";
    record.kind = cm::ImportKind::kAt;
    record.flags = record.flags | cm::ImportRecordFlags::kWasLoadedWithEmptyLoader;
    entry->ast.import_records.push_back(record);
    entry->ast.rules.push_back(MakeImportRule(0));
    SetCSS(graph, 2, entry);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    auto order = c.FindImportedFilesInCSSOrder({2});
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0].kind, lnk::CssImportKind::kSourceIndex);
}

TEST(LinkerCSS, FindImportedFilesInCSSOrderReplacesDuplicateSourceWithLayers)
{
    g::LinkerGraph graph = MakeLinkerGraph(4);

    auto shared = MakeCSSRepr();
    shared->ast.layers_post_import = {{"post"}};
    SetCSS(graph, 2, shared);

    auto entry_one = MakeCSSRepr();
    cm::ImportRecord record_one;
    record_one.source_index = cm::Index32::Make(2);
    record_one.kind = cm::ImportKind::kAt;
    entry_one->ast.import_records.push_back(record_one);
    entry_one->ast.rules.push_back(MakeImportRule(0));
    SetCSS(graph, 1, entry_one);

    auto entry_two = MakeCSSRepr();
    cm::ImportRecord record_two;
    record_two.source_index = cm::Index32::Make(2);
    record_two.kind = cm::ImportKind::kAt;
    entry_two->ast.import_records.push_back(record_two);
    entry_two->ast.rules.push_back(MakeImportRule(0));
    SetCSS(graph, 3, entry_two);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    auto order = c.FindImportedFilesInCSSOrder({1, 3});

    // Both entries import the same file. The first occurrence is dropped in
    // favour of a kLayers entry carrying the shared file's post-import layers.
    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[0].kind, lnk::CssImportKind::kLayers);
    EXPECT_EQ(order[0].layers, (std::vector<std::vector<std::string>>{{"post"}}));
    EXPECT_EQ(order[1].kind, lnk::CssImportKind::kSourceIndex);
    EXPECT_EQ(order[1].source_index, 1u);
    EXPECT_EQ(order[2].kind, lnk::CssImportKind::kSourceIndex);
    EXPECT_EQ(order[2].source_index, 2u);
    EXPECT_EQ(order[3].kind, lnk::CssImportKind::kSourceIndex);
    EXPECT_EQ(order[3].source_index, 3u);
}

TEST(LinkerCSS, FindImportedFilesInCSSOrderMergesAdjacentLayerEntries)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);

    auto entry = MakeCSSRepr();
    entry->ast.layers_pre_import = {{"theme"}, {"base"}};
    SetCSS(graph, 2, entry);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    auto order = c.FindImportedFilesInCSSOrder({2});
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0].kind, lnk::CssImportKind::kLayers);
    EXPECT_EQ(order[0].layers, (std::vector<std::vector<std::string>>{{"theme"}, {"base"}}));
    EXPECT_EQ(order[1].kind, lnk::CssImportKind::kSourceIndex);
    EXPECT_EQ(order[1].source_index, 2u);
}

// ---------------------------------------------------------------------------
// LinkerContext::WrapRulesWithConditions
// ---------------------------------------------------------------------------

TEST(LinkerCSS, WrapRulesWithConditionsNoConditionsIsPassThrough)
{
    std::vector<css::Rule> rules;
    rules.push_back(MakeNamedRule("foo"));

    std::vector<cm::ImportRecord> records;
    records.push_back(cm::ImportRecord{});

    lnk::LinkerContext c;
    auto [wrapped, import_records] = c.WrapRulesWithConditions(rules, records, {}, {});

    ASSERT_EQ(wrapped.size(), 1u);
    EXPECT_EQ(import_records.size(), 1u);
    EXPECT_NE(AsKnownAt(wrapped[0]), nullptr);
    EXPECT_EQ(AsKnownAt(wrapped[0])->at_token, "foo");
}

TEST(LinkerCSS, WrapRulesWithConditionsWrapsInLayer)
{
    auto children = std::make_shared<std::vector<css::Token>>();
    children->push_back(IdentToken("a"));
    children->push_back(IdentToken("b"));

    css::Token layer_token = IdentToken("outer");
    layer_token.children = children;

    css::ImportConditions conditions = MakeConditions();
    conditions.layers.push_back(layer_token);

    std::vector<css::Rule> rules;
    rules.push_back(MakeNamedRule("foo"));

    lnk::LinkerContext c;
    auto [wrapped, record_out] = c.WrapRulesWithConditions(rules, {}, {conditions}, {});

    ASSERT_EQ(wrapped.size(), 1u);
    auto* layer = AsKnownAt(wrapped[0]);
    ASSERT_TRUE(layer != nullptr);
    EXPECT_EQ(layer->at_token, "layer");
    ASSERT_EQ(layer->prelude.size(), 2u);
    EXPECT_EQ(layer->prelude[0].text, "a");
    EXPECT_EQ(layer->prelude[1].text, "b");
    ASSERT_EQ(layer->rules.size(), 1u);
    EXPECT_NE(AsKnownAt(layer->rules[0]), nullptr);
    EXPECT_EQ(record_out.size(), 0u);
}

TEST(LinkerCSS, WrapRulesWithConditionsWrapsInSupports)
{
    css::ImportConditions conditions = MakeConditions({}, {"display:grid"});

    std::vector<css::Rule> rules;
    rules.push_back(MakeNamedRule("foo"));

    lnk::LinkerContext c;
    auto [wrapped, record_out] = c.WrapRulesWithConditions(rules, {}, {conditions}, {});

    ASSERT_EQ(wrapped.size(), 1u);
    auto* supports = AsKnownAt(wrapped[0]);
    ASSERT_TRUE(supports != nullptr);
    EXPECT_EQ(supports->at_token, "supports");
    ASSERT_EQ(supports->prelude.size(), 1u);
    EXPECT_EQ(supports->prelude[0].kind, css::TokenType::kOpenParen);
    EXPECT_EQ(supports->prelude[0].text, "(");
    EXPECT_EQ(record_out.size(), 0u);
}

TEST(LinkerCSS, WrapRulesWithConditionsNestsLayerOutsideSupports)
{
    auto layer_cond = MakeConditions({"theme"});
    auto supports_cond = MakeConditions({}, {"display:grid"});

    std::vector<css::ImportConditions> conditions;
    conditions.push_back(layer_cond);
    conditions.push_back(supports_cond);

    std::vector<css::Rule> rules;
    rules.push_back(MakeNamedRule("foo"));

    lnk::LinkerContext c;
    auto [wrapped, record_out] = c.WrapRulesWithConditions(rules, {}, conditions, {});

    // Conditions nest innermost-first: the last condition ends up on the
    // inside, so the outermost wrapper is the @layer.
    ASSERT_EQ(wrapped.size(), 1u);
    auto* layer = AsKnownAt(wrapped[0]);
    ASSERT_TRUE(layer != nullptr);
    EXPECT_EQ(layer->at_token, "layer");
    ASSERT_EQ(layer->rules.size(), 1u);
    auto* supports = AsKnownAt(layer->rules[0]);
    ASSERT_TRUE(supports != nullptr);
    EXPECT_EQ(supports->at_token, "supports");
    ASSERT_EQ(supports->rules.size(), 1u);
    EXPECT_NE(AsKnownAt(supports->rules[0]), nullptr);
    EXPECT_EQ(record_out.size(), 0u);
}