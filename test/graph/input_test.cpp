// Unit tests for the scan-to-compile input layer described in the "Input
// section" of graph.hpp and implemented in src/graph/input.cpp: the
// InputFile/InputFileRepr payload union, the GetImportRecords dispatch that
// retrieves a payload's import records regardless of which concrete kind it
// holds, and the TopLevelSymbolToParts overlay that answers queries against
// both the parser's map and the linker's mutable extension layer.

#include "test/guchho_test.hpp"

#include "guchho/compiler.hpp"
#include "guchho/graph.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/logger.hpp"

#include <memory>
#include <string>
#include <vector>

namespace compiler   = guchho::compiler;
namespace config     = guchho::config;
namespace graph      = guchho::graph;


namespace {

// Builds a JavaScript payload with one import record whose path text is
// exactly "path". Callers that need several records push more afterward,
// since ImportRecords() exposes the live vector for exactly that purpose.
std::shared_ptr<graph::JSRepr> MakeJSRepr(const std::string& path) {
    auto repr = std::make_shared<graph::JSRepr>();
    compiler::ImportRecord record;
    record.path.text = path;
    repr->ImportRecords().push_back(std::move(record));
    return repr;
}

// Builds a CSS payload carrying "path" as its single import record.
std::shared_ptr<graph::CSSRepr> MakeCSSRepr(const std::string& path) {
    auto repr = std::make_shared<graph::CSSRepr>();
    compiler::ImportRecord record;
    record.path.text = path;
    repr->ImportRecords().push_back(std::move(record));
    return repr;
}

// Builds an HTML payload carrying "path" as its single import record.
std::shared_ptr<graph::HTMLRepr> MakeHTMLRepr(const std::string& path) {
    auto repr = std::make_shared<graph::HTMLRepr>();
    compiler::ImportRecord record;
    record.path.text = path;
    repr->ImportRecords().push_back(std::move(record));
    return repr;
}

// Returns the first import record held by "repr" as a tagged union, using
// the free GetImportRecords dispatch that the rest of the codebase relies on.
compiler::ImportRecord* FirstRecord(graph::InputFileRepr& repr) {
    std::vector<compiler::ImportRecord>* records = graph::GetImportRecords(repr);
    if (records == nullptr || records->empty()) {
        return nullptr;
    }
    return &(*records)[0];
}

} // namespace

// ---------------------------------------------------------------------------
// InputFile defaults
// ---------------------------------------------------------------------------

TEST(GraphInput, InputFileDefaultsToMonostateRepr) {
    graph::InputFile file;
    const auto& repr = file.repr;
    EXPECT_TRUE(std::holds_alternative<std::monostate>(repr));
    EXPECT_EQ(nullptr, graph::GetImportRecords(repr));
}

TEST(GraphInput, InputFileDefaultsToNoneLoaderAndHasSideEffects) {
    graph::InputFile file;
    EXPECT_EQ(config::Loader::kNone, file.loader);
    EXPECT_EQ(graph::SideEffectsKind::kHasSideEffects, file.side_effects.kind);
    EXPECT_FALSE(file.omit_from_source_maps_and_metafile);
}

// ---------------------------------------------------------------------------
// GetImportRecords dispatch
// ---------------------------------------------------------------------------

TEST(GraphInput, GetImportRecordsReturnsNullForEmptyAndCopyReprs) {
    graph::InputFile empty;
    EXPECT_EQ(nullptr, graph::GetImportRecords(empty.repr));

    graph::InputFile copy;
    copy.repr = std::make_shared<graph::CopyRepr>();
    EXPECT_EQ(nullptr, graph::GetImportRecords(copy.repr));
}

TEST(GraphInput, GetImportRecordsReturnsJSRecords) {
    graph::InputFile file;
    file.repr = MakeJSRepr("dep.js");
    EXPECT_NE(nullptr, FirstRecord(file.repr));
    EXPECT_EQ("dep.js", FirstRecord(file.repr)->path.text);
}

TEST(GraphInput, GetImportRecordsReturnsCSSRecords) {
    graph::InputFile file;
    file.repr = MakeCSSRepr("dep.css");
    EXPECT_NE(nullptr, FirstRecord(file.repr));
    EXPECT_EQ("dep.css", FirstRecord(file.repr)->path.text);
}

TEST(GraphInput, GetImportRecordsReturnsHTMLRecords) {
    graph::InputFile file;
    file.repr = MakeHTMLRepr("dep.html");
    EXPECT_NE(nullptr, FirstRecord(file.repr));
    EXPECT_EQ("dep.html", FirstRecord(file.repr)->path.text);
}

TEST(GraphInput, GetImportRecordsSwitchesVariant) {
    graph::InputFile file;

    file.repr = MakeJSRepr("a.js");
    EXPECT_EQ("a.js", FirstRecord(file.repr)->path.text);

    file.repr = MakeCSSRepr("a.css");
    EXPECT_EQ("a.css", FirstRecord(file.repr)->path.text);

    file.repr = MakeHTMLRepr("a.html");
    EXPECT_EQ("a.html", FirstRecord(file.repr)->path.text);
}

// ---------------------------------------------------------------------------
// JSRepr
// ---------------------------------------------------------------------------

TEST(GraphInput, JSReprImportRecordsAccessorsShareTheASTVector) {
    graph::JSRepr repr;
    repr.ImportRecords().push_back(compiler::ImportRecord{});
    EXPECT_EQ(1u, repr.ImportRecords().size());

    const graph::JSRepr& const_repr = repr;
    EXPECT_EQ(1u, const_repr.ImportRecords().size());
    EXPECT_EQ(&repr.ast.import_records, &const_repr.ImportRecords());
}

TEST(GraphInput, JSReprTopLevelSymbolToPartsUnknownRefIsEmpty) {
    graph::JSRepr repr;
    EXPECT_TRUE(repr.TopLevelSymbolToParts(compiler::Ref{0, 0}).empty());
    EXPECT_TRUE(repr.TopLevelSymbolToParts(compiler::Ref{9, 9}).empty());
}

TEST(GraphInput, JSReprTopLevelSymbolToPartsReadsParserMap) {
    graph::JSRepr repr;
    repr.ast.top_level_symbol_to_parts_from_parser[compiler::Ref{0, 1}] = {2, 3};
    const auto& parts = repr.TopLevelSymbolToParts(compiler::Ref{0, 1});
    ASSERT_EQ(2u, parts.size());
    EXPECT_EQ(2u, parts[0]);
    EXPECT_EQ(3u, parts[1]);
}

TEST(GraphInput, JSReprTopLevelSymbolToPartsOverlayWinsOverParser) {
    graph::JSRepr repr;
    repr.ast.top_level_symbol_to_parts_from_parser[compiler::Ref{0, 1}] = {2, 3};
    repr.meta.top_level_symbol_to_parts_overlay[compiler::Ref{0, 1}] = {5};
    const auto& parts = repr.TopLevelSymbolToParts(compiler::Ref{0, 1});
    ASSERT_EQ(1u, parts.size());
    EXPECT_EQ(5u, parts[0]);
}

TEST(GraphInput, JSReprTopLevelSymbolToPartsParserForDifferentRef) {
    graph::JSRepr repr;
    repr.ast.top_level_symbol_to_parts_from_parser[compiler::Ref{0, 1}] = {2};
    EXPECT_TRUE(repr.TopLevelSymbolToParts(compiler::Ref{0, 2}).empty());
}