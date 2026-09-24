// Tests for "tsconfig.json" parsing (tsconfig_json.cpp).
//
// Every test drives resolver::ParseTSConfigJSON directly against a tiny
// in-memory file system. The "extends" callback is faked to hand back
// pre-parsed base configs, so inheritance and override rules are exercised
// without ever touching the real disk or a full resolver pass.

#include "test/guchho_test.hpp"

#include "guchho/cache.hpp"
#include "guchho/config.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"
#include "guchho/resolver.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace filesystem = guchho::filesystem;
namespace logger     = guchho::logger;
namespace config     = guchho::config;
namespace cache      = guchho::cache;
namespace resolver   = guchho::resolver;

namespace {

// A minimal in-memory file system. ParseTSConfigJSON only ever asks for
// absolute-path checks and path joining, so those are the only operations
// that are faked; everything else keeps the Fs interface satisfied with a
// trivially failing default.
class TestFs : public filesystem::Fs {
public:
    bool IsAbs(std::string_view path) override
    {
        return !path.empty() && path.front() == '/';
    }

    std::string Join(std::initializer_list<std::string_view> parts) override
    {
        std::string out;
        for (std::string_view part : parts) {
            if (part == ".")
                continue;
            if (part.starts_with("./"))
                part.remove_prefix(2);
            while (!part.empty() && part.front() == '/') {
                if (out.empty()) {
                    break;
                }
                part.remove_prefix(1);
            }
            while (!out.empty() && out.back() == '/')
                out.pop_back();
            if (part.empty())
                continue;
            out += out.empty() ? part : "/" + std::string(part);
        }
        return out.empty() ? "/" : out;
    }

    filesystem::FsResult<filesystem::DirEntries> ReadDirectory(const std::string&) override
    {
        return {};
    }

    filesystem::FsResult<std::string> ReadFile(const std::string&) override
    {
        return {};
    }

    filesystem::FsResult<std::shared_ptr<filesystem::OpenedFile>> OpenFile(const std::string&) override
    {
        return {};
    }

    filesystem::ModKeyResult ModKey(const std::string&) override
    {
        return {};
    }

    std::optional<std::string> Abs(std::string_view) override
    {
        return std::nullopt;
    }

    std::string Dir(std::string_view) override
    {
        return "/";
    }

    std::string Base(std::string_view) override
    {
        return "/";
    }

    std::string Ext(std::string_view) override
    {
        return "";
    }

    std::string Cwd() override
    {
        return "/";
    }

    std::optional<std::string> Rel(std::string_view, std::string_view) override
    {
        return std::nullopt;
    }

    std::optional<std::string> EvalSymlinks(std::string_view) override
    {
        return std::nullopt;
    }

    std::pair<std::string, filesystem::EntryKind> Kind(std::string_view, std::string_view) override
    {
        return {};
    }

    filesystem::WatchData GetWatchData() override
    {
        return {};
    }
};

// A logger that buffers everything, like the other unit-test suites.
logger::Log NewLog()
{
    return logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll, {});
}

// Builds a Source carrying "contents" as the text of the file at "path".
logger::Source SourceForTest(const std::string& path, const std::string& contents)
{
    logger::Source source;
    source.key_path.text    = path;
    source.pretty_paths.abs = path;
    source.pretty_paths.rel = path;
    source.identifier_name  = path;
    source.contents         = contents;
    return source;
}

// Parses "text" as a tsconfig file rooted at "/app". Returns the resulting
// config plus every message the parse recorded. Passing "extends" supplies
// the fake base-config callback used by inheritance tests.
struct ParseResult {
    std::unique_ptr<resolver::TSConfigJSON> config;
    std::vector<logger::Msg>                msgs;
    std::unordered_map<std::string, std::unique_ptr<resolver::TSConfigJSON>> bases;
};

ParseResult ParseConfigText(
    const std::string&                          text,
    const std::string&                          file_dir  = "/app",
    const std::string&                          config_dir = "/app",
    std::unordered_map<std::string, std::unique_ptr<resolver::TSConfigJSON>>* bases = nullptr)
{
    TestFs                                  fs;
    cache::JSONCache                        json_cache;
    logger::Log                             log = NewLog();
    logger::Source                          source    = SourceForTest(config_dir + "/tsconfig.json", text);
    resolver::TSConfigExtendsCallback       extends;
    if (bases != nullptr) {
        extends = [bases](const std::string& name, logger::Range) -> resolver::TSConfigJSON* {
            auto it = bases->find(name);
            return it == bases->end() ? nullptr : it->second.get();
        };
    }

    resolver::TSConfigJSON* parsed = resolver::ParseTSConfigJSON(
        log, source, json_cache, fs, file_dir, config_dir, extends);

    ParseResult result;
    result.config.reset(parsed);
    result.msgs = log.done();
    if (bases != nullptr) {
        result.bases = std::move(*bases);
    }
    return result;
}

// The sorted list of warning IDs a parse produced, in a canonical order so
// multi-warning expectations do not depend on report ordering.
std::vector<logger::MsgID> WarningIDs(const std::vector<logger::Msg>& msgs)
{
    std::vector<logger::MsgID> ids;
    for (const logger::Msg& m : msgs) {
        if (m.kind == logger::MsgKind::kWarning) {
            ids.push_back(m.id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

// The default state a freshly parsed config carries before any option is set.
void ExpectPlainDefaults(const resolver::TSConfigJSON& config)
{
    EXPECT_TRUE(config.abs_path.ends_with("/tsconfig.json"));
    EXPECT_FALSE(config.base_url.has_value());
    EXPECT_TRUE(config.base_url_for_paths.empty());
    EXPECT_TRUE(config.paths == nullptr);
    EXPECT_TRUE(config.ts_target_key.lower_value.empty());
    EXPECT_EQ(config.ts_target_key.range.len, 0);
    EXPECT_FALSE(config.ts_strict.has_value());
    EXPECT_FALSE(config.ts_always_strict.has_value());
    EXPECT_EQ(config.settings.ExperimentalDecorators, config::MaybeBool::kUnspecified);
    EXPECT_EQ(config.settings.ImportsNotUsedAsValues, config::TSImportsNotUsedAsValues::kNone);
    EXPECT_EQ(config.settings.PreserveValueImports, config::MaybeBool::kUnspecified);
    EXPECT_EQ(config.settings.Target, config::TSTarget::kUnspecified);
    EXPECT_EQ(config.settings.UseDefineForClassFields, config::MaybeBool::kUnspecified);
    EXPECT_EQ(config.settings.VerbatimModuleSyntax, config::MaybeBool::kUnspecified);
    EXPECT_EQ(config.jsx_settings.JSX, config::TSJSX::kNone);
    EXPECT_TRUE(config.jsx_settings.JSXFactory.empty());
    EXPECT_TRUE(config.jsx_settings.JSXFragmentFactory.empty());
    EXPECT_FALSE(config.jsx_settings.JSXImportSource.has_value());
    EXPECT_TRUE(config.TSAlwaysStrictOrStrict() == nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// Empty and minimal configs
// ---------------------------------------------------------------------------

TEST(TSConfigJSON, EmptyConfigHasAllDefaults)
{
    auto result = ParseConfigText(R"({})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    ExpectPlainDefaults(*result.config);
}

TEST(TSConfigJSON, UnknownFieldsAreIgnored)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"notARealOption":true,"watch":false}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    ExpectPlainDefaults(*result.config);
}

TEST(TSConfigJSON, MissingCompilerOptionsLeavesDefaults)
{
    auto result = ParseConfigText(R"({"files":["a.ts"],"include":["src"]})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    ExpectPlainDefaults(*result.config);
}

// ---------------------------------------------------------------------------
// "strict" / "alwaysStrict"
// ---------------------------------------------------------------------------

TEST(TSConfigJSON, StrictIsRecordedWithValueAndSpan)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"strict":true}})");
    ASSERT_TRUE(result.config != nullptr);
    ASSERT_TRUE(result.config->ts_strict.has_value());
    EXPECT_TRUE(result.config->ts_strict->Value);
    EXPECT_EQ(result.config->ts_strict->Name, std::string("strict"));
    EXPECT_TRUE(result.config->ts_strict->RangeData.len > 0);
    EXPECT_TRUE(result.config->ts_always_strict.has_value() == false);

    const config::TSAlwaysStrict* effective = result.config->TSAlwaysStrictOrStrict();
    ASSERT_TRUE(effective != nullptr);
    EXPECT_TRUE(effective->Value);
    EXPECT_EQ(effective->Name, std::string("strict"));
}

TEST(TSConfigJSON, AlwaysStrictWinsOverStrict)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"strict":false,"alwaysStrict":true}})");
    ASSERT_TRUE(result.config != nullptr);
    ASSERT_TRUE(result.config->ts_strict.has_value());
    EXPECT_FALSE(result.config->ts_strict->Value);
    ASSERT_TRUE(result.config->ts_always_strict.has_value());
    EXPECT_TRUE(result.config->ts_always_strict->Value);
    EXPECT_EQ(result.config->ts_always_strict->Name, std::string("alwaysStrict"));

    const config::TSAlwaysStrict* effective = result.config->TSAlwaysStrictOrStrict();
    ASSERT_TRUE(effective != nullptr);
    EXPECT_TRUE(effective->Value);
}

TEST(TSConfigJSON, StrictOutsideCompilerOptionsWarns)
{
    auto result = ParseConfigText(R"({"strict":true,"compilerOptions":{}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_EQ(WarningIDs(result.msgs),
              std::vector<logger::MsgID>{logger::MsgID::kTSConfigJSON_InvalidTopLevelOption});
    EXPECT_FALSE(result.config->ts_strict.has_value());
}

// ---------------------------------------------------------------------------
// "target"
// ---------------------------------------------------------------------------

TEST(TSConfigJSON, TargetBelowES2022)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"target":"es5"}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kBelowES2022);
    EXPECT_EQ(result.config->ts_target_key.lower_value, std::string("es5"));
    EXPECT_TRUE(result.config->ts_target_key.range.len > 0);
}

TEST(TSConfigJSON, TargetAtOrAboveES2022)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"target":"esnext"}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kAtOrAboveES2022);
    EXPECT_EQ(result.config->ts_target_key.lower_value, std::string("esnext"));
}

TEST(TSConfigJSON, TargetIsMatchedCaseInsensitively)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"target":"ES2022"}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kAtOrAboveES2022);
    EXPECT_EQ(result.config->ts_target_key.lower_value, std::string("es2022"));
}

TEST(TSConfigJSON, InvalidTargetWarnsAndLeavesDefault)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"target":"es98"}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_EQ(WarningIDs(result.msgs),
              std::vector<logger::MsgID>{logger::MsgID::kTSConfigJSON_InvalidTarget});
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kUnspecified);
    EXPECT_TRUE(result.config->ts_target_key.lower_value.empty());
}

TEST(TSConfigJSON, InvalidTargetIsQuietInsideNodeModules)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"target":"es98"}})",
                                   "/app", "/app/node_modules/pkg");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kUnspecified);
}

TEST(TSConfigJSON, BrokenJSONReturnsNullWithAnError)
{
    auto result = ParseConfigText(R"({"compilerOptions": {"target": es)");
    EXPECT_TRUE(result.config == nullptr);
    EXPECT_FALSE(result.msgs.empty());
}

// ---------------------------------------------------------------------------
// "tsconfig.json" lenient grammar
// ---------------------------------------------------------------------------

TEST(TSConfigJSON, CommentsAndTrailingCommasAreAccepted)
{
    auto result = ParseConfigText(
        "// leading comment\n"
        "{\n"
        "  \"compilerOptions\": {\n"
        "    \"target\": \"es2022\",\n"
        "  },\n"                            // trailing comma
        "}");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kAtOrAboveES2022);
    EXPECT_EQ(result.config->ts_target_key.lower_value, std::string("es2022"));
}

// ---------------------------------------------------------------------------
// "jsx", factories and import source
// ---------------------------------------------------------------------------

TEST(TSConfigJSON, JsxModes)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"jsx":"react-jsxdev"}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->jsx_settings.JSX, config::TSJSX::kReactJSXDev);
}

TEST(TSConfigJSON, UnrecognizedJsxStaysOnDefault)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"jsx":"quickhack"}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->jsx_settings.JSX, config::TSJSX::kNone);
}

TEST(TSConfigJSON, JsxFactoriesAndImportSource)
{
    auto result = ParseConfigText(R"({
        "compilerOptions": {
            "jsxFactory": "h",
            "jsxFragmentFactory": "React.Fragment",
            "jsxImportSource": "preact"
        }
    })");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->jsx_settings.JSXFactory, std::vector<std::string>{"h"});
    EXPECT_EQ(result.config->jsx_settings.JSXFragmentFactory,
              (std::vector<std::string>{"React", "Fragment"}));
    ASSERT_TRUE(result.config->jsx_settings.JSXImportSource.has_value());
    EXPECT_EQ(*result.config->jsx_settings.JSXImportSource, std::string("preact"));
}

TEST(TSConfigJSON, InvalidJsxFactoryWarnsAndFallsBack)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"jsxFactory":"a.1.b"}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_EQ(WarningIDs(result.msgs),
              std::vector<logger::MsgID>{logger::MsgID::kTSConfigJSON_InvalidJSX});
    EXPECT_TRUE(result.config->jsx_settings.JSXFactory.empty());
}

// ---------------------------------------------------------------------------
// "baseUrl"
// ---------------------------------------------------------------------------

TEST(TSConfigJSON, RelativeBaseUrlIsAbsolutizedAgainstFileDir)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"baseUrl":"dist"}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    ASSERT_TRUE(result.config->base_url.has_value());
    EXPECT_EQ(*result.config->base_url, std::string("/app/dist"));
}

TEST(TSConfigJSON, AbsoluteBaseUrlStaysPut)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"baseUrl":"/shared"}})");
    ASSERT_TRUE(result.config != nullptr);
    ASSERT_TRUE(result.config->base_url.has_value());
    EXPECT_EQ(*result.config->base_url, std::string("/shared"));
}

TEST(TSConfigJSON, ConfigDirTemplateInBaseUrl)
{
    auto result = ParseConfigText(R"({"compilerOptions":{"baseUrl":"${configDir}/dist"}})");
    ASSERT_TRUE(result.config != nullptr);
    ASSERT_TRUE(result.config->base_url.has_value());
    EXPECT_EQ(*result.config->base_url, std::string("/app/dist"));
}

// ---------------------------------------------------------------------------
// "paths"
// ---------------------------------------------------------------------------

TEST(TSConfigJSON, PathsAreMappedRelativeToFileDir)
{
    auto result = ParseConfigText(R"({
        "compilerOptions": {
            "paths": {
                "@/*": ["./src/*"],
                "shared/*": ["./shared/*", "./fallback/*"]
            }
        }
    })");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    ASSERT_TRUE(result.config->paths != nullptr);
    EXPECT_EQ(result.config->base_url_for_paths, std::string("/app"));

    auto it = result.config->paths->map.find("@/*");
    ASSERT_TRUE(it != result.config->paths->map.end());
    EXPECT_EQ(it->second.size(), size_t(1));
    EXPECT_EQ(it->second[0].text, std::string("./src/*"));

    it = result.config->paths->map.find("shared/*");
    ASSERT_TRUE(it != result.config->paths->map.end());
    EXPECT_EQ(it->second.size(), size_t(2));
    EXPECT_EQ(it->second[0].text, std::string("./shared/*"));
    EXPECT_EQ(it->second[1].text, std::string("./fallback/*"));
}

TEST(TSConfigJSON, ConfigDirTemplateInPathsTarget)
{
    auto result = ParseConfigText(R"({
        "compilerOptions": {
            "paths": { "@/*": ["${configDir}/generated/*"] }
        }
    })");
    ASSERT_TRUE(result.config != nullptr);
    ASSERT_TRUE(result.config->paths != nullptr);
    auto it = result.config->paths->map.find("@/*");
    ASSERT_TRUE(it != result.config->paths->map.end());
    EXPECT_EQ(it->second.size(), size_t(1));
    EXPECT_EQ(it->second[0].text, std::string("/app/generated/*"));
}

TEST(TSConfigJSON, InvalidPathPatternWarnsAndIsDropped)
{
    auto result = ParseConfigText(R"({
        "compilerOptions": {
            "paths": { "**/x/*": ["./ok/*"] }
        }
    })");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_EQ(WarningIDs(result.msgs),
              std::vector<logger::MsgID>{logger::MsgID::kTSConfigJSON_InvalidPaths});
    ASSERT_TRUE(result.config->paths != nullptr);
    EXPECT_TRUE(result.config->paths->map.find("**/x/*") == result.config->paths->map.end());
}

TEST(TSConfigJSON, NonArrayPathValueWarns)
{
    auto result = ParseConfigText(R"({
        "compilerOptions": {
            "paths": { "src/*": "./src/shared/*" }
        }
    })");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_EQ(WarningIDs(result.msgs),
              std::vector<logger::MsgID>{logger::MsgID::kTSConfigJSON_InvalidPaths});
    ASSERT_TRUE(result.config->paths != nullptr);
    EXPECT_TRUE(result.config->paths->map.find("src/*") == result.config->paths->map.end());
}

TEST(TSConfigJSON, NoBaseURLPatternValidator)
{
    TestFs                                            fs;
    cache::JSONCache                                  json_cache;
    logger::Log                                       log = NewLog();
    logger::Source                                    source = SourceForTest("/app/tsconfig.json", R"({"x":1})");
    logger::LineColumnTracker*                        tracker = nullptr;
    const logger::Loc                                loc{0};

    EXPECT_TRUE(resolver::IsValidTSConfigPathNoBaseURLPattern("./src/*", log, source, tracker, loc));
    EXPECT_TRUE(resolver::IsValidTSConfigPathNoBaseURLPattern("../src/*", log, source, tracker, loc));
    EXPECT_TRUE(resolver::IsValidTSConfigPathNoBaseURLPattern("/abs/*", log, source, tracker, loc));
    EXPECT_TRUE(resolver::IsValidTSConfigPathNoBaseURLPattern("c:/abs/*", log, source, tracker, loc));

    EXPECT_FALSE(resolver::IsValidTSConfigPathNoBaseURLPattern("src/*", log, source, tracker, loc));
    EXPECT_EQ(WarningIDs(log.done()),
              std::vector<logger::MsgID>{logger::MsgID::kTSConfigJSON_InvalidPaths});

    delete tracker;
}

// ---------------------------------------------------------------------------
// Remaining compiler options
// ---------------------------------------------------------------------------

TEST(TSConfigJSON, BooleanTriStates)
{
    auto result = ParseConfigText(R"({
        "compilerOptions": {
            "experimentalDecorators": true,
            "useDefineForClassFields": false,
            "preserveValueImports": true,
            "verbatimModuleSyntax": true
        }
    })");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->settings.ExperimentalDecorators, config::MaybeBool::kTrue);
    EXPECT_EQ(result.config->settings.UseDefineForClassFields, config::MaybeBool::kFalse);
    EXPECT_EQ(result.config->settings.PreserveValueImports, config::MaybeBool::kTrue);
    EXPECT_EQ(result.config->settings.VerbatimModuleSyntax, config::MaybeBool::kTrue);
}

TEST(TSConfigJSON, ImportsNotUsedAsValues)
{
    auto result = ParseConfigText(R"({
        "compilerOptions": { "importsNotUsedAsValues": "error" }
    })");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->settings.ImportsNotUsedAsValues, config::TSImportsNotUsedAsValues::kError);
}

TEST(TSConfigJSON, InvalidImportsNotUsedAsValuesWarns)
{
    auto result = ParseConfigText(R"({
        "compilerOptions": { "importsNotUsedAsValues": "always" }
    })");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_EQ(WarningIDs(result.msgs),
              std::vector<logger::MsgID>{
                  logger::MsgID::kTSConfigJSON_InvalidImportsNotUsedAsValues});
    EXPECT_EQ(result.config->settings.ImportsNotUsedAsValues, config::TSImportsNotUsedAsValues::kNone);
}

TEST(TSConfigJSON, TopLevelOptionWarnsOnce)
{
    auto result = ParseConfigText(R"({"target":"esnext","strict":true,"compilerOptions":{}})");
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_EQ(WarningIDs(result.msgs),
              std::vector<logger::MsgID>{logger::MsgID::kTSConfigJSON_InvalidTopLevelOption});
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kUnspecified);
    EXPECT_FALSE(result.config->ts_strict.has_value());
}

// ---------------------------------------------------------------------------
// "extends"
// ---------------------------------------------------------------------------

TEST(TSConfigJSON, ExtendsInheritsBaseFields)
{
    std::unordered_map<std::string, std::unique_ptr<resolver::TSConfigJSON>> bases;
    bases["./base.json"] =
        ParseConfigText(R"({"compilerOptions":{"target":"es2022","strict":true}})").config;

    auto result = ParseConfigText(R"({"extends":"./base.json"})", "/app", "/app", &bases);
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kAtOrAboveES2022);
    EXPECT_EQ(result.config->ts_target_key.lower_value, std::string("es2022"));
    ASSERT_TRUE(result.config->ts_strict.has_value());
    EXPECT_TRUE(result.config->ts_strict->Value);
    ASSERT_TRUE(result.config->TSAlwaysStrictOrStrict() != nullptr);
    EXPECT_TRUE(result.config->TSAlwaysStrictOrStrict()->Value);
}

TEST(TSConfigJSON, ExtendsChildOverridesBase)
{
    std::unordered_map<std::string, std::unique_ptr<resolver::TSConfigJSON>> bases;
    bases["./base.json"] =
        ParseConfigText(R"({"compilerOptions":{"target":"es5","strict":true}})").config;

    auto result = ParseConfigText(R"({
        "extends":"./base.json",
        "compilerOptions":{"target":"esnext","strict":false}
    })", "/app", "/app", &bases);
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kAtOrAboveES2022);
    ASSERT_TRUE(result.config->ts_strict.has_value());
    EXPECT_FALSE(result.config->ts_strict->Value);
}

TEST(TSConfigJSON, ArrayExtendsLaterBasesPrevail)
{
    std::unordered_map<std::string, std::unique_ptr<resolver::TSConfigJSON>> bases;
    bases["./a.json"] =
        ParseConfigText(R"({"compilerOptions":{"target":"es5"}})").config;
    bases["./b.json"] =
        ParseConfigText(R"({"compilerOptions":{"target":"esnext"}})").config;

    auto result = ParseConfigText(R"({"extends":["./a.json","./b.json"]})", "/app", "/app", &bases);
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kAtOrAboveES2022);
    EXPECT_EQ(result.config->ts_target_key.lower_value, std::string("esnext"));
}

TEST(TSConfigJSON, ExtendsPathsAreInherited)
{
    std::unordered_map<std::string, std::unique_ptr<resolver::TSConfigJSON>> bases;
    bases["./base.json"] = ParseConfigText(R"({
        "compilerOptions": { "paths": { "@/*": ["./src/*"] } }
    })").config;

    auto result = ParseConfigText(R"({"extends":"./base.json"})", "/app", "/app", &bases);
    ASSERT_TRUE(result.config != nullptr);
    ASSERT_TRUE(result.config->paths != nullptr);
    EXPECT_EQ(result.config->base_url_for_paths, std::string("/app"));
    ASSERT_TRUE(result.config->paths->map.count("@/*") == 1);
}

TEST(TSConfigJSON, MissingExtendsIsIgnored)
{
    std::unordered_map<std::string, std::unique_ptr<resolver::TSConfigJSON>> bases;
    auto result = ParseConfigText(R"({"extends":"./missing.json","compilerOptions":{"jsx":"react"}})",
                                  "/app", "/app", &bases);
    ASSERT_TRUE(result.config != nullptr);
    EXPECT_TRUE(result.msgs.empty());
    EXPECT_EQ(result.config->jsx_settings.JSX, config::TSJSX::kReact);
    EXPECT_EQ(result.config->settings.Target, config::TSTarget::kUnspecified);
}