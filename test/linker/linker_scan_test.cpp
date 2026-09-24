// Tests for the linker scan pass (linker_scan.cpp): tree shaking, code
// splitting reachability, wrapper propagation, export-star resolution, and
// named import matching.

#include "test/guchho_test.hpp"

#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/graph.hpp"
#include "guchho/helpers.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_runtime.hpp"
#include "guchho/linker.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace g = ::guchho::graph;
namespace js = ::guchho::javascript;
namespace cm = ::guchho::compiler;
namespace cf = ::guchho::config;
namespace hp = ::guchho::helpers;
namespace lnk = ::guchho::linker;
namespace lg = ::guchho::logger;

namespace {

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// A fresh graph with "source_count" files and a matching symbol table.
g::LinkerGraph MakeLinkerGraph(uint32_t source_count)
{
    g::LinkerGraph graph;
    graph.files.resize(source_count);
    graph.symbols = cm::NewSymbolMap(source_count);
    return graph;
}

// Installs a JavaScript representation into a file slot.
void SetJS(g::LinkerGraph& graph, uint32_t source_index, std::shared_ptr<g::JSRepr> repr)
{
    graph.files[source_index].input_file.repr = std::move(repr);
}

// Installs a CSS representation into a file slot.
void SetCSS(g::LinkerGraph& graph, uint32_t source_index, std::shared_ptr<g::CSSRepr> repr)
{
    graph.files[source_index].input_file.repr = std::move(repr);
}

// Direct access to a file's JS representation once installed.
g::JSRepr& JSReprOf(g::LinkerGraph& graph, uint32_t source_index)
{
    return *std::get<std::shared_ptr<g::JSRepr>>(graph.files[source_index].input_file.repr);
}

// Appends an import record to "repr". "source_index" = UINT32_MAX leaves the
// record unresolved (external).
uint32_t AddJSRecord(std::shared_ptr<g::JSRepr> repr, cm::ImportKind kind, uint32_t source_index)
{
    uint32_t index = static_cast<uint32_t>(repr->ast.import_records.size());
    cm::ImportRecord record;
    record.kind = kind;
    if (source_index != UINT32_MAX) {
        record.source_index = cm::Index32::Make(source_index);
    }
    repr->ast.import_records.push_back(std::move(record));
    return index;
}

// Builds a named import with the given alias pointing at "import_record_index".
js::NamedImport MakeNamedImport(std::string alias, uint32_t import_record_index)
{
    js::NamedImport import;
    import.alias = std::move(alias);
    import.import_record_index = import_record_index;
    return import;
}

// Adds a part that can be tree-shaken away, referencing "record_index".
void AddRemovablePart(std::shared_ptr<g::JSRepr> repr, uint32_t record_index)
{
    js::Part part;
    part.can_be_removed_if_unused = true;
    part.import_record_indices.push_back(record_index);
    repr->ast.parts.push_back(std::move(part));
}

// Adds a part that cannot be tree-shaken away.
void AddUnremovablePart(std::shared_ptr<g::JSRepr> repr)
{
    js::Part part;
    repr->ast.parts.push_back(std::move(part));
}

// Marks "source_index" as a user-specified entry point.
void MakeEntry(g::LinkerGraph& graph, uint32_t source_index)
{
    graph.files[source_index].entry_point_kind = g::EntryPointKind::kUserSpecified;
}

// Gives every file a bit set sized for "bit_count" bits and an "unset"
// distance sentinel, so reachability distances are meaningful.
void PrepareReachability(g::LinkerGraph& graph, uint32_t bit_count = 2)
{
    for (auto& file : graph.files) {
        file.entry_bits = hp::BitSet(bit_count);
        file.distance_from_entry_point = UINT32_MAX;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// MarkFileLiveForTreeShaking

TEST(MarkFileLive, EntryFileBecomesLive)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    SetJS(graph, 1, std::make_shared<g::JSRepr>());

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_TRUE(c.graph.files[1].is_live);
}

TEST(MarkFileLive, StaticImportWithSideEffectsIsLive)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    AddRemovablePart(repr1, rec);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, std::make_shared<g::JSRepr>());

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_TRUE(c.graph.files[1].is_live);
    EXPECT_TRUE(c.graph.files[2].is_live);
    EXPECT_TRUE(JSReprOf(c.graph, 1).ast.parts[0].is_live);
}

TEST(MarkFileLive, SideEffectFreeStaticImportIsSkipped)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    AddRemovablePart(repr1, rec);
    SetJS(graph, 1, repr1);
    graph.files[2].input_file.side_effects.kind = g::SideEffectsKind::kNoSideEffectsEmptyAST;
    SetJS(graph, 2, std::make_shared<g::JSRepr>());

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_FALSE(c.graph.files[2].is_live);
    EXPECT_FALSE(JSReprOf(c.graph, 1).ast.parts[0].is_live);
}

TEST(MarkFileLive, IgnoreDCEAnnotationsForcesSideEffectFreeImportLive)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    AddRemovablePart(repr1, rec);
    SetJS(graph, 1, repr1);
    graph.files[2].input_file.side_effects.kind = g::SideEffectsKind::kNoSideEffectsEmptyAST;
    SetJS(graph, 2, std::make_shared<g::JSRepr>());

    cf::Options options;
    options.IgnoreDCEAnnotations = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_TRUE(c.graph.files[2].is_live);
}

TEST(MarkFileLive, ExternalImportWithoutSideEffectsKeepsPartRemovable)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, UINT32_MAX);
    repr1->ast.import_records[rec].flags = cm::ImportRecordFlags::kIsExternalWithoutSideEffects;
    AddRemovablePart(repr1, rec);
    SetJS(graph, 1, repr1);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_TRUE(c.graph.files[1].is_live);
    EXPECT_FALSE(JSReprOf(c.graph, 1).ast.parts[0].is_live);
}

TEST(MarkFileLive, ExternalImportWithoutFlagKeepsPartLive)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, UINT32_MAX);
    AddRemovablePart(repr1, rec);
    SetJS(graph, 1, repr1);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_TRUE(JSReprOf(c.graph, 1).ast.parts[0].is_live);
}

TEST(MarkFileLive, JSWithCSSSourceIndexMarksCSSLive)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    repr1->css_source_index = cm::Index32::Make(2);
    SetJS(graph, 1, repr1);
    SetCSS(graph, 2, std::make_shared<g::CSSRepr>());

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_TRUE(c.graph.files[2].is_live);
}

TEST(MarkFileLive, CSSImportRecursesToJSFile)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto css = std::make_shared<g::CSSRepr>();
    cm::ImportRecord record;
    record.kind = cm::ImportKind::kAt;
    record.source_index = cm::Index32::Make(2);
    css->ast.import_records.push_back(std::move(record));
    SetCSS(graph, 1, css);
    SetJS(graph, 2, std::make_shared<g::JSRepr>());

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_TRUE(c.graph.files[1].is_live);
    EXPECT_TRUE(c.graph.files[2].is_live);
}

TEST(MarkFileLive, ImportCycleTerminates)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec1 = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    AddRemovablePart(repr1, rec1);
    auto repr2 = std::make_shared<g::JSRepr>();
    uint32_t rec2 = AddJSRecord(repr2, cm::ImportKind::kStmt, 1);
    AddRemovablePart(repr2, rec2);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, repr2);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_TRUE(c.graph.files[1].is_live);
    EXPECT_TRUE(c.graph.files[2].is_live);
    EXPECT_TRUE(JSReprOf(c.graph, 1).ast.parts[0].is_live);
    EXPECT_TRUE(JSReprOf(c.graph, 2).ast.parts[0].is_live);
}

TEST(MarkFileLive, EntryKeepsRemovablePartsWhenTreeShakingDisabled)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    AddUnremovablePart(repr1); // placeholder part 0 unused here
    js::Part removable;
    removable.can_be_removed_if_unused = true;
    repr1->ast.parts.push_back(std::move(removable));
    SetJS(graph, 1, repr1);
    MakeEntry(graph, 1);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_TRUE(JSReprOf(c.graph, 1).ast.parts[1].is_live);
}

TEST(MarkFileLive, TreeShakingEnabledDropsRemovableEntryParts)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    js::Part removable;
    removable.can_be_removed_if_unused = true;
    repr1->ast.parts.push_back(std::move(removable));
    SetJS(graph, 1, repr1);
    MakeEntry(graph, 1);

    cf::Options options;
    options.TreeShaking = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_TRUE(c.graph.files[1].is_live);
    EXPECT_FALSE(JSReprOf(c.graph, 1).ast.parts[0].is_live);
}

TEST(MarkFileLive, NonEntryFileKeepsOnlyNonRemovableParts)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    js::Part removable;
    removable.can_be_removed_if_unused = true;
    repr1->ast.parts.push_back(std::move(removable));
    js::Part unremovable;
    repr1->ast.parts.push_back(std::move(unremovable));
    SetJS(graph, 1, repr1);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileLiveForTreeShaking(1);

    EXPECT_FALSE(JSReprOf(c.graph, 1).ast.parts[0].is_live);
    EXPECT_TRUE(JSReprOf(c.graph, 1).ast.parts[1].is_live);
}

// ---------------------------------------------------------------------------
// MarkPartLiveForTreeShaking

TEST(MarkPartLive, MarksPartAndOwningFileLive)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    js::Part part;
    repr1->ast.parts.push_back(std::move(part));
    SetJS(graph, 1, repr1);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkPartLiveForTreeShaking(1, 0);

    EXPECT_TRUE(c.graph.files[1].is_live);
    EXPECT_TRUE(JSReprOf(c.graph, 1).ast.parts[0].is_live);
}

TEST(MarkPartLive, RecursesIntoDependencies)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    js::Part part1;
    part1.dependencies.push_back(js::Dependency{.source_index = 2, .part_index = 1});
    repr1->ast.parts.resize(1);
    repr1->ast.parts[0] = std::move(part1);
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.parts.resize(2);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, repr2);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkPartLiveForTreeShaking(1, 0);

    EXPECT_TRUE(JSReprOf(c.graph, 1).ast.parts[0].is_live);
    EXPECT_TRUE(JSReprOf(c.graph, 2).ast.parts[1].is_live);
}

TEST(MarkPartLive, CycleStopsViaLiveFlag)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    repr1->ast.parts.resize(1);
    repr1->ast.parts[0].dependencies.push_back(js::Dependency{.source_index = 2, .part_index = 1});
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.parts.resize(2);
    repr2->ast.parts[1].dependencies.push_back(js::Dependency{.source_index = 1, .part_index = 0});
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, repr2);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkPartLiveForTreeShaking(1, 0);

    EXPECT_TRUE(JSReprOf(c.graph, 1).ast.parts[0].is_live);
    EXPECT_TRUE(JSReprOf(c.graph, 2).ast.parts[1].is_live);
}

// ---------------------------------------------------------------------------
// IsExternalDynamicImport

cm::ImportRecord MakeRecord(cm::ImportKind kind, uint32_t source_index)
{
    cm::ImportRecord record;
    record.kind = kind;
    if (source_index != UINT32_MAX) {
        record.source_index = cm::Index32::Make(source_index);
    }
    return record;
}

TEST(IsExternalDynamicImport, TrueForDynamicImportOfAnotherEntry)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    MakeEntry(graph, 2);

    cf::Options options;
    options.CodeSplitting = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    EXPECT_TRUE(c.IsExternalDynamicImport(MakeRecord(cm::ImportKind::kDynamic, 2), 1));
}

TEST(IsExternalDynamicImport, FalseForDynamicImportOfSelf)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    MakeEntry(graph, 1);

    cf::Options options;
    options.CodeSplitting = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    EXPECT_FALSE(c.IsExternalDynamicImport(MakeRecord(cm::ImportKind::kDynamic, 1), 1));
}

TEST(IsExternalDynamicImport, FalseWhenCodeSplittingDisabled)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    MakeEntry(graph, 2);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    EXPECT_FALSE(c.IsExternalDynamicImport(MakeRecord(cm::ImportKind::kDynamic, 2), 1));
}

TEST(IsExternalDynamicImport, FalseForStaticImports)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    MakeEntry(graph, 2);

    cf::Options options;
    options.CodeSplitting = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    EXPECT_FALSE(c.IsExternalDynamicImport(MakeRecord(cm::ImportKind::kStmt, 2), 1));
}

TEST(IsExternalDynamicImport, FalseWhenTargetIsNotAnEntryPoint)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);

    cf::Options options;
    options.CodeSplitting = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    EXPECT_FALSE(c.IsExternalDynamicImport(MakeRecord(cm::ImportKind::kDynamic, 2), 1));
}

TEST(IsExternalDynamicImport, FalseWhenSourceUnresolved)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    MakeEntry(graph, 2);

    cf::Options options;
    options.CodeSplitting = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    EXPECT_FALSE(c.IsExternalDynamicImport(MakeRecord(cm::ImportKind::kDynamic, UINT32_MAX), 1));
}

// ---------------------------------------------------------------------------
// MarkFileReachableForCodeSplitting

TEST(MarkFileReachable, MarksEntryPointBitAndDistance)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    PrepareReachability(graph);
    SetJS(graph, 1, std::make_shared<g::JSRepr>());
    graph.files[1].is_live = true;

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileReachableForCodeSplitting(1, 0, 0);

    EXPECT_TRUE(c.graph.files[1].entry_bits.HasBit(0));
    EXPECT_EQ(c.graph.files[1].distance_from_entry_point, 0u);
}

TEST(MarkFileReachable, PropagatesToStaticallyImportedFiles)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    PrepareReachability(graph);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    AddRemovablePart(repr1, rec);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, std::make_shared<g::JSRepr>());
    graph.files[1].is_live = true;
    graph.files[2].is_live = true;

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileReachableForCodeSplitting(1, 0, 0);

    EXPECT_TRUE(c.graph.files[1].entry_bits.HasBit(0));
    EXPECT_TRUE(c.graph.files[2].entry_bits.HasBit(0));
    EXPECT_EQ(c.graph.files[2].distance_from_entry_point, 1u);
}

TEST(MarkFileReachable, SkipsNonLiveImports)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    PrepareReachability(graph);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    AddRemovablePart(repr1, rec);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, std::make_shared<g::JSRepr>());
    graph.files[1].is_live = true;

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileReachableForCodeSplitting(1, 0, 0);

    EXPECT_FALSE(c.graph.files[2].entry_bits.HasBit(0));
    EXPECT_EQ(c.graph.files[2].distance_from_entry_point, UINT32_MAX);
}

TEST(MarkFileReachable, UpdatesDistanceWhenShorterAndKeepsWhenLonger)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    PrepareReachability(graph);
    SetJS(graph, 2, std::make_shared<g::JSRepr>());
    graph.files[2].is_live = true;

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileReachableForCodeSplitting(2, 1, 1);

    EXPECT_EQ(c.graph.files[2].distance_from_entry_point, 1u);
    EXPECT_TRUE(c.graph.files[2].entry_bits.HasBit(1));

    c.MarkFileReachableForCodeSplitting(2, 0, 0);

    EXPECT_EQ(c.graph.files[2].distance_from_entry_point, 0u);
    EXPECT_TRUE(c.graph.files[2].entry_bits.HasBit(0));

    c.MarkFileReachableForCodeSplitting(2, 1, 7);

    EXPECT_EQ(c.graph.files[2].distance_from_entry_point, 0u);
    EXPECT_TRUE(c.graph.files[2].entry_bits.HasBit(1));
}

TEST(MarkFileReachable, FollowsPartDependenciesAndCSSAssociation)
{
    g::LinkerGraph graph = MakeLinkerGraph(5);
    PrepareReachability(graph);
    auto repr1 = std::make_shared<g::JSRepr>();
    repr1->css_source_index = cm::Index32::Make(4);
    repr1->ast.parts.resize(1);
    repr1->ast.parts[0].dependencies.push_back(js::Dependency{.source_index = 3, .part_index = 0});
    SetJS(graph, 1, repr1);
    SetJS(graph, 3, std::make_shared<g::JSRepr>());
    SetCSS(graph, 4, std::make_shared<g::CSSRepr>());
    graph.files[1].is_live = true;
    graph.files[3].is_live = true;
    graph.files[4].is_live = true;

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileReachableForCodeSplitting(1, 0, 0);

    EXPECT_TRUE(c.graph.files[3].entry_bits.HasBit(0));
    EXPECT_EQ(c.graph.files[3].distance_from_entry_point, 1u);
    EXPECT_TRUE(c.graph.files[4].entry_bits.HasBit(0));
    EXPECT_EQ(c.graph.files[4].distance_from_entry_point, 1u);
}

TEST(MarkFileReachable, SkipsExternalDynamicImportEdge)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    PrepareReachability(graph);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kDynamic, 2);
    AddRemovablePart(repr1, rec);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, std::make_shared<g::JSRepr>());
    MakeEntry(graph, 2);
    graph.files[1].is_live = true;
    graph.files[2].is_live = true;

    cf::Options options;
    options.CodeSplitting = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MarkFileReachableForCodeSplitting(1, 0, 0);

    EXPECT_FALSE(c.graph.files[2].entry_bits.HasBit(0));
    EXPECT_EQ(c.graph.files[2].distance_from_entry_point, UINT32_MAX);
}

// ---------------------------------------------------------------------------
// RecursivelyWrapDependencies

TEST(RecursivelyWrap, ESMExportsGetESMWrap)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    repr1->ast.exports_kind = js::ExportsKind::kESM;
    SetJS(graph, 1, repr1);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.RecursivelyWrapDependencies(1);

    EXPECT_EQ(JSReprOf(c.graph, 1).meta.wrap, g::WrapKind::kESM);
    EXPECT_TRUE(JSReprOf(c.graph, 1).meta.did_wrap_dependencies);
}

TEST(RecursivelyWrap, CommonJSGetsCJSWrap)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    repr1->ast.exports_kind = js::ExportsKind::kCommonJS;
    SetJS(graph, 1, repr1);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.RecursivelyWrapDependencies(1);

    EXPECT_EQ(JSReprOf(c.graph, 1).meta.wrap, g::WrapKind::kCJS);
}

TEST(RecursivelyWrap, PreservesExplicitWrap)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    repr1->ast.exports_kind = js::ExportsKind::kCommonJS;
    repr1->meta.wrap = g::WrapKind::kESM;
    SetJS(graph, 1, repr1);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.RecursivelyWrapDependencies(1);

    EXPECT_EQ(JSReprOf(c.graph, 1).meta.wrap, g::WrapKind::kESM);
}

TEST(RecursivelyWrap, RecursesIntoImports)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    repr1->ast.parts.push_back(js::Part{});
    repr1->ast.parts[0].import_record_indices.push_back(rec);
    SetJS(graph, 1, repr1);
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.exports_kind = js::ExportsKind::kCommonJS;
    SetJS(graph, 2, repr2);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.RecursivelyWrapDependencies(1);

    EXPECT_TRUE(JSReprOf(c.graph, 1).meta.did_wrap_dependencies);
    EXPECT_EQ(JSReprOf(c.graph, 2).meta.wrap, g::WrapKind::kCJS);
}

TEST(RecursivelyWrap, SkipsRuntimeFile)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    SetJS(graph, js::kSourceIndex, std::make_shared<g::JSRepr>());

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.RecursivelyWrapDependencies(js::kSourceIndex);

    EXPECT_EQ(JSReprOf(c.graph, js::kSourceIndex).meta.wrap, g::WrapKind::kNone);
}

// ---------------------------------------------------------------------------
// HasDynamicExportsDueToExportStar

void AddStarRecord(std::shared_ptr<g::JSRepr> repr, uint32_t source_index)
{
    uint32_t index = AddJSRecord(repr, cm::ImportKind::kStmt, source_index);
    repr->ast.export_star_import_records.push_back(index);
}

TEST(HasDynamicExportStar, CommonJSReturnsTrueImmediately)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    repr1->ast.exports_kind = js::ExportsKind::kCommonJS;
    SetJS(graph, 1, repr1);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    std::unordered_set<uint32_t> visited;

    EXPECT_TRUE(c.HasDynamicExportsDueToExportStar(1, visited));
}

TEST(HasDynamicExportStar, AlreadyDynamicFallbackReturnsTrue)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    repr1->ast.exports_kind = js::ExportsKind::kESMWithDynamicFallback;
    SetJS(graph, 1, repr1);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    std::unordered_set<uint32_t> visited;

    EXPECT_TRUE(c.HasDynamicExportsDueToExportStar(1, visited));
}

TEST(HasDynamicExportStar, ExternalStarPromotedWhenFormatCantKeepESM)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    AddStarRecord(repr1, UINT32_MAX);
    SetJS(graph, 1, repr1);

    cf::Options options;
    options.OutputFormat = cf::Format::kCommonJS;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    std::unordered_set<uint32_t> visited;

    EXPECT_TRUE(c.HasDynamicExportsDueToExportStar(1, visited));
    EXPECT_EQ(JSReprOf(c.graph, 1).ast.exports_kind, js::ExportsKind::kESMWithDynamicFallback);
}

TEST(HasDynamicExportStar, ExternalStarKeptWhenEntryKeepsESMSyntax)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    AddStarRecord(repr1, UINT32_MAX);
    SetJS(graph, 1, repr1);
    MakeEntry(graph, 1);

    cf::Options options;
    options.OutputFormat = cf::Format::kESModule;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    std::unordered_set<uint32_t> visited;

    EXPECT_FALSE(c.HasDynamicExportsDueToExportStar(1, visited));
    EXPECT_EQ(JSReprOf(c.graph, 1).ast.exports_kind, js::ExportsKind::kNone);
}

TEST(HasDynamicExportStar, ResolvedTargetPromotesDynamicFallback)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    AddStarRecord(repr1, 2);
    SetJS(graph, 1, repr1);
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.exports_kind = js::ExportsKind::kCommonJS;
    SetJS(graph, 2, repr2);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    std::unordered_set<uint32_t> visited;

    EXPECT_TRUE(c.HasDynamicExportsDueToExportStar(1, visited));
    EXPECT_EQ(JSReprOf(c.graph, 1).ast.exports_kind, js::ExportsKind::kESMWithDynamicFallback);
}

TEST(HasDynamicExportStar, CycleDoesNotPromote)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    AddStarRecord(repr1, 2);
    SetJS(graph, 1, repr1);
    auto repr2 = std::make_shared<g::JSRepr>();
    AddStarRecord(repr2, 1);
    SetJS(graph, 2, repr2);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    std::unordered_set<uint32_t> visited;

    EXPECT_FALSE(c.HasDynamicExportsDueToExportStar(1, visited));
    EXPECT_EQ(JSReprOf(c.graph, 1).ast.exports_kind, js::ExportsKind::kNone);
    EXPECT_EQ(JSReprOf(c.graph, 2).ast.exports_kind, js::ExportsKind::kNone);
}

// ---------------------------------------------------------------------------
// AddExportsForExportStar

TEST(AddExportsForExportStar, CopiesNamedExportsSkippingDefault)
{
    g::LinkerGraph graph = MakeLinkerGraph(4);
    auto repr2 = std::make_shared<g::JSRepr>();
    AddStarRecord(repr2, 3);
    SetJS(graph, 2, repr2);
    auto repr3 = std::make_shared<g::JSRepr>();
    repr3->ast.named_exports["x"] = js::NamedExport{cm::Ref{3, 0}, lg::Loc{}};
    repr3->ast.named_exports["default"] = js::NamedExport{cm::Ref{3, 1}, lg::Loc{}};
    SetJS(graph, 3, repr3);

    std::unordered_map<std::string, g::ExportData> resolved;
    std::vector<uint32_t> stack;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.AddExportsForExportStar(resolved, 2, stack);

    ASSERT_EQ(resolved.size(), 1u);
    cm::Ref export_ref{3, 0};
    EXPECT_EQ(resolved["x"].ref, export_ref);
    EXPECT_EQ(resolved["x"].source_index, 3u);
    const g::ImportData& bind = JSReprOf(c.graph, 2).meta.imports_to_bind[export_ref];
    EXPECT_EQ(bind.ref, export_ref);
    EXPECT_EQ(bind.source_index, 3u);
}

TEST(AddExportsForExportStar, SkipsCommonJSTarget)
{
    g::LinkerGraph graph = MakeLinkerGraph(4);
    auto repr2 = std::make_shared<g::JSRepr>();
    AddStarRecord(repr2, 3);
    SetJS(graph, 2, repr2);
    auto repr3 = std::make_shared<g::JSRepr>();
    repr3->ast.exports_kind = js::ExportsKind::kCommonJS;
    repr3->ast.named_exports["x"] = js::NamedExport{cm::Ref{3, 0}, lg::Loc{}};
    SetJS(graph, 3, repr3);

    std::unordered_map<std::string, g::ExportData> resolved;
    std::vector<uint32_t> stack;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.AddExportsForExportStar(resolved, 2, stack);

    EXPECT_TRUE(resolved.empty());
}

TEST(AddExportsForExportStar, ExplicitExportsShadowStarExports)
{
    g::LinkerGraph graph = MakeLinkerGraph(4);
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.named_exports["x"] = js::NamedExport{cm::Ref{2, 5}, lg::Loc{}};
    AddStarRecord(repr2, 3);
    SetJS(graph, 2, repr2);
    auto repr3 = std::make_shared<g::JSRepr>();
    repr3->ast.named_exports["x"] = js::NamedExport{cm::Ref{3, 0}, lg::Loc{}};
    SetJS(graph, 3, repr3);

    std::unordered_map<std::string, g::ExportData> resolved;
    std::vector<uint32_t> stack;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.AddExportsForExportStar(resolved, 2, stack);

    EXPECT_TRUE(resolved.empty());
}

TEST(AddExportsForExportStar, RecordsConflictFromDifferentSourcesAsAmbiguous)
{
    g::LinkerGraph graph = MakeLinkerGraph(5);
    auto repr2 = std::make_shared<g::JSRepr>();
    AddStarRecord(repr2, 3);
    AddStarRecord(repr2, 4);
    SetJS(graph, 2, repr2);
    auto repr3 = std::make_shared<g::JSRepr>();
    repr3->ast.named_exports["x"] = js::NamedExport{cm::Ref{3, 0}, lg::Loc{}};
    SetJS(graph, 3, repr3);
    auto repr4 = std::make_shared<g::JSRepr>();
    repr4->ast.named_exports["x"] = js::NamedExport{cm::Ref{4, 0}, lg::Loc{}};
    SetJS(graph, 4, repr4);

    std::unordered_map<std::string, g::ExportData> resolved;
    std::vector<uint32_t> stack;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.AddExportsForExportStar(resolved, 2, stack);

    ASSERT_EQ(resolved.size(), 1u);
    cm::Ref kept_ref{3, 0};
    EXPECT_EQ(resolved["x"].ref, kept_ref);
    EXPECT_EQ(resolved["x"].source_index, 3u);
    ASSERT_EQ(resolved["x"].potentially_ambiguous_export_star_refs.size(), 1u);
    cm::Ref other_ref{4, 0};
    EXPECT_EQ(resolved["x"].potentially_ambiguous_export_star_refs[0].ref, other_ref);
    EXPECT_EQ(resolved["x"].potentially_ambiguous_export_star_refs[0].source_index, 4u);
}

TEST(AddExportsForExportStar, SameSourceAliasIsNotAmbiguous)
{
    g::LinkerGraph graph = MakeLinkerGraph(4);
    auto repr2 = std::make_shared<g::JSRepr>();
    AddStarRecord(repr2, 3);
    AddStarRecord(repr2, 3);
    SetJS(graph, 2, repr2);
    auto repr3 = std::make_shared<g::JSRepr>();
    repr3->ast.named_exports["x"] = js::NamedExport{cm::Ref{3, 0}, lg::Loc{}};
    SetJS(graph, 3, repr3);

    std::unordered_map<std::string, g::ExportData> resolved;
    std::vector<uint32_t> stack;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.AddExportsForExportStar(resolved, 2, stack);

    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_TRUE(resolved["x"].potentially_ambiguous_export_star_refs.empty());
}

TEST(AddExportsForExportStar, RecursesThroughChainsAndStopsCycles)
{
    g::LinkerGraph graph = MakeLinkerGraph(5);
    auto repr2 = std::make_shared<g::JSRepr>();
    AddStarRecord(repr2, 3);
    SetJS(graph, 2, repr2);
    auto repr3 = std::make_shared<g::JSRepr>();
    repr3->ast.named_exports["y"] = js::NamedExport{cm::Ref{3, 0}, lg::Loc{}};
    AddStarRecord(repr3, 4);
    SetJS(graph, 3, repr3);
    auto repr4 = std::make_shared<g::JSRepr>();
    repr4->ast.named_exports["z"] = js::NamedExport{cm::Ref{4, 0}, lg::Loc{}};
    AddStarRecord(repr4, 2);
    SetJS(graph, 4, repr4);

    std::unordered_map<std::string, g::ExportData> resolved;
    std::vector<uint32_t> stack;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.AddExportsForExportStar(resolved, 2, stack);

    ASSERT_EQ(resolved.size(), 2u);
    cm::Ref y_ref{3, 0};
    cm::Ref z_ref{4, 0};
    EXPECT_EQ(resolved["y"].ref, y_ref);
    EXPECT_EQ(resolved["z"].ref, z_ref);
}

// ---------------------------------------------------------------------------
// AdvanceImportTracker

lnk::ImportTracker MakeTracker(uint32_t source_index, cm::Ref import_ref)
{
    return lnk::ImportTracker{.source_index = source_index, .import_ref = import_ref};
}

TEST(AdvanceImportTracker, NoMatchWhenImportNotListed)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    SetJS(graph, 1, std::make_shared<g::JSRepr>());

    lnk::ImportStatus status = lnk::ImportStatus::kNoMatch;
    std::vector<g::ImportData> ambiguous;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    auto result = c.AdvanceImportTracker(MakeTracker(1, cm::Ref{1, 42}), status, ambiguous);

    EXPECT_EQ(status, lnk::ImportStatus::kNoMatch);
    EXPECT_EQ(result.import_ref, cm::kInvalidRef);
    EXPECT_EQ(result.source_index, 0u);
}

TEST(AdvanceImportTracker, ExternalWhenRecordUnresolved)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, UINT32_MAX);
    repr1->ast.named_imports[cm::Ref{1, 0}] = MakeNamedImport("thing", rec);
    SetJS(graph, 1, repr1);

    lnk::ImportStatus status = lnk::ImportStatus::kNoMatch;
    std::vector<g::ImportData> ambiguous;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    auto result = c.AdvanceImportTracker(MakeTracker(1, cm::Ref{1, 0}), status, ambiguous);

    EXPECT_EQ(status, lnk::ImportStatus::kExternal);
    EXPECT_EQ(result.import_ref, cm::kInvalidRef);
}

TEST(AdvanceImportTracker, CommonJSWithoutExports)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    repr1->ast.named_imports[cm::Ref{1, 0}] = MakeNamedImport("thing", rec);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, std::make_shared<g::JSRepr>());

    lnk::ImportStatus status = lnk::ImportStatus::kNoMatch;
    std::vector<g::ImportData> ambiguous;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    auto result = c.AdvanceImportTracker(MakeTracker(1, cm::Ref{1, 0}), status, ambiguous);

    EXPECT_EQ(status, lnk::ImportStatus::kCommonJSWithoutExports);
    EXPECT_EQ(result.source_index, 2u);
    EXPECT_EQ(result.import_ref, cm::kInvalidRef);
}

TEST(AdvanceImportTracker, LazyExportSkipsCommonJSWithoutExports)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    repr1->ast.named_imports[cm::Ref{1, 0}] = MakeNamedImport("thing", rec);
    SetJS(graph, 1, repr1);
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.has_lazy_export = true;
    SetJS(graph, 2, repr2);

    lnk::ImportStatus status = lnk::ImportStatus::kNoMatch;
    std::vector<g::ImportData> ambiguous;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    auto result = c.AdvanceImportTracker(MakeTracker(1, cm::Ref{1, 0}), status, ambiguous);

    EXPECT_EQ(status, lnk::ImportStatus::kNoMatch);
    EXPECT_EQ(result.source_index, 2u);
}

TEST(AdvanceImportTracker, CommonJSModule)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    repr1->ast.named_imports[cm::Ref{1, 0}] = MakeNamedImport("thing", rec);
    SetJS(graph, 1, repr1);
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.exports_kind = js::ExportsKind::kCommonJS;
    repr2->ast.uses_module_ref = true;
    SetJS(graph, 2, repr2);

    lnk::ImportStatus status = lnk::ImportStatus::kNoMatch;
    std::vector<g::ImportData> ambiguous;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    auto result = c.AdvanceImportTracker(MakeTracker(1, cm::Ref{1, 0}), status, ambiguous);

    EXPECT_EQ(status, lnk::ImportStatus::kCommonJS);
    EXPECT_EQ(result.source_index, 2u);
    EXPECT_EQ(result.import_ref, cm::kInvalidRef);
}

TEST(AdvanceImportTracker, FoundViaResolvedExports)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    repr1->ast.named_imports[cm::Ref{1, 0}] = MakeNamedImport("thing", rec);
    SetJS(graph, 1, repr1);
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.export_keyword.len = 1;
    repr2->meta.resolved_exports["thing"] = g::ExportData{
        .ref = cm::Ref{2, 7},
        .name_loc = {.start = 4},
        .source_index = 2,
    };
    SetJS(graph, 2, repr2);

    lnk::ImportStatus status = lnk::ImportStatus::kNoMatch;
    std::vector<g::ImportData> ambiguous;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    auto result = c.AdvanceImportTracker(MakeTracker(1, cm::Ref{1, 0}), status, ambiguous);

    EXPECT_EQ(status, lnk::ImportStatus::kFound);
    EXPECT_EQ(result.source_index, 2u);
    cm::Ref local_ref{2, 7};
    EXPECT_EQ(result.import_ref, local_ref);
    EXPECT_EQ(result.name_loc.start, 4);
}

TEST(AdvanceImportTracker, StarAliasViaResolvedExportStar)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    js::NamedImport star_import = MakeNamedImport("*", rec);
    star_import.alias_is_star = true;
    repr1->ast.named_imports[cm::Ref{1, 0}] = std::move(star_import);
    SetJS(graph, 1, repr1);
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.export_keyword.len = 1;
    repr2->meta.resolved_export_star = g::ExportData{
        .ref = cm::Ref{3, 2},
        .name_loc = {.start = 9},
        .source_index = 3,
    };
    SetJS(graph, 2, repr2);

    lnk::ImportStatus status = lnk::ImportStatus::kNoMatch;
    std::vector<g::ImportData> ambiguous;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    auto result = c.AdvanceImportTracker(MakeTracker(1, cm::Ref{1, 0}), status, ambiguous);

    EXPECT_EQ(status, lnk::ImportStatus::kFound);
    EXPECT_EQ(result.source_index, 3u);
    cm::Ref star_ref{3, 2};
    EXPECT_EQ(result.import_ref, star_ref);
    EXPECT_EQ(result.name_loc.start, 9);
}

TEST(AdvanceImportTracker, DynamicFallbackWhenExportMissing)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    repr1->ast.named_imports[cm::Ref{1, 0}] = MakeNamedImport("thing", rec);
    SetJS(graph, 1, repr1);
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.exports_kind = js::ExportsKind::kESMWithDynamicFallback;
    repr2->ast.exports_ref = cm::Ref{2, 9};
    repr2->ast.export_keyword.len = 1;
    SetJS(graph, 2, repr2);

    lnk::ImportStatus status = lnk::ImportStatus::kNoMatch;
    std::vector<g::ImportData> ambiguous;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    auto result = c.AdvanceImportTracker(MakeTracker(1, cm::Ref{1, 0}), status, ambiguous);

    EXPECT_EQ(status, lnk::ImportStatus::kDynamicFallback);
    EXPECT_EQ(result.source_index, 2u);
    cm::Ref exports_ref_local{2, 9};
    EXPECT_EQ(result.import_ref, exports_ref_local);
}

TEST(AdvanceImportTracker, ProbablyTypeScriptType)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    auto repr1 = std::make_shared<g::JSRepr>();
    uint32_t rec = AddJSRecord(repr1, cm::ImportKind::kStmt, 2);
    js::NamedImport ts_import = MakeNamedImport("thing", rec);
    ts_import.is_exported = true;
    repr1->ast.named_imports[cm::Ref{1, 0}] = std::move(ts_import);
    graph.files[1].input_file.loader = cf::Loader::kTS;
    SetJS(graph, 1, repr1);
    auto repr2 = std::make_shared<g::JSRepr>();
    repr2->ast.export_keyword.len = 1;
    SetJS(graph, 2, repr2);

    lnk::ImportStatus status = lnk::ImportStatus::kNoMatch;
    std::vector<g::ImportData> ambiguous;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.AdvanceImportTracker(MakeTracker(1, cm::Ref{1, 0}), status, ambiguous);

    EXPECT_EQ(status, lnk::ImportStatus::kProbablyTypeScriptType);
}