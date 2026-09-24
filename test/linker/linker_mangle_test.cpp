// Tests for the linker mangling passes (linker_mangle.cpp):
//   - PreventExportsFromBeingRenamed (pass-through export pinning)
//   - MangleProps (object property name minification)
//   - MangleLocalCSS (CSS local identifier mangling)

#include "test/guchho_test.hpp"

#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/graph.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_runtime.hpp"
#include "guchho/linker.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace g = ::guchho::graph;
namespace js = ::guchho::javascript;
namespace cm = ::guchho::compiler;
namespace cf = ::guchho::config;
namespace lnk = ::guchho::linker;

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

// A fresh JS representation with one empty part.
std::shared_ptr<g::JSRepr> MakeJSRepr()
{
    auto repr = std::make_shared<g::JSRepr>();
    repr->ast.parts.resize(1);
    return repr;
}

// A fresh CSS representation.
std::shared_ptr<g::CSSRepr> MakeCSSRepr()
{
    return std::make_shared<g::CSSRepr>();
}

// Resizes the symbol table for "source_index" so that "inner_index" is the
// last entry, then fills that symbol and returns its reference.
cm::Ref AddSymbol(g::LinkerGraph& graph, uint32_t source_index, uint32_t inner_index,
    std::string name, uint32_t count, cm::SymbolKind kind = cm::SymbolKind::kOther,
    cm::Ref link = cm::kInvalidRef)
{
    graph.symbols.symbols_for_source[source_index].resize(inner_index + 1);
    auto* symbol = graph.symbols.Get(cm::Ref{source_index, inner_index});
    symbol->original_name = std::move(name);
    symbol->use_count_estimate = count;
    symbol->kind = kind;
    symbol->link = link;
    return cm::Ref{source_index, inner_index};
}

// A "var name = ...;" (or exported) local statement binding "ref".
js::Stmt MakeLocalStmt(bool is_export, cm::Ref ref)
{
    auto slocal = std::make_shared<js::SLocal>();
    slocal->is_export = is_export;
    auto bident = std::make_shared<js::BIdentifier>();
    bident->ref = ref;
    js::Decl decl;
    decl.binding.data = std::move(bident);
    slocal->decls.push_back(std::move(decl));
    js::Stmt stmt;
    stmt.data = std::move(slocal);
    return stmt;
}

// An empty "import ... from '...'" statement using "record_index".
js::Stmt MakeImportStmt(uint32_t record_index)
{
    auto simport = std::make_shared<js::SImport>();
    simport->import_record_index = record_index;
    js::Stmt stmt;
    stmt.data = std::move(simport);
    return stmt;
}

// An "export { ... }" statement.
js::Stmt MakeExportClauseStmt()
{
    js::Stmt stmt;
    stmt.data = std::make_shared<js::SExportClause>();
    return stmt;
}

// An "export default ..." statement.
js::Stmt MakeExportDefaultStmt()
{
    auto sdefault = std::make_shared<js::SExportDefault>();
    js::Stmt stmt;
    stmt.data = std::move(sdefault);
    return stmt;
}

// An "export * from '...'" statement.
js::Stmt MakeExportStarStmt()
{
    auto sstar = std::make_shared<js::SExportStar>();
    js::Stmt stmt;
    stmt.data = std::move(sstar);
    return stmt;
}

// An "export { ... } from '...'" statement.
js::Stmt MakeExportFromStmt()
{
    auto sfrom = std::make_shared<js::SExportFrom>();
    js::Stmt stmt;
    stmt.data = std::move(sfrom);
    return stmt;
}

// A statement that is neither an import nor an export.
js::Stmt MakeNeutralStmt()
{
    auto sexpr = std::make_shared<js::SExpr>();
    js::Stmt stmt;
    stmt.data = std::move(sexpr);
    return stmt;
}

// A module scope with one member "name" bound to "ref".
void AddModuleMember(js::AST& ast, js::Scope& scope, std::string name, cm::Ref ref)
{
    ast.module_scope = &scope;
    js::ScopeMember member;
    member.ref = ref;
    scope.members[std::move(name)] = member;
}

} // namespace

// ---------------------------------------------------------------------------
// PreventExportsFromBeingRenamed

TEST(PreventExports, ExportedLocalGetsMustNotBeRenamed)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr = MakeJSRepr();
    cm::Ref ref = AddSymbol(graph, 1, 0, "foo", 1);
    repr->ast.parts[0].stmts = {MakeLocalStmt(true, ref)};
    SetJS(graph, 1, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.PreventExportsFromBeingRenamed(1);

    EXPECT_TRUE(cm::Has(c.graph.symbols.Get(ref)->flags, cm::SymbolFlags::kMustNotBeRenamed));
}

TEST(PreventExports, UnexportedLocalWithModuleScopeUsesFallback)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr = MakeJSRepr();
    cm::Ref local = AddSymbol(graph, 1, 0, "local", 1);
    cm::Ref member = AddSymbol(graph, 1, 1, "member", 1);
    js::Scope scope;
    AddModuleMember(repr->ast, scope, "memberName", member);
    repr->ast.parts[0].stmts = {MakeLocalStmt(false, local)};
    SetJS(graph, 1, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.PreventExportsFromBeingRenamed(1);

    EXPECT_FALSE(cm::Has(c.graph.symbols.Get(local)->flags, cm::SymbolFlags::kMustNotBeRenamed));
    EXPECT_TRUE(cm::Has(c.graph.symbols.Get(member)->flags, cm::SymbolFlags::kMustNotBeRenamed));
}

TEST(PreventExports, ExportedFunctionBecomesUnbound)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr = MakeJSRepr();
    cm::Ref ref = AddSymbol(graph, 1, 0, "fn", 1);
    auto sfunc = std::make_shared<js::SFunction>();
    sfunc->is_export = true;
    auto loc_ref = std::make_shared<cm::LocRef>();
    loc_ref->ref = ref;
    sfunc->fn.name = std::move(loc_ref);
    js::Stmt stmt;
    stmt.data = std::move(sfunc);
    repr->ast.parts[0].stmts = {std::move(stmt)};
    SetJS(graph, 1, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.PreventExportsFromBeingRenamed(1);

    EXPECT_EQ(c.graph.symbols.Get(ref)->kind, cm::SymbolKind::kUnbound);
}

TEST(PreventExports, ExportedClassBecomesUnbound)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr = MakeJSRepr();
    cm::Ref ref = AddSymbol(graph, 1, 0, "Cls", 1);
    auto sclass = std::make_shared<js::SClass>();
    sclass->is_export = true;
    auto loc_ref = std::make_shared<cm::LocRef>();
    loc_ref->ref = ref;
    sclass->class_.name = std::move(loc_ref);
    js::Stmt stmt;
    stmt.data = std::move(sclass);
    repr->ast.parts[0].stmts = {std::move(stmt)};
    SetJS(graph, 1, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.PreventExportsFromBeingRenamed(1);

    EXPECT_EQ(c.graph.symbols.Get(ref)->kind, cm::SymbolKind::kUnbound);
}

TEST(PreventExports, InternalImportFallsBackToModuleScope)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr = MakeJSRepr();
    cm::Ref member = AddSymbol(graph, 1, 0, "member", 1);
    js::Scope scope;
    AddModuleMember(repr->ast, scope, "memberName", member);
    repr->ast.import_records.push_back(cm::ImportRecord{});
    repr->ast.import_records[0].source_index = cm::Index32::Make(7);
    repr->ast.parts[0].stmts = {MakeImportStmt(0)};
    SetJS(graph, 1, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.PreventExportsFromBeingRenamed(1);

    EXPECT_TRUE(cm::Has(c.graph.symbols.Get(member)->flags, cm::SymbolFlags::kMustNotBeRenamed));
}

TEST(PreventExports, ExternalImportSuppressesModuleScopeFallback)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr = MakeJSRepr();
    cm::Ref member = AddSymbol(graph, 1, 0, "member", 1);
    js::Scope scope;
    AddModuleMember(repr->ast, scope, "memberName", member);
    repr->ast.import_records.push_back(cm::ImportRecord{});
    repr->ast.parts[0].stmts = {MakeImportStmt(0)};
    SetJS(graph, 1, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.PreventExportsFromBeingRenamed(1);

    EXPECT_FALSE(cm::Has(c.graph.symbols.Get(member)->flags, cm::SymbolFlags::kMustNotBeRenamed));
}

TEST(PreventExports, ExportClauseSuppressesModuleScopeFallback)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr = MakeJSRepr();
    cm::Ref member = AddSymbol(graph, 1, 0, "member", 1);
    js::Scope scope;
    AddModuleMember(repr->ast, scope, "memberName", member);
    repr->ast.parts[0].stmts = {MakeExportClauseStmt()};
    SetJS(graph, 1, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.PreventExportsFromBeingRenamed(1);

    EXPECT_FALSE(cm::Has(c.graph.symbols.Get(member)->flags, cm::SymbolFlags::kMustNotBeRenamed));
}

TEST(PreventExports, OtherExportStatementsSuppressFallback)
{
    for (auto& stmt : {MakeExportDefaultStmt(), MakeExportStarStmt(), MakeExportFromStmt()}) {
        g::LinkerGraph graph = MakeLinkerGraph(2);
        auto repr = MakeJSRepr();
        cm::Ref member = AddSymbol(graph, 1, 0, "member", 1);
        js::Scope scope;
        AddModuleMember(repr->ast, scope, "memberName", member);
        repr->ast.parts[0].stmts = {stmt};
        SetJS(graph, 1, repr);

        lnk::LinkerContext c;
        c.graph = std::move(graph);
        c.PreventExportsFromBeingRenamed(1);

        EXPECT_FALSE(cm::Has(c.graph.symbols.Get(member)->flags, cm::SymbolFlags::kMustNotBeRenamed));
    }
}

TEST(PreventExports, NonExportingFileWithNoScopeIsNoOp)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    auto repr = MakeJSRepr();
    cm::Ref ref = AddSymbol(graph, 1, 0, "plain", 1);
    repr->ast.parts[0].stmts = {MakeNeutralStmt()};
    SetJS(graph, 1, repr);

    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.PreventExportsFromBeingRenamed(1);

    EXPECT_FALSE(cm::Has(c.graph.symbols.Get(ref)->flags, cm::SymbolFlags::kMustNotBeRenamed));
}

// Points the graph's "reachable_files" list and "stable_source_indices" table
// at the two files in play. File 1 is always the first index.
void SetReachableTwoFiles(g::LinkerGraph& graph)
{
    graph.reachable_files = {1, 2};
    graph.stable_source_indices = {0, 0, 1};
}

// Adds a property named "name" mapping to "ref" inside a JS repr.
void AddMangledProp(std::shared_ptr<g::JSRepr> repr, std::string name, cm::Ref ref)
{
    repr->ast.mangled_props[std::move(name)] = ref;
}

// ---------------------------------------------------------------------------
// MangleProps

TEST(MangleProps, AssignsShortNamesByUsageAcrossFiles)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref onClick = AddSymbol(graph, 1, 0, "onClick", 3);
    cm::Ref title = AddSymbol(graph, 1, 1, "title", 2);
    cm::Ref onClick_other = AddSymbol(graph, 2, 0, "onClick", 5);
    cm::Ref flip = AddSymbol(graph, 2, 1, "flip", 1);

    auto repr1 = MakeJSRepr();
    AddMangledProp(repr1, "onClick", onClick);
    AddMangledProp(repr1, "title", title);
    auto repr2 = MakeJSRepr();
    AddMangledProp(repr2, "onClick", onClick_other);
    AddMangledProp(repr2, "flip", flip);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, repr2);

    std::unordered_map<std::string, bool> cache;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.MangleProps(cache);

    EXPECT_EQ(c.mangled_props.size(), 3u);
    EXPECT_EQ(c.mangled_props[onClick], "a");
    EXPECT_EQ(c.mangled_props[title], "b");
    EXPECT_EQ(c.mangled_props[flip], "c");
    EXPECT_TRUE(cache["onClick"]);
    EXPECT_TRUE(cache["title"]);
    EXPECT_TRUE(cache["flip"]);
}

TEST(MangleProps, MergesSamePropertyNameAcrossFiles)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref onClick = AddSymbol(graph, 1, 0, "onClick", 3);
    cm::Ref onClick_other = AddSymbol(graph, 2, 0, "onClick", 5);

    auto repr1 = MakeJSRepr();
    AddMangledProp(repr1, "onClick", onClick);
    auto repr2 = MakeJSRepr();
    AddMangledProp(repr2, "onClick", onClick_other);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, repr2);

    std::unordered_map<std::string, bool> cache;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.MangleProps(cache);

    EXPECT_EQ(c.mangled_props.size(), 1u);
    EXPECT_EQ(c.mangled_props[onClick], "a");
    EXPECT_EQ(c.graph.symbols.Get(onClick_other)->link, onClick);
    EXPECT_EQ(c.graph.symbols.Get(onClick)->use_count_estimate, 8u);
    EXPECT_EQ(c.graph.symbols.Get(onClick)->original_name, "onClick");
}

TEST(MangleProps, IgnoresNamesAlreadyCached)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref onClick = AddSymbol(graph, 1, 0, "onClick", 8);
    cm::Ref title = AddSymbol(graph, 1, 1, "title", 2);
    cm::Ref flip = AddSymbol(graph, 2, 1, "flip", 1);

    auto repr1 = MakeJSRepr();
    AddMangledProp(repr1, "onClick", onClick);
    AddMangledProp(repr1, "title", title);
    auto repr2 = MakeJSRepr();
    AddMangledProp(repr2, "flip", flip);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, repr2);

    std::unordered_map<std::string, bool> cache;
    cache["title"] = true;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.MangleProps(cache);

    EXPECT_EQ(c.mangled_props.size(), 2u);
    EXPECT_EQ(c.mangled_props[onClick], "a");
    EXPECT_FALSE(c.mangled_props.count(title));
    EXPECT_EQ(c.mangled_props[flip], "b");
    EXPECT_TRUE(cache["onClick"]);
    EXPECT_TRUE(cache["flip"]);
}

TEST(MangleProps, SkipsReservedPropertyNames)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref onClick = AddSymbol(graph, 1, 0, "onClick", 5);
    cm::Ref title = AddSymbol(graph, 1, 1, "title", 2);

    auto repr1 = MakeJSRepr();
    AddMangledProp(repr1, "onClick", onClick);
    AddMangledProp(repr1, "title", title);
    repr1->ast.reserved_props["a"] = true;
    repr1->ast.reserved_props["b"] = true;
    SetJS(graph, 1, repr1);

    std::unordered_map<std::string, bool> cache;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.MangleProps(cache);

    EXPECT_EQ(c.mangled_props[onClick], "c");
    EXPECT_EQ(c.mangled_props[title], "d");
}

TEST(MangleProps, SkipsRuntimeSourceIndex)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    graph.reachable_files = {js::kSourceIndex, 1};
    graph.stable_source_indices = {0, 1};

    cm::Ref runtime_prop = AddSymbol(graph, js::kSourceIndex, 0, "runtimeProp", 10);
    cm::Ref onClick = AddSymbol(graph, 1, 0, "onClick", 1);

    auto runtime_repr = MakeJSRepr();
    AddMangledProp(runtime_repr, "runtimeProp", runtime_prop);
    auto repr1 = MakeJSRepr();
    AddMangledProp(repr1, "onClick", onClick);
    SetJS(graph, js::kSourceIndex, runtime_repr);
    SetJS(graph, 1, repr1);

    std::unordered_map<std::string, bool> cache;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.MangleProps(cache);

    EXPECT_EQ(c.mangled_props.size(), 1u);
    EXPECT_EQ(c.mangled_props[onClick], "a");
    EXPECT_FALSE(c.mangled_props.count(runtime_prop));
}

TEST(MangleProps, BreaksTiesByStableSourceIndex)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref x = AddSymbol(graph, 1, 0, "x", 4);
    cm::Ref y = AddSymbol(graph, 2, 0, "y", 4);

    auto repr1 = MakeJSRepr();
    AddMangledProp(repr1, "x", x);
    auto repr2 = MakeJSRepr();
    AddMangledProp(repr2, "y", y);
    SetJS(graph, 1, repr1);
    SetJS(graph, 2, repr2);

    std::unordered_map<std::string, bool> cache;
    lnk::LinkerContext c;
    c.graph = std::move(graph);
    c.MangleProps(cache);

    EXPECT_EQ(c.mangled_props[x], "a");
    EXPECT_EQ(c.mangled_props[y], "b");
}

// ---------------------------------------------------------------------------
// MangleLocalCSS

// Installs a CSS file with the given identifier name and a full symbol row
// of "kinds" (one Symbol per kind entry, named "<base><i>" with "<counts>").
cm::Ref AddCSSSymbol(g::LinkerGraph& graph, uint32_t source_index, uint32_t inner_index,
    std::string name, uint32_t count, cm::SymbolKind kind)
{
    return AddSymbol(graph, source_index, inner_index, std::move(name), count, kind);
}

std::shared_ptr<g::CSSRepr> MakeCSSFile(g::LinkerGraph& graph, uint32_t source_index,
    std::string identifier_name)
{
    graph.files[source_index].input_file.source.identifier_name = std::move(identifier_name);
    auto repr = MakeCSSRepr();
    SetCSS(graph, source_index, repr);
    return repr;
}

TEST(MangleLocalCSS, MinifiedAssignsShortNamesToLocals)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref a = AddCSSSymbol(graph, 1, 0, "card", 5, cm::SymbolKind::kLocalCSS);
    cm::Ref b = AddCSSSymbol(graph, 2, 0, "badge", 2, cm::SymbolKind::kLocalCSS);
    MakeCSSFile(graph, 1, "one");
    MakeCSSFile(graph, 2, "two");

    cf::Options options;
    options.MinifyIdentifiers = true;
    std::unordered_map<std::string, bool> used;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MangleLocalCSS(used);

    EXPECT_EQ(c.mangled_props.size(), 2u);
    EXPECT_EQ(c.mangled_props[a], "a");
    EXPECT_EQ(c.mangled_props[b], "b");
    EXPECT_TRUE(used["a"]);
    EXPECT_TRUE(used["b"]);
}

TEST(MangleLocalCSS, MinifiedSkipsGlobalNames)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    AddCSSSymbol(graph, 1, 0, "a", 0, cm::SymbolKind::kGlobalCSS);
    cm::Ref local = AddCSSSymbol(graph, 2, 0, "card", 5, cm::SymbolKind::kLocalCSS);
    MakeCSSFile(graph, 1, "one");
    MakeCSSFile(graph, 2, "two");

    cf::Options options;
    options.MinifyIdentifiers = true;
    std::unordered_map<std::string, bool> used;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MangleLocalCSS(used);

    EXPECT_EQ(c.mangled_props.size(), 1u);
    EXPECT_EQ(c.mangled_props[local], "b");
    EXPECT_TRUE(used["b"]);
}

TEST(MangleLocalCSS, MinifiedSkipsPreviouslyUsedNames)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref local = AddCSSSymbol(graph, 1, 0, "card", 5, cm::SymbolKind::kLocalCSS);
    MakeCSSFile(graph, 1, "one");

    cf::Options options;
    options.MinifyIdentifiers = true;
    std::unordered_map<std::string, bool> used;
    used["a"] = true;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MangleLocalCSS(used);

    EXPECT_EQ(c.mangled_props[local], "b");
    EXPECT_TRUE(used["b"]);
}

TEST(MangleLocalCSS, NonMinifiedUsesSourcePrefix)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref title = AddCSSSymbol(graph, 1, 0, "title", 5, cm::SymbolKind::kLocalCSS);
    MakeCSSFile(graph, 1, "comp");

    cf::Options options;
    options.MinifyIdentifiers = false;
    std::unordered_map<std::string, bool> used;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MangleLocalCSS(used);

    EXPECT_EQ(c.mangled_props[title], "comp_title");
    EXPECT_TRUE(used["comp_title"]);
}

TEST(MangleLocalCSS, NonMinifiedSuffixedOnGlobalCollision)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref title = AddCSSSymbol(graph, 1, 0, "title", 5, cm::SymbolKind::kLocalCSS);
    AddCSSSymbol(graph, 1, 1, "comp_title", 0, cm::SymbolKind::kGlobalCSS);
    MakeCSSFile(graph, 1, "comp");

    cf::Options options;
    options.MinifyIdentifiers = false;
    std::unordered_map<std::string, bool> used;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MangleLocalCSS(used);

    EXPECT_EQ(c.mangled_props[title], "comp_title2");
    EXPECT_TRUE(used["comp_title2"]);
}

TEST(MangleLocalCSS, NonMinifiedSuffixedOnDuplicateBaseName)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref first = AddCSSSymbol(graph, 1, 0, "title", 4, cm::SymbolKind::kLocalCSS);
    cm::Ref second = AddCSSSymbol(graph, 2, 0, "title", 1, cm::SymbolKind::kLocalCSS);
    MakeCSSFile(graph, 1, "comp");
    MakeCSSFile(graph, 2, "comp");

    cf::Options options;
    options.MinifyIdentifiers = false;
    std::unordered_map<std::string, bool> used;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MangleLocalCSS(used);

    EXPECT_EQ(c.mangled_props[first], "comp_title");
    EXPECT_EQ(c.mangled_props[second], "comp_title2");
    EXPECT_TRUE(used["comp_title"]);
    EXPECT_TRUE(used["comp_title2"]);
}

TEST(MangleLocalCSS, FollowsLinkedSymbols)
{
    g::LinkerGraph graph = MakeLinkerGraph(3);
    SetReachableTwoFiles(graph);

    cm::Ref dup = AddCSSSymbol(graph, 1, 0, "card_dup", 0, cm::SymbolKind::kLocalCSS);
    cm::Ref target = AddCSSSymbol(graph, 2, 0, "card", 6, cm::SymbolKind::kLocalCSS);
    graph.symbols.Get(dup)->link = target;
    MakeCSSFile(graph, 1, "one");
    MakeCSSFile(graph, 2, "two");

    cf::Options options;
    options.MinifyIdentifiers = true;
    std::unordered_map<std::string, bool> used;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MangleLocalCSS(used);

    EXPECT_EQ(c.mangled_props.size(), 1u);
    EXPECT_EQ(c.mangled_props[target], "a");
    EXPECT_FALSE(c.mangled_props.count(dup));
}

TEST(MangleLocalCSS, GlobalSymbolsAreNeverMangled)
{
    g::LinkerGraph graph = MakeLinkerGraph(2);
    graph.reachable_files = {1};
    graph.stable_source_indices = {0, 0};

    AddCSSSymbol(graph, 1, 0, "app", 0, cm::SymbolKind::kGlobalCSS);
    MakeCSSFile(graph, 1, "comp");

    cf::Options options;
    options.MinifyIdentifiers = true;
    std::unordered_map<std::string, bool> used;
    lnk::LinkerContext c;
    c.options = &options;
    c.graph = std::move(graph);
    c.MangleLocalCSS(used);

    EXPECT_TRUE(c.mangled_props.empty());
    EXPECT_TRUE(used.empty());
}