// Tests for the linker chunk construction pass (linker_chunk.cpp).

#include "test/guchho_test.hpp"

#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/graph.hpp"
#include "guchho/helpers.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_runtime.hpp"
#include "guchho/linker.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace g = ::guchho::graph;
namespace js = ::guchho::javascript;
namespace cm = ::guchho::compiler;
namespace cf = ::guchho::config;
namespace hp = ::guchho::helpers;
namespace lnk = ::guchho::linker;

namespace {

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// A fresh graph with "source_count" files and a matching symbol table, ready
// for direct calls into the linker chunk helpers.
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

// A JS representation with "part_count" empty parts.
std::shared_ptr<g::JSRepr> MakeJSRepr(uint32_t part_count)
{
    auto repr = std::make_shared<g::JSRepr>();
    repr->ast.parts.resize(part_count);
    return repr;
}

// A "var a = 1;" style local statement with "decl_count" declarators.
js::Stmt MakeLocalStmt(js::LocalKind kind, bool is_export, size_t decl_count)
{
    auto slocal = std::make_shared<js::SLocal>();
    slocal->kind = kind;
    slocal->is_export = is_export;
    for (size_t i = 0; i < decl_count; i++) {
        slocal->decls.push_back(js::Decl{});
    }
    js::Stmt stmt;
    stmt.data = std::move(slocal);
    return stmt;
}

// A statement that is not a local declaration, so the merge stops at it.
js::Stmt MakeNonLocalStmt()
{
    auto sexpr = std::make_shared<js::SExpr>();
    js::Stmt stmt;
    stmt.data = std::move(sexpr);
    return stmt;
}

// A live part holding one plain declaration.
js::Part MakeLiveDeclPart()
{
    js::Part part;
    part.is_live = true;
    part.stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));
    return part;
}

// A live part that records an import but whose body is a plain declaration.
js::Part MakeLivePartWithImport(uint32_t record_index)
{
    js::Part part;
    part.is_live = true;
    part.import_record_indices.push_back(record_index);
    part.stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));
    return part;
}

// A live part whose body is exactly one import statement.
js::Part MakeLiveSingleImportPart(uint32_t record_index)
{
    js::Part part;
    part.is_live = true;
    auto simport = std::make_shared<js::SImport>();
    simport->import_record_index = record_index;
    js::Stmt stmt;
    stmt.data = std::move(simport);
    part.stmts.push_back(std::move(stmt));
    return part;
}

// A one-bit entry-point set with the single bit at "entry_index".
hp::BitSet SingleEntryBitSet(uint32_t entry_index)
{
    hp::BitSet bits(1);
    bits.SetBit(entry_index, true);
    return bits;
}

} // namespace

// ---------------------------------------------------------------------------
// MergeAdjacentLocalStmts
// ---------------------------------------------------------------------------

TEST(LinkerChunk, MergeAdjacentLocalStmtsFusesSameKindRun)
{
    std::vector<js::Stmt> stmts;
    stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));
    stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));

    auto result = lnk::MergeAdjacentLocalStmts(stmts);

    ASSERT_EQ(result.size(), 1u);
    auto* slocal = std::get_if<std::shared_ptr<js::SLocal>>(&result[0].data);
    ASSERT_TRUE(slocal != nullptr);
    EXPECT_EQ((*slocal)->decls.size(), 2u);
}

TEST(LinkerChunk, MergeAdjacentLocalStmtsFusesRunOfThree)
{
    std::vector<js::Stmt> stmts;
    stmts.push_back(MakeLocalStmt(js::LocalKind::kLet, false, 1));
    stmts.push_back(MakeLocalStmt(js::LocalKind::kLet, false, 2));
    stmts.push_back(MakeLocalStmt(js::LocalKind::kLet, false, 3));

    auto result = lnk::MergeAdjacentLocalStmts(stmts);

    ASSERT_EQ(result.size(), 1u);
    auto* slocal = std::get_if<std::shared_ptr<js::SLocal>>(&result[0].data);
    ASSERT_TRUE(slocal != nullptr);
    EXPECT_EQ((*slocal)->decls.size(), 6u);
}

TEST(LinkerChunk, MergeAdjacentLocalStmtsStopsAtNonLocalStatement)
{
    std::vector<js::Stmt> stmts;
    stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));
    stmts.push_back(MakeNonLocalStmt());
    stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));

    auto result = lnk::MergeAdjacentLocalStmts(stmts);

    ASSERT_EQ(result.size(), 3u);
    for (js::Stmt& stmt : result) {
        EXPECT_TRUE(std::get_if<std::shared_ptr<js::SLocal>>(&stmt.data) != nullptr ||
                    std::get_if<std::shared_ptr<js::SExpr>>(&stmt.data) != nullptr);
        if (auto* local = std::get_if<std::shared_ptr<js::SLocal>>(&stmt.data)) {
            EXPECT_EQ((*local)->decls.size(), 1u);
        }
    }
}

TEST(LinkerChunk, MergeAdjacentLocalStmtsDistinguishesKinds)
{
    std::vector<js::Stmt> stmts;
    stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));
    stmts.push_back(MakeLocalStmt(js::LocalKind::kConst, false, 1));

    auto result = lnk::MergeAdjacentLocalStmts(stmts);

    ASSERT_EQ(result.size(), 2u);
    auto* slocal = std::get_if<std::shared_ptr<js::SLocal>>(&result[0].data);
    ASSERT_TRUE(slocal != nullptr);
    EXPECT_EQ((*slocal)->kind, js::LocalKind::kVar);
    EXPECT_EQ((*slocal)->decls.size(), 1u);
}

TEST(LinkerChunk, MergeAdjacentLocalStmtsDistinguishesExportStatus)
{
    std::vector<js::Stmt> stmts;
    stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));
    stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, true, 1));

    auto result = lnk::MergeAdjacentLocalStmts(stmts);

    ASSERT_EQ(result.size(), 2u);
}

TEST(LinkerChunk, MergeAdjacentLocalStmtsResumesAfterInterruption)
{
    std::vector<js::Stmt> stmts;
    stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));
    stmts.push_back(MakeNonLocalStmt());
    stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));
    stmts.push_back(MakeLocalStmt(js::LocalKind::kVar, false, 1));

    auto result = lnk::MergeAdjacentLocalStmts(stmts);

    ASSERT_EQ(result.size(), 3u);
    EXPECT_TRUE(std::get_if<std::shared_ptr<js::SLocal>>(&result[0].data) != nullptr);
    EXPECT_TRUE(std::get_if<std::shared_ptr<js::SExpr>>(&result[1].data) != nullptr);
    auto* merged = std::get_if<std::shared_ptr<js::SLocal>>(&result[2].data);
    ASSERT_TRUE(merged != nullptr);
    EXPECT_EQ((*merged)->decls.size(), 2u);
}

TEST(LinkerChunk, MergeAdjacentLocalStmtsLeavesFirstStatementUntouched)
{
    auto first = std::make_shared<js::SLocal>();
    first->kind = js::LocalKind::kConst;
    first->decls.push_back(js::Decl{});

    auto second = std::make_shared<js::SLocal>();
    second->kind = js::LocalKind::kConst;
    second->decls.push_back(js::Decl{});

    std::vector<js::Stmt> stmts;
    js::Stmt first_stmt;
    first_stmt.data = first;
    stmts.push_back(std::move(first_stmt));
    js::Stmt second_stmt;
    second_stmt.data = second;
    stmts.push_back(std::move(second_stmt));

    auto result = lnk::MergeAdjacentLocalStmts(stmts);

    ASSERT_EQ(result.size(), 1u);
    auto* slocal = std::get_if<std::shared_ptr<js::SLocal>>(&result[0].data);
    ASSERT_TRUE(slocal != nullptr);
    EXPECT_NE((*slocal).get(), first.get());

    // The original statement was cloned, never mutated.
    EXPECT_EQ(first->decls.size(), 1u);
    EXPECT_EQ(second->decls.size(), 1u);
}

TEST(LinkerChunk, MergeAdjacentLocalStmtsHandlesEmptyInput)
{
    std::vector<js::Stmt> stmts;
    auto result = lnk::MergeAdjacentLocalStmts(stmts);
    EXPECT_TRUE(result.empty());
}

TEST(LinkerChunk, MergeAdjacentLocalStmtsSingleStatementIsUnchanged)
{
    std::vector<js::Stmt> stmts;
    stmts.push_back(MakeLocalStmt(js::LocalKind::kConst, true, 2));

    auto result = lnk::MergeAdjacentLocalStmts(stmts);

    ASSERT_EQ(result.size(), 1u);
    auto* slocal = std::get_if<std::shared_ptr<js::SLocal>>(&result[0].data);
    ASSERT_TRUE(slocal != nullptr);
    EXPECT_TRUE((*slocal)->is_export);
    EXPECT_EQ((*slocal)->decls.size(), 2u);
}

// ---------------------------------------------------------------------------
// LinkerContext::ShouldIncludePart
// ---------------------------------------------------------------------------

TEST(LinkerChunk, ShouldIncludePartSkipsSingleImportOfUnwrappedFile)
{
    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = MakeLinkerGraph(3);

    auto dependency = MakeJSRepr(1);
    dependency->meta.wrap = g::WrapKind::kNone;
    SetJS(c.graph, 1, dependency);

    auto importer = MakeJSRepr(1);
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(1);
    importer->ast.import_records.push_back(record);
    SetJS(c.graph, 2, importer);

    js::Part part = MakeLiveSingleImportPart(0);
    EXPECT_FALSE(c.ShouldIncludePart(*importer, part));
}

TEST(LinkerChunk, ShouldIncludePartKeepsSingleImportOfWrappedFile)
{
    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = MakeLinkerGraph(3);

    auto dependency = MakeJSRepr(1);
    dependency->meta.wrap = g::WrapKind::kCJS;
    SetJS(c.graph, 1, dependency);

    auto importer = MakeJSRepr(1);
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(1);
    importer->ast.import_records.push_back(record);
    SetJS(c.graph, 2, importer);

    js::Part part = MakeLiveSingleImportPart(0);
    EXPECT_TRUE(c.ShouldIncludePart(*importer, part));
}

TEST(LinkerChunk, ShouldIncludePartKeepsSingleImportOfExternalFile)
{
    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = MakeLinkerGraph(2);

    auto importer = MakeJSRepr(1);
    importer->ast.import_records.push_back(cm::ImportRecord{});
    SetJS(c.graph, 1, importer);

    js::Part part = MakeLiveSingleImportPart(0);
    EXPECT_TRUE(c.ShouldIncludePart(*importer, part));
}

TEST(LinkerChunk, ShouldIncludePartKeepsSingleImportOfCSSFile)
{
    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = MakeLinkerGraph(3);

    c.graph.files[1].input_file.repr = std::make_shared<g::CSSRepr>();

    auto importer = MakeJSRepr(1);
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(1);
    importer->ast.import_records.push_back(record);
    SetJS(c.graph, 2, importer);

    js::Part part = MakeLiveSingleImportPart(0);
    EXPECT_TRUE(c.ShouldIncludePart(*importer, part));
}

TEST(LinkerChunk, ShouldIncludePartKeepsMultiStatementPart)
{
    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = MakeLinkerGraph(3);

    auto dependency = MakeJSRepr(1);
    dependency->meta.wrap = g::WrapKind::kNone;
    SetJS(c.graph, 1, dependency);

    auto importer = MakeJSRepr(1);
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(1);
    importer->ast.import_records.push_back(record);
    SetJS(c.graph, 2, importer);

    js::Part part = MakeLivePartWithImport(0);
    EXPECT_TRUE(c.ShouldIncludePart(*importer, part));
}

// ---------------------------------------------------------------------------
// LinkerContext::FindImportedPartsInJSOrder
// ---------------------------------------------------------------------------

TEST(LinkerChunk, FindImportedPartsInJSOrderEmitsDependenciesBeforeDependents)
{
    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = MakeLinkerGraph(3);
    c.graph.stable_source_indices = {0, 1, 2};
    auto entry_bits = SingleEntryBitSet(0);

    auto runtime = MakeJSRepr(2);
    runtime->meta.wrap = g::WrapKind::kNone;
    runtime->ast.parts[0].is_live = true;
    runtime->ast.parts[1] = MakeLiveDeclPart();
    SetJS(c.graph, js::kSourceIndex, runtime);
    c.graph.files[js::kSourceIndex].entry_bits = entry_bits;
    c.graph.files[js::kSourceIndex].distance_from_entry_point = 0;

    auto entry = MakeJSRepr(2);
    entry->meta.wrap = g::WrapKind::kNone;
    entry->ast.parts[0].is_live = true;
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(2);
    record.kind = cm::ImportKind::kStmt;
    entry->ast.import_records.push_back(record);
    entry->ast.parts[1] = MakeLivePartWithImport(0);
    SetJS(c.graph, 1, entry);
    c.graph.files[1].entry_bits = entry_bits;
    c.graph.files[1].distance_from_entry_point = 0;

    auto dep = MakeJSRepr(2);
    dep->meta.wrap = g::WrapKind::kNone;
    dep->ast.parts[0].is_live = true;
    dep->ast.parts[1] = MakeLiveDeclPart();
    SetJS(c.graph, 2, dep);
    c.graph.files[2].entry_bits = entry_bits;
    c.graph.files[2].distance_from_entry_point = 1;

    lnk::ChunkInfo chunk;
    chunk.entry_bits = entry_bits;
    chunk.files_with_parts_in_chunk[1] = true;
    chunk.files_with_parts_in_chunk[2] = true;

    std::vector<uint32_t> js_files;
    std::vector<lnk::PartRange> js_parts;
    c.FindImportedPartsInJSOrder(chunk, js_files, js_parts);

    EXPECT_EQ(js_files, (std::vector<uint32_t>{js::kSourceIndex, 2, 1}));

    // Prefix first (runtime part 1), then: runtime ns part, entry ns part,
    // dep ns part + dep part 1 (extended into one range), entry part 1.
    ASSERT_EQ(js_parts.size(), 5u);
    EXPECT_EQ(js_parts[0].source_index, js::kSourceIndex);
    EXPECT_EQ(js_parts[0].part_index_begin, 1u);
    EXPECT_EQ(js_parts[0].part_index_end, 2u);

    EXPECT_EQ(js_parts[1].source_index, js::kSourceIndex);
    EXPECT_EQ(js_parts[1].part_index_begin, 0u);
    EXPECT_EQ(js_parts[1].part_index_end, 1u);

    EXPECT_EQ(js_parts[2].source_index, 1u);
    EXPECT_EQ(js_parts[2].part_index_begin, 0u);
    EXPECT_EQ(js_parts[2].part_index_end, 1u);

    // The dependency's namespace-export part and its single body part are
    // contiguous, so AppendOrExtendPartRange folds them into one range.
    EXPECT_EQ(js_parts[3].source_index, 2u);
    EXPECT_EQ(js_parts[3].part_index_begin, 0u);
    EXPECT_EQ(js_parts[3].part_index_end, 2u);

    EXPECT_EQ(js_parts[4].source_index, 1u);
    EXPECT_EQ(js_parts[4].part_index_begin, 1u);
    EXPECT_EQ(js_parts[4].part_index_end, 2u);
}

TEST(LinkerChunk, FindImportedPartsInJSOrderEmitsUnSplittableFileAsFullRange)
{
    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = MakeLinkerGraph(3);
    c.graph.stable_source_indices = {0, 1, 2};
    auto entry_bits = SingleEntryBitSet(0);

    auto runtime = MakeJSRepr(2);
    runtime->meta.wrap = g::WrapKind::kNone;
    runtime->ast.parts[0].is_live = true;
    runtime->ast.parts[1] = MakeLiveDeclPart();
    SetJS(c.graph, js::kSourceIndex, runtime);
    c.graph.files[js::kSourceIndex].entry_bits = entry_bits;
    c.graph.files[js::kSourceIndex].distance_from_entry_point = 0;

    auto entry = MakeJSRepr(2);
    entry->meta.wrap = g::WrapKind::kNone;
    entry->ast.parts[0].is_live = true;
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(2);
    record.kind = cm::ImportKind::kStmt;
    entry->ast.import_records.push_back(record);
    entry->ast.parts[1] = MakeLivePartWithImport(0);
    SetJS(c.graph, 1, entry);
    c.graph.files[1].entry_bits = entry_bits;
    c.graph.files[1].distance_from_entry_point = 0;

    // A CommonJS file cannot be split: it contributes one full range covering
    // every part instead of individual part ranges.
    auto cjs = MakeJSRepr(2);
    cjs->meta.wrap = g::WrapKind::kCJS;
    cjs->ast.parts[0].is_live = true;
    cjs->ast.parts[1].is_live = true;
    SetJS(c.graph, 2, cjs);
    c.graph.files[2].entry_bits = entry_bits;
    c.graph.files[2].distance_from_entry_point = 1;

    lnk::ChunkInfo chunk;
    chunk.entry_bits = entry_bits;
    chunk.files_with_parts_in_chunk[1] = true;
    chunk.files_with_parts_in_chunk[2] = true;

    std::vector<uint32_t> js_files;
    std::vector<lnk::PartRange> js_parts;
    c.FindImportedPartsInJSOrder(chunk, js_files, js_parts);

    EXPECT_EQ(js_files, (std::vector<uint32_t>{js::kSourceIndex, 2, 1}));

    // Prefix: runtime part 1, then the CommonJS file as one full range.
    ASSERT_EQ(js_parts.size(), 4u);
    EXPECT_EQ(js_parts[0].source_index, js::kSourceIndex);
    EXPECT_EQ(js_parts[0].part_index_begin, 1u);
    EXPECT_EQ(js_parts[0].part_index_end, 2u);

    EXPECT_EQ(js_parts[1].source_index, 2u);
    EXPECT_EQ(js_parts[1].part_index_begin, 0u);
    EXPECT_EQ(js_parts[1].part_index_end, 2u);

    // Body: runtime ns part, then the entry's ns part and part 1, which are
    // contiguous (the CommonJS file only contributed prefix parts) and fold
    // into a single range.
    EXPECT_EQ(js_parts[2].source_index, js::kSourceIndex);
    EXPECT_EQ(js_parts[2].part_index_begin, 0u);
    EXPECT_EQ(js_parts[2].part_index_end, 1u);

    EXPECT_EQ(js_parts[3].source_index, 1u);
    EXPECT_EQ(js_parts[3].part_index_begin, 0u);
    EXPECT_EQ(js_parts[3].part_index_end, 2u);
}

TEST(LinkerChunk, FindImportedPartsInJSOrderSkipsFilesOwnedByAnotherChunk)
{
    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = MakeLinkerGraph(3);
    c.graph.stable_source_indices = {0, 1, 2};

    hp::BitSet entry_bits(2);
    entry_bits.SetBit(0, true);
    hp::BitSet other_entry_bits(2);
    other_entry_bits.SetBit(1, true);

    auto runtime = MakeJSRepr(2);
    runtime->meta.wrap = g::WrapKind::kNone;
    runtime->ast.parts[0].is_live = true;
    runtime->ast.parts[1] = MakeLiveDeclPart();
    SetJS(c.graph, js::kSourceIndex, runtime);
    c.graph.files[js::kSourceIndex].entry_bits = entry_bits;

    auto entry = MakeJSRepr(2);
    entry->meta.wrap = g::WrapKind::kNone;
    entry->ast.parts[0].is_live = true;
    cm::ImportRecord record;
    record.source_index = cm::Index32::Make(2);
    record.kind = cm::ImportKind::kStmt;
    entry->ast.import_records.push_back(record);
    entry->ast.parts[1] = MakeLivePartWithImport(0);
    SetJS(c.graph, 1, entry);
    c.graph.files[1].entry_bits = entry_bits;

    // This file belongs to the other entry point's chunk. it is traversed
    // because the entry statically imports it, but it contributes no parts.
    auto dep = MakeJSRepr(2);
    dep->meta.wrap = g::WrapKind::kNone;
    dep->ast.parts[0].is_live = true;
    dep->ast.parts[1] = MakeLiveDeclPart();
    SetJS(c.graph, 2, dep);
    c.graph.files[2].entry_bits = other_entry_bits;

    lnk::ChunkInfo chunk;
    chunk.entry_bits = entry_bits;
    chunk.files_with_parts_in_chunk[1] = true;
    chunk.files_with_parts_in_chunk[2] = true;

    std::vector<uint32_t> js_files;
    std::vector<lnk::PartRange> js_parts;
    c.FindImportedPartsInJSOrder(chunk, js_files, js_parts);

    EXPECT_EQ(js_files, (std::vector<uint32_t>{js::kSourceIndex, 1}));

    ASSERT_EQ(js_parts.size(), 3u);
    EXPECT_EQ(js_parts[0].source_index, js::kSourceIndex);
    EXPECT_EQ(js_parts[0].part_index_begin, 1u);
    EXPECT_EQ(js_parts[0].part_index_end, 2u);

    EXPECT_EQ(js_parts[1].source_index, js::kSourceIndex);
    EXPECT_EQ(js_parts[1].part_index_begin, 0u);
    EXPECT_EQ(js_parts[1].part_index_end, 1u);

    // The entry's ns part and part 1 are contiguous in this chunk (the other
    // chunk's dependency contributed nothing) and fold into one range.
    EXPECT_EQ(js_parts[2].source_index, 1u);
    EXPECT_EQ(js_parts[2].part_index_begin, 0u);
    EXPECT_EQ(js_parts[2].part_index_end, 2u);
}