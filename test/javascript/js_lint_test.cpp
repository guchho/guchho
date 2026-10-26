#include "test/helpers/javascript_test.hpp"

#include <string>
#include <vector>

using namespace guchho::logger;

namespace {

struct LintConfig {
    config::LintLevel undefined = config::LintLevel::kOff;
    config::LintLevel unused = config::LintLevel::kOff;
    config::LintLevel parameters = config::LintLevel::kOff;
    config::Platform platform = config::Platform::kBrowser;
    config::ProcessedDefines *defines = nullptr;
};

std::vector<Msg> Lint(const std::string &source, const LintConfig &lint) {
    config::Options options{};
    options.Lint.UndefinedVariables = lint.undefined;
    options.Lint.UnusedVariables = lint.unused;
    options.Lint.UnusedParameters = lint.parameters;
    options.OutputPlatform = lint.platform;
    options.Defines = lint.defines;

    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    js::Parse(log, SourceForTest(source), js::OptionsFromConfig(&options));
    return log.done();
}

std::vector<Msg> WithId(const std::vector<Msg> &msgs, MsgID id) {
    std::vector<Msg> result;
    for (const Msg &msg : msgs) {
        if (msg.id == id) {
            result.push_back(msg);
        }
    }
    return result;
}

std::vector<Msg> Undefined(const std::string &source,
                           config::Platform platform = config::Platform::kBrowser,
                           config::ProcessedDefines *defines = nullptr) {
    LintConfig lint;
    lint.undefined = config::LintLevel::kWarning;
    lint.platform = platform;
    lint.defines = defines;
    return WithId(Lint(source, lint), MsgID::kJS_UndefinedVariable);
}

std::vector<Msg> Unused(const std::string &source,
                        config::LintLevel parameters = config::LintLevel::kOff) {
    LintConfig lint;
    lint.unused = config::LintLevel::kWarning;
    lint.parameters = parameters;
    return WithId(Lint(source, lint), MsgID::kJS_UnusedVariable);
}

std::vector<Msg> UnusedParameters(const std::string &source) {
    LintConfig lint;
    lint.parameters = config::LintLevel::kWarning;
    return WithId(Lint(source, lint), MsgID::kJS_UnusedParameter);
}

} // namespace

// ---------------------------------------------------------------------------
// Undefined variables
// ---------------------------------------------------------------------------

TEST(JsLintUndefined, ReportsUnresolvedIdentifier) {
    auto msgs = Undefined("consol;");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("consol"), std::string::npos);
    ASSERT_NE(msgs[0].data.location, nullptr);
    EXPECT_EQ(msgs[0].data.location->line_text, "consol;");
}

TEST(JsLintUndefined, IsOffByDefault) {
    LintConfig lint;
    auto msgs = WithId(Lint("consol;", lint), MsgID::kJS_UndefinedVariable);
    EXPECT_TRUE(msgs.empty());
}

TEST(JsLintUndefined, LaterVarDeclarationIsHoisted) {
    EXPECT_TRUE(Undefined("x; var x = 1;").empty());
}

TEST(JsLintUndefined, LaterLetDeclarationIsNotReported) {
    // Scope tables are complete before lint runs, so a later lexical
    // declaration is visible here. TDZ analysis is intentionally out of scope.
    EXPECT_TRUE(Undefined("x; let x = 1;").empty());
}

TEST(JsLintUndefined, EnclosingScopeDeclarationIsVisible) {
    EXPECT_TRUE(Undefined("function f() { let x = 1; function g() { return x; } }").empty());
}

TEST(JsLintUndefined, FunctionParameterAndLocalDeclaration) {
    EXPECT_TRUE(Undefined("function f(x) { let y = x; return y; }").empty());
}

TEST(JsLintUndefined, ImportedBinding) {
    EXPECT_TRUE(Undefined("import { foo } from \"x\"; foo();").empty());
}

TEST(JsLintUndefined, ExportedBinding) {
    EXPECT_TRUE(Undefined("const x = 1; export { x };").empty());
}

TEST(JsLintUndefined, BrowserGlobalsAreKnown) {
    EXPECT_TRUE(Undefined("console.log(window.document.title);").empty());
}

TEST(JsLintUndefined, WebStorageGlobalsAreKnown) {
    EXPECT_TRUE(Undefined("localStorage.length; sessionStorage.length;").empty());
    EXPECT_FALSE(config::IsKnownGlobalName("localStorage", config::Platform::kNode));
    EXPECT_FALSE(config::IsKnownGlobalName("sessionStorage", config::Platform::kNode));
}

TEST(JsLintUndefined, ModernBrowserApisAreKnown) {
    EXPECT_TRUE(Undefined(
        "crypto.subtle; caches.open('v1'); new AudioContext(); "
        "new WebGL2RenderingContext(); navigation.currentEntry; "
        "scheduler.postTask(() => {}); visualViewport;")
                    .empty());
    EXPECT_FALSE(config::IsKnownGlobalName("AudioContext", config::Platform::kNode));
}

TEST(JsLintUndefined, NodeGlobalsAreKnownForNode) {
    EXPECT_TRUE(Undefined("process.exit(0);", config::Platform::kNode).empty());
}

TEST(JsLintUndefined, BrowserOnlyGlobalIsReportedForNode) {
    EXPECT_FALSE(config::IsKnownGlobalName("window", config::Platform::kNode));
    auto msgs = Undefined("window.title;", config::Platform::kNode);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("window"), std::string::npos);
}

TEST(JsLintUndefined, PropertyNamesAreNotReferences) {
    EXPECT_TRUE(Undefined("const user = {}; user.name;").empty());
}

TEST(JsLintUndefined, ObjectLiteralKeysAreNotReferences) {
    EXPECT_TRUE(Undefined("const o = { foo: 1 };").empty());
}

TEST(JsLintUndefined, DestructuringBindingsAreDeclarations) {
    EXPECT_TRUE(Undefined("const obj = {}; const { a, b } = obj; a; b;").empty());
}

TEST(JsLintUndefined, CatchParameter) {
    EXPECT_TRUE(Undefined("try {} catch (e) { console.log(e); }").empty());
}

TEST(JsLintUndefined, TypeofOperandIsSuppressed) {
    EXPECT_TRUE(Undefined("typeof missing;").empty());
}

TEST(JsLintUndefined, WriteTargetIsSuppressed) {
    EXPECT_TRUE(Undefined("missing = 1;").empty());
}

TEST(JsLintUndefined, DeleteTargetIsSuppressed) {
    EXPECT_TRUE(Undefined("delete missing;").empty());
}

TEST(JsLintUndefined, UpdateTargetIsSuppressed) {
    EXPECT_TRUE(Undefined("missing++;").empty());
    EXPECT_TRUE(Undefined("missing += 1;").empty());
}

TEST(JsLintUndefined, ShorthandPropertyIsAVariableReference) {
    auto msgs = Undefined("const o = { missing };");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("missing"), std::string::npos);
}

TEST(JsLintUndefined, PropertyValueIsAVariableReference) {
    auto msgs = Undefined("const o = { key: missing };");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("missing"), std::string::npos);
}

TEST(JsLintUndefined, WithScopeIsSuppressed) {
    EXPECT_TRUE(Undefined("var obj = {}; with (obj) { missing; }").empty());
}

TEST(JsLintUndefined, DirectEvalIsSuppressed) {
    EXPECT_TRUE(Undefined("eval(\"var missing = 1\"); missing;").empty());
}

TEST(JsLintUndefined, ConfiguredDefineIsSuppressed) {
    config::ProcessedDefines defines;
    config::DefineData define;
    define.KeyParts = {"DEBUG"};
    define.DefineExprData = std::make_shared<config::DefineExpr>();
    defines.IdentifierDefines["DEBUG"] = define;
    EXPECT_TRUE(Undefined("DEBUG;", config::Platform::kBrowser, &defines).empty());
}

TEST(JsLintUndefined, SuggestsUniqueVisibleCandidate) {
    auto msgs = Undefined("let value = 1; console.log(valu);");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("value"), std::string::npos);
    ASSERT_EQ(msgs[0].data.fixes.size(), 1u);
    EXPECT_EQ(msgs[0].data.fixes[0].replacement, "value");
    EXPECT_EQ(msgs[0].data.fixes[0].safety, FixSafety::kRequiresJudgment);
}

TEST(JsLintUndefined, SuggestsKnownGlobal) {
    auto msgs = Undefined("console.log(consol);");
    ASSERT_EQ(msgs.size(), 1u);
    ASSERT_EQ(msgs[0].data.fixes.size(), 1u);
    EXPECT_EQ(msgs[0].data.fixes[0].replacement, "console");
}

TEST(JsLintUndefined, NoSuggestionWhenCandidatesTie) {
    auto msgs = Undefined("let cat = 1; let cot = 2; console.log(cut);");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_TRUE(msgs[0].data.fixes.empty());
    EXPECT_EQ(msgs[0].data.text.find("Did you mean"), std::string::npos);
}

TEST(JsLintUndefined, DuplicateReferencesReportOncePerLocation) {
    auto msgs = Undefined("missing; missing;");
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_NE(msgs[0].data.location->column, msgs[1].data.location->column);
}

TEST(JsLintUndefined, ErrorLevelPromotesTheDiagnostic) {
    LintConfig lint;
    lint.undefined = config::LintLevel::kError;
    auto msgs = WithId(Lint("missing;", lint), MsgID::kJS_UndefinedVariable);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].kind, MsgKind::kError);
}

// ---------------------------------------------------------------------------
// Unused variables
// ---------------------------------------------------------------------------

TEST(JsLintUnused, ReportsUnreferencedLocalConst) {
    const std::string source = "function f() { const message = \"Hello\"; } f();";
    auto msgs = Unused(source);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("message"), std::string::npos);
    ASSERT_NE(msgs[0].data.location, nullptr);
    EXPECT_EQ(msgs[0].data.location->column, static_cast<int>(source.find("message")));
}

TEST(JsLintUnused, ReportsUnreferencedLocalLetAndVar) {
    auto msgs = Unused("function f() { let a = 1; var b = 2; } f();");
    ASSERT_EQ(msgs.size(), 2u);
}

TEST(JsLintUnused, ReferencedLocalIsNotReported) {
    EXPECT_TRUE(Unused("function f() { const a = 1; return a; } f();").empty());
}

TEST(JsLintUnused, TopLevelScriptBindingsAreExternallyObservable) {
    EXPECT_TRUE(Unused("const a = 1;").empty());
}

TEST(JsLintUnused, ExportedBindingIsNotReported) {
    EXPECT_TRUE(Unused("const a = 1; export { a };").empty());
}

TEST(JsLintUnused, DefaultExportedBindingIsNotReported) {
    EXPECT_TRUE(Unused("function f() {} export default f;").empty());
}

TEST(JsLintUnused, TopLevelEsmBindingIsReported) {
    auto msgs = Unused("const a = 1; export {};");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("a"), std::string::npos);
}

TEST(JsLintUnused, ShadowedNestedScopeReportsOnlyOuterBinding) {
    const std::string source =
        "function f() { const x = 1; { const x = 2; console.log(x); } } f();";
    auto msgs = Unused(source);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].data.location->column, static_cast<int>(source.find("x")));
}

TEST(JsLintUnused, DestructuredBindingUsageIsTracked) {
    auto msgs = Unused(
        "function f(obj) { const { a, b } = obj; return a; } f({});");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("b"), std::string::npos);
}

TEST(JsLintUnused, UsedImportIsNotReported) {
    EXPECT_TRUE(Unused("import { foo } from \"x\"; foo();").empty());
}

TEST(JsLintUnused, UnusedImportIsReported) {
    auto msgs = Unused("import { foo } from \"x\"; export {};");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("foo"), std::string::npos);
}

TEST(JsLintUnused, NamespaceImportUsageIsTracked) {
    EXPECT_TRUE(Unused("import * as ns from \"x\"; ns.foo();").empty());
}

TEST(JsLintUnused, CatchParameterUsageIsTracked) {
    EXPECT_TRUE(Unused("function f() { try {} catch (e) { console.log(e); } } f();").empty());
}

TEST(JsLintUnused, UnusedCatchParameterIsReported) {
    auto msgs = Unused("function f() { try {} catch (e) {} } f();");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("e"), std::string::npos);
}

TEST(JsLintUnused, FunctionParametersAreNotReportedByDefault) {
    EXPECT_TRUE(Unused("function f(unused) {} f();").empty());
}

TEST(JsLintUnused, FunctionParametersAreReportedWhenEnabled) {
    auto msgs = UnusedParameters("function f(unused) {} f();");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("unused"), std::string::npos);
}

TEST(JsLintUnused, MultipleIndependentBindingsAreReported) {
    auto msgs = Unused("function f() { const a = 1; let b = 2; } f();");
    ASSERT_EQ(msgs.size(), 2u);
}

TEST(JsLintUnused, ValidCodeProducesNoWarnings) {
    EXPECT_TRUE(Unused(
        "function add(a, b) { const sum = a + b; return sum; } "
        "export const result = add(1, 2);")
        .empty());
}

TEST(JsLintUnused, HoistedVarUsesTheDeclarationLocation) {
    const std::string source = "function f() { { var value = 1; } } f();";
    auto msgs = Unused(source);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].data.location->column, static_cast<int>(source.find("value")));
}

TEST(JsLintUnused, HoistedFunctionUsesTheDeclarationLocation) {
    const std::string source = "function f() { function g() {} } f();";
    auto msgs = Unused(source);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].data.text.find("g"), std::string::npos);
    EXPECT_EQ(msgs[0].data.location->column, static_cast<int>(source.find("g()")));
}

TEST(JsLintUnused, DirectEvalSuppressesUnusedAnalysis) {
    EXPECT_TRUE(Unused("function f() { const a = 1; eval(\"a\"); } f();").empty());
}
