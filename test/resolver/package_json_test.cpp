// Tests for the "package.json" resolution data in package_json.cpp: the pure
// helpers that classify map keys, the glob-to-regex translator, the ESM
// package-name splitter, the invalid-segment scanner, and the
// "imports"/"exports" map parser.
//
// The map parser is exercised with real JSON text so the tests cover the shape
// validation and the key-mixing rules a hand-built AST would never reach.
// Everything else is a pure function and is checked with hand-built inputs.

#include "test/guchho_test.hpp"

#include "guchho/javascript/js_parser.hpp"
#include "guchho/logger.hpp"
#include "guchho/resolver.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace javascript = guchho::javascript;
namespace logger     = guchho::logger;
namespace resolver   = guchho::resolver;

namespace {

// A logger that buffers everything, like the other resolver unit suites.
logger::Log NewLog()
{
    return logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll, {});
}

// Builds a source object for "package.json" whose text is exactly "contents".
// The JSON parser and the map parser derive their ranges from this text, so the
// offsets produced during parsing line up with the supplied contents.
logger::Source MakeSource(const std::string& contents)
{
    return logger::Source{
            .pretty_paths = {.abs = "/pkg.json", .rel = "pkg.json"},
            .contents     = contents,
            .key_path     = {.text = "/pkg.json", .namespace_ = "file"},
    };
}

// Parses "contents" as JSON and returns the expression. Fails the test when the
// JSON is malformed so a bad fixture is caught immediately.
javascript::Expr ParseJSON(const std::string& contents)
{
    logger::Source source = MakeSource(contents);
    logger::Log    log    = NewLog();
    auto [expr, ok] = javascript::Parser::ParseJSON(log, source, javascript::JSONOptions{});
    EXPECT_TRUE(ok);
    return expr;
}

// Parses "contents" as an "exports"/"imports" map. Some inputs emit warnings;
// the log stays alive for the whole call so those diagnostics have a sink.
std::unique_ptr<resolver::PjMap> ParseMap(const std::string& json_text)
{
    logger::Source   source = MakeSource(json_text);
    logger::Log      log    = NewLog();
    javascript::Expr expr   = ParseJSON(json_text);
    return resolver::ParseImportsExportsMap(source, log, expr, "exports", logger::Loc{});
}

// Builds an object entry whose children are string-valued, one per key, with
// each value echoing its key so lookups can be verified by value.
resolver::PjEntry ObjectEntryWithStrings(std::vector<std::string> keys)
{
    resolver::PjEntry entry;
    entry.kind = resolver::PjKind::kObject;
    for (std::string& key : keys) {
        resolver::PjMapEntry item;
        item.key             = std::move(key);
        item.value.kind      = resolver::PjKind::kString;
        item.value.str_data  = item.key;
        entry.map_data.push_back(std::move(item));
    }
    return entry;
}

} // namespace

// ---------------------------------------------------------------------------
// ParseImportsExportsMap: map shapes
// ---------------------------------------------------------------------------

TEST(PackageJSON, ImportsExportsSubpathMap)
{
    std::unique_ptr<resolver::PjMap> map =
        ParseMap(R"({"./feature": "./feature.js", ".": "./index.js"})");

    ASSERT_NE(map.get(), nullptr);
    EXPECT_EQ(map->property_key, std::string("exports"));
    EXPECT_EQ(map->root->kind, resolver::PjKind::kObject);
    EXPECT_TRUE(resolver::PjEntryKeysStartWithDot(*map->root));
    EXPECT_EQ(map->root->map_data.size(), size_t(2));
    EXPECT_EQ(map->root->map_data[0].key, std::string("./feature"));
    EXPECT_EQ(map->root->map_data[1].key, std::string("."));

    const resolver::PjEntry* dot = resolver::PjEntryValueForKey(*map->root, ".");
    ASSERT_NE(dot, nullptr);
    EXPECT_EQ(dot->kind, resolver::PjKind::kString);
    EXPECT_EQ(dot->str_data, std::string("./index.js"));

    const resolver::PjEntry* feature = resolver::PjEntryValueForKey(*map->root, "./feature");
    ASSERT_NE(feature, nullptr);
    EXPECT_EQ(feature->str_data, std::string("./feature.js"));
}

TEST(PackageJSON, ImportsExportsConditionalSugar)
{
    std::unique_ptr<resolver::PjMap> map = ParseMap(R"({"./feature": {"import": "./esm.js", "require": "./cjs.js"}})");

    ASSERT_NE(map.get(), nullptr);
    EXPECT_EQ(map->root->kind, resolver::PjKind::kObject);

    const resolver::PjEntry* feature = resolver::PjEntryValueForKey(*map->root, "./feature");
    ASSERT_NE(feature, nullptr);
    EXPECT_EQ(feature->kind, resolver::PjKind::kObject);
    EXPECT_FALSE(resolver::PjEntryKeysStartWithDot(*feature));

    EXPECT_EQ(feature->map_data.size(), size_t(2));
    EXPECT_EQ(feature->map_data[0].key, std::string("import"));
    EXPECT_EQ(feature->map_data[0].value.str_data, std::string("./esm.js"));
    EXPECT_EQ(feature->map_data[1].key, std::string("require"));
    EXPECT_EQ(feature->map_data[1].value.str_data, std::string("./cjs.js"));
}

TEST(PackageJSON, ImportsExportsTopLevelString)
{
    std::unique_ptr<resolver::PjMap> map = ParseMap(R"("./index.js")");

    ASSERT_NE(map.get(), nullptr);
    EXPECT_EQ(map->root->kind, resolver::PjKind::kString);
    EXPECT_EQ(map->root->str_data, std::string("./index.js"));
}

TEST(PackageJSON, ImportsExportsTopLevelArray)
{
    std::unique_ptr<resolver::PjMap> map = ParseMap(R"(["./a.js", "./b.js"])");

    ASSERT_NE(map.get(), nullptr);
    EXPECT_EQ(map->root->kind, resolver::PjKind::kArray);
    EXPECT_EQ(map->root->arr_data.size(), size_t(2));
    EXPECT_EQ(map->root->arr_data[0].str_data, std::string("./a.js"));
    EXPECT_EQ(map->root->arr_data[1].str_data, std::string("./b.js"));
}

TEST(PackageJSON, ImportsExportsNestedFallback)
{
    std::unique_ptr<resolver::PjMap> map =
        ParseMap(R"({"./x": ["./x.js", {"import": "./x.mjs", "default": "./x.js"}]})");

    ASSERT_NE(map.get(), nullptr);
    const resolver::PjEntry* x = resolver::PjEntryValueForKey(*map->root, "./x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->kind, resolver::PjKind::kArray);
    EXPECT_EQ(x->arr_data.size(), size_t(2));
    EXPECT_EQ(x->arr_data[0].kind, resolver::PjKind::kString);
    EXPECT_EQ(x->arr_data[1].kind, resolver::PjKind::kObject);
}

TEST(PackageJSON, ImportsExportsEmptyObject)
{
    std::unique_ptr<resolver::PjMap> map = ParseMap("{}");

    ASSERT_NE(map.get(), nullptr);
    EXPECT_EQ(map->root->kind, resolver::PjKind::kObject);
    EXPECT_TRUE(map->root->map_data.empty());
    EXPECT_FALSE(resolver::PjEntryKeysStartWithDot(*map->root));
}

// ---------------------------------------------------------------------------
// ParseImportsExportsMap: rejection rules
// ---------------------------------------------------------------------------

TEST(PackageJSON, ImportsExportsNullYieldsNoMap)
{
    std::unique_ptr<resolver::PjMap> map = ParseMap("null");
    EXPECT_EQ(map.get(), nullptr);
}

TEST(PackageJSON, ImportsExportsInvalidValue)
{
    std::unique_ptr<resolver::PjMap> map = ParseMap("true");
    ASSERT_NE(map.get(), nullptr);
    EXPECT_EQ(map->root->kind, resolver::PjKind::kInvalid);
}

TEST(PackageJSON, ImportsExportsMixedKeysRejected)
{
    // "import" is a condition name, "." is a subpath key: the two keying
    // schemes must never be mixed inside one object.
    std::unique_ptr<resolver::PjMap> map = ParseMap(R"({"import": "./a.js", ".": "./b.js"})");
    ASSERT_NE(map.get(), nullptr);
    EXPECT_EQ(map->root->kind, resolver::PjKind::kInvalid);
}

TEST(PackageJSON, ImportsExportsExpansionKeys)
{
    std::unique_ptr<resolver::PjMap> map = ParseMap(R"({"./features/*": "./features/*.js", "./styles/": "./styles/", ".": "./index.js"})");

    ASSERT_NE(map.get(), nullptr);
    // Every subpath key stays in the ordered map.
    EXPECT_EQ(map->root->map_data.size(), size_t(3));

    // Only the wildcard and trailing-slash keys are collected for the
    // expansion pass, ordered most-specific-first.
    EXPECT_EQ(map->root->expansion_keys.size(), size_t(2));
    EXPECT_EQ(map->root->expansion_keys[0].key, std::string("./features/*"));
    EXPECT_EQ(map->root->expansion_keys[1].key, std::string("./styles/"));

    const resolver::PjEntry* dot = resolver::PjEntryValueForKey(*map->root, ".");
    ASSERT_NE(dot, nullptr);
    EXPECT_EQ(dot->str_data, std::string("./index.js"));
}

// ---------------------------------------------------------------------------
// PjEntryKeysStartWithDot / PjEntryValueForKey
// ---------------------------------------------------------------------------

TEST(PackageJSON, PjEntryKeysStartWithDot)
{
    resolver::PjEntry dot_keys = ObjectEntryWithStrings({".", "./feature"});
    EXPECT_TRUE(resolver::PjEntryKeysStartWithDot(dot_keys));

    resolver::PjEntry condition_keys = ObjectEntryWithStrings({"import", "require"});
    EXPECT_FALSE(resolver::PjEntryKeysStartWithDot(condition_keys));

    resolver::PjEntry empty;
    EXPECT_FALSE(resolver::PjEntryKeysStartWithDot(empty));
}

TEST(PackageJSON, PjEntryValueForKey)
{
    resolver::PjEntry entry = ObjectEntryWithStrings({".", "./foo"});

    const resolver::PjEntry* found = resolver::PjEntryValueForKey(entry, ".");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->str_data, std::string("."));

    const resolver::PjEntry* found_foo = resolver::PjEntryValueForKey(entry, "./foo");
    ASSERT_NE(found_foo, nullptr);
    EXPECT_EQ(found_foo->str_data, std::string("./foo"));

    EXPECT_EQ(resolver::PjEntryValueForKey(entry, "./bar"), nullptr);
    EXPECT_EQ(resolver::PjEntryValueForKey(ObjectEntryWithStrings({}), "."), nullptr);
}

// ---------------------------------------------------------------------------
// GlobstarToEscapedRegexp
// ---------------------------------------------------------------------------

TEST(PackageJSON, GlobstarToEscapedRegexp)
{
    auto [globstar_pattern, globstar_wild] =
        resolver::GlobstarToEscapedRegexp("/app/src/**/index.*");
    EXPECT_TRUE(globstar_wild);
    EXPECT_EQ(globstar_pattern,
              std::string("^/app/src/(?:[^/]*(?:/|$))*index\\.[^/]*$"));

    auto [plain_pattern, plain_wild] = resolver::GlobstarToEscapedRegexp("/plain/path.js");
    EXPECT_FALSE(plain_wild);
    EXPECT_EQ(plain_pattern, std::string("^/plain/path\\.js$"));

    auto [leading_globstar_pattern, leading_globstar_wild] =
        resolver::GlobstarToEscapedRegexp("**/lib/*.js");
    EXPECT_TRUE(leading_globstar_wild);
    EXPECT_EQ(leading_globstar_pattern,
              std::string("^(?:[^/]*(?:/|$))*lib/[^/]*\\.js$"));

    auto [single_star_pattern, single_star_wild] = resolver::GlobstarToEscapedRegexp("/x/*/y");
    EXPECT_TRUE(single_star_wild);
    EXPECT_EQ(single_star_pattern, std::string("^/x/[^/]*/y$"));

    auto [question_pattern, question_wild] = resolver::GlobstarToEscapedRegexp("a?b");
    EXPECT_TRUE(question_wild);
    EXPECT_EQ(question_pattern, std::string("^a.b$"));

    auto [regex_chars, regex_wild] = resolver::GlobstarToEscapedRegexp("a.+b");
    EXPECT_FALSE(regex_wild);
    EXPECT_EQ(regex_chars, std::string("^a\\.\\+b$"));
}

// ---------------------------------------------------------------------------
// EsmParsePackageName
// ---------------------------------------------------------------------------

TEST(PackageJSON, EsmParsePackageName)
{
    std::string name;
    std::string subpath;

    EXPECT_TRUE(resolver::EsmParsePackageName("lodash", name, subpath));
    EXPECT_EQ(name, std::string("lodash"));
    EXPECT_EQ(subpath, std::string("."));

    EXPECT_TRUE(resolver::EsmParsePackageName("lodash/zip", name, subpath));
    EXPECT_EQ(name, std::string("lodash"));
    EXPECT_EQ(subpath, std::string("./zip"));

    EXPECT_TRUE(resolver::EsmParsePackageName("@scope/pkg/util", name, subpath));
    EXPECT_EQ(name, std::string("@scope/pkg"));
    EXPECT_EQ(subpath, std::string("./util"));

    EXPECT_TRUE(resolver::EsmParsePackageName("@scope/pkg", name, subpath));
    EXPECT_EQ(name, std::string("@scope/pkg"));
    EXPECT_EQ(subpath, std::string("."));

    // Scoped names need both segments.
    EXPECT_FALSE(resolver::EsmParsePackageName("@scope", name, subpath));

    // Empty input is not a package.
    EXPECT_FALSE(resolver::EsmParsePackageName("", name, subpath));

    // Relative spellings are not packages.
    EXPECT_FALSE(resolver::EsmParsePackageName(".", name, subpath));
    EXPECT_FALSE(resolver::EsmParsePackageName("./local", name, subpath));

    // Backslashes and percent signs make the name invalid.
    EXPECT_FALSE(resolver::EsmParsePackageName("a\\b", name, subpath));
    EXPECT_FALSE(resolver::EsmParsePackageName("foo%20", name, subpath));
    EXPECT_FALSE(resolver::EsmParsePackageName("@scope/pkg%20/x", name, subpath));
}

// ---------------------------------------------------------------------------
// FindInvalidSegment
// ---------------------------------------------------------------------------

TEST(PackageJSON, FindInvalidSegment)
{
    EXPECT_TRUE(resolver::FindInvalidSegment("./a/b").empty());
    EXPECT_TRUE(resolver::FindInvalidSegment("a").empty());
    EXPECT_TRUE(resolver::FindInvalidSegment("").empty());

    EXPECT_EQ(resolver::FindInvalidSegment("./a/../b"), std::string_view(".."));
    EXPECT_EQ(resolver::FindInvalidSegment("./a/./b"), std::string_view("."));
    EXPECT_EQ(resolver::FindInvalidSegment("./a/node_modules/b"),
              std::string_view("node_modules"));
    EXPECT_EQ(resolver::FindInvalidSegment("./node_modules"),
              std::string_view("node_modules"));

    // Windows separators are treated the same as slashes.
    EXPECT_EQ(resolver::FindInvalidSegment("./a\\..\\b"), std::string_view(".."));

    // The leading segment is exempt: "node_modules/x" is just a package name.
    EXPECT_TRUE(resolver::FindInvalidSegment("node_modules/x").empty());
}