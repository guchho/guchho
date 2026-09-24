// Tests for the JavaScript chunk generation pass (linker_js.cpp):
//   - RequireOrImportMetaForSource
//   - RenameSymbolsInChunk
//   - GenerateGlobalNamePrefix / SanitizeGlobalName
//   - GenerateCodeForFileInChunkJS
//   - GenerateEntryPointTailJS

#include "test/guchho_test.hpp"

#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/graph.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_printer.hpp"
#include "guchho/javascript/js_renamer.hpp"
#include "guchho/linker.hpp"
#include "guchho/logger.hpp"

#include <memory>
#include <string>
#include <utility>

namespace g = ::guchho::graph;
namespace js = ::guchho::javascript;
namespace cm = ::guchho::compiler;
namespace cf = ::guchho::config;
namespace cp = ::guchho::compat;
namespace lg = ::guchho::logger;
namespace lnk = ::guchho::linker;

namespace {

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// A fresh graph with "source_count" files, a matching symbol table and a
// stable-source-index table sized to match, ready for direct calls into the
// linker JS helpers.
g::LinkerGraph MakeLinkerGraph(uint32_t source_count)
{
    g::LinkerGraph graph;
    graph.files.resize(source_count);
    graph.symbols = cm::NewSymbolMap(source_count);
    graph.stable_source_indices.resize(source_count);
    for (uint32_t i = 0; i < source_count; i++) {
        graph.stable_source_indices[i] = i;
    }
    return graph;
}

// Installs a JavaScript representation into a file slot.
void SetJS(g::LinkerGraph& graph, uint32_t source_index, std::shared_ptr<g::JSRepr> repr)
{
    graph.files[source_index].input_file.repr = std::move(repr);
}

// A JS representation with "part_count" parts. Every part starts dead, so a
// test must mark the ones it wants emitted with MakeLivePart.
std::shared_ptr<g::JSRepr> MakeJSRepr(uint32_t part_count)
{
    auto repr = std::make_shared<g::JSRepr>();
    repr->ast.parts.resize(part_count);
    return repr;
}

// Appends a renamable top-level symbol to a file's symbol table and returns
// its reference. The symbol is not linked to anything, so FollowSymbols stops
// at it immediately.
cm::Ref AddSymbol(g::LinkerGraph& graph, uint32_t source_index, std::string name,
                  cm::SymbolKind kind = cm::SymbolKind::kHoisted)
{
    auto& symbols = graph.symbols.symbols_for_source[source_index];
    auto ref = cm::Ref{source_index, static_cast<uint32_t>(symbols.size())};
    cm::Symbol symbol;
    symbol.original_name = std::move(name);
    symbol.kind = kind;
    symbol.link = cm::kInvalidRef;
    symbols.push_back(std::move(symbol));
    return ref;
}

// A "var name = value;" statement bound to "ref".
js::Stmt MakeLocalStmt(cm::Ref ref, double value)
{
    auto slocal = std::make_shared<js::SLocal>();
    slocal->kind = js::LocalKind::kVar;
    js::Decl decl;
    decl.binding.data = std::make_shared<js::BIdentifier>(js::BIdentifier{.ref = ref});
    decl.value_or_nil = js::Expr(std::make_shared<js::ENumber>(js::ENumber{.value = value}),
                                 lg::Loc{});
    slocal->decls.push_back(std::move(decl));
    js::Stmt stmt;
    stmt.data = std::move(slocal);
    return stmt;
}

// A live part holding "stmts"; every ref in "declared" is recorded as a
// top-level declaration of that part.
js::Part MakeLivePart(std::vector<js::Stmt> stmts, std::vector<cm::Ref> declared = {})
{
    js::Part part;
    part.is_live = true;
    part.stmts = std::move(stmts);
    for (cm::Ref ref : declared) {
        part.declared_symbols.push_back(js::DeclaredSymbol{.ref = ref, .is_top_level = true});
    }
    return part;
}

// Wires a file's AST to a module scope owned by the caller. Callers must
// declare the scope before the LinkerContext so it outlives the graph.
void SetModuleScope(g::JSRepr& repr, js::Scope& scope)
{
    repr.ast.module_scope = &scope;
}

// True when "haystack" contains "needle" at least once.
bool Contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// How often "needle" appears in "haystack".
size_t Count(const std::string& haystack, const std::string& needle)
{
    size_t count = 0;
    size_t at = 0;
    while ((at = haystack.find(needle, at)) != std::string::npos) {
        count++;
        at += needle.size();
    }
    return count;
}

} // namespace

// ---------------------------------------------------------------------------
// LinkerContext::SanitizeGlobalName
// ---------------------------------------------------------------------------

TEST(LinkerJS, SanitizeGlobalNameKeepsPlainIdentifier)
{
    EXPECT_EQ(lnk::LinkerContext::SanitizeGlobalName("dep"), "dep");
}

TEST(LinkerJS, SanitizeGlobalNameReplacesPathSeparators)
{
    EXPECT_EQ(lnk::LinkerContext::SanitizeGlobalName("dep/sub"), "dep_sub");
}

TEST(LinkerJS, SanitizeGlobalNameCollapsesDelimiterRuns)
{
    EXPECT_EQ(lnk::LinkerContext::SanitizeGlobalName("a--//b"), "a_b");
}

TEST(LinkerJS, SanitizeGlobalNameLeavesEmptyInputEmpty)
{
    EXPECT_EQ(lnk::LinkerContext::SanitizeGlobalName(""), "");
}

TEST(LinkerJS, SanitizeGlobalNamePrefixesLeadingDigit)
{
    EXPECT_EQ(lnk::LinkerContext::SanitizeGlobalName("9lives"), "_9lives");
}

TEST(LinkerJS, SanitizeGlobalNameSingleDelimiterBecomesUnderscore)
{
    EXPECT_EQ(lnk::LinkerContext::SanitizeGlobalName("/"), "_");
}

TEST(LinkerJS, SanitizeGlobalNameKeepsDollarAndUnderscore)
{
    EXPECT_EQ(lnk::LinkerContext::SanitizeGlobalName("a$b_c"), "a$b_c");
}

TEST(LinkerJS, SanitizeGlobalNameKeepsDigitsInsideName)
{
    EXPECT_EQ(lnk::LinkerContext::SanitizeGlobalName("v2"), "v2");
}

TEST(LinkerJS, SanitizeGlobalNameKeepsTrailingDelimiterAsUnderscore)
{
    EXPECT_EQ(lnk::LinkerContext::SanitizeGlobalName("a/"), "a_");
}

// ---------------------------------------------------------------------------
// LinkerContext::GenerateGlobalNamePrefix
// ---------------------------------------------------------------------------

TEST(LinkerJS, GlobalNamePrefixIsEmptyWithoutGlobalName)
{
    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "");
}

TEST(LinkerJS, GlobalNamePrefixDeclaresVarForSingleName)
{
    cf::Options options;
    options.GlobalName = {"App"};
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "var App = ");
}

TEST(LinkerJS, GlobalNamePrefixMinifiedSingleNameDropsSpaces)
{
    cf::Options options;
    options.GlobalName = {"App"};
    options.MinifyWhitespace = true;
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "var App=");
}

TEST(LinkerJS, GlobalNamePrefixUsesLogicalAssignmentForTwoNames)
{
    cf::Options options;
    options.GlobalName = {"App", "Widget"};
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "var App;\n(App ||= {}).Widget = ");
}

TEST(LinkerJS, GlobalNamePrefixMinifiedTwoNamesUseCompactLogicalAssignment)
{
    cf::Options options;
    options.GlobalName = {"App", "Widget"};
    options.MinifyWhitespace = true;
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "var App;(App||={}).Widget=");
}

TEST(LinkerJS, GlobalNamePrefixChainsThreeNames)
{
    cf::Options options;
    options.GlobalName = {"A", "B", "C"};
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "var A;\n((A ||= {}).B ||= {}).C = ");
}

TEST(LinkerJS, GlobalNamePrefixFallsBackWhenLogicalAssignmentUnsupported)
{
    cf::Options options;
    options.GlobalName = {"A", "B"};
    options.UnsupportedJSFeatures = cp::JSFeature::kLogicalAssignment;
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "var A = A || {};\nA.B = ");
}

TEST(LinkerJS, GlobalNamePrefixWritesOntoThisObject)
{
    cf::Options options;
    options.GlobalName = {"this", "App"};
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "this.App = ");
}

TEST(LinkerJS, GlobalNamePrefixWritesOntoImportMeta)
{
    cf::Options options;
    options.GlobalName = {"import", "meta", "App"};
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "import.meta.App = ");
}

TEST(LinkerJS, GlobalNamePrefixQuotesInvalidFirstSegment)
{
    cf::Options options;
    options.GlobalName = {"foo-bar"};
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "this[\"foo-bar\"] = ");
}

TEST(LinkerJS, GlobalNamePrefixQuotesInvalidNestedSegment)
{
    cf::Options options;
    options.GlobalName = {"A", "foo-bar"};
    lnk::LinkerContext c;
    c.options = &options;

    EXPECT_EQ(c.GenerateGlobalNamePrefix(), "var A;\n(A ||= {})[\"foo-bar\"] = ");
}

// ---------------------------------------------------------------------------
// LinkerContext::RequireOrImportMetaForSource
// ---------------------------------------------------------------------------

TEST(LinkerJS, RequireOrImportMetaExposesExportsForESMFile)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSRepr(1);
    repr->ast.wrapper_ref = cm::Ref{0, 7};
    repr->ast.exports_ref = cm::Ref{0, 8};
    repr->meta.wrap = g::WrapKind::kESM;
    SetJS(graph, 0, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    auto meta = c.RequireOrImportMetaForSource(0);
    EXPECT_EQ(meta.wrapper_ref, (cm::Ref{0, 7}));
    EXPECT_EQ(meta.exports_ref, (cm::Ref{0, 8}));
    EXPECT_FALSE(meta.is_wrapper_async);
}

TEST(LinkerJS, RequireOrImportMetaHidesExportsForCJSFile)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSRepr(1);
    repr->ast.wrapper_ref = cm::Ref{0, 7};
    repr->ast.exports_ref = cm::Ref{0, 8};
    repr->meta.wrap = g::WrapKind::kCJS;
    SetJS(graph, 0, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    auto meta = c.RequireOrImportMetaForSource(0);
    EXPECT_EQ(meta.wrapper_ref, (cm::Ref{0, 7}));
    EXPECT_EQ(meta.exports_ref, cm::Ref{});
}

TEST(LinkerJS, RequireOrImportMetaHidesExportsForUnwrappedFile)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSRepr(1);
    repr->ast.wrapper_ref = cm::Ref{0, 7};
    repr->ast.exports_ref = cm::Ref{0, 8};
    repr->meta.wrap = g::WrapKind::kNone;
    SetJS(graph, 0, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    auto meta = c.RequireOrImportMetaForSource(0);
    EXPECT_EQ(meta.exports_ref, cm::Ref{});
}

TEST(LinkerJS, RequireOrImportMetaForwardsAsyncWrapperFlag)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSRepr(1);
    repr->ast.wrapper_ref = cm::Ref{0, 7};
    repr->meta.wrap = g::WrapKind::kESM;
    repr->meta.is_async_or_has_async_dependency = true;
    SetJS(graph, 0, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);

    auto meta = c.RequireOrImportMetaForSource(0);
    EXPECT_TRUE(meta.is_wrapper_async);
}

// ---------------------------------------------------------------------------
// LinkerContext::RenameSymbolsInChunk
// ---------------------------------------------------------------------------

TEST(LinkerJS, RenameSymbolsInChunkNonJSChunkReturnsNoOpRenamer)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref ref = AddSymbol(graph, 0, "originalName");

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    lnk::ChunkInfo chunk;
    chunk.chunk_repr = lnk::ChunkReprCSS{};
    std::vector<uint32_t> files_in_order;

    auto renamer = c.RenameSymbolsInChunk(chunk, files_in_order);
    ASSERT_TRUE(renamer != nullptr);
    EXPECT_EQ(renamer->NameForSymbol(ref), "originalName");
}

TEST(LinkerJS, RenameSymbolsInChunkKeepsDistinctTopLevelNames)
{
    js::Scope scope0;
    js::Scope scope1;

    g::LinkerGraph graph = MakeLinkerGraph(2);
    cm::Ref alpha = AddSymbol(graph, 0, "alpha");
    cm::Ref beta = AddSymbol(graph, 1, "beta");

    auto repr0 = MakeJSRepr(1);
    SetModuleScope(*repr0, scope0);
    repr0->ast.parts[0] = MakeLivePart({MakeLocalStmt(alpha, 1)}, {alpha});
    SetJS(graph, 0, repr0);

    auto repr1 = MakeJSRepr(1);
    SetModuleScope(*repr1, scope1);
    repr1->ast.parts[0] = MakeLivePart({MakeLocalStmt(beta, 2)}, {beta});
    SetJS(graph, 1, repr1);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    lnk::ChunkInfo chunk;
    std::vector<uint32_t> files_in_order = {0, 1};

    auto renamer = c.RenameSymbolsInChunk(chunk, files_in_order);
    ASSERT_TRUE(renamer != nullptr);
    EXPECT_EQ(renamer->NameForSymbol(alpha), "alpha");
    EXPECT_EQ(renamer->NameForSymbol(beta), "beta");
}

TEST(LinkerJS, RenameSymbolsInChunkDisambiguatesDuplicateTopLevelNames)
{
    js::Scope scope0;
    js::Scope scope1;

    g::LinkerGraph graph = MakeLinkerGraph(2);
    cm::Ref first = AddSymbol(graph, 0, "foo");
    cm::Ref second = AddSymbol(graph, 1, "foo");

    auto repr0 = MakeJSRepr(1);
    SetModuleScope(*repr0, scope0);
    repr0->ast.parts[0] = MakeLivePart({MakeLocalStmt(first, 1)}, {first});
    SetJS(graph, 0, repr0);

    auto repr1 = MakeJSRepr(1);
    SetModuleScope(*repr1, scope1);
    repr1->ast.parts[0] = MakeLivePart({MakeLocalStmt(second, 2)}, {second});
    SetJS(graph, 1, repr1);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    lnk::ChunkInfo chunk;
    std::vector<uint32_t> files_in_order = {0, 1};

    auto renamer = c.RenameSymbolsInChunk(chunk, files_in_order);
    ASSERT_TRUE(renamer != nullptr);
    EXPECT_EQ(renamer->NameForSymbol(first), "foo");
    EXPECT_EQ(renamer->NameForSymbol(second), "foo2");
}

TEST(LinkerJS, RenameSymbolsInChunkAvoidsReservedCommonJSExportsName)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref ref = AddSymbol(graph, 0, "exports");

    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    repr->ast.parts[0] = MakeLivePart({MakeLocalStmt(ref, 1)}, {ref});
    SetJS(graph, 0, repr);

    cf::Options options;
    options.OutputFormat = cf::Format::kCommonJS;
    options.OutputPlatform = cf::Platform::kNode;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    lnk::ChunkInfo chunk;
    std::vector<uint32_t> files_in_order = {0};

    auto renamer = c.RenameSymbolsInChunk(chunk, files_in_order);
    ASSERT_TRUE(renamer != nullptr);
    EXPECT_EQ(renamer->NameForSymbol(ref), "exports2");
}

TEST(LinkerJS, RenameSymbolsInChunkKeepsExportsNameWithoutCommonJSNode)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref ref = AddSymbol(graph, 0, "exports");

    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    repr->ast.parts[0] = MakeLivePart({MakeLocalStmt(ref, 1)}, {ref});
    SetJS(graph, 0, repr);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    lnk::ChunkInfo chunk;
    std::vector<uint32_t> files_in_order = {0};

    auto renamer = c.RenameSymbolsInChunk(chunk, files_in_order);
    ASSERT_TRUE(renamer != nullptr);
    EXPECT_EQ(renamer->NameForSymbol(ref), "exports");
}

TEST(LinkerJS, RenameSymbolsInChunkAvoidsReservedBundledRequireName)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref ref = AddSymbol(graph, 0, "require");

    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    repr->ast.parts[0] = MakeLivePart({MakeLocalStmt(ref, 1)}, {ref});
    SetJS(graph, 0, repr);

    cf::Options options;
    options.BuildMode = cf::Mode::kBundle;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    lnk::ChunkInfo chunk;
    std::vector<uint32_t> files_in_order = {0};

    auto renamer = c.RenameSymbolsInChunk(chunk, files_in_order);
    ASSERT_TRUE(renamer != nullptr);
    EXPECT_EQ(renamer->NameForSymbol(ref), "require2");
}

TEST(LinkerJS, RenameSymbolsInChunkReservesUnboundModuleScopeName)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref unbound = AddSymbol(graph, 0, "foo", cm::SymbolKind::kUnbound);
    cm::Ref local = AddSymbol(graph, 0, "foo");

    js::ScopeMember member;
    member.ref = unbound;
    scope0.members["foo"] = member;

    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    repr->ast.parts[0] = MakeLivePart({MakeLocalStmt(local, 1)}, {local});
    SetJS(graph, 0, repr);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    lnk::ChunkInfo chunk;
    std::vector<uint32_t> files_in_order = {0};

    auto renamer = c.RenameSymbolsInChunk(chunk, files_in_order);
    ASSERT_TRUE(renamer != nullptr);
    EXPECT_EQ(renamer->NameForSymbol(unbound), "foo");
    EXPECT_EQ(renamer->NameForSymbol(local), "foo2");
}

TEST(LinkerJS, RenameSymbolsInChunkMinifiesIdentifiers)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref ref = AddSymbol(graph, 0, "myLongVariableName");

    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    js::Part part = MakeLivePart({MakeLocalStmt(ref, 1)}, {ref});
    part.symbol_uses[ref] = js::SymbolUse{.count_estimate = 5};
    repr->ast.parts[0] = std::move(part);
    SetJS(graph, 0, repr);

    cf::Options options;
    options.MinifyIdentifiers = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    lnk::ChunkInfo chunk;
    std::vector<uint32_t> files_in_order = {0};

    auto renamer = c.RenameSymbolsInChunk(chunk, files_in_order);
    ASSERT_TRUE(renamer != nullptr);
    std::string renamed = renamer->NameForSymbol(ref);
    EXPECT_NE(renamed, "myLongVariableName");
    EXPECT_EQ(renamed, "a");
}

// ---------------------------------------------------------------------------
// LinkerContext::GenerateEntryPointTailJS
// ---------------------------------------------------------------------------

TEST(LinkerJS, EntryPointTailPreserveFormatWithoutWrapperEmitsNothing)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    repr->meta.wrap = g::WrapKind::kNone;
    SetJS(graph, 0, repr);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    auto renamer = js::NewNoOpRenamer(c.graph.symbols);
    auto result = c.GenerateEntryPointTailJS(*renamer, cm::Ref{}, cm::Ref{}, 0);

    EXPECT_EQ(result.source_index, 0u);
    EXPECT_TRUE(result.print_result.js.empty());
}

TEST(LinkerJS, EntryPointTailPreserveFormatCallsWrapper)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref wrapper = AddSymbol(graph, 0, "require_entry");
    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    repr->meta.wrap = g::WrapKind::kCJS;
    repr->ast.wrapper_ref = wrapper;
    SetJS(graph, 0, repr);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    auto renamer = js::NewNoOpRenamer(c.graph.symbols);
    auto result = c.GenerateEntryPointTailJS(*renamer, cm::Ref{}, cm::Ref{}, 0);

    EXPECT_TRUE(Contains(result.print_result.js, "require_entry()"));
}

TEST(LinkerJS, EntryPointTailCommonJSAssignsModuleExports)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref module = AddSymbol(graph, 0, "module");
    cm::Ref wrapper = AddSymbol(graph, 0, "require_entry");
    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    repr->meta.wrap = g::WrapKind::kCJS;
    repr->ast.wrapper_ref = wrapper;
    SetJS(graph, 0, repr);

    cf::Options options;
    options.OutputFormat = cf::Format::kCommonJS;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.unbound_module_ref = module;

    auto renamer = js::NewNoOpRenamer(c.graph.symbols);
    auto result = c.GenerateEntryPointTailJS(*renamer, cm::Ref{}, cm::Ref{}, 0);

    EXPECT_TRUE(Contains(result.print_result.js, "module.exports = require_entry();"));
}

TEST(LinkerJS, EntryPointTailESModuleExportsDefaultWrapper)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref wrapper = AddSymbol(graph, 0, "require_entry");
    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    repr->meta.wrap = g::WrapKind::kCJS;
    repr->ast.wrapper_ref = wrapper;
    SetJS(graph, 0, repr);

    cf::Options options;
    options.OutputFormat = cf::Format::kESModule;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    auto renamer = js::NewNoOpRenamer(c.graph.symbols);
    auto result = c.GenerateEntryPointTailJS(*renamer, cm::Ref{}, cm::Ref{}, 0);

    EXPECT_TRUE(Contains(result.print_result.js, "export default require_entry();"));
}

TEST(LinkerJS, EntryPointTailIIFEWithGlobalNameReturnsWrapper)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref wrapper = AddSymbol(graph, 0, "require_entry");
    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    repr->meta.wrap = g::WrapKind::kCJS;
    repr->ast.wrapper_ref = wrapper;
    SetJS(graph, 0, repr);

    cf::Options options;
    options.OutputFormat = cf::Format::kIIFE;
    options.GlobalName = {"App"};
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    auto renamer = js::NewNoOpRenamer(c.graph.symbols);
    auto result = c.GenerateEntryPointTailJS(*renamer, cm::Ref{}, cm::Ref{}, 0);

    EXPECT_TRUE(Contains(result.print_result.js, "return require_entry();"));
}

TEST(LinkerJS, EntryPointTailESModuleAwaitsAsyncESMWrapper)
{
    js::Scope scope0;

    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref wrapper = AddSymbol(graph, 0, "init_entry");
    auto repr = MakeJSRepr(1);
    SetModuleScope(*repr, scope0);
    repr->meta.wrap = g::WrapKind::kESM;
    repr->meta.is_async_or_has_async_dependency = true;
    repr->ast.wrapper_ref = wrapper;
    SetJS(graph, 0, repr);

    cf::Options options;
    options.OutputFormat = cf::Format::kESModule;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    auto renamer = js::NewNoOpRenamer(c.graph.symbols);
    auto result = c.GenerateEntryPointTailJS(*renamer, cm::Ref{}, cm::Ref{}, 0);

    EXPECT_TRUE(Contains(result.print_result.js, "await init_entry()"));
}

// ---------------------------------------------------------------------------
// LinkerContext::GenerateCodeForFileInChunkJS
// ---------------------------------------------------------------------------

TEST(LinkerJS, GenerateCodeForFilePrintsLivePartStatements)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref ref = AddSymbol(graph, 0, "x");

    auto repr = MakeJSRepr(2);
    repr->ast.parts[1] = MakeLivePart({MakeLocalStmt(ref, 1)}, {ref});
    SetJS(graph, 0, repr);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    auto renamer = js::NewNoOpRenamer(c.graph.symbols);
    auto result = c.GenerateCodeForFileInChunkJS(
        *renamer, lnk::PartRange{.source_index = 0, .part_index_begin = 0, .part_index_end = 2},
        cm::Ref{}, cm::Ref{}, cm::Ref{}, {});

    EXPECT_EQ(result.source_index, 0u);
    EXPECT_TRUE(Contains(result.print_result.js, "var x = 1;"));
}

TEST(LinkerJS, GenerateCodeForFileSkipsDeadParts)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref live = AddSymbol(graph, 0, "alive");
    cm::Ref dead = AddSymbol(graph, 0, "never");

    auto repr = MakeJSRepr(2);
    repr->ast.parts[0].stmts = {MakeLocalStmt(dead, 2)};
    repr->ast.parts[0].declared_symbols.push_back(js::DeclaredSymbol{.ref = dead, .is_top_level = true});
    repr->ast.parts[1] = MakeLivePart({MakeLocalStmt(live, 1)}, {live});
    SetJS(graph, 0, repr);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    auto renamer = js::NewNoOpRenamer(c.graph.symbols);
    auto result = c.GenerateCodeForFileInChunkJS(
        *renamer, lnk::PartRange{.source_index = 0, .part_index_begin = 0, .part_index_end = 2},
        cm::Ref{}, cm::Ref{}, cm::Ref{}, {});

    EXPECT_TRUE(Contains(result.print_result.js, "var alive = 1;"));
    EXPECT_FALSE(Contains(result.print_result.js, "never"));
}

TEST(LinkerJS, GenerateCodeForFileMergesAdjacentLocalsWhenMinifyingSyntax)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref a = AddSymbol(graph, 0, "first");
    cm::Ref b = AddSymbol(graph, 0, "second");

    auto repr = MakeJSRepr(2);
    repr->ast.parts[1] = MakeLivePart({MakeLocalStmt(a, 1), MakeLocalStmt(b, 2)}, {a, b});
    SetJS(graph, 0, repr);

    cf::Options options;
    options.MinifySyntax = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);

    auto renamer = js::NewNoOpRenamer(c.graph.symbols);
    auto result = c.GenerateCodeForFileInChunkJS(
        *renamer, lnk::PartRange{.source_index = 0, .part_index_begin = 0, .part_index_end = 2},
        cm::Ref{}, cm::Ref{}, cm::Ref{}, {});

    EXPECT_EQ(Count(result.print_result.js, "var "), 1u);
    EXPECT_TRUE(Contains(result.print_result.js, "second = 2"));
}

TEST(LinkerJS, GenerateCodeForFileWrapsCommonJSPartBody)
{
    g::LinkerGraph graph = MakeLinkerGraph(1);
    cm::Ref wrapper = AddSymbol(graph, 0, "require_mod");
    cm::Ref exports = AddSymbol(graph, 0, "exports");
    cm::Ref body = AddSymbol(graph, 0, "value");
    cm::Ref runtime = AddSymbol(graph, 0, "__commonJS");

    auto repr = MakeJSRepr(3);
    repr->ast.parts[2] = MakeLivePart({MakeLocalStmt(body, 7)}, {body});
    repr->ast.wrapper_ref = wrapper;
    repr->ast.exports_ref = exports;
    repr->ast.uses_exports_ref = true;
    repr->meta.wrap = g::WrapKind::kCJS;
    repr->meta.wrapper_part_index = cm::Index32::Make(1);
    SetJS(graph, 0, repr);

    cf::Options options;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.cjs_runtime_ref = runtime;

    auto renamer = js::NewNoOpRenamer(c.graph.symbols);
    auto result = c.GenerateCodeForFileInChunkJS(
        *renamer, lnk::PartRange{.source_index = 0, .part_index_begin = 0, .part_index_end = 3},
        cm::Ref{}, cm::Ref{}, cm::Ref{}, {});

    EXPECT_TRUE(Contains(result.print_result.js, "var require_mod = __commonJS("));
    EXPECT_TRUE(Contains(result.print_result.js, "var value = 7;"));
    EXPECT_TRUE(Contains(result.print_result.js, "exports"));
}
