// Tests for the linker graph (graph.cpp and input.cpp).

#include "test/guchho_test.hpp"

#include "guchho/compiler.hpp"
#include "guchho/graph.hpp"
#include "guchho/helpers.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_runtime.hpp"
#include "guchho/logger.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace g = ::guchho::graph;
namespace js = ::guchho::javascript;
namespace cm = ::guchho::compiler;
namespace lg = ::guchho::logger;
namespace hp = ::guchho::helpers;

namespace {

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// An InputFile whose payload is a JavaScript representation.
g::InputFile MakeJSInputFile(std::shared_ptr<g::JSRepr> repr)
{
    g::InputFile input;
    input.repr = std::move(repr);
    return input;
}

// Installs a JavaScript representation into a directly-constructed graph,
// returning nothing so the graph owns the only surviving shared pointer.
void SetJS(g::LinkerGraph& graph, uint32_t source_index, std::shared_ptr<g::JSRepr> repr)
{
    graph.files[source_index].input_file.repr = std::move(repr);
}

// An AST with a freshly heap-allocated module scope, mirroring the parser's
// convention that scopes are never freed.
std::shared_ptr<g::JSRepr> MakeJSWithScope()
{
    auto repr = std::make_shared<g::JSRepr>();
    repr->ast.module_scope = new js::Scope();
    return repr;
}

// A graph with "source_count" files and a matching symbol table, ready for
// direct calls into the linker helper methods.
g::LinkerGraph MakeLinkerGraph(uint32_t source_count)
{
    g::LinkerGraph graph;
    graph.files.resize(source_count);
    graph.symbols = cm::NewSymbolMap(source_count);
    return graph;
}

} // namespace

// ---------------------------------------------------------------------------
// LinkerFile::LineColumnTracker
// ---------------------------------------------------------------------------

TEST(Graph, LineColumnTrackerConstructedOnDemand)
{
    g::LinkerFile file;
    file.input_file.source.contents = "ab\ncdef\nghi\n";

    lg::LineColumnTracker& first = file.LineColumnTracker();
    lg::LineColumnTracker& second = file.LineColumnTracker();
    EXPECT_EQ(&first, &second);

    auto loc_a = first.MakeMsgData(lg::Range{lg::Loc{0}, 2}, "line one start");
    ASSERT_TRUE(loc_a.location != nullptr);
    EXPECT_EQ(loc_a.location->line, 1);
    EXPECT_EQ(loc_a.location->column, 0);

    auto loc_b = first.MakeMsgData(lg::Range{lg::Loc{5}, 1}, "inside cdef");
    ASSERT_TRUE(loc_b.location != nullptr);
    EXPECT_EQ(loc_b.location->line, 2);
    EXPECT_EQ(loc_b.location->column, 2);
}

// ---------------------------------------------------------------------------
// JSRepr::TopLevelSymbolToParts
// ---------------------------------------------------------------------------

TEST(Graph, TopLevelSymbolToPartsOverlayWins)
{
    auto repr = std::make_shared<g::JSRepr>();
    repr->ast.top_level_symbol_to_parts_from_parser[cm::Ref{1, 2}] = {0};
    repr->meta.top_level_symbol_to_parts_overlay[cm::Ref{1, 2}] = {2, 7};

    const std::vector<uint32_t>& parts = repr->TopLevelSymbolToParts(cm::Ref{1, 2});
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0], 2u);
    EXPECT_EQ(parts[1], 7u);
}

TEST(Graph, TopLevelSymbolToPartsFallsBackToParser)
{
    auto repr = std::make_shared<g::JSRepr>();
    repr->ast.top_level_symbol_to_parts_from_parser[cm::Ref{1, 2}] = {3};

    const std::vector<uint32_t>& parts = repr->TopLevelSymbolToParts(cm::Ref{1, 2});
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0], 3u);
}

TEST(Graph, TopLevelSymbolToPartsUnknownRefIsEmpty)
{
    auto repr = std::make_shared<g::JSRepr>();
    const std::vector<uint32_t>& parts = repr->TopLevelSymbolToParts(cm::Ref{9, 9});
    EXPECT_TRUE(parts.empty());
}

// ---------------------------------------------------------------------------
// CloneLinkerGraph
// ---------------------------------------------------------------------------

TEST(Graph, CloneGraphClonesReachableSliceOnly)
{
    std::vector<g::InputFile> inputs(3);
    inputs[0] = MakeJSInputFile(MakeJSWithScope());
    inputs[2].repr = std::make_shared<g::CSSRepr>();

    std::vector<uint32_t> reachable = {0, 2};
    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, reachable, {}, false);

    ASSERT_EQ(graph.files.size(), 3u);
    EXPECT_TRUE(std::get_if<std::shared_ptr<g::JSRepr>>(&graph.files[0].input_file.repr) != nullptr);

    // File 1 was not reachable, so its slot is left untouched.
    EXPECT_EQ(std::get_if<std::shared_ptr<g::JSRepr>>(&graph.files[1].input_file.repr), nullptr);
    EXPECT_TRUE(std::get_if<std::shared_ptr<g::CSSRepr>>(&graph.files[2].input_file.repr) != nullptr);

    ASSERT_EQ(graph.reachable_files.size(), 2u);
    EXPECT_EQ(graph.reachable_files[0], 0u);
    EXPECT_EQ(graph.reachable_files[1], 2u);
    EXPECT_EQ(graph.stable_source_indices[0], 0u);
    EXPECT_EQ(graph.stable_source_indices[2], 1u);
    EXPECT_EQ(graph.files[0].distance_from_entry_point, UINT32_MAX);
    EXPECT_EQ(graph.files[2].distance_from_entry_point, UINT32_MAX);
}

TEST(Graph, CloneGraphDeepCopiesJSRepr)
{
    auto repr = MakeJSWithScope();
    repr->ast.parts.resize(1);
    cm::Symbol symbol;
    symbol.original_name = "foo";
    repr->ast.symbols.push_back(symbol);

    std::vector<g::InputFile> inputs(1);
    inputs[0] = MakeJSInputFile(repr);
    const auto* input_js = std::get_if<std::shared_ptr<g::JSRepr>>(&inputs[0].repr);
    ASSERT_TRUE(input_js != nullptr);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0}, {}, false);
    const auto* clone_js = std::get_if<std::shared_ptr<g::JSRepr>>(&graph.files[0].input_file.repr);
    ASSERT_TRUE(clone_js != nullptr);

    EXPECT_NE((*clone_js).get(), (*input_js).get());
    ASSERT_EQ((*clone_js)->ast.parts.size(), 1u);
}

TEST(Graph, CloneGraphMovesSymbolsIntoGraphTable)
{
    auto repr = MakeJSWithScope();
    cm::Symbol first;
    first.original_name = "alpha";
    repr->ast.symbols.push_back(first);
    cm::Symbol second;
    second.original_name = "beta";
    repr->ast.symbols.push_back(second);

    std::vector<g::InputFile> inputs(1);
    inputs[0] = MakeJSInputFile(repr);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0}, {}, false);

    const auto* clone_js = std::get_if<std::shared_ptr<g::JSRepr>>(&graph.files[0].input_file.repr);
    ASSERT_TRUE(clone_js != nullptr);
    EXPECT_TRUE((*clone_js)->ast.symbols.empty());

    ASSERT_EQ(graph.symbols.symbols_for_source[0].size(), 2u);
    EXPECT_EQ(graph.symbols.symbols_for_source[0][0].original_name, "alpha");
    EXPECT_EQ(graph.symbols.symbols_for_source[0][1].original_name, "beta");
}

TEST(Graph, CloneGraphRebuildsResolvedExports)
{
    auto repr = MakeJSWithScope();
    cm::Symbol symbol;
    symbol.original_name = "x";
    repr->ast.symbols.push_back(symbol);
    repr->ast.named_exports["foo"] = js::NamedExport{cm::Ref{1, 0}, lg::Loc{4}};

    std::vector<g::InputFile> inputs(2);
    inputs[1] = MakeJSInputFile(repr);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {1}, {}, false);
    const auto* clone_js = std::get_if<std::shared_ptr<g::JSRepr>>(&graph.files[1].input_file.repr);
    ASSERT_TRUE(clone_js != nullptr);

    auto it = (*clone_js)->meta.resolved_exports.find("foo");
    ASSERT_TRUE(it != (*clone_js)->meta.resolved_exports.end());
    EXPECT_EQ(it->second.ref.source_index, 1u);
    EXPECT_EQ(it->second.ref.inner_index, 0u);
    EXPECT_EQ(it->second.source_index, 1u);
    EXPECT_EQ(it->second.name_loc.start, 4);
    EXPECT_TRUE(it->second.potentially_ambiguous_export_star_refs.empty());
}

TEST(Graph, CloneGraphClearsLinkerPrivateMaps)
{
    auto repr = MakeJSWithScope();
    repr->meta.is_probably_typescript_type[cm::Ref{1, 0}] = true;
    repr->meta.imports_to_bind[cm::Ref{1, 0}] = g::ImportData{};
    EXPECT_EQ(repr->meta.is_probably_typescript_type.size(), 1u);
    EXPECT_EQ(repr->meta.imports_to_bind.size(), 1u);

    std::vector<g::InputFile> inputs(2);
    inputs[1] = MakeJSInputFile(repr);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {1}, {}, false);
    const auto* clone_js = std::get_if<std::shared_ptr<g::JSRepr>>(&graph.files[1].input_file.repr);
    ASSERT_TRUE(clone_js != nullptr);
    EXPECT_TRUE((*clone_js)->meta.is_probably_typescript_type.empty());
    EXPECT_TRUE((*clone_js)->meta.imports_to_bind.empty());
}

TEST(Graph, CloneGraphDeepCopiesModuleScope)
{
    auto repr = MakeJSWithScope();
    js::Scope* original_scope = repr->ast.module_scope;
    original_scope->members["x"] = js::ScopeMember{cm::Ref{1, 0}, lg::Loc{}};
    original_scope->generated = {cm::Ref{1, 1}, cm::Ref{1, 2}};

    std::vector<g::InputFile> inputs(1);
    inputs[0] = MakeJSInputFile(repr);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0}, {}, false);
    const auto* clone_js = std::get_if<std::shared_ptr<g::JSRepr>>(&graph.files[0].input_file.repr);
    ASSERT_TRUE(clone_js != nullptr);

    js::Scope* clone_scope = (*clone_js)->ast.module_scope;
    ASSERT_TRUE(clone_scope != nullptr);
    EXPECT_NE(clone_scope, original_scope);
    EXPECT_EQ(clone_scope->members.count("x"), 1u);
    ASSERT_EQ(clone_scope->generated.size(), 2u);
    EXPECT_EQ(clone_scope->generated[0].inner_index, 1u);
    EXPECT_EQ(clone_scope->generated[1].inner_index, 2u);
}

TEST(Graph, CloneGraphCreatesScopeWhenInputHadNone)
{
    auto repr = std::make_shared<g::JSRepr>();

    std::vector<g::InputFile> inputs(1);
    inputs[0] = MakeJSInputFile(repr);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0}, {}, false);
    const auto* clone_js = std::get_if<std::shared_ptr<g::JSRepr>>(&graph.files[0].input_file.repr);
    ASSERT_TRUE(clone_js != nullptr);
    ASSERT_TRUE((*clone_js)->ast.module_scope != nullptr);
}

TEST(Graph, CloneGraphTagsUserSpecifiedEntryPoints)
{
    std::vector<g::InputFile> inputs(2);
    inputs[0] = MakeJSInputFile(MakeJSWithScope());
    inputs[1] = MakeJSInputFile(MakeJSWithScope());

    std::vector<g::EntryPoint> entry_points;
    g::EntryPoint entry;
    entry.output_path = "out/entry.js";
    entry.source_index = 1;
    entry_points.push_back(entry);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0, 1}, entry_points, false);

    EXPECT_FALSE(graph.files[0].IsEntryPoint());
    EXPECT_TRUE(graph.files[1].IsEntryPoint());
    EXPECT_TRUE(graph.files[1].IsUserSpecifiedEntryPoint());
    EXPECT_EQ(graph.files[1].entry_point_kind, g::EntryPointKind::kUserSpecified);

    ASSERT_EQ(graph.EntryPoints().size(), 1u);
    EXPECT_EQ(graph.EntryPoints()[0].output_path, "out/entry.js");
    EXPECT_EQ(graph.EntryPoints()[0].source_index, 1u);
}

TEST(Graph, CloneGraphPromotesDynamicImportsWithCodeSplitting)
{
    auto repr = MakeJSWithScope();
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(2);
    record.kind = cm::ImportKind::kDynamic;
    record.assert_or_with = std::make_shared<cm::ImportAssertOrWith>();
    repr->ast.import_records.push_back(record);

    std::vector<g::InputFile> inputs(3);
    inputs[0] = MakeJSInputFile(repr);

    std::vector<g::EntryPoint> entry_points;
    g::EntryPoint user_entry;
    user_entry.source_index = 0;
    entry_points.push_back(user_entry);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0, 1, 2}, entry_points, true);

    ASSERT_EQ(graph.EntryPoints().size(), 2u);
    EXPECT_EQ(graph.EntryPoints()[1].source_index, 2u);
    EXPECT_EQ(graph.files[2].entry_point_kind, g::EntryPointKind::kDynamicImport);

    // The promoted import's assert/with clause was dropped so the generated
    // entry-point import does not block on the original resource attributes.
    const auto* clone_js = std::get_if<std::shared_ptr<g::JSRepr>>(&graph.files[0].input_file.repr);
    ASSERT_TRUE(clone_js != nullptr);
    ASSERT_EQ((*clone_js)->ast.import_records.size(), 1u);
    EXPECT_EQ((*clone_js)->ast.import_records[0].assert_or_with, nullptr);
}

TEST(Graph, CloneGraphKeepsDynamicImportsWithoutCodeSplitting)
{
    auto repr = MakeJSWithScope();
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(2);
    record.kind = cm::ImportKind::kDynamic;
    record.assert_or_with = std::make_shared<cm::ImportAssertOrWith>();
    repr->ast.import_records.push_back(record);

    std::vector<g::InputFile> inputs(3);
    inputs[0] = MakeJSInputFile(repr);

    std::vector<g::EntryPoint> entry_points;
    g::EntryPoint user_entry;
    user_entry.source_index = 0;
    entry_points.push_back(user_entry);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0, 1, 2}, entry_points, false);

    ASSERT_EQ(graph.EntryPoints().size(), 1u);
    EXPECT_EQ(graph.files[2].entry_point_kind, g::EntryPointKind::kNone);

    const auto* clone_js = std::get_if<std::shared_ptr<g::JSRepr>>(&graph.files[0].input_file.repr);
    ASSERT_TRUE(clone_js != nullptr);
    ASSERT_EQ((*clone_js)->ast.import_records.size(), 1u);
    EXPECT_TRUE((*clone_js)->ast.import_records[0].assert_or_with != nullptr);
}

TEST(Graph, CloneGraphSortsPromotedDynamicEntryPoints)
{
    auto repr = MakeJSWithScope();
    cm::ImportRecord to_two;
    to_two.source_index = cm::Index32::Make(2);
    to_two.kind = cm::ImportKind::kDynamic;
    repr->ast.import_records.push_back(to_two);
    cm::ImportRecord to_one;
    to_one.source_index = cm::Index32::Make(1);
    to_one.kind = cm::ImportKind::kDynamic;
    repr->ast.import_records.push_back(to_one);

    std::vector<g::InputFile> inputs(3);
    inputs[0] = MakeJSInputFile(repr);

    std::vector<g::EntryPoint> entry_points;
    g::EntryPoint user_entry;
    user_entry.source_index = 0;
    entry_points.push_back(user_entry);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0, 1, 2}, entry_points, true);

    ASSERT_EQ(graph.EntryPoints().size(), 3u);
    EXPECT_EQ(graph.EntryPoints()[1].source_index, 1u);
    EXPECT_EQ(graph.EntryPoints()[2].source_index, 2u);
    EXPECT_EQ(graph.files[1].entry_point_kind, g::EntryPointKind::kDynamicImport);
    EXPECT_EQ(graph.files[2].entry_point_kind, g::EntryPointKind::kDynamicImport);
}

TEST(Graph, CloneGraphDoesNotDuplicateEntryPointTargets)
{
    auto repr = MakeJSWithScope();
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(2);
    record.kind = cm::ImportKind::kDynamic;
    repr->ast.import_records.push_back(record);

    std::vector<g::InputFile> inputs(3);
    inputs[0] = MakeJSInputFile(repr);

    std::vector<g::EntryPoint> entry_points;
    g::EntryPoint user_entry;
    user_entry.source_index = 0;
    entry_points.push_back(user_entry);
    g::EntryPoint also_listed;
    also_listed.source_index = 2;
    entry_points.push_back(also_listed);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0, 2}, entry_points, true);

    ASSERT_EQ(graph.EntryPoints().size(), 2u);
    EXPECT_EQ(graph.files[2].entry_point_kind, g::EntryPointKind::kUserSpecified);
}

TEST(Graph, CloneGraphSizesEntryBitsByEntryPointCount)
{
    std::vector<g::InputFile> inputs(2);
    inputs[0] = MakeJSInputFile(MakeJSWithScope());
    inputs[1] = MakeJSInputFile(MakeJSWithScope());

    std::vector<g::EntryPoint> entry_points;
    g::EntryPoint first;
    first.source_index = 0;
    entry_points.push_back(first);
    g::EntryPoint second;
    second.source_index = 1;
    entry_points.push_back(second);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0, 1}, entry_points, false);

    EXPECT_EQ(graph.files[0].entry_bits, hp::BitSet(2u));
    EXPECT_EQ(graph.files[1].entry_bits, hp::BitSet(2u));
    EXPECT_TRUE(graph.files[0].entry_bits.IsEmpty());
    EXPECT_TRUE(graph.files[1].entry_bits.IsEmpty());
}

TEST(Graph, CloneGraphFoldsTSEnumsAndConstValues)
{
    auto repr_zero = MakeJSWithScope();
    repr_zero->ast.ts_enums[cm::Ref{0, 1}]["A"] = js::TSEnumValue{u"A", 1.0, false};
    repr_zero->ast.const_values[cm::Ref{0, 2}] = js::ConstValue{42.0, u"", js::ConstValueKind::kNumber};

    auto repr_one = MakeJSWithScope();
    repr_one->ast.ts_enums[cm::Ref{1, 3}]["B"] = js::TSEnumValue{u"B", 2.0, false};
    repr_one->ast.const_values[cm::Ref{1, 4}] = js::ConstValue{0.0, u"hi", js::ConstValueKind::kString};

    std::vector<g::InputFile> inputs(2);
    inputs[0] = MakeJSInputFile(repr_zero);
    inputs[1] = MakeJSInputFile(repr_one);

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0, 1}, {}, false);

    ASSERT_EQ(graph.ts_enums.size(), 2u);
    EXPECT_EQ((graph.ts_enums[cm::Ref{0, 1}]["A"].number), 1.0);
    EXPECT_EQ((graph.ts_enums[cm::Ref{1, 3}]["B"].number), 2.0);

    ASSERT_EQ(graph.const_values.size(), 2u);
    EXPECT_EQ((graph.const_values[cm::Ref{0, 2}].number), 42.0);
    EXPECT_EQ((graph.const_values[cm::Ref{0, 2}].kind), js::ConstValueKind::kNumber);
    EXPECT_EQ((graph.const_values[cm::Ref{1, 4}].string), u"hi");
}

TEST(Graph, CloneGraphClonesCSSRepr)
{
    auto css_repr = std::make_shared<g::CSSRepr>();
    cm::Symbol symbol;
    symbol.original_name = "css-symbol";
    css_repr->ast.symbols.push_back(symbol);

    std::vector<g::InputFile> inputs(1);
    inputs[0].repr = css_repr;

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0}, {}, false);
    const auto* clone_css = std::get_if<std::shared_ptr<g::CSSRepr>>(&graph.files[0].input_file.repr);
    ASSERT_TRUE(clone_css != nullptr);

    EXPECT_NE((*clone_css).get(), css_repr.get());
    ASSERT_EQ(graph.symbols.symbols_for_source[0].size(), 1u);
    EXPECT_EQ(graph.symbols.symbols_for_source[0][0].original_name, "css-symbol");
}

TEST(Graph, CloneGraphClonesHTMLRepr)
{
    auto html_repr = std::make_shared<g::HTMLRepr>();

    std::vector<g::InputFile> inputs(1);
    inputs[0].repr = html_repr;

    g::LinkerGraph graph = g::CloneLinkerGraph(inputs, {0}, {}, false);
    const auto* clone_html = std::get_if<std::shared_ptr<g::HTMLRepr>>(&graph.files[0].input_file.repr);
    ASSERT_TRUE(clone_html != nullptr);
    EXPECT_NE((*clone_html).get(), html_repr.get());
}

// ---------------------------------------------------------------------------
// LinkerGraph::AddPartToFile
// ---------------------------------------------------------------------------

TEST(Graph, AddPartToFileAppendsAndReturnsIndex)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    SetJS(graph, 0, MakeJSWithScope());

    js::Part part;
    EXPECT_EQ(graph.AddPartToFile(0, std::move(part)), 0u);
    EXPECT_EQ(graph.AddPartToFile(0, js::Part{}), 1u);

    auto repr = std::get<std::shared_ptr<g::JSRepr>>(graph.files[0].input_file.repr);
    EXPECT_EQ(repr->ast.parts.size(), 2u);
}

TEST(Graph, AddPartToFileRegistersTopLevelSymbols)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSWithScope();
    repr->ast.top_level_symbol_to_parts_from_parser[cm::Ref{0, 5}] = {1};
    SetJS(graph, 0, repr);

    js::Part part;
    part.declared_symbols.push_back(js::DeclaredSymbol{cm::Ref{0, 5}, true});
    part.declared_symbols.push_back(js::DeclaredSymbol{cm::Ref{0, 6}, false});
    graph.AddPartToFile(0, std::move(part));

    // Existing parser entry was extended with the new part index.
    auto overlay = repr->meta.top_level_symbol_to_parts_overlay.find(cm::Ref{0, 5});
    ASSERT_TRUE(overlay != repr->meta.top_level_symbol_to_parts_overlay.end());
    ASSERT_EQ(overlay->second.size(), 2u);
    EXPECT_EQ(overlay->second[0], 1u);
    EXPECT_EQ(overlay->second[1], 0u);

    // Non-top-level declarations are not registered.
    EXPECT_EQ((repr->meta.top_level_symbol_to_parts_overlay.count(cm::Ref{0, 6})), 0u);
}

TEST(Graph, AddPartToFileSeedsOverlayFromNothing)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSWithScope();
    SetJS(graph, 0, repr);

    js::Part part;
    part.declared_symbols.push_back(js::DeclaredSymbol{cm::Ref{0, 7}, true});
    graph.AddPartToFile(0, std::move(part));

    auto overlay = repr->meta.top_level_symbol_to_parts_overlay.find(cm::Ref{0, 7});
    ASSERT_TRUE(overlay != repr->meta.top_level_symbol_to_parts_overlay.end());
    ASSERT_EQ(overlay->second.size(), 1u);
    EXPECT_EQ(overlay->second[0], 0u);
}

TEST(Graph, AddPartToFilePreservesExistingSymbolUses)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSWithScope();
    SetJS(graph, 0, repr);

    js::Part part;
    part.symbol_uses[cm::Ref{0, 1}] = js::SymbolUse{5};
    graph.AddPartToFile(0, std::move(part));

    ASSERT_EQ((repr->ast.parts[0].symbol_uses.at(cm::Ref{0, 1}).count_estimate), 5u);
}

// ---------------------------------------------------------------------------
// LinkerGraph::GenerateNewSymbol
// ---------------------------------------------------------------------------

TEST(Graph, GenerateNewSymbolMintsRefAndRegisters)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSWithScope();
    cm::Symbol existing;
    existing.original_name = "preExisting";
    graph.symbols.symbols_for_source[0].push_back(existing);
    SetJS(graph, 0, repr);

    cm::Ref ref = graph.GenerateNewSymbol(0, cm::SymbolKind::kConst, "helper");

    EXPECT_EQ(ref.source_index, 0u);
    EXPECT_EQ(ref.inner_index, 1u);

    ASSERT_EQ(graph.symbols.symbols_for_source[0].size(), 2u);
    EXPECT_EQ(graph.symbols.symbols_for_source[0][1].original_name, "helper");
    EXPECT_EQ(graph.symbols.symbols_for_source[0][1].kind, cm::SymbolKind::kConst);
    EXPECT_EQ(graph.symbols.symbols_for_source[0][1].link.inner_index, 0xFFFFFFFFu);

    ASSERT_EQ(repr->ast.module_scope->generated.size(), 1u);
    EXPECT_EQ(repr->ast.module_scope->generated[0].source_index, 0u);
    EXPECT_EQ(repr->ast.module_scope->generated[0].inner_index, 1u);
}

// ---------------------------------------------------------------------------
// LinkerGraph::GenerateSymbolImportAndUse
// ---------------------------------------------------------------------------

TEST(Graph, GenerateSymbolImportAndUseZeroCountIsNoop)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr_zero = MakeJSWithScope();
    repr_zero->ast.parts.resize(1);
    SetJS(graph, 0, repr_zero);
    auto repr_one = MakeJSWithScope();
    repr_one->ast.top_level_symbol_to_parts_from_parser[cm::Ref{1, 3}] = {0};
    SetJS(graph, 1, repr_one);

    graph.GenerateSymbolImportAndUse(0, 0, cm::Ref{1, 3}, 0, 1);

    EXPECT_TRUE(repr_zero->ast.parts[0].symbol_uses.empty());
    EXPECT_TRUE(repr_zero->ast.parts[0].dependencies.empty());
    EXPECT_TRUE(repr_zero->meta.imports_to_bind.empty());
}

TEST(Graph, GenerateSymbolImportAndUseStagesCrossFileImport)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr_zero = MakeJSWithScope();
    repr_zero->ast.parts.resize(1);
    SetJS(graph, 0, repr_zero);
    auto repr_one = MakeJSWithScope();
    repr_one->ast.parts.resize(1);
    repr_one->ast.top_level_symbol_to_parts_from_parser[cm::Ref{1, 3}] = {0};
    SetJS(graph, 1, repr_one);

    graph.GenerateSymbolImportAndUse(0, 0, cm::Ref{1, 3}, 2, 1);

    EXPECT_EQ((repr_zero->ast.parts[0].symbol_uses.at(cm::Ref{1, 3}).count_estimate), 2u);

    auto import = repr_zero->meta.imports_to_bind.find(cm::Ref{1, 3});
    ASSERT_TRUE(import != repr_zero->meta.imports_to_bind.end());
    EXPECT_EQ(import->second.ref.source_index, 1u);
    EXPECT_EQ(import->second.source_index, 1u);

    ASSERT_EQ(repr_zero->ast.parts[0].dependencies.size(), 1u);
    EXPECT_EQ(repr_zero->ast.parts[0].dependencies[0].source_index, 1u);
    EXPECT_EQ(repr_zero->ast.parts[0].dependencies[0].part_index, 0u);
}

TEST(Graph, GenerateSymbolImportAndUseAddsAllDeclaringParts)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr_zero = MakeJSWithScope();
    repr_zero->ast.parts.resize(1);
    SetJS(graph, 0, repr_zero);
    auto repr_one = MakeJSWithScope();
    repr_one->ast.top_level_symbol_to_parts_from_parser[cm::Ref{1, 3}] = {0, 2};
    SetJS(graph, 1, repr_one);

    graph.GenerateSymbolImportAndUse(0, 0, cm::Ref{1, 3}, 1, 1);

    ASSERT_EQ(repr_zero->ast.parts[0].dependencies.size(), 2u);
    EXPECT_EQ(repr_zero->ast.parts[0].dependencies[0].source_index, 1u);
    EXPECT_EQ(repr_zero->ast.parts[0].dependencies[0].part_index, 0u);
    EXPECT_EQ(repr_zero->ast.parts[0].dependencies[1].source_index, 1u);
    EXPECT_EQ(repr_zero->ast.parts[0].dependencies[1].part_index, 2u);
}

TEST(Graph, GenerateSymbolImportAndUseSameFileStagesNoImport)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSWithScope();
    repr->ast.parts.resize(1);
    repr->ast.top_level_symbol_to_parts_from_parser[cm::Ref{0, 2}] = {0};
    SetJS(graph, 0, repr);

    graph.GenerateSymbolImportAndUse(0, 0, cm::Ref{0, 2}, 1, 0);

    EXPECT_EQ((repr->ast.parts[0].symbol_uses.at(cm::Ref{0, 2}).count_estimate), 1u);
    EXPECT_TRUE(repr->meta.imports_to_bind.empty());
    ASSERT_EQ(repr->ast.parts[0].dependencies.size(), 1u);
    EXPECT_EQ(repr->ast.parts[0].dependencies[0].source_index, 0u);
    EXPECT_EQ(repr->ast.parts[0].dependencies[0].part_index, 0u);
}

TEST(Graph, GenerateSymbolImportAndUseFlagsExportAndModuleUsage)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSWithScope();
    repr->ast.parts.resize(2);
    repr->ast.exports_ref = cm::Ref{0, 7};
    repr->ast.module_ref = cm::Ref{0, 8};
    SetJS(graph, 0, repr);

    graph.GenerateSymbolImportAndUse(0, 0, cm::Ref{0, 7}, 1, 0);
    EXPECT_TRUE(repr->ast.uses_exports_ref);
    EXPECT_FALSE(repr->ast.uses_module_ref);

    graph.GenerateSymbolImportAndUse(0, 1, cm::Ref{0, 8}, 1, 0);
    EXPECT_TRUE(repr->ast.uses_module_ref);
}

// ---------------------------------------------------------------------------
// LinkerGraph::GenerateRuntimeSymbolImportAndUse
// ---------------------------------------------------------------------------

TEST(Graph, GenerateRuntimeSymbolImportAndUseWiresRuntimeExport)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto runtime = MakeJSWithScope();
    runtime->ast.parts.resize(1);
    runtime->ast.named_exports["__toESM"] = js::NamedExport{cm::Ref{0, 5}, lg::Loc{}};
    runtime->ast.top_level_symbol_to_parts_from_parser[cm::Ref{0, 5}] = {0};
    SetJS(graph, js::kSourceIndex, runtime);

    auto user = MakeJSWithScope();
    user->ast.parts.resize(1);
    SetJS(graph, 1, user);

    graph.GenerateRuntimeSymbolImportAndUse(1, 0, "__toESM", 2);

    EXPECT_EQ((user->ast.parts[0].symbol_uses.at(cm::Ref{0, 5}).count_estimate), 2u);

    auto import = user->meta.imports_to_bind.find(cm::Ref{0, 5});
    ASSERT_TRUE(import != user->meta.imports_to_bind.end());
    EXPECT_EQ(import->second.source_index, js::kSourceIndex);

    ASSERT_EQ(user->ast.parts[0].dependencies.size(), 1u);
    EXPECT_EQ(user->ast.parts[0].dependencies[0].source_index, js::kSourceIndex);
    EXPECT_EQ(user->ast.parts[0].dependencies[0].part_index, 0u);
}

TEST(Graph, GenerateRuntimeSymbolImportAndUseZeroCountIsNoop)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto runtime = MakeJSWithScope();
    runtime->ast.named_exports["__toESM"] = js::NamedExport{cm::Ref{0, 5}, lg::Loc{}};
    SetJS(graph, js::kSourceIndex, runtime);

    auto user = MakeJSWithScope();
    user->ast.parts.resize(1);
    SetJS(graph, 1, user);

    graph.GenerateRuntimeSymbolImportAndUse(1, 0, "__toESM", 0);

    EXPECT_TRUE(user->ast.parts[0].symbol_uses.empty());
    EXPECT_TRUE(user->meta.imports_to_bind.empty());
    EXPECT_TRUE(user->ast.parts[0].dependencies.empty());
}