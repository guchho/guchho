// Unit tests for the AST caches defined in src/cache/cache_ast.cpp: the
// CSSCache, JSONCache, JSCache, and HtmlCache, plus the JsonOptionsEqual
// helper they share. Each cache stores one entry per source key path and
// serves a hit only when the stored source and options still match, so the
// tests below exercise (a) the hit path, (b) every stated cause of a miss, and
// (c) the message replay that keeps diagnostics identical across hits.

#include "test/guchho_test.hpp"

#include "guchho/cache.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/css/css_parser.hpp"
#include "guchho/css/css_printer.hpp"
#include "guchho/html/html_bridge.hpp"
#include "guchho/html/html_printer.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/javascript/js_printer.hpp"
#include "guchho/javascript/js_renamer.hpp"
#include "guchho/logger.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cache = guchho::cache;
namespace compat = guchho::compat;
namespace compiler = guchho::compiler;
namespace config = guchho::config;
namespace css = guchho::css;
namespace html = guchho::html;
namespace javascript = guchho::javascript;
namespace js = guchho::javascript;

using guchho::logger::DeferLogKind;
using guchho::logger::Log;
using guchho::logger::NewDeferLog;
using guchho::logger::Path;
using guchho::logger::Source;

namespace {

// Builds a source whose key path is exactly "path". The key path string is the
// cache's map key, so two sources that share a path but differ in text collide
// at the map level; the full-source equality check inside each cache then turns
// that collision into a miss (see the *ChangedContentsMiss tests below).
Source MakeSource(std::string contents, std::string path) {
    Source source;
    source.index = 0;
    source.identifier_name = path;
    source.pretty_paths = {path, path};
    source.key_path = Path{path, {}, {}, {}, {}};
    source.contents = std::move(contents);
    return source;
}

// A fresh capturing log for feeding a cache Parse call. Every cache takes the
// log by non-const reference (it forwards diagnostics into it), so the caller
// must hold the log in a local rather than passing a temporary.
Log NewLog() {
    return NewDeferLog(DeferLogKind::kDeferLogAll, {});
}

// Runs "fn" against a fresh capturing log and reports how many diagnostics the
// operation forwarded into it. Each cache replays stored messages on a hit and
// forwards freshly emitted ones on a miss, so equal counts across two calls
// mean the diagnostic output was preserved.
size_t CapturedMessageCount(std::function<void(Log&)> fn) {
    Log log = NewLog();
    fn(log);
    return log.done().size();
}

// Serializes a CSS AST the same way test/css/css_printer_test.cpp does: the
// parser's symbols are moved into a fresh symbol map, then the minified
// printer renders the tree. Two ASTs that serialize identically are
// observationally equal to the bundler.
std::string PrintCSS(css::AST ast) {
    css::PrinterOptions options;
    options.minify_whitespace = true;

    compiler::SymbolMap symbols = compiler::NewSymbolMap(1);
    symbols.symbols_for_source[0] = std::move(ast.symbols);

    return css::Print(ast, symbols, options).css;
}

// Serializes a JavaScript AST with the runtime omitted, mirroring the helper
// used by the JS parser tests. Used to compare a freshly parsed AST against
// the AST a cache hands back on a hit.
std::string PrintJS(const javascript::AST& ast) {
    compiler::SymbolMap symbol_map;
    symbol_map.symbols_for_source.resize(1);
    symbol_map.symbols_for_source[0] = ast.symbols;

    auto renamer = javascript::NewNoOpRenamer(symbol_map);

    javascript::PrinterOptions print_options;
    print_options.omit_runtime_for_tests = true;

    return javascript::Print(ast, symbol_map, *renamer, print_options).js;
}

} // namespace

// ---------------------------------------------------------------------------
// JsonOptionsEqual
// ---------------------------------------------------------------------------

TEST(CacheAst, JsonOptionsEqualAcceptIdenticalOptions) {
    EXPECT_TRUE(cache::JsonOptionsEqual(javascript::JSONOptions{}, javascript::JSONOptions{}));
}

TEST(CacheAst, JsonOptionsEqualRejectEveryDifference) {
    javascript::JSONOptions options;

    javascript::JSONOptions unsupported = options;
    unsupported.unsupported_js_features = compat::JSFeature::kClass;
    EXPECT_FALSE(cache::JsonOptionsEqual(options, unsupported));

    javascript::JSONOptions flavor = options;
    flavor.flavor = javascript::JSONFlavor::kTSConfigJSON;
    EXPECT_FALSE(cache::JsonOptionsEqual(options, flavor));

    javascript::JSONOptions suffix = options;
    suffix.error_suffix = "here";
    EXPECT_FALSE(cache::JsonOptionsEqual(options, suffix));

    javascript::JSONOptions define = options;
    define.is_for_define = true;
    EXPECT_FALSE(cache::JsonOptionsEqual(options, define));
}

// ---------------------------------------------------------------------------
// CSSCache
// ---------------------------------------------------------------------------

TEST(CacheAst, CSSCacheServesASecondParseFromTheCache) {
    cache::CSSCache container;
    css::ParserOptions options;
    Log log = NewLog();

    bool was_cached = false;
    css::AST first = container.Parse(log,
                                     MakeSource(".a { color: red; }", "app.css"),
                                     options, &was_cached);
    EXPECT_FALSE(was_cached);

    was_cached = false;
    css::AST second = container.Parse(log,
                                      MakeSource(".a { color: red; }", "app.css"),
                                      options, &was_cached);
    EXPECT_TRUE(was_cached);

    EXPECT_EQ(PrintCSS(first), PrintCSS(second));
}

TEST(CacheAst, CSSCacheMissesWhenParserOptionsChange) {
    cache::CSSCache container;
    css::ParserOptions options;
    Log log = NewLog();

    bool was_cached = false;
    container.Parse(log, MakeSource(".a { color: red; }", "app.css"), options, &was_cached);
    EXPECT_FALSE(was_cached);

    css::ParserOptions changed = options;
    changed.minify_syntax = true;

    was_cached = false;
    container.Parse(log, MakeSource(".a { color: red; }", "app.css"), changed, &was_cached);
    EXPECT_FALSE(was_cached);
}

TEST(CacheAst, CSSCacheMissesWhenContentUnderSameKeyPathChanges) {
    cache::CSSCache container;
    css::ParserOptions options;
    Log log = NewLog();

    bool was_cached = false;
    container.Parse(log, MakeSource(".a { color: red; }", "app.css"), options, &was_cached);
    EXPECT_FALSE(was_cached);

    was_cached = false;
    container.Parse(log, MakeSource(".b { color: blue; }", "app.css"), options, &was_cached);
    EXPECT_FALSE(was_cached);
}

TEST(CacheAst, CSSCacheReplaysMessagesOnEveryHit) {
    cache::CSSCache container;
    css::ParserOptions options;
    Source source = MakeSource("@charset \"latin1\";", "app.css");

    size_t first = CapturedMessageCount([&](Log& log) {
        container.Parse(log, source, options);
    });
    EXPECT_EQ(1u, first);

    size_t second = CapturedMessageCount([&](Log& log) {
        bool was_cached = false;
        container.Parse(log, source, options, &was_cached);
        EXPECT_TRUE(was_cached);
    });
    EXPECT_EQ(first, second);
}

// ---------------------------------------------------------------------------
// JSONCache
// ---------------------------------------------------------------------------

TEST(CacheAst, JSONCacheServesASecondParseFromTheCache) {
    cache::JSONCache container;
    javascript::JSONOptions options;
    Log log = NewLog();

    bool was_cached = false;
    auto [first_expr, first_ok] = container.Parse(
        log, MakeSource("123", "data.json"), options, &was_cached);
    EXPECT_TRUE(first_ok);
    EXPECT_FALSE(was_cached);

    was_cached = false;
    auto [second_expr, second_ok] = container.Parse(
        log, MakeSource("123", "data.json"), options, &was_cached);
    EXPECT_TRUE(second_ok);
    EXPECT_TRUE(was_cached);
}

TEST(CacheAst, JSONCacheCachesFailures) {
    cache::JSONCache container;
    javascript::JSONOptions options;
    Source source = MakeSource("undefined", "data.json");

    size_t first = CapturedMessageCount([&](Log& log) {
        auto [expr, ok] = container.Parse(log, source, options);
        EXPECT_FALSE(ok);
    });

    size_t second = CapturedMessageCount([&](Log& log) {
        bool was_cached = false;
        auto [expr, ok] = container.Parse(log, source, options, &was_cached);
        EXPECT_TRUE(was_cached);
        EXPECT_FALSE(ok);
    });

    EXPECT_GT(first, 0u);
    EXPECT_EQ(first, second);
}

TEST(CacheAst, JSONCacheMissesWhenErrorSuffixChanges) {
    cache::JSONCache container;
    javascript::JSONOptions options;
    options.error_suffix = "original";
    Log log = NewLog();

    bool was_cached = false;
    container.Parse(log, MakeSource("123", "data.json"), options, &was_cached);
    EXPECT_FALSE(was_cached);

    javascript::JSONOptions changed = options;
    changed.error_suffix = "rewritten";

    was_cached = false;
    container.Parse(log, MakeSource("123", "data.json"), changed, &was_cached);
    EXPECT_FALSE(was_cached);
}

// ---------------------------------------------------------------------------
// JSCache
// ---------------------------------------------------------------------------

TEST(CacheAst, JSCacheServesASecondParseFromTheCache) {
    config::Options config_options{};
    js::Options options = js::OptionsFromConfig(&config_options);

    cache::JSCache container;
    Source source = MakeSource("var x = 1;", "app.js");
    Log log = NewLog();

    bool was_cached = false;
    auto [first, first_ok] = container.Parse(log, source, options, &was_cached);
    EXPECT_TRUE(first_ok);
    EXPECT_FALSE(was_cached);

    was_cached = false;
    auto [second, second_ok] = container.Parse(log, source, options, &was_cached);
    EXPECT_TRUE(second_ok);
    EXPECT_TRUE(was_cached);

    EXPECT_EQ(PrintJS(first), PrintJS(second));
}

TEST(CacheAst, JSCacheCachesFailures) {
    config::Options config_options{};
    js::Options options = js::OptionsFromConfig(&config_options);
    Source source = MakeSource("var = ;", "app.js");

    cache::JSCache container;

    size_t first = CapturedMessageCount([&](Log& log) {
        auto [ast, ok] = container.Parse(log, source, options);
        EXPECT_FALSE(ok);
    });

    size_t second = CapturedMessageCount([&](Log& log) {
        bool was_cached = false;
        auto [ast, ok] = container.Parse(log, source, options, &was_cached);
        EXPECT_TRUE(was_cached);
        EXPECT_FALSE(ok);
    });

    EXPECT_GT(first, 0u);
    EXPECT_EQ(first, second);
}

TEST(CacheAst, JSCacheMissesWhenOptionsChange) {
    config::Options plain_config{};
    js::Options plain_options = js::OptionsFromConfig(&plain_config);

    config::Options minify_config{};
    minify_config.MinifySyntax = true;
    js::Options minify_options = js::OptionsFromConfig(&minify_config);

    cache::JSCache container;
    Source source = MakeSource("var x = 1;", "app.js");
    Log log = NewLog();

    bool was_cached = false;
    container.Parse(log, source, plain_options, &was_cached);
    EXPECT_FALSE(was_cached);

    was_cached = false;
    container.Parse(log, source, minify_options, &was_cached);
    EXPECT_FALSE(was_cached);
}

TEST(CacheAst, JSCacheMissesWhenContentUnderSameKeyPathChanges) {
    config::Options config_options{};
    js::Options options = js::OptionsFromConfig(&config_options);

    cache::JSCache container;
    Log log = NewLog();

    bool was_cached = false;
    container.Parse(log, MakeSource("var x = 1;", "app.js"), options, &was_cached);
    EXPECT_FALSE(was_cached);

    was_cached = false;
    container.Parse(log, MakeSource("var y = 2;", "app.js"), options, &was_cached);
    EXPECT_FALSE(was_cached);
}

// ---------------------------------------------------------------------------
// HtmlCache
// ---------------------------------------------------------------------------

TEST(CacheAst, HtmlCacheServesASecondParseFromTheCache) {
    cache::HtmlCache container;
    html::BridgeOptions options;
    options.parse_fragment = true;
    Source source = MakeSource("<p>hi</p>", "page.html");
    Log log = NewLog();

    bool was_cached = false;
    html::AST first = container.Parse(log, source, options, &was_cached).first;
    EXPECT_FALSE(was_cached);
    EXPECT_EQ("<p>hi</p>", html::Print(*first.node, {}));

    was_cached = false;
    html::AST second = container.Parse(log, source, options, &was_cached).first;
    EXPECT_TRUE(was_cached);
    EXPECT_EQ(html::Print(*first.node, {}), html::Print(*second.node, {}));
}

TEST(CacheAst, HtmlCacheGivesEachCallerAPrivateTree) {
    cache::HtmlCache container;
    html::BridgeOptions options;
    options.parse_fragment = true;
    Source source = MakeSource("<p>hi</p>", "page.html");
    Log log = NewLog();

    bool was_cached = false;
    html::AST first = container.Parse(log, source, options, &was_cached).first;
    EXPECT_FALSE(was_cached);
    std::string baseline = html::Print(*first.node, {});

    // Rewriting the first caller's tree must not disturb the stored entry: a
    // later hit still receives the pristine original.
    first.node->child_nodes[0]->tag_name = "code";
    EXPECT_EQ("<code>hi</code>", html::Print(*first.node, {}));

    was_cached = false;
    html::AST second = container.Parse(log, source, options, &was_cached).first;
    EXPECT_TRUE(was_cached);
    EXPECT_EQ(baseline, html::Print(*second.node, {}));
}

TEST(CacheAst, HtmlCacheMissesWhenBridgeOptionsChange) {
    cache::HtmlCache container;
    html::BridgeOptions options;
    Log log = NewLog();

    bool was_cached = false;
    container.Parse(log, MakeSource("<p>hi</p>", "page.html"), options, &was_cached);
    EXPECT_FALSE(was_cached);

    html::BridgeOptions changed = options;
    changed.parse_fragment = true;

    was_cached = false;
    container.Parse(log, MakeSource("<p>hi</p>", "page.html"), changed, &was_cached);
    EXPECT_FALSE(was_cached);
}