#include "test/guchho_test.hpp"

#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/javascript/js_printer.hpp"
#include "guchho/javascript/js_renamer.hpp"
#include "guchho/logger.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace js = guchho::javascript;
namespace compiler = guchho::compiler;
namespace config = guchho::config;
namespace compat = guchho::compat;
namespace logger = guchho::logger;

namespace {

struct LiteralString {
    std::string value;

    template <size_t N>
    LiteralString(const char (&lit)[N])
        : value(lit, N - 1) {}
    LiteralString(std::string lit)
        : value(std::move(lit)) {}
};

logger::Source SourceForTest(std::string contents) {
    logger::Source s;
    s.index = 0;
    s.identifier_name = "stdin";
    s.pretty_paths = {std::string("<stdin>"), std::string("<stdin>")};
    s.key_path = logger::Path{"<stdin>", {}, {}, {}, {}};
    s.contents = std::move(contents);
    return s;
}

void expectPrintedCommon(const std::string& contents, const std::string& expected, config::Options* options) {
    logger::Log log = logger::NewDeferLog(logger::DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    auto parsed = js::Parse(log, SourceForTest(contents), js::OptionsFromConfig(options));
    auto msgs = log.done();

    std::string text;
    for (const auto& msg : msgs) {
        if (msg.kind != logger::MsgKind::kError) {
            continue;
        }
        text += msg.String(logger::OutputOptions{}, logger::TerminalInfo{});
    }
    EXPECT_EQ(text, "");
    ASSERT_TRUE(parsed.second) << "Parse error";

    compiler::SymbolMap symbols;
    symbols.symbols_for_source.resize(1);
    symbols.symbols_for_source[0] = parsed.first.symbols;
    auto renamer = js::NewNoOpRenamer(symbols);

    js::PrinterOptions print_options;
    print_options.ascii_only = options->ASCIIOnly;
    print_options.minify_syntax = options->MinifySyntax;
    print_options.minify_whitespace = options->MinifyWhitespace;
    print_options.unsupported_features = options->UnsupportedJSFeatures;

    js::PrintResult result = js::Print(parsed.first, symbols, *renamer, print_options);
    EXPECT_EQ(result.js, expected);
}

void expectPrinted(LiteralString contents, LiteralString expected) {
    config::Options options{};
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedMinify(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.MinifyWhitespace = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedMangle(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.MinifySyntax = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedMangleMinify(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.MinifySyntax = true;
    options.MinifyWhitespace = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedASCII(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.ASCIIOnly = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedMinifyASCII(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.MinifyWhitespace = true;
    options.ASCIIOnly = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedTarget(int es_version, LiteralString contents, LiteralString expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedTargetMinify(int es_version, LiteralString contents, LiteralString expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    options.MinifyWhitespace = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedTargetMangle(int es_version, LiteralString contents, LiteralString expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    options.MinifySyntax = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedTargetMangleMinify(int es_version, LiteralString contents, LiteralString expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    options.MinifySyntax = true;
    options.MinifyWhitespace = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedTargetASCII(int es_version, LiteralString contents, LiteralString expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    options.ASCIIOnly = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedJSX(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.JSX.Parse = true;
    options.JSX.Preserve = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedJSXASCII(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.JSX.Parse = true;
    options.JSX.Preserve = true;
    options.ASCIIOnly = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

void expectPrintedJSXMinify(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.JSX.Parse = true;
    options.JSX.Preserve = true;
    options.MinifyWhitespace = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

} // namespace

TEST(JsPrinter, TestNumber) {
    // Check "1eN"
    expectPrinted("x = 1e-100", "x = 1e-100;\n");
    expectPrinted("x = 1e-4", "x = 1e-4;\n");
    expectPrinted("x = 1e-3", "x = 1e-3;\n");
    expectPrinted("x = 1e-2", "x = 0.01;\n");
    expectPrinted("x = 1e-1", "x = 0.1;\n");
    expectPrinted("x = 1e0", "x = 1;\n");
    expectPrinted("x = 1e1", "x = 10;\n");
    expectPrinted("x = 1e2", "x = 100;\n");
    expectPrinted("x = 1e3", "x = 1e3;\n");
    expectPrinted("x = 1e4", "x = 1e4;\n");
    expectPrinted("x = 1e100", "x = 1e100;\n");
    expectPrintedMinify("x = 1e-100", "x=1e-100;");
    expectPrintedMinify("x = 1e-5", "x=1e-5;");
    expectPrintedMinify("x = 1e-4", "x=1e-4;");
    expectPrintedMinify("x = 1e-3", "x=.001;");
    expectPrintedMinify("x = 1e-2", "x=.01;");
    expectPrintedMinify("x = 1e-1", "x=.1;");
    expectPrintedMinify("x = 1e0", "x=1;");
    expectPrintedMinify("x = 1e1", "x=10;");
    expectPrintedMinify("x = 1e2", "x=100;");
    expectPrintedMinify("x = 1e3", "x=1e3;");
    expectPrintedMinify("x = 1e4", "x=1e4;");
    expectPrintedMinify("x = 1e100", "x=1e100;");

    // Check "12eN"
    expectPrinted("x = 12e-100", "x = 12e-100;\n");
    expectPrinted("x = 12e-5", "x = 12e-5;\n");
    expectPrinted("x = 12e-4", "x = 12e-4;\n");
    expectPrinted("x = 12e-3", "x = 0.012;\n");
    expectPrinted("x = 12e-2", "x = 0.12;\n");
    expectPrinted("x = 12e-1", "x = 1.2;\n");
    expectPrinted("x = 12e0", "x = 12;\n");
    expectPrinted("x = 12e1", "x = 120;\n");
    expectPrinted("x = 12e2", "x = 1200;\n");
    expectPrinted("x = 12e3", "x = 12e3;\n");
    expectPrinted("x = 12e4", "x = 12e4;\n");
    expectPrinted("x = 12e100", "x = 12e100;\n");
    expectPrintedMinify("x = 12e-100", "x=12e-100;");
    expectPrintedMinify("x = 12e-6", "x=12e-6;");
    expectPrintedMinify("x = 12e-5", "x=12e-5;");
    expectPrintedMinify("x = 12e-4", "x=.0012;");
    expectPrintedMinify("x = 12e-3", "x=.012;");
    expectPrintedMinify("x = 12e-2", "x=.12;");
    expectPrintedMinify("x = 12e-1", "x=1.2;");
    expectPrintedMinify("x = 12e0", "x=12;");
    expectPrintedMinify("x = 12e1", "x=120;");
    expectPrintedMinify("x = 12e2", "x=1200;");
    expectPrintedMinify("x = 12e3", "x=12e3;");
    expectPrintedMinify("x = 12e4", "x=12e4;");
    expectPrintedMinify("x = 12e100", "x=12e100;");

    // Check cases for "A.BeX" => "ABeY" simplification
    expectPrinted("x = 123456789", "x = 123456789;\n");
    expectPrinted("x = 1123456789", "x = 1123456789;\n");
    expectPrinted("x = 10123456789", "x = 10123456789;\n");
    expectPrinted("x = 100123456789", "x = 100123456789;\n");
    expectPrinted("x = 1000123456789", "x = 1000123456789;\n");
    expectPrinted("x = 10000123456789", "x = 10000123456789;\n");
    expectPrinted("x = 100000123456789", "x = 100000123456789;\n");
    expectPrinted("x = 1000000123456789", "x = 1000000123456789;\n");
    expectPrinted("x = 10000000123456789", "x = 10000000123456788;\n");
    expectPrinted("x = 100000000123456789", "x = 100000000123456780;\n");
    expectPrinted("x = 1000000000123456789", "x = 1000000000123456800;\n");
    expectPrinted("x = 10000000000123456789", "x = 10000000000123458e3;\n");
    expectPrinted("x = 100000000000123456789", "x = 10000000000012345e4;\n");


    // int32
    expectPrinted("x = 0x7fff_ffff", "x = 2147483647;\n");
    expectPrinted("x = 0x8000_0000", "x = 2147483648;\n");
    expectPrinted("x = 0x8000_0001", "x = 2147483649;\n");
    expectPrinted("x = -0x7fff_ffff", "x = -2147483647;\n");
    expectPrinted("x = -0x8000_0000", "x = -2147483648;\n");
    expectPrinted("x = -0x8000_0001", "x = -2147483649;\n");

    // uint32
    expectPrinted("x = 0xffff_ffff", "x = 4294967295;\n");
    expectPrinted("x = 0x1_0000_0000", "x = 4294967296;\n");
    expectPrinted("x = 0x1_0000_0001", "x = 4294967297;\n");
    expectPrinted("x = -0xffff_ffff", "x = -4294967295;\n");
    expectPrinted("x = -0x1_0000_0000", "x = -4294967296;\n");
    expectPrinted("x = -0x1_0000_0001", "x = -4294967297;\n");

    // int64
    expectPrinted("x = 0x7fff_ffff_ffff_fdff", "x = 9223372036854775e3;\n");
    expectPrinted("x = 0x8000_0000_0000_0000", "x = 9223372036854776e3;\n");
    expectPrinted("x = 0x8000_0000_0000_3000", "x = 9223372036854788e3;\n");
    expectPrinted("x = -0x7fff_ffff_ffff_fdff", "x = -9223372036854775e3;\n");
    expectPrinted("x = -0x8000_0000_0000_0000", "x = -9223372036854776e3;\n");
    expectPrinted("x = -0x8000_0000_0000_3000", "x = -9223372036854788e3;\n");

    // uint64
    expectPrinted("x = 0xffff_ffff_ffff_fbff", "x = 1844674407370955e4;\n");
    expectPrinted("x = 0x1_0000_0000_0000_0000", "x = 18446744073709552e3;\n");
    expectPrinted("x = 0x1_0000_0000_0000_1000", "x = 18446744073709556e3;\n");
    expectPrinted("x = -0xffff_ffff_ffff_fbff", "x = -1844674407370955e4;\n");
    expectPrinted("x = -0x1_0000_0000_0000_0000", "x = -18446744073709552e3;\n");
    expectPrinted("x = -0x1_0000_0000_0000_1000", "x = -18446744073709556e3;\n");

    // Check the hex vs. decimal decision boundary when minifying
    expectPrinted("x = 999999999999", "x = 999999999999;\n");
    expectPrinted("x = 1000000000001", "x = 1000000000001;\n");
    expectPrinted("x = 0x0FFF_FFFF_FFFF_FF80", "x = 1152921504606846800;\n");
    expectPrinted("x = 0x1000_0000_0000_0000", "x = 1152921504606847e3;\n");
    expectPrinted("x = 0xFFFF_FFFF_FFFF_F000", "x = 18446744073709548e3;\n");
    expectPrinted("x = 0xFFFF_FFFF_FFFF_F800", "x = 1844674407370955e4;\n");
    expectPrinted("x = 0xFFFF_FFFF_FFFF_FFFF", "x = 18446744073709552e3;\n");
    expectPrintedMinify("x = 999999999999", "x=999999999999;");
    expectPrintedMinify("x = 1000000000001", "x=0xe8d4a51001;");
    expectPrintedMinify("x = 0x0FFF_FFFF_FFFF_FF80", "x=0xfffffffffffff80;");
    expectPrintedMinify("x = 0x1000_0000_0000_0000", "x=1152921504606847e3;");
    expectPrintedMinify("x = 0xFFFF_FFFF_FFFF_F000", "x=0xfffffffffffff000;");
    expectPrintedMinify("x = 0xFFFF_FFFF_FFFF_F800", "x=1844674407370955e4;");
    expectPrintedMinify("x = 0xFFFF_FFFF_FFFF_FFFF", "x=18446744073709552e3;");

    // Check printing a space in between a number and a subsequent "."
    expectPrintedMinify("x = 0.0001 .y", "x=1e-4.y;");
    expectPrintedMinify("x = 0.001 .y", "x=.001.y;");
    expectPrintedMinify("x = 0.01 .y", "x=.01.y;");
    expectPrintedMinify("x = 0.1 .y", "x=.1.y;");
    expectPrintedMinify("x = 0 .y", "x=0 .y;");
    expectPrintedMinify("x = 10 .y", "x=10 .y;");
    expectPrintedMinify("x = 100 .y", "x=100 .y;");
    expectPrintedMinify("x = 1000 .y", "x=1e3.y;");
    expectPrintedMinify("x = 12345 .y", "x=12345 .y;");
    expectPrintedMinify("x = 0xFFFF_0000_FFFF_0000 .y", "x=0xffff0000ffff0000.y;");
}

TEST(JsPrinter, TestArray) {
    expectPrinted("[]", "[];\n");
    expectPrinted("[,]", "[,];\n");
    expectPrinted("[,,]", "[, ,];\n");
}

TEST(JsPrinter, TestSplat) {
    expectPrinted("[...(a, b)]", "[...(a, b)];\n");
    expectPrinted("x(...(a, b))", "x(...(a, b));\n");
    expectPrinted("({...(a, b)})", "({ ...(a, b) });\n");
}

TEST(JsPrinter, TestNew) {
    expectPrinted("new x", "new x();\n");
    expectPrinted("new x()", "new x();\n");
    expectPrinted("new (x)", "new x();\n");
    expectPrinted("new (x())", "new (x())();\n");
    expectPrinted("new (new x())", "new new x()();\n");
    expectPrinted("new (x + x)", "new (x + x)();\n");
    expectPrinted("(new x)()", "new x()();\n");

    expectPrinted("new foo().bar", "new foo().bar;\n");
    expectPrinted("new (foo().bar)", "new (foo()).bar();\n");
    expectPrinted("new (foo()).bar", "new (foo()).bar();\n");
    expectPrinted("new foo()[bar]", "new foo()[bar];\n");
    expectPrinted("new (foo()[bar])", "new (foo())[bar]();\n");
    expectPrinted("new (foo())[bar]", "new (foo())[bar]();\n");

    expectPrinted("new (import('foo').bar)", "new (import(\"foo\")).bar();\n");
    expectPrinted("new (import('foo')).bar", "new (import(\"foo\")).bar();\n");
    expectPrinted("new (import('foo')[bar])", "new (import(\"foo\"))[bar]();\n");
    expectPrinted("new (import('foo'))[bar]", "new (import(\"foo\"))[bar]();\n");

    expectPrintedMinify("new x", "new x;");
    expectPrintedMinify("new x.y", "new x.y;");
    expectPrintedMinify("(new x).y", "new x().y;");
    expectPrintedMinify("new x().y", "new x().y;");
    expectPrintedMinify("new x() + y", "new x+y;");
    expectPrintedMinify("new x() ** 2", "new x**2;");

    // Test preservation of Webpack-specific comments
    expectPrinted("new Worker(// webpackFoo: 1\n // webpackBar: 2\n 'path');", "new Worker(\n  // webpackFoo: 1\n  // webpackBar: 2\n  \"path\"\n);\n");
    expectPrinted("new Worker(/* webpackFoo: 1 */ /* webpackBar: 2 */ 'path');", "new Worker(\n  /* webpackFoo: 1 */\n  /* webpackBar: 2 */\n  \"path\"\n);\n");
    expectPrinted("new Worker(\n    /* multi\n     * line\n     * webpackBar: */ 'path');", "new Worker(\n  /* multi\n   * line\n   * webpackBar: */\n  \"path\"\n);\n");
    expectPrinted("new Worker(/* webpackFoo: 1 */ 'path' /* webpackBar:2 */);", "new Worker(\n  /* webpackFoo: 1 */\n  \"path\"\n  /* webpackBar:2 */\n);\n");
    expectPrinted("new Worker(/* webpackFoo: 1 */ 'path' /* webpackBar:2 */ ,);", "new Worker(\n  /* webpackFoo: 1 */\n  \"path\"\n);\n"); // Not currently handled
    expectPrinted("new Worker(/* webpackFoo: 1 */ 'path', /* webpackBar:2 */ );", "new Worker(\n  /* webpackFoo: 1 */\n  \"path\"\n  /* webpackBar:2 */\n);\n");
    expectPrinted("new Worker(new URL('path', /* webpackFoo: these can go anywhere */ import.meta.url))",
        "new Worker(new URL(\n  \"path\",\n  /* webpackFoo: these can go anywhere */\n  import.meta.url\n));\n");
}

TEST(JsPrinter, TestCall) {
    expectPrinted("x()()()", "x()()();\n");
    expectPrinted("x().y()[z]()", "x().y()[z]();\n");
    expectPrinted("(--x)();", "(--x)();\n");
    expectPrinted("(x--)();", "(x--)();\n");

    expectPrinted("eval(x)", "eval(x);\n");
    expectPrinted("eval?.(x)", "eval?.(x);\n");
    expectPrinted("(eval)(x)", "eval(x);\n");
    expectPrinted("(eval)?.(x)", "eval?.(x);\n");

    expectPrinted("eval(x, y)", "eval(x, y);\n");
    expectPrinted("eval?.(x, y)", "eval?.(x, y);\n");
    expectPrinted("(1, eval)(x)", "(1, eval)(x);\n");
    expectPrinted("(1, eval)?.(x)", "(1, eval)?.(x);\n");
    expectPrintedMangle("(1 ? eval : 2)(x)", "(0, eval)(x);\n");
    expectPrintedMangle("(1 ? eval : 2)?.(x)", "eval?.(x);\n");

    expectPrintedMinify("eval?.(x)", "eval?.(x);");
    expectPrintedMinify("eval(x,y)", "eval(x,y);");
    expectPrintedMinify("eval?.(x,y)", "eval?.(x,y);");
    expectPrintedMinify("(1, eval)(x)", "(1,eval)(x);");
    expectPrintedMinify("(1, eval)?.(x)", "(1,eval)?.(x);");
    expectPrintedMangleMinify("(1 ? eval : 2)(x)", "(0,eval)(x);");
    expectPrintedMangleMinify("(1 ? eval : 2)?.(x)", "eval?.(x);");
}

TEST(JsPrinter, TestMember) {
    expectPrinted("x.y[z]", "x.y[z];\n");
    expectPrinted("((x+1).y+1)[z]", "((x + 1).y + 1)[z];\n");
}

TEST(JsPrinter, TestComma) {
    expectPrinted("1, 2, 3", "1, 2, 3;\n");
    expectPrinted("(1, 2), 3", "1, 2, 3;\n");
    expectPrinted("1, (2, 3)", "1, 2, 3;\n");
    expectPrinted("a ? (b, c) : (d, e)", "a ? (b, c) : (d, e);\n");
    expectPrinted("let x = (a, b)", "let x = (a, b);\n");
    expectPrinted("(x = a), b", "x = a, b;\n");
    expectPrinted("x = (a, b)", "x = (a, b);\n");
    expectPrinted("x((1, 2))", "x((1, 2));\n");
}

TEST(JsPrinter, TestUnary) {
    expectPrinted("+(x--)", "+x--;\n");
    expectPrinted("-(x++)", "-x++;\n");
}

TEST(JsPrinter, TestNullish) {
    // "??" can't directly contain "||" or "&&"
    expectPrinted("(a && b) ?? c", "(a && b) ?? c;\n");
    expectPrinted("(a || b) ?? c", "(a || b) ?? c;\n");
    expectPrinted("a ?? (b && c)", "a ?? (b && c);\n");
    expectPrinted("a ?? (b || c)", "a ?? (b || c);\n");

    // "||" and "&&" can't directly contain "??"
    expectPrinted("a && (b ?? c)", "a && (b ?? c);\n");
    expectPrinted("a || (b ?? c)", "a || (b ?? c);\n");
    expectPrinted("(a ?? b) && c", "(a ?? b) && c;\n");
    expectPrinted("(a ?? b) || c", "(a ?? b) || c;\n");
}

TEST(JsPrinter, TestString) {
    expectPrinted("let x = ''", "let x = \"\";\n");
    expectPrinted("let x = '\b'", "let x = \"\\b\";\n");
    expectPrinted("let x = '\f'", "let x = \"\\f\";\n");
    expectPrinted("let x = '\t'", "let x = \"\t\";\n");
    expectPrinted("let x = '\v'", "let x = \"\\v\";\n");
    expectPrinted("let x = '\\n'", "let x = \"\\n\";\n");
    expectPrinted("let x = '\\''", "let x = \"'\";\n");
    expectPrinted("let x = '\\\"'", "let x = '\"';\n");
    expectPrinted("let x = '\\'\"'", "let x = `'\"`;\n");
    expectPrinted("let x = '\\\\'", "let x = \"\\\\\";\n");
    expectPrinted("let x = '\x00'", "let x = \"\\0\";\n");
    expectPrinted("let x = '\x00!'", "let x = \"\\0!\";\n");
    expectPrinted("let x = '\x00" "1'", "let x = \"\\x001\";\n");
    expectPrinted("let x = '\\0'", "let x = \"\\0\";\n");
    expectPrinted("let x = '\\0!'", "let x = \"\\0!\";\n");
    expectPrinted("let x = '\x07'", "let x = \"\\x07\";\n");
    expectPrinted("let x = '\x07!'", "let x = \"\\x07!\";\n");
    expectPrinted("let x = '\x07" "1'", "let x = \"\\x071\";\n");
    expectPrinted("let x = '\\7'", "let x = \"\\x07\";\n");
    expectPrinted("let x = '\\7!'", "let x = \"\\x07!\";\n");
    expectPrinted("let x = '\\01'", "let x = \"\x01\";\n");
    expectPrinted("let x = '\x10'", "let x = \"\x10\";\n");
    expectPrinted("let x = '\\x10'", "let x = \"\x10\";\n");
    expectPrinted("let x = '\x1B'", "let x = \"\\x1B\";\n");
    expectPrinted("let x = '\\x1B'", "let x = \"\\x1B\";\n");
    expectPrinted("let x = '\uABCD'", "let x = \"\uABCD\";\n");
    expectPrinted("let x = '\\uABCD'", "let x = \"\uABCD\";\n");
    expectPrinted("let x = '\U000123AB'", "let x = \"\U000123AB\";\n");
    expectPrinted("let x = '\\u{123AB}'", "let x = \"\U000123AB\";\n");
    expectPrinted("let x = '\\uD808\\uDFAB'", "let x = \"\U000123AB\";\n");
    expectPrinted("let x = '\\uD808'", "let x = \"\\uD808\";\n");
    expectPrinted("let x = '\\uD808X'", "let x = \"\\uD808X\";\n");
    expectPrinted("let x = '\\uDFAB'", "let x = \"\\uDFAB\";\n");
    expectPrinted("let x = '\\uDFABX'", "let x = \"\\uDFABX\";\n");

    expectPrinted("let x = '\\x80'", "let x = \"\xC2\x80\";\n");
    expectPrinted("let x = '\\xFF'", "let x = \"\xC3\xBF\";\n");
    expectPrinted("let x = '\\xF0\\x9F\\x8D\\x95'", "let x = \"\xC3\xB0\xC2\x9F\xC2\x8D\xC2\x95\";\n");
    expectPrinted("let x = '\\uD801\\uDC02\\uDC03\\uD804'", "let x = \"\U00010402\\uDC03\\uD804\";\n");
}

TEST(JsPrinter, TestTemplate) {
    expectPrinted("let x = `\\0`", "let x = `\\0`;\n");
    expectPrinted("let x = `\\x01`", "let x = `\x01`;\n");
    expectPrinted("let x = `\\0${0}`", "let x = `\\0${0}`;\n");
    expectPrinted("let x = `\\x01${0}`", "let x = `\x01${0}`;\n");
    expectPrinted("let x = `${0}\\0`", "let x = `${0}\\0`;\n");
    expectPrinted("let x = `${0}\\x01`", "let x = `${0}\x01`;\n");
    expectPrinted("let x = `${0}\\0${1}`", "let x = `${0}\\0${1}`;\n");
    expectPrinted("let x = `${0}\\x01${1}`", "let x = `${0}\x01${1}`;\n");

    expectPrinted("let x = String.raw`\\1`", "let x = String.raw`\\1`;\n");
    expectPrinted("let x = String.raw`\\x01`", "let x = String.raw`\\x01`;\n");
    expectPrinted("let x = String.raw`\\1${0}`", "let x = String.raw`\\1${0}`;\n");
    expectPrinted("let x = String.raw`\\x01${0}`", "let x = String.raw`\\x01${0}`;\n");
    expectPrinted("let x = String.raw`${0}\\1`", "let x = String.raw`${0}\\1`;\n");
    expectPrinted("let x = String.raw`${0}\\x01`", "let x = String.raw`${0}\\x01`;\n");
    expectPrinted("let x = String.raw`${0}\\1${1}`", "let x = String.raw`${0}\\1${1}`;\n");
    expectPrinted("let x = String.raw`${0}\\x01${1}`", "let x = String.raw`${0}\\x01${1}`;\n");

    expectPrinted("let x = `${y}`", "let x = `${y}`;\n");
    expectPrinted("let x = `$(y)`", "let x = `$(y)`;\n");
    expectPrinted("let x = `{y}$`", "let x = `{y}$`;\n");
    expectPrinted("let x = `$}y{`", "let x = `$}y{`;\n");
    expectPrinted("let x = `\\${y}`", "let x = `\\${y}`;\n");
    expectPrinted("let x = `$\\{y}`", "let x = `\\${y}`;\n");

    expectPrinted("await tag`x`", "await tag`x`;\n");
    expectPrinted("await (tag`x`)", "await tag`x`;\n");
    expectPrinted("(await tag)`x`", "(await tag)`x`;\n");

    expectPrinted("await tag`${x}`", "await tag`${x}`;\n");
    expectPrinted("await (tag`${x}`)", "await tag`${x}`;\n");
    expectPrinted("(await tag)`${x}`", "(await tag)`${x}`;\n");

    expectPrinted("new tag`x`", "new tag`x`();\n");
    expectPrinted("new (tag`x`)", "new tag`x`();\n");
    expectPrinted("new tag()`x`", "new tag()`x`;\n");
    expectPrinted("(new tag)`x`", "new tag()`x`;\n");
    expectPrintedMinify("new tag`x`", "new tag`x`;");
    expectPrintedMinify("new (tag`x`)", "new tag`x`;");
    expectPrintedMinify("new tag()`x`", "new tag()`x`;");
    expectPrintedMinify("(new tag)`x`", "new tag()`x`;");

    expectPrinted("new tag`${x}`", "new tag`${x}`();\n");
    expectPrinted("new (tag`${x}`)", "new tag`${x}`();\n");
    expectPrinted("new tag()`${x}`", "new tag()`${x}`;\n");
    expectPrinted("(new tag)`${x}`", "new tag()`${x}`;\n");
    expectPrintedMinify("new tag`${x}`", "new tag`${x}`;");
    expectPrintedMinify("new (tag`${x}`)", "new tag`${x}`;");
    expectPrintedMinify("new tag()`${x}`", "new tag()`${x}`;");
    expectPrintedMinify("(new tag)`${x}`", "new tag()`${x}`;");
}

TEST(JsPrinter, TestObject) {
    expectPrinted("let x = {'(':')'}", "let x = { \"(\": \")\" };\n");
    expectPrinted("({})", "({});\n");
    expectPrinted("({}.x)", "({}).x;\n");
    expectPrinted("({} = {})", "({} = {});\n");
    expectPrinted("(x, {} = {})", "x, {} = {};\n");
    expectPrinted("let x = () => ({})", "let x = () => ({});\n");
    expectPrinted("let x = () => ({}.x)", "let x = () => ({}).x;\n");
    expectPrinted("let x = () => ({} = {})", "let x = () => ({} = {});\n");
    expectPrinted("let x = () => (x, {} = {})", "let x = () => (x, {} = {});\n");

    // "{ __proto__: __proto__ }" must not become "{ __proto__ }"
    expectPrinted("function foo(__proto__) { return { __proto__: __proto__ } }", "function foo(__proto__) {\n  return { __proto__: __proto__ };\n}\n");
    expectPrinted("function foo(__proto__) { return { '__proto__': __proto__ } }", "function foo(__proto__) {\n  return { \"__proto__\": __proto__ };\n}\n");
    expectPrinted("function foo(__proto__) { return { ['__proto__']: __proto__ } }", "function foo(__proto__) {\n  return { [\"__proto__\"]: __proto__ };\n}\n");
    expectPrinted("import { __proto__ } from 'foo'; let foo = () => ({ __proto__: __proto__ })", "import { __proto__ } from \"foo\";\nlet foo = () => ({ __proto__: __proto__ });\n");
    expectPrinted("import { __proto__ } from 'foo'; let foo = () => ({ '__proto__': __proto__ })", "import { __proto__ } from \"foo\";\nlet foo = () => ({ \"__proto__\": __proto__ });\n");
    expectPrinted("import { __proto__ } from 'foo'; let foo = () => ({ ['__proto__']: __proto__ })", "import { __proto__ } from \"foo\";\nlet foo = () => ({ [\"__proto__\"]: __proto__ });\n");

    // Don't use ES6+ features (such as a shorthand or computed property name) in ES5
    expectPrintedTarget(5, "function foo(__proto__) { return { __proto__ } }", "function foo(__proto__) {\n  return { __proto__: __proto__ };\n}\n");
}

TEST(JsPrinter, TestSwitch) {
    // Ideally comments on case clauses would be preserved
    expectPrinted("switch (x) { /* 1 */ case 1: /* 2 */ case 2: /* default */ default: break }",
        "switch (x) {\n  /* 1 */\n  case 1:\n  /* 2 */\n  case 2:\n  /* default */\n  default:\n    break;\n}\n");
}

TEST(JsPrinter, TestFor) {
    // Make sure "in" expressions are forbidden in the right places
    expectPrinted("for ((a in b);;);", "for ((a in b); ; ) ;\n");
    expectPrinted("for (a ? b : (c in d);;);", "for (a ? b : (c in d); ; ) ;\n");
    expectPrinted("for ((a ? b : c in d).foo;;);", "for ((a ? b : c in d).foo; ; ) ;\n");
    expectPrinted("for (var x = (a in b);;);", "for (var x = (a in b); ; ) ;\n");
    expectPrinted("for (x = (a in b);;);", "for (x = (a in b); ; ) ;\n");
    expectPrinted("for (x == (a in b);;);", "for (x == (a in b); ; ) ;\n");
    expectPrinted("for (1 * (x == a in b);;);", "for (1 * (x == a in b); ; ) ;\n");
    expectPrinted("for (a ? b : x = (c in d);;);", "for (a ? b : x = (c in d); ; ) ;\n");
    expectPrinted("for (var x = y = (a in b);;);", "for (var x = y = (a in b); ; ) ;\n");
    expectPrinted("for ([a in b];;);", "for ([a in b]; ; ) ;\n");
    expectPrinted("for (x(a in b);;);", "for (x(a in b); ; ) ;\n");
    expectPrinted("for (x[a in b];;);", "for (x[a in b]; ; ) ;\n");
    expectPrinted("for (x?.[a in b];;);", "for (x?.[a in b]; ; ) ;\n");
    expectPrinted("for ((x => a in b);;);", "for (((x) => a in b); ; ) ;\n");

    // Make sure for-of loops with commas are wrapped in parentheses
    expectPrinted("for (let a in b, c);", "for (let a in b, c) ;\n");
    expectPrinted("for (let a of (b, c));", "for (let a of (b, c)) ;\n");
}

TEST(JsPrinter, TestFunction) {
    expectPrinted(
        "function foo(a = (b, c), ...d) {}",
        "function foo(a = (b, c), ...d) {\n}\n");
    expectPrinted(
        "function foo({[1 + 2]: a = 3} = {[1 + 2]: 3}) {}",
        "function foo({ [1 + 2]: a = 3 } = { [1 + 2]: 3 }) {\n}\n");
    expectPrinted(
        "function foo([a = (1, 2), ...[b, ...c]] = [1, [2, 3]]) {}",
        "function foo([a = (1, 2), ...[b, ...c]] = [1, [2, 3]]) {\n}\n");
    expectPrinted(
        "function foo([] = []) {}",
        "function foo([] = []) {\n}\n");
    expectPrinted(
        "function foo([,] = [,]) {}",
        "function foo([,] = [,]) {\n}\n");
    expectPrinted(
        "function foo([,,] = [,,]) {}",
        "function foo([, ,] = [, ,]) {\n}\n");
}

TEST(JsPrinter, TestCommentsAndParentheses) {
    expectPrinted("(/* foo */ { x() { foo() } }.x());", "/* foo */\n({ x() {\n  foo();\n} }).x();\n");
    expectPrinted("(/* foo */ function f() { foo(f) }());", "/* foo */\n(function f() {\n  foo(f);\n})();\n");
    expectPrinted("(/* foo */ class x { static y() { foo(x) } }.y());", "/* foo */\n(class x {\n  static y() {\n    foo(x);\n  }\n}).y();\n");
    expectPrinted("(/* @__PURE__ */ (() => foo())());", "/* @__PURE__ */ (() => foo())();\n");
    expectPrinted("export default (/* foo */ function f() {});", "export default (\n  /* foo */\n  (function f() {\n  })\n);\n");
    expectPrinted("export default (/* foo */ class x {});", "export default (\n  /* foo */\n  class x {\n  }\n);\n");
    expectPrinted("x = () => (/* foo */ {});", "x = () => (\n  /* foo */\n  {}\n);\n");
    expectPrinted("for ((/* foo */ let).x of y) ;", "for (\n  /* foo */\n  (let).x of y\n) ;\n");
    expectPrinted("for (/* foo */ (let).x of y) ;", "for (\n  /* foo */\n  (let).x of y\n) ;\n");
    expectPrinted("function *x() { yield (/* foo */ y) }", "function* x() {\n  yield (\n    /* foo */\n    y\n  );\n}\n");
}

TEST(JsPrinter, TestPureComment) {
    expectPrinted(
        "(function() { foo() })",
        "(function() {\n  foo();\n});\n");
    expectPrinted(
        "(function() { foo() })()",
        "(function() {\n  foo();\n})();\n");
    expectPrinted(
        "/*@__PURE__*/(function() { foo() })()",
        "/* @__PURE__ */ (function() {\n  foo();\n})();\n");

    expectPrinted(
        "new (function() {})",
        "new (function() {\n})();\n");
    expectPrinted(
        "new (function() {})()",
        "new (function() {\n})();\n");
    expectPrinted(
        "/*@__PURE__*/new (function() {})()",
        "/* @__PURE__ */ new (function() {\n})();\n");

    expectPrinted(
        "export default (function() { foo() })",
        "export default (function() {\n  foo();\n});\n");
    expectPrinted(
        "export default (function() { foo() })()",
        "export default (function() {\n  foo();\n})();\n");
    expectPrinted(
        "export default /*@__PURE__*/(function() { foo() })()",
        "export default /* @__PURE__ */ (function() {\n  foo();\n})();\n");
}

TEST(JsPrinter, TestGenerator) {
    expectPrinted(
        "function* foo() {}",
        "function* foo() {\n}\n");
    expectPrinted(
        "(function* () {})",
        "(function* () {\n});\n");
    expectPrinted(
        "(function* foo() {})",
        "(function* foo() {\n});\n");

    expectPrinted(
        "class Foo { *foo() {} }",
        "class Foo {\n  *foo() {\n  }\n}\n");
    expectPrinted(
        "class Foo { static *foo() {} }",
        "class Foo {\n  static *foo() {\n  }\n}\n");
    expectPrinted(
        "class Foo { *[foo]() {} }",
        "class Foo {\n  *[foo]() {\n  }\n}\n");
    expectPrinted(
        "class Foo { static *[foo]() {} }",
        "class Foo {\n  static *[foo]() {\n  }\n}\n");

    expectPrinted(
        "(class { *foo() {} })",
        "(class {\n  *foo() {\n  }\n});\n");
    expectPrinted(
        "(class { static *foo() {} })",
        "(class {\n  static *foo() {\n  }\n});\n");
    expectPrinted(
        "(class { *[foo]() {} })",
        "(class {\n  *[foo]() {\n  }\n});\n");
    expectPrinted(
        "(class { static *[foo]() {} })",
        "(class {\n  static *[foo]() {\n  }\n});\n");
}

TEST(JsPrinter, TestArrow) {
    expectPrinted("() => {}", "() => {\n};\n");
    expectPrinted("x => (x, 0)", "(x) => (x, 0);\n");
    expectPrinted("x => {y}", "(x) => {\n  y;\n};\n");
    expectPrinted(
        "(a = (b, c), ...d) => {}",
        "(a = (b, c), ...d) => {\n};\n");
    expectPrinted(
        "({[1 + 2]: a = 3} = {[1 + 2]: 3}) => {}",
        "({ [1 + 2]: a = 3 } = { [1 + 2]: 3 }) => {\n};\n");
    expectPrinted(
        "([a = (1, 2), ...[b, ...c]] = [1, [2, 3]]) => {}",
        "([a = (1, 2), ...[b, ...c]] = [1, [2, 3]]) => {\n};\n");
    expectPrinted(
        "([] = []) => {}",
        "([] = []) => {\n};\n");
    expectPrinted(
        "([,] = [,]) => {}",
        "([,] = [,]) => {\n};\n");
    expectPrinted(
        "([,,] = [,,]) => {}",
        "([, ,] = [, ,]) => {\n};\n");
    expectPrinted(
        "a = () => {}",
        "a = () => {\n};\n");
    expectPrinted(
        "a || (() => {})",
        "a || (() => {\n});\n");
    expectPrinted(
        "({a = b, c = d}) => {}",
        "({ a = b, c = d }) => {\n};\n");
    expectPrinted(
        "([{a = b, c = d} = {}] = []) => {}",
        "([{ a = b, c = d } = {}] = []) => {\n};\n");
    expectPrinted(
        "({a: [b = c] = []} = {}) => {}",
        "({ a: [b = c] = [] } = {}) => {\n};\n");

    // These are not arrow functions but initially look like one
    expectPrinted("(a = b, c)", "a = b, c;\n");
    expectPrinted("([...a = b])", "[...a = b];\n");
    expectPrinted("([...a, ...b])", "[...a, ...b];\n");
    expectPrinted("({a: b, c() {}})", "({ a: b, c() {\n} });\n");
    expectPrinted("({a: b, get c() {}})", "({ a: b, get c() {\n} });\n");
    expectPrinted("({a: b, set c(x) {}})", "({ a: b, set c(x) {\n} });\n");
}

TEST(JsPrinter, TestClass) {
    expectPrinted("class Foo extends (a, b) {}", "class Foo extends (a, b) {\n}\n");
    expectPrinted("class Foo { get foo() {} }", "class Foo {\n  get foo() {\n  }\n}\n");
    expectPrinted("class Foo { set foo(x) {} }", "class Foo {\n  set foo(x) {\n  }\n}\n");
    expectPrinted("class Foo { static foo() {} }", "class Foo {\n  static foo() {\n  }\n}\n");
    expectPrinted("class Foo { static get foo() {} }", "class Foo {\n  static get foo() {\n  }\n}\n");
    expectPrinted("class Foo { static set foo(x) {} }", "class Foo {\n  static set foo(x) {\n  }\n}\n");
}

TEST(JsPrinter, TestAutoAccessors) {
    expectPrinted("class Foo { accessor x; static accessor y }", "class Foo {\n  accessor x;\n  static accessor y;\n}\n");
    expectPrinted("class Foo { accessor [x]; static accessor [y] }", "class Foo {\n  accessor [x];\n  static accessor [y];\n}\n");
    expectPrintedMinify("class Foo { accessor x; static accessor y }", "class Foo{accessor x;static accessor y}");
    expectPrintedMinify("class Foo { accessor [x]; static accessor [y] }", "class Foo{accessor[x];static accessor[y]}");
}

TEST(JsPrinter, TestPrivateIdentifiers) {
    expectPrinted("class Foo { #foo; foo() { return #foo in this } }", "class Foo {\n  #foo;\n  foo() {\n    return #foo in this;\n  }\n}\n");
    expectPrintedMinify("class Foo { #foo; foo() { return #foo in this } }", "class Foo{#foo;foo(){return#foo in this}}");
}

TEST(JsPrinter, TestDecorators) {
    const std::string example =
        "class Foo {\n@w\nw; @x x; @a1\n@b1@b2\n@c1@c2@c3\ny = @y1 @y2 class {}; @a1\n@b1@b2\n@c1@c2@c3 z =\n@z1\n@z2\nclass {}}";
    expectPrinted(example,
        "class Foo {\n  @w\n  w;\n  @x x;\n  @a1\n  @b1 @b2\n  @c1 @c2 @c3\n  "
        "y = @y1 @y2 class {\n  };\n  @a1\n  @b1 @b2\n  @c1 @c2 @c3 z = @z1 @z2 class {\n  };\n}\n");
    expectPrintedMinify(example,
        "class Foo{@w w;@x x;@a1@b1@b2@c1@c2@c3 y=@y1@y2 class{};@a1@b1@b2@c1@c2@c3 z=@z1@z2 class{}}");
}

TEST(JsPrinter, TestImport) {
    expectPrinted("import('path');", "import(\"path\");\n"); // The semicolon must not be a separate statement

    // Test preservation of Webpack-specific comments
    expectPrinted("import(// webpackFoo: 1\n // webpackBar: 2\n 'path');", "import(\n  // webpackFoo: 1\n  // webpackBar: 2\n  \"path\"\n);\n");
    expectPrinted("import(// webpackFoo: 1\n // webpackBar: 2\n 'path', {type: 'module'});", "import(\n  // webpackFoo: 1\n  // webpackBar: 2\n  \"path\",\n  { type: \"module\" }\n);\n");
    expectPrinted("import(/* webpackFoo: 1 */ /* webpackBar: 2 */ 'path');", "import(\n  /* webpackFoo: 1 */\n  /* webpackBar: 2 */\n  \"path\"\n);\n");
    expectPrinted("import(/* webpackFoo: 1 */ /* webpackBar: 2 */ 'path', {type: 'module'});", "import(\n  /* webpackFoo: 1 */\n  /* webpackBar: 2 */\n  \"path\",\n  { type: \"module\" }\n);\n");
    expectPrinted("import(\n    /* multi\n     * line\n     * webpackBar: */ 'path');", "import(\n  /* multi\n   * line\n   * webpackBar: */\n  \"path\"\n);\n");
    expectPrinted("import(/* webpackFoo: 1 */ 'path' /* webpackBar:2 */);", "import(\n  /* webpackFoo: 1 */\n  \"path\"\n  /* webpackBar:2 */\n);\n");
    expectPrinted("import(/* webpackFoo: 1 */ 'path' /* webpackBar:2 */ ,);", "import(\n  /* webpackFoo: 1 */\n  \"path\"\n);\n"); // Not currently handled
    expectPrinted("import(/* webpackFoo: 1 */ 'path', /* webpackBar:2 */ );", "import(\n  /* webpackFoo: 1 */\n  \"path\"\n  /* webpackBar:2 */\n);\n");
    expectPrinted("import(/* webpackFoo: 1 */ 'path', { type: 'module' } /* webpackBar:2 */ );", "import(\n  /* webpackFoo: 1 */\n  \"path\",\n  { type: \"module\" }\n  /* webpackBar:2 */\n);\n");
    expectPrinted("import(new URL('path', /* webpackFoo: these can go anywhere */ import.meta.url))",
        "import(new URL(\n  \"path\",\n  /* webpackFoo: these can go anywhere */\n  import.meta.url\n));\n");

    // See: https://github.com/tc39/proposal-defer-import-eval
    expectPrintedMinify("import defer * as foo from 'bar'", "import defer*as foo from\"bar\";");

    // See: https://github.com/tc39/proposal-source-phase-imports
    expectPrintedMinify("import source foo from 'bar'", "import source foo from\"bar\";");
}

TEST(JsPrinter, TestExportDefault) {
    expectPrinted("export default function() {}", "export default function() {\n}\n");
    expectPrinted("export default function foo() {}", "export default function foo() {\n}\n");
    expectPrinted("export default async function() {}", "export default async function() {\n}\n");
    expectPrinted("export default async function foo() {}", "export default async function foo() {\n}\n");
    expectPrinted("export default class {}", "export default class {\n}\n");
    expectPrinted("export default class foo {}", "export default class foo {\n}\n");

    expectPrinted("export default (function() {})", "export default (function() {\n});\n");
    expectPrinted("export default (function foo() {})", "export default (function foo() {\n});\n");
    expectPrinted("export default (async function() {})", "export default (async function() {\n});\n");
    expectPrinted("export default (async function foo() {})", "export default (async function foo() {\n});\n");
    expectPrinted("export default (class {})", "export default (class {\n});\n");
    expectPrinted("export default (class foo {})", "export default (class foo {\n});\n");

    expectPrinted("export default (function() {}.toString())", "export default (function() {\n}).toString();\n");
    expectPrinted("export default (function foo() {}.toString())", "export default (function foo() {\n}).toString();\n");
    expectPrinted("export default (async function() {}.toString())", "export default (async function() {\n}).toString();\n");
    expectPrinted("export default (async function foo() {}.toString())", "export default (async function foo() {\n}).toString();\n");
    expectPrinted("export default (class {}.toString())", "export default (class {\n}).toString();\n");
    expectPrinted("export default (class foo {}.toString())", "export default (class foo {\n}).toString();\n");

    expectPrintedMinify("export default function() {}", "export default function(){}");
    expectPrintedMinify("export default function foo() {}", "export default function foo(){}");
    expectPrintedMinify("export default async function() {}", "export default async function(){}");
    expectPrintedMinify("export default async function foo() {}", "export default async function foo(){}");
    expectPrintedMinify("export default class {}", "export default class{}");
    expectPrintedMinify("export default class foo {}", "export default class foo{}");
}

TEST(JsPrinter, TestWhitespace) {
    expectPrinted("- -x", "- -x;\n");
    expectPrinted("+ -x", "+-x;\n");
    expectPrinted("- +x", "-+x;\n");
    expectPrinted("+ +x", "+ +x;\n");
    expectPrinted("- --x", "- --x;\n");
    expectPrinted("+ --x", "+--x;\n");
    expectPrinted("- ++x", "-++x;\n");
    expectPrinted("+ ++x", "+ ++x;\n");

    expectPrintedMinify("- -x", "- -x;");
    expectPrintedMinify("+ -x", "+-x;");
    expectPrintedMinify("- +x", "-+x;");
    expectPrintedMinify("+ +x", "+ +x;");
    expectPrintedMinify("- --x", "- --x;");
    expectPrintedMinify("+ --x", "+--x;");
    expectPrintedMinify("- ++x", "-++x;");
    expectPrintedMinify("+ ++x", "+ ++x;");

    expectPrintedMinify("x - --y", "x- --y;");
    expectPrintedMinify("x + --y", "x+--y;");
    expectPrintedMinify("x - ++y", "x-++y;");
    expectPrintedMinify("x + ++y", "x+ ++y;");

    expectPrintedMinify("x-- > y", "x-- >y;");
    expectPrintedMinify("x < !--y", "x<! --y;");
    expectPrintedMinify("x > !--y", "x>!--y;");
    expectPrintedMinify("!--y", "!--y;");

    expectPrintedMinify("1 + -0", "1+-0;");
    expectPrintedMinify("1 - -0", "1- -0;");
    expectPrintedMinify("1 + -Infinity", "1+-Infinity;");
    expectPrintedMinify("1 - -Infinity", "1- -Infinity;");

    expectPrintedMinify("/x/ / /y/", "/x// /y/;");
    expectPrintedMinify("/x/ + Foo", "/x/+Foo;");
    expectPrintedMinify("/x/ instanceof Foo", "/x/ instanceof Foo;");
    expectPrintedMinify("[x] instanceof Foo", "[x]instanceof Foo;");

    expectPrintedMinify("throw x", "throw x;");
    expectPrintedMinify("throw typeof x", "throw typeof x;");
    expectPrintedMinify("throw delete x", "throw delete x;");
    expectPrintedMinify("throw function(){}", "throw function(){};");

    expectPrintedMinify("x in function(){}", "x in function(){};");
    expectPrintedMinify("x instanceof function(){}", "x instanceof function(){};");
    expectPrintedMinify("\u03C0 in function(){}", "\u03C0 in function(){};");
    expectPrintedMinify("\u03C0 instanceof function(){}", "\u03C0 instanceof function(){};");

    expectPrintedMinify("()=>({})", "()=>({});");
    expectPrintedMinify("()=>({}[1])", "()=>({})[1];");
    expectPrintedMinify("()=>({}+0)", "()=>\"[object Object]0\";");
    expectPrintedMinify("()=>function(){}", "()=>function(){};");

    expectPrintedMinify("(function(){})", "(function(){});");
    expectPrintedMinify("(class{})", "(class{});");
    expectPrintedMinify("({})", "({});");
}

TEST(JsPrinter, TestMangle) {
    expectPrintedMangle("let x = '\\n'", "let x = `\n`;\n");
    expectPrintedMangle("let x = `\n`", "let x = `\n`;\n");
    expectPrintedMangle("let x = '\\n${}'", "let x = \"\\n${}\";\n");
    expectPrintedMangle("let x = `\n\\${}`", "let x = \"\\n${}\";\n");
    expectPrintedMangle("let x = `\n\\${}${y}\\${}`", "let x = `\n\\${}${y}\\${}`;\n");
}

TEST(JsPrinter, TestMinify) {
    expectPrintedMinify("0.1", ".1;");
    expectPrintedMinify("1.2", "1.2;");

    expectPrintedMinify("() => {}", "()=>{};");
    expectPrintedMinify("(a) => {}", "a=>{};");
    expectPrintedMinify("(...a) => {}", "(...a)=>{};");
    expectPrintedMinify("(a = 0) => {}", "(a=0)=>{};");
    expectPrintedMinify("(a, b) => {}", "(a,b)=>{};");

    expectPrinted("true ** 2", "true ** 2;\n");
    expectPrinted("false ** 2", "false ** 2;\n");
    expectPrintedMinify("true ** 2", "true**2;");
    expectPrintedMinify("false ** 2", "false**2;");
    expectPrintedMangle("true ** 2", "(!0) ** 2;\n");
    expectPrintedMangle("false ** 2", "(!1) ** 2;\n");

    expectPrintedMinify("import a from 'path'", "import a from\"path\";");
    expectPrintedMinify("import * as ns from 'path'", "import*as ns from\"path\";");
    expectPrintedMinify("import {a, b as c} from 'path'", "import{a,b as c}from\"path\";");
    expectPrintedMinify("import {a, ' ' as c} from 'path'", "import{a,\" \"as c}from\"path\";");

    expectPrintedMinify("export * as ns from 'path'", "export*as ns from\"path\";");
    expectPrintedMinify("export * as ' ' from 'path'", "export*as\" \"from\"path\";");
    expectPrintedMinify("export {a, b as c} from 'path'", "export{a,b as c}from\"path\";");
    expectPrintedMinify("export {' ', '-' as ';'} from 'path'", "export{\" \",\"-\"as\";\"}from\"path\";");
    expectPrintedMinify("let a, b; export {a, b as c}", "let a,b;export{a,b as c};");
    expectPrintedMinify("let a, b; export {a, b as ' '}", "let a,b;export{a,b as\" \"};");

    // Print some strings using template literals when minifying
    expectPrinted("x = '\\n'", "x = \"\\n\";\n");
    expectPrintedMangle("x = '\\n'", "x = `\n`;\n");
    expectPrintedMangle("x = {'\\n': 0}", "x = { \"\\n\": 0 };\n");
    expectPrintedMangle("x = class{'\\n' = 0}", "x = class {\n  \"\\n\" = 0;\n};\n");
    expectPrintedMangle("class Foo{'\\n' = 0}", "class Foo {\n  \"\\n\" = 0;\n}\n");

    // Special identifiers must not be minified
    expectPrintedMinify("exports", "exports;");
    expectPrintedMinify("require", "require;");
    expectPrintedMinify("module", "module;");

    // Comment statements must not affect their surroundings when minified
    expectPrintedMinify("//!single\nthrow 1 + 2", "//!single\nthrow 1+2;");
    expectPrintedMinify("/*!multi-\nline*/\nthrow 1 + 2", "/*!multi-\nline*/throw 1+2;");
}

TEST(JsPrinter, TestES5) {
    expectPrintedTargetMangle(5, "foo('a\\n\\n\\nb')", "foo(\"a\\n\\n\\nb\");\n");
    expectPrintedTargetMangle(2015, "foo('a\\n\\n\\nb')", "foo(`a\n\n\nb`);\n");

    expectPrintedTarget(5, "foo({a, b})", "foo({ a: a, b: b });\n");
    expectPrintedTarget(2015, "foo({a, b})", "foo({ a, b });\n");

    expectPrintedTarget(5, "x => x", "(function(x) {\n  return x;\n});\n");
    expectPrintedTarget(2015, "x => x", "(x) => x;\n");

    expectPrintedTarget(5, "() => {}", "(function() {\n});\n");
    expectPrintedTarget(2015, "() => {}", "() => {\n};\n");

    expectPrintedTargetMinify(5, "x => x", "(function(x){return x});");
    expectPrintedTargetMinify(2015, "x => x", "x=>x;");

    expectPrintedTargetMinify(5, "() => {}", "(function(){});");
    expectPrintedTargetMinify(2015, "() => {}", "()=>{};");
}

TEST(JsPrinter, TestASCIIOnly) {
    expectPrinted("let \u03C0 = '\u03C0'", "let \u03C0 = \"\u03C0\";\n");
    expectPrinted("let \u03C0_ = '\u03C0'", "let \u03C0_ = \"\u03C0\";\n");
    expectPrinted("let _\u03C0 = '\u03C0'", "let _\u03C0 = \"\u03C0\";\n");
    expectPrintedASCII("let \u03C0 = '\u03C0'", "let \\u03C0 = \"\\u03C0\";\n");
    expectPrintedASCII("let \u03C0_ = '\u03C0'", "let \\u03C0_ = \"\\u03C0\";\n");
    expectPrintedASCII("let _\u03C0 = '\u03C0'", "let _\\u03C0 = \"\\u03C0\";\n");

    expectPrinted("let \u8C93 = '\U0001F408'", "let \u8C93 = \"\U0001F408\";\n");
    expectPrinted("let \u8C93abc = '\U0001F408'", "let \u8C93abc = \"\U0001F408\";\n");
    expectPrinted("let abc\u8C93 = '\U0001F408'", "let abc\u8C93 = \"\U0001F408\";\n");
    expectPrintedASCII("let \u8C93 = '\U0001F408'", "let \\u8C93 = \"\\u{1F408}\";\n");
    expectPrintedASCII("let \u8C93abc = '\U0001F408'", "let \\u8C93abc = \"\\u{1F408}\";\n");
    expectPrintedASCII("let abc\u8C93 = '\U0001F408'", "let abc\\u8C93 = \"\\u{1F408}\";\n");

    // Test a character outside the BMP
    expectPrinted("var \U00010000", "var \U00010000;\n");
    expectPrinted("var \\u{10000}", "var \U00010000;\n");
    expectPrintedASCII("var \U00010000", "var \\u{10000};\n");
    expectPrintedASCII("var \\u{10000}", "var \\u{10000};\n");
    expectPrintedTargetASCII(2015, "'\U00010000'", "\"\\u{10000}\";\n");
    expectPrintedTargetASCII(5, "'\U00010000'", "\"\\uD800\\uDC00\";\n");
    expectPrintedTargetASCII(2015, "x.\U00010000", "x[\"\\u{10000}\"];\n");
    expectPrintedTargetASCII(5, "x.\U00010000", "x[\"\\uD800\\uDC00\"];\n");

    // Escapes should use consistent case
    expectPrintedASCII("var \\u{100a} = {\\u100A: '\\u100A'}", "var \\u100A = { \\u100A: \"\\u100A\" };\n");
    expectPrintedASCII("var \\u{1000a} = {\\u{1000A}: '\\u{1000A}'}", "var \\u{1000A} = { \"\\u{1000A}\": \"\\u{1000A}\" };\n");

    // These characters should always be escaped
    expectPrinted("let x = '\u2028'", "let x = \"\\u2028\";\n");
    expectPrinted("let x = '\u2029'", "let x = \"\\u2029\";\n");
    expectPrinted("let x = '\uFEFF'", "let x = \"\\uFEFF\";\n");

    // There should still be a space before "extends"
    expectPrintedASCII("class \U00010000 extends \u03C0 {}", "class \\u{10000} extends \\u03C0 {\n}\n");
    expectPrintedASCII("(class \U00010000 extends \u03C0 {})", "(class \\u{10000} extends \\u03C0 {\n});\n");
    expectPrintedMinifyASCII("class \U00010000 extends \u03C0 {}", "class \\u{10000} extends \\u03C0{}");
    expectPrintedMinifyASCII("(class \U00010000 extends \u03C0 {})", "(class \\u{10000} extends \\u03C0{});");
}

TEST(JsPrinter, TestJSX) {
    expectPrintedJSX("<a/>", "<a />;\n");
    expectPrintedJSX("<A/>", "<A />;\n");
    expectPrintedJSX("<a.b/>", "<a.b />;\n");
    expectPrintedJSX("<A.B/>", "<A.B />;\n");
    expectPrintedJSX("<a-b/>", "<a-b />;\n");
    expectPrintedJSX("<a:b/>", "<a:b />;\n");
    expectPrintedJSX("<a></a>", "<a />;\n");
    expectPrintedJSX("<a b></a>", "<a b />;\n");

    expectPrintedJSX("<a b={true}></a>", "<a b={true} />;\n");
    expectPrintedJSX("<a b='x'></a>", "<a b='x' />;\n");
    expectPrintedJSX("<a b=\"x\"></a>", "<a b=\"x\" />;\n");
    expectPrintedJSX("<a b={'x'}></a>", "<a b={\"x\"} />;\n");
    expectPrintedJSX("<a b={`'`}></a>", "<a b={`'`} />;\n");
    expectPrintedJSX("<a b={`\"`}></a>", "<a b={`\"`} />;\n");
    expectPrintedJSX("<a b={`'\"`}></a>", "<a b={`'\"`} />;\n");
    expectPrintedJSX("<a b=\"&quot;\"></a>", "<a b=\"&quot;\" />;\n");
    expectPrintedJSX("<a b=\"&amp;\"></a>", "<a b=\"&amp;\" />;\n");

    expectPrintedJSX("<a>x</a>", "<a>x</a>;\n");
    expectPrintedJSX("<a>x\ny</a>", "<a>x\ny</a>;\n");
    expectPrintedJSX("<a>{'x'}{'y'}</a>", "<a>{\"x\"}{\"y\"}</a>;\n");
    expectPrintedJSX("<a> x</a>", "<a> x</a>;\n");
    expectPrintedJSX("<a>x </a>", "<a>x </a>;\n");
    expectPrintedJSX("<a>&#10;</a>", "<a>&#10;</a>;\n");
    expectPrintedJSX("<a>&amp;</a>", "<a>&amp;</a>;\n");
    expectPrintedJSX("<a>&lt;</a>", "<a>&lt;</a>;\n");
    expectPrintedJSX("<a>&gt;</a>", "<a>&gt;</a>;\n");
    expectPrintedJSX("<a>&#123;</a>", "<a>&#123;</a>;\n");
    expectPrintedJSX("<a>&#125;</a>", "<a>&#125;</a>;\n");

    expectPrintedJSX("<a><x/></a>", "<a><x /></a>;\n");
    expectPrintedJSX("<a><x/><y/></a>", "<a><x /><y /></a>;\n");
    expectPrintedJSX("<a>b<c/>d</a>", "<a>b<c />d</a>;\n");

    expectPrintedJSX("<></>", "<></>;\n");
    expectPrintedJSX("<>x<y/>z</>", "<>x<y />z</>;\n");

    // JSX elements as JSX attribute values
    expectPrintedJSX("<a b=<c/>/>", "<a b=<c /> />;\n");
    expectPrintedJSX("<a b=<>c</>/>", "<a b=<>c</> />;\n");
    expectPrintedJSX("<a b=<>{c}</>/>", "<a b=<>{c}</> />;\n");
    expectPrintedJSX("<a b={<c/>}/>", "<a b={<c />} />;\n");
    expectPrintedJSX("<a b={<>c</>}/>", "<a b={<>c</>} />;\n");
    expectPrintedJSX("<a b={<>{c}</>}/>", "<a b={<>{c}</>} />;\n");

    // These can't be escaped because JSX lacks a syntax for escapes
    expectPrintedJSXASCII("<\u03C0/>", "<\u03C0 />;\n");
    expectPrintedJSXASCII("<\u03C0.\U00010000/>", "<\u03C0.\U00010000 />;\n");
    expectPrintedJSXASCII("<\U00010000.\u03C0/>", "<\U00010000.\u03C0 />;\n");
    expectPrintedJSXASCII("<\u03C0>x</\u03C0>", "<\u03C0>x</\u03C0>;\n");
    expectPrintedJSXASCII("<\U00010000>x</\U00010000>", "<\U00010000>x</\U00010000>;\n");
    expectPrintedJSXASCII("<a \u03C0/>", "<a \u03C0 />;\n");
    expectPrintedJSXASCII("<a \U00010000/>", "<a \U00010000 />;\n");

    // JSX text is deliberately not printed as ASCII when JSX preservation is
    // enabled. This is because:
    //
    // a) The JSX specification doesn't say how JSX text is supposed to be interpreted
    // b) Enabling JSX preservation means that JSX will be transformed again anyway
    // c) People do very weird/custom things with JSX that "preserve" shouldn't break

    expectPrintedJSXASCII("<a b='\u03C0'/>", "<a b='\u03C0' />;\n");
    expectPrintedJSXASCII("<a b='\U00010000'/>", "<a b='\U00010000' />;\n");
    expectPrintedJSXASCII("<a>\u03C0</a>", "<a>\u03C0</a>;\n");
    expectPrintedJSXASCII("<a>\U00010000</a>", "<a>\U00010000</a>;\n");

    expectPrintedJSXMinify("<a b c={x,y} d='true'/>", "<a b c={(x,y)}d='true'/>;");
    expectPrintedJSXMinify("<a><b/><c/></a>", "<a><b/><c/></a>;");
    expectPrintedJSXMinify("<a> x <b/> y </a>", "<a> x <b/> y </a>;");
    expectPrintedJSXMinify("<a>{' x '}{'<b/>'}{' y '}</a>", "<a>{\" x \"}{\"<b/>\"}{\" y \"}</a>;");
}

TEST(JsPrinter, TestJSXSingleLine) {
    expectPrintedJSX("<x/>", "<x />;\n");
    expectPrintedJSX("<x y/>", "<x y />;\n");
    expectPrintedJSX("<x\n/>", "<x />;\n");
    expectPrintedJSX("<x\ny/>", "<x\n  y\n/>;\n");
    expectPrintedJSX("<x y\n/>", "<x\n  y\n/>;\n");
    expectPrintedJSX("<x\n{...y}/>", "<x\n  {...y}\n/>;\n");

    expectPrintedJSXMinify("<x/>", "<x/>;");
    expectPrintedJSXMinify("<x y/>", "<x y/>;");
    expectPrintedJSXMinify("<x\n/>", "<x/>;");
    expectPrintedJSXMinify("<x\ny/>", "<x y/>;");
    expectPrintedJSXMinify("<x y\n/>", "<x y/>;");
    expectPrintedJSXMinify("<x\n{...y}/>", "<x{...y}/>;");
}

TEST(JsPrinter, TestAvoidSlashScript) {
    // Positive cases
    expectPrinted("x = '</script'", "x = \"<\\/script\";\n");
    expectPrinted("x = `</script`", "x = `<\\/script`;\n");
    expectPrinted("x = `</SCRIPT`", "x = `<\\/SCRIPT`;\n");
    expectPrinted("x = `</ScRiPt`", "x = `<\\/ScRiPt`;\n");
    expectPrinted("x = `</script${y}`", "x = `<\\/script${y}`;\n");
    expectPrinted("x = `${y}</script`", "x = `${y}<\\/script`;\n");
    expectPrintedMinify("x = 1 < /script/.exec(y).length", "x=1< /script/.exec(y).length;");
    expectPrintedMinify("x = 1 < /SCRIPT/.exec(y).length", "x=1< /SCRIPT/.exec(y).length;");
    expectPrintedMinify("x = 1 < /ScRiPt/.exec(y).length", "x=1< /ScRiPt/.exec(y).length;");
    expectPrintedMinify("x = 1 << /script/.exec(y).length", "x=1<< /script/.exec(y).length;");
    expectPrinted("//! </script\n//! >/script\n//! /script", "//! <\\/script\n//! >/script\n//! /script\n");
    expectPrinted("//! </SCRIPT\n//! >/SCRIPT\n//! /SCRIPT", "//! <\\/SCRIPT\n//! >/SCRIPT\n//! /SCRIPT\n");
    expectPrinted("//! </ScRiPt\n//! >/ScRiPt\n//! /ScRiPt", "//! <\\/ScRiPt\n//! >/ScRiPt\n//! /ScRiPt\n");
    expectPrinted("/*! </script \n </script */", "/*! <\\/script \n <\\/script */\n");
    expectPrinted("/*! </SCRIPT \n </SCRIPT */", "/*! <\\/SCRIPT \n <\\/SCRIPT */\n");
    expectPrinted("/*! </ScRiPt \n </ScRiPt */", "/*! <\\/ScRiPt \n <\\/ScRiPt */\n");
    expectPrinted("String.raw`</script`",
        "import { __template } from \"<runtime>\";\nvar _a;\nString.raw(_a || (_a = __template([\"<\\/script\"])));\n");
    expectPrinted("String.raw`</script${a}`",
        "import { __template } from \"<runtime>\";\nvar _a;\nString.raw(_a || (_a = __template([\"<\\/script\", \"\"])), a);\n");
    expectPrinted("String.raw`${a}</script`",
        "import { __template } from \"<runtime>\";\nvar _a;\nString.raw(_a || (_a = __template([\"\", \"<\\/script\"])), a);\n");
    expectPrinted("String.raw`</SCRIPT`",
        "import { __template } from \"<runtime>\";\nvar _a;\nString.raw(_a || (_a = __template([\"<\\/SCRIPT\"])));\n");
    expectPrinted("String.raw`</ScRiPt`",
        "import { __template } from \"<runtime>\";\nvar _a;\nString.raw(_a || (_a = __template([\"<\\/ScRiPt\"])));\n");

    // Negative cases
    expectPrinted("x = '</'", "x = \"</\";\n");
    expectPrinted("x = '</ script'", "x = \"</ script\";\n");
    expectPrinted("x = '< /script'", "x = \"< /script\";\n");
    expectPrinted("x = '/script>'", "x = \"/script>\";\n");
    expectPrinted("x = '<script>'", "x = \"<script>\";\n");
    expectPrintedMinify("x = 1 < / script/.exec(y).length", "x=1</ script/.exec(y).length;");
    expectPrintedMinify("x = 1 << / script/.exec(y).length", "x=1<</ script/.exec(y).length;");
}

TEST(JsPrinter, TestInfinity) {
    expectPrinted("x = Infinity", "x = Infinity;\n");
    expectPrinted("x = -Infinity", "x = -Infinity;\n");
    expectPrinted("x = (Infinity).toString", "x = Infinity.toString;\n");
    expectPrinted("x = (-Infinity).toString", "x = (-Infinity).toString;\n");
    expectPrinted("x = (Infinity) ** 2", "x = Infinity ** 2;\n");
    expectPrinted("x = (-Infinity) ** 2", "x = (-Infinity) ** 2;\n");
    expectPrinted("x = ~Infinity", "x = ~Infinity;\n");
    expectPrinted("x = ~-Infinity", "x = ~-Infinity;\n");
    expectPrinted("x = Infinity * y", "x = Infinity * y;\n");
    expectPrinted("x = Infinity / y", "x = Infinity / y;\n");
    expectPrinted("x = y * Infinity", "x = y * Infinity;\n");
    expectPrinted("x = y / Infinity", "x = y / Infinity;\n");
    expectPrinted("throw Infinity", "throw Infinity;\n");

    expectPrintedMinify("x = Infinity", "x=Infinity;");
    expectPrintedMinify("x = -Infinity", "x=-Infinity;");
    expectPrintedMinify("x = (Infinity).toString", "x=Infinity.toString;");
    expectPrintedMinify("x = (-Infinity).toString", "x=(-Infinity).toString;");
    expectPrintedMinify("x = (Infinity) ** 2", "x=Infinity**2;");
    expectPrintedMinify("x = (-Infinity) ** 2", "x=(-Infinity)**2;");
    expectPrintedMinify("x = ~Infinity", "x=~Infinity;");
    expectPrintedMinify("x = ~-Infinity", "x=~-Infinity;");
    expectPrintedMinify("x = Infinity * y", "x=Infinity*y;");
    expectPrintedMinify("x = Infinity / y", "x=Infinity/y;");
    expectPrintedMinify("x = y * Infinity", "x=y*Infinity;");
    expectPrintedMinify("x = y / Infinity", "x=y/Infinity;");
    expectPrintedMinify("throw Infinity", "throw Infinity;");

    expectPrintedMangle("x = Infinity", "x = 1 / 0;\n");
    expectPrintedMangle("x = -Infinity", "x = -1 / 0;\n");
    expectPrintedMangle("x = (Infinity).toString", "x = (1 / 0).toString;\n");
    expectPrintedMangle("x = (-Infinity).toString", "x = (-1 / 0).toString;\n");
    expectPrintedMangle("x = Infinity ** 2", "x = (1 / 0) ** 2;\n");
    expectPrintedMangle("x = (-Infinity) ** 2", "x = (-1 / 0) ** 2;\n");
    expectPrintedMangle("x = Infinity * y", "x = 1 / 0 * y;\n");
    expectPrintedMangle("x = Infinity / y", "x = 1 / 0 / y;\n");
    expectPrintedMangle("x = y * Infinity", "x = y * (1 / 0);\n");
    expectPrintedMangle("x = y / Infinity", "x = y / (1 / 0);\n");
    expectPrintedMangle("throw Infinity", "throw 1 / 0;\n");

    expectPrintedMangleMinify("x = Infinity", "x=1/0;");
    expectPrintedMangleMinify("x = -Infinity", "x=-1/0;");
    expectPrintedMangleMinify("x = (Infinity).toString", "x=(1/0).toString;");
    expectPrintedMangleMinify("x = (-Infinity).toString", "x=(-1/0).toString;");
    expectPrintedMangleMinify("x = Infinity ** 2", "x=(1/0)**2;");
    expectPrintedMangleMinify("x = (-Infinity) ** 2", "x=(-1/0)**2;");
    expectPrintedMangleMinify("x = Infinity * y", "x=1/0*y;");
    expectPrintedMangleMinify("x = Infinity / y", "x=1/0/y;");
    expectPrintedMangleMinify("x = y * Infinity", "x=y*(1/0);");
    expectPrintedMangleMinify("x = y / Infinity", "x=y/(1/0);");
    expectPrintedMangleMinify("throw Infinity", "throw 1/0;");
}

TEST(JsPrinter, TestBinaryOperatorVisitor) {
    // Make sure the inner "/*b*/" comment doesn't disappear due to weird binary visitor stuff
    expectPrintedMangle("x = (0, /*a*/ (0, /*b*/ (0, /*c*/ 1 == 2) + 3) * 4)", "x = /*a*/\n/*b*/\n(/*c*/\n!1 + 3) * 4;\n");

    // Make sure deeply-nested ASTs don't cause a stack overflow
    std::string x = "x = f()";
    for (int i = 0; i < 10000; i++) {
        x += " || f()";
    }
    x += ";\n";
    expectPrinted(x, x);
}

// See: https://github.com/tc39/proposal-explicit-resource-management
TEST(JsPrinter, TestUsing) {
    expectPrinted("using x = y", "using x = y;\n");
    expectPrinted("using x = y, z = _", "using x = y, z = _;\n");
    expectPrintedMinify("using x = y", "using x=y;");
    expectPrintedMinify("using x = y, z = _", "using x=y,z=_;");

    expectPrinted("await using x = y", "await using x = y;\n");
    expectPrinted("await using x = y, z = _", "await using x = y, z = _;\n");
    expectPrintedMinify("await using x = y", "await using x=y;");
    expectPrintedMinify("await using x = y, z = _", "await using x=y,z=_;");
}

TEST(JsPrinter, TestMinifyBigInt) {
    expectPrintedTargetMangle(2019, "x = 0b100101n", "x = /* @__PURE__ */ BigInt(37);\n");
    expectPrintedTargetMangle(2019, "x = 0B100101n", "x = /* @__PURE__ */ BigInt(37);\n");
    expectPrintedTargetMangle(2019, "x = 0o76543210n", "x = /* @__PURE__ */ BigInt(16434824);\n");
    expectPrintedTargetMangle(2019, "x = 0O76543210n", "x = /* @__PURE__ */ BigInt(16434824);\n");
    expectPrintedTargetMangle(2019, "x = 0xFEDCBA9876543210n", "x = /* @__PURE__ */ BigInt(\"0xFEDCBA9876543210\");\n");
    expectPrintedTargetMangle(2019, "x = 0XFEDCBA9876543210n", "x = /* @__PURE__ */ BigInt(\"0XFEDCBA9876543210\");\n");
    expectPrintedTargetMangle(2019, "x = 0xb0ba_cafe_f00dn", "x = /* @__PURE__ */ BigInt(0xb0bacafef00d);\n");
    expectPrintedTargetMangle(2019, "x = 0xB0BA_CAFE_F00Dn", "x = /* @__PURE__ */ BigInt(0xB0BACAFEF00D);\n");
    expectPrintedTargetMangle(2019, "x = 102030405060708090807060504030201n", "x = /* @__PURE__ */ BigInt(\"102030405060708090807060504030201\");\n");
}

TEST(JsPrinter, TestRegExp) {
    expectPrinted("x = /re/", "x = /re/;\n");
    expectPrinted("x = /re/gimsuy", "x = /re/gimsuy;\n");
    expectPrinted("x = a/b/c", "x = a / b / c;\n");
    expectPrintedMinify("x = /re/gimsuy", "x=/re/gimsuy;");
    expectPrintedMinify("x = a/b/c", "x=a/b/c;");
}

TEST(JsPrinter, TestLabels) {
    expectPrinted("foo: while (true) break foo", "foo: while (true) break foo;\n");
    expectPrinted("foo: for (;;) continue foo", "foo: for (; ; ) continue foo;\n");
    expectPrinted("foo: { break foo }", "foo: {\n  break foo;\n}\n");
    expectPrintedMinify("foo: while (true) break foo", "foo:while(true)break foo;");
}

TEST(JsPrinter, TestTryCatchFinally) {
    expectPrinted("try {} catch {}", "try {\n} catch {\n}\n");
    expectPrinted("try {} catch (e) {}", "try {\n} catch (e) {\n}\n");
    expectPrinted("try {} finally {}", "try {\n} finally {\n}\n");
    expectPrinted("try {} catch (e) {} finally {}", "try {\n} catch (e) {\n} finally {\n}\n");
    expectPrintedMinify("try {} catch (e) {} finally {}", "try{}catch(e){}finally{}");
}

TEST(JsPrinter, TestOptionalChain) {
    expectPrinted("a?.b", "a?.b;\n");
    expectPrinted("a?.b.c", "a?.b.c;\n");
    expectPrinted("(a?.b).c", "(a?.b).c;\n");
    expectPrinted("a?.()", "a?.();\n");
    expectPrinted("a?.[b]", "a?.[b];\n");
    expectPrinted("a?.b?.c", "a?.b?.c;\n");
    expectPrintedMinify("a?.b.c", "a?.b.c;");
}

TEST(JsPrinter, TestLogicalAssignment) {
    expectPrinted("a ||= b", "a ||= b;\n");
    expectPrinted("a &&= b", "a &&= b;\n");
    expectPrinted("a \?\?= b", "a \?\?= b;\n");
    expectPrintedMinify("a ||= b", "a||=b;");
    expectPrintedMinify("a &&= b", "a&&=b;");
    expectPrintedMinify("a \?\?= b", "a\?\?=b;");
}

TEST(JsPrinter, TestStaticBlock) {
    expectPrinted("class A { static {} }", "class A {\n  static {\n  }\n}\n");
    expectPrinted("class A { static { x = y } }", "class A {\n  static {\n    x = y;\n  }\n}\n");
    expectPrintedMinify("class A { static { x = y } }", "class A{static{x=y}}");
}

TEST(JsPrinter, TestAsyncAwait) {
    expectPrinted("async function f() {}", "async function f() {\n}\n");
    expectPrinted("async function f() { await x }", "async function f() {\n  await x;\n}\n");
    expectPrinted("x = async () => {}", "x = async () => {\n};\n");
    expectPrinted("x = async y => await y", "x = async (y) => await y;\n");
    expectPrintedMinify("async function f() { await x }", "async function f(){await x}");
}

TEST(JsPrinter, TestYieldDelegation) {
    expectPrinted("function* f() { yield* g() }", "function* f() {\n  yield* g();\n}\n");
    expectPrinted("function* f() { yield yield* g() }", "function* f() {\n  yield yield* g();\n}\n");
    expectPrintedMinify("function* f() { yield* g() }", "function*f(){yield*g()}");
}

TEST(JsPrinter, TestExponentiationParentheses) {
    expectPrinted("x ** y", "x ** y;\n");
    expectPrinted("2 ** 3 ** 4", "2 ** 3 ** 4;\n");
    expectPrinted("(2 ** 3) ** 4", "(2 ** 3) ** 4;\n");
    expectPrinted("(-x) ** 2", "(-x) ** 2;\n");
    expectPrinted("x ** -y", "x ** -y;\n");
    expectPrintedMinify("(2 ** 3) ** 4", "(2**3)**4;");
}

TEST(JsPrinter, TestExponentiationAssignment) {
    expectPrinted("x **= y", "x **= y;\n");
    expectPrinted("a.b **= c", "a.b **= c;\n");
    expectPrintedMinify("x **= y", "x**=y;");
}

TEST(JsPrinter, TestTernary) {
    expectPrinted("a ? b : c", "a ? b : c;\n");
    expectPrinted("a ? b : c ? d : e", "a ? b : c ? d : e;\n");
    expectPrinted("(a ? b : c) ? d : e", "(a ? b : c) ? d : e;\n");
    expectPrinted("a ? b ? c : d : e", "a ? b ? c : d : e;\n");
    expectPrintedMinify("a ? b : c", "a?b:c;");
}

TEST(JsPrinter, TestClassFields) {
    expectPrinted("class A { x }", "class A {\n  x;\n}\n");
    expectPrinted("class A { x = y }", "class A {\n  x = y;\n}\n");
    expectPrinted("class A { static x = y }", "class A {\n  static x = y;\n}\n");
    expectPrinted("class A { [x] = y }", "class A {\n  [x] = y;\n}\n");
    expectPrinted("class A { #x = y }", "class A {\n  #x = y;\n}\n");
    expectPrinted("class A { static #x = y }", "class A {\n  static #x = y;\n}\n");
    expectPrintedMinify("class A { static x = y; #p = q }", "class A{static x=y;#p=q}");
}

TEST(JsPrinter, TestPrivateMembers) {
    expectPrinted("class A { #m() {} }", "class A {\n  #m() {\n  }\n}\n");
    expectPrinted("class A { get #x() {} set #x(v) {} }", "class A {\n  get #x() {\n  }\n  set #x(v) {\n  }\n}\n");
    expectPrinted("class A { #x; m() { return this.#x } }", "class A {\n  #x;\n  m() {\n    return this.#x;\n  }\n}\n");
    expectPrinted("class A { #foo; m() { return this.#foo } }", "class A {\n  #foo;\n  m() {\n    return this.#foo;\n  }\n}\n");
    expectPrintedMinify("class A { #m() {} }", "class A{#m(){}}");
}

TEST(JsPrinter, TestDoWhile) {
    expectPrinted("do {} while (x)", "do {\n} while (x);\n");
    expectPrinted("do x(); while (x)", "do\n  x();\nwhile (x);\n");
    expectPrintedMinify("do { x() } while (x)", "do{x()}while(x);");
}

TEST(JsPrinter, TestEmptyStatement) {
    expectPrinted(";", ";\n");
    expectPrinted(";;", ";\n;\n");
    expectPrinted("while (x) ;", "while (x) ;\n");
    expectPrintedMinify(";;", ";;");
}

TEST(JsPrinter, TestDebugger) {
    expectPrinted("debugger", "debugger;\n");
    expectPrintedMinify("debugger", "debugger;");
}

TEST(JsPrinter, TestWithStatement) {
    expectPrinted("with (x) {}", "with (x) {\n}\n");
    expectPrinted("with (x) y()", "with (x) y();\n");
    expectPrintedMinify("with (x) y()", "with(x)y();");
}

TEST(JsPrinter, TestDirectivePrologue) {
    expectPrinted("\"use strict\"", "\"use strict\";\n");
    expectPrinted("\"use strict\"; \"use strict\"", "\"use strict\";\n");
    expectPrinted("\"use strict\"; x()", "\"use strict\";\nx();\n");
    expectPrintedMinify("\"use strict\"; x()", "\"use strict\";x();");
}

TEST(JsPrinter, TestBigIntLiteral) {
    expectPrinted("x = 123n", "x = 123n;\n");
    expectPrinted("x = 0xFFn", "x = 0xFFn;\n");
    expectPrinted("x = 0b101n", "x = 0b101n;\n");
    expectPrinted("x = 0o17n", "x = 0o17n;\n");
    expectPrintedMinify("x = 123n", "x=123n;");
}

TEST(JsPrinter, TestNumericSeparators) {
    expectPrinted("x = 1_000_000", "x = 1e6;\n");
    expectPrinted("x = 0xFF_FF", "x = 65535;\n");
    expectPrinted("x = 1_0.2_5", "x = 10.25;\n");
}

TEST(JsPrinter, TestUndefined) {
    // A bare "undefined" expression statement is pure and removed entirely
    expectPrinted("undefined", "");
    expectPrinted("x = undefined", "x = void 0;\n");
    expectPrintedMangle("x = undefined", "x = void 0;\n");
    expectPrintedMangleMinify("x = undefined", "x=void 0;");
}

TEST(JsPrinter, TestSuperAndNewTarget) {
    expectPrinted("class A extends B { constructor() { super() } }",
        "class A extends B {\n  constructor() {\n    super();\n  }\n}\n");
    expectPrinted("class A extends B { m() { super.m() } }",
        "class A extends B {\n  m() {\n    super.m();\n  }\n}\n");
    expectPrinted("function f() { return new.target }",
        "function f() {\n  return new.target;\n}\n");
    expectPrintedMinify("class A extends B { constructor() { super() } }",
        "class A extends B{constructor(){super()}}");
}

TEST(JsPrinter, TestForAwait) {
    expectPrinted("async function f() { for await (const x of y) ; }",
        "async function f() {\n  for await (const x of y) ;\n}\n");
    expectPrinted("async function f() { for await (x of y) { g(x) } }",
        "async function f() {\n  for await (x of y) {\n    g(x);\n  }\n}\n");
    expectPrintedMinify("async function f() { for await (const x of y) ; }",
        "async function f(){for await(const x of y);}");
}

TEST(JsPrinter, TestCatchBindingPatterns) {
    expectPrinted("try {} catch ({ a, b }) {}", "try {\n} catch ({ a, b }) {\n}\n");
    expectPrinted("try {} catch ([a]) {}", "try {\n} catch ([a]) {\n}\n");
    expectPrintedMinify("try {} catch ({ a }) {}", "try{}catch({a}){}");
}

TEST(JsPrinter, TestSwitchStatements) {
    expectPrinted("switch (x) { case 1: y(); break; default: z() }",
        "switch (x) {\n  case 1:\n    y();\n    break;\n  default:\n    z();\n}\n");
    expectPrinted("switch (x) { default: y(); case 1: }",
        "switch (x) {\n  default:\n    y();\n  case 1:\n}\n");
    expectPrintedMinify("switch (x) { case 1: y(); break; default: z() }",
        "switch(x){case 1:y();break;default:z()}");
}

TEST(JsPrinter, TestObjectMethodVariants) {
    expectPrinted("({ async f() {} })", "({ async f() {\n} });\n");
    expectPrinted("({ async *g() {} })", "({ async *g() {\n} });\n");
    expectPrinted("({ *g() {} })", "({ *g() {\n} });\n");
    expectPrinted("({ get [x]() {} })", "({ get [x]() {\n} });\n");
    expectPrinted("({ set [x](v) {} })", "({ set [x](v) {\n} });\n");
    expectPrintedMinify("({ async *g() {} })", "({async*g(){}});");
}

TEST(JsPrinter, TestOptionalChainCalls) {
    expectPrinted("a?.b(c)?.d", "a?.b(c)?.d;\n");
    expectPrinted("a?.[b][c]", "a?.[b][c];\n");
    expectPrinted("delete a?.b", "delete a?.b;\n");
    expectPrintedMinify("a?.b(c)?.d", "a?.b(c)?.d;");
}

TEST(JsPrinter, TestYieldExpressions) {
    expectPrinted("function* f() { g(yield, yield) }",
        "function* f() {\n  g(yield, yield);\n}\n");
    expectPrinted("function* f() { x = yield }",
        "function* f() {\n  x = yield;\n}\n");
    expectPrinted("function* f() { yield (yield x) }",
        "function* f() {\n  yield yield x;\n}\n");
}

TEST(JsPrinter, TestAwaitExpressions) {
    expectPrinted("async function f() { g(await x) }",
        "async function f() {\n  g(await x);\n}\n");
    expectPrinted("async function f() { (await x).y }",
        "async function f() {\n  (await x).y;\n}\n");
    expectPrinted("async function f() { for (const a of await b) ; }",
        "async function f() {\n  for (const a of await b) ;\n}\n");
}

TEST(JsPrinter, TestBinaryPrecedence) {
    expectPrinted("a % b * c", "a % b * c;\n");
    expectPrinted("(a + b) * c", "(a + b) * c;\n");
    expectPrinted("a + b * c", "a + b * c;\n");
    expectPrinted("a - (b - c)", "a - (b - c);\n");
    expectPrinted("a << b | c", "a << b | c;\n");
    expectPrinted("(a << b) | c", "a << b | c;\n");
    expectPrinted("a && b || c && d", "a && b || c && d;\n");
    expectPrinted("a | b ^ c & d", "a | b ^ c & d;\n");
    expectPrintedMinify("a + b * c", "a+b*c;");
}

TEST(JsPrinter, TestUnaryOperators) {
    expectPrinted("typeof x === 'string'", "typeof x === \"string\";\n");
    expectPrinted("void x()", "void x();\n");
    expectPrinted("delete a.b", "delete a.b;\n");
    expectPrinted("delete a['b']", "delete a[\"b\"];\n");
    expectPrintedMinify("typeof x === 'string'", "typeof x===\"string\";");
}

TEST(JsPrinter, TestBindingPatterns) {
    expectPrinted("var { a: { b }, c: [d] } = x;",
        "var { a: { b }, c: [d] } = x;\n");
    expectPrinted("var [a = 1, , b = 2, ...c] = x;",
        "var [a = 1, , b = 2, ...c] = x;\n");
    expectPrinted("var { a = 1, b: { c: d = 2 } } = x;",
        "var { a = 1, b: { c: d = 2 } } = x;\n");
    expectPrintedMinify("var [a, ...b] = x;", "var[a,...b]=x;");
}

TEST(JsPrinter, TestImportAttributes) {
    expectPrinted("import x from './y.json' with { type: 'json' }",
        "import x from \"./y.json\" with { type: \"json\" };\n");
    expectPrinted("import('./y.json', { with: { type: 'json' } })",
        "import(\"./y.json\", { with: { type: \"json\" } });\n");
    expectPrintedMinify("import x from './y.json' with { type: 'json' }",
        "import x from\"./y.json\"with{type:\"json\"};");
}

TEST(JsPrinter, TestImportExportAliases) {
    expectPrinted("import { a as b } from './x'", "import { a as b } from \"./x\";\n");
    expectPrinted("import { 'a b' as c } from './x'", "import { \"a b\" as c } from \"./x\";\n");
    expectPrinted("var a; export { a as 'b c' };", "var a;\nexport { a as \"b c\" };\n");
    expectPrinted("export { a as 'b c' } from './x';", "export { a as \"b c\" } from \"./x\";\n");
    expectPrintedMinify("import { a as b } from './x'", "import{a as b}from\"./x\";");
    expectPrintedMinify("var a; export { a as 'b c' };", "var a;export{a as\"b c\"};");
}

// The following tests probe round-trip correctness: the printed output must
// re-parse to the same AST. Losing parentheses here changes program meaning.

TEST(JsPrinter, TestArrowBodyParens) {
    // An object literal body must keep its parens or it becomes a block body
    expectPrinted("() => ({})", "() => " "({})" ";\n");
    expectPrinted("x = () => ({})", "x = () => " "({})" ";\n");
}

TEST(JsPrinter, TestStatementStartParens) {
    // An assignment to an object pattern at statement start must stay wrapped,
    // or the "{" would parse as a block
    expectPrinted("({ a } = b)", "({ a } = b);\n");
    expectPrinted("({ a: x.y } = b)", "({ a: x.y } = b);\n");
    // Array patterns are unambiguous at statement start, so the parens are
    // dropped as redundant
    expectPrinted("([a] = b)", "[a] = b;\n");
}

TEST(JsPrinter, TestRequiredOperandParens) {
    // Sequence expressions as operands must keep their parens
    expectPrinted("x ** (a, b)", "x ** (a, b);\n");
    expectPrinted("typeof (a, b)", "typeof (a, b);\n");
    expectPrinted("-(a, b)", "-(a, b);\n");
    expectPrinted("void (a, b)", "void (a, b);\n");
    // Await/yield cannot be the unparenthesized left operand of these operators
    expectPrinted("async function f() { (await x) ** 2 }",
        "async function f() {\n  (await x) ** 2;\n}\n");
    expectPrinted("function* g() { x = (yield) || y }",
        "function* g() {\n  x = (yield) || y;\n}\n");
    // A call on an awaited value must stay wrapped
    expectPrinted("async function f() { (await x)(y) }",
        "async function f() {\n  (await x)(y);\n}\n");
}

TEST(JsPrinter, TestKeywordPropertyNames) {
    // Keywords are legal as property names and after dot
    expectPrinted("p.catch(onRejected).finally(cleanUp)",
        "p.catch(onRejected).finally(cleanUp);\n");
    expectPrinted("x.default", "x.default;\n");
    expectPrinted("({ get: 1, set: 2, async: 3, static: 4 })",
        "({ get: 1, set: 2, async: 3, static: 4 });\n");
}

TEST(JsPrinter, TestDirectiveAfterStatement) {
    // Only directives in the prologue are directives; the last one here is a
    // plain expression statement and must still be printed
    expectPrinted("\"use strict\"; x(); \"use strict\"",
        "\"use strict\";\nx();\n\"use strict\";\n");
}

TEST(JsPrinter, TestAnonymousDefaultExports) {
    expectPrinted("export default class {}", "export default class {\n}\n");
    // Anonymous functions print without a space before the parameter list
    expectPrinted("export default function () {}", "export default function() {\n}\n");
    expectPrintedMinify("export default function () {}", "export default function(){}");
}

TEST(JsPrinter, TestLoopAndBindingEdges) {
    expectPrinted("for (const [a] of b) ;", "for (const [a] of b) ;\n");
    expectPrinted("for (const { a } of c) ;", "for (const { a } of c) ;\n");
    expectPrinted("for (let i = 0, j = 1; i < j; i++) ;", "for (let i = 0, j = 1; i < j; i++) ;\n");
    expectPrinted("for (;;i++, j--) ;", "for (; ; i++, j--) ;\n");
    expectPrinted("x = [a, , b]", "x = [a, , b];\n");
}

TEST(JsPrinter, TestAssignmentChains) {
    expectPrinted("a = b = c", "a = b = c;\n");
    expectPrinted("a += b *= c", "a += b *= c;\n");
    expectPrintedMinify("a = b = c", "a=b=c;");
}

TEST(JsPrinter, TestMiscRoundTrip) {
    expectPrinted("new f(...a)", "new f(...a);\n");
    expectPrinted("x = /^\\/$/.test(s)", "x = /^\\/$/.test(s);\n");
    expectPrinted("x = -0", "x = -0;\n");
    expectPrinted("class A { static {} static {} }",
        "class A {\n  static {\n  }\n  static {\n  }\n}\n");
}

TEST(JsPrinter, TestTargetMangleMinify) {
    // Arrow functions downgraded to ES5
    expectPrintedTargetMangleMinify(5, "var f = x => x", "var f=function(x){return x};");
    expectPrintedTargetMangleMinify(5, "var f = () => {}", "var f=function(){};");
    expectPrintedTargetMangleMinify(5, "var f = (a, b) => a + b", "var f=function(a,b){return a+b};");
    expectPrintedTargetMangleMinify(5, "var f = x => ({})", "var f=function(x){return{}};");
    expectPrintedTargetMangleMinify(2015, "var f = x => x", "var f=x=>x;");
    expectPrintedTargetMangleMinify(2015, "var f = () => {}", "var f=()=>{};");

    // Template literals downgraded to strings
    expectPrintedTargetMangleMinify(5, "foo('a\\n\\n\\nb')", "foo(\"a\\n\\n\\nb\");");
    expectPrintedTargetMangleMinify(2015, "foo('a\\n\\n\\nb')", "foo(`a\n\n\nb`);");

    // Shorthand properties expanded in ES5
    expectPrintedTargetMangleMinify(5, "foo({a, b})", "foo({a:a,b:b});");
    expectPrintedTargetMangleMinify(2015, "foo({a, b})", "foo({a,b});");

    // Boolean mangling
    expectPrintedTargetMangleMinify(5, "x = true", "x=!0;");
    expectPrintedTargetMangleMinify(5, "x = false", "x=!1;");
    expectPrintedTargetMangleMinify(2016, "x = true ** 2", "x=(!0)**2;");
    expectPrintedTargetMangleMinify(2016, "x = false ** 2", "x=(!1)**2;");

    // Infinity mangling
    expectPrintedTargetMangleMinify(5, "x = Infinity", "x=1/0;");
    expectPrintedTargetMangleMinify(5, "x = -Infinity", "x=-1/0;");

    // Undefined mangling
    expectPrintedTargetMangleMinify(5, "x = undefined", "x=void 0;");

    // String template literal to string with newline mangling
    expectPrintedTargetMangleMinify(5, "x = '\\n'", "x=\"\\n\";");
    expectPrintedTargetMangleMinify(2015, "x = '\\n'", "x=`\n`;");

    // Class fields (class fields are ES2022, so keep ES2022+)
    expectPrintedTargetMangleMinify(2022, "class A { x = y }", "class A{x=y}");
    expectPrintedTargetMangleMinify(2022, "class A { static x = y }", "class A{static x=y}");

    // Let/const (no lowering, just mangle)
    expectPrintedTargetMangleMinify(2015, "let x = 1", "let x=1;");
    expectPrintedTargetMangleMinify(2015, "const x = 1", "const x=1;");

    // Default parameters
    expectPrintedTargetMangleMinify(2015, "function f(x = 1) {}", "function f(x=1){}");

    // Rest parameters
    expectPrintedTargetMangleMinify(2015, "function f(...a) {}", "function f(...a){}");

    // Spread elements
    expectPrintedTargetMangleMinify(2015, "f(...a)", "f(...a);");

    // Destructuring preserved in ES2015+
    expectPrintedTargetMangleMinify(2015, "var {a, b} = x", "var{a,b}=x;");
    expectPrintedTargetMangleMinify(2015, "var [a, b] = x", "var[a,b]=x;");

    // Logical assignment (ES2021+)
    expectPrintedTargetMangleMinify(2021, "a ||= b", "a||=b;");
    expectPrintedTargetMangleMinify(2021, "a &&= b", "a&&=b;");
    expectPrintedTargetMangleMinify(2021, "a \?\?= b", "a\?\?=b;");

    // Computed property names
    expectPrintedTargetMangleMinify(2015, "x = {[y]: z}", "x={[y]:z};");

    // Method definitions
    expectPrintedTargetMangleMinify(2015, "x = {foo() {}}", "x={foo(){}};");

    // Class methods
    expectPrintedTargetMangleMinify(2015, "class A { foo() {} }", "class A{foo(){}}");

    // Async functions
    expectPrintedTargetMangleMinify(2017, "async function f() {}", "async function f(){}");
    expectPrintedTargetMangleMinify(2017, "x = async () => {}", "x=async()=>{};");

    // Generator functions
    expectPrintedTargetMangleMinify(2015, "function* f() {}", "function*f(){}");

    // Import/export
    expectPrintedTargetMangleMinify(2015, "export default 1", "export default 1;");

    // Nullish coalescing
    expectPrintedTargetMangleMinify(2020, "x ?? y", "x??y;");

    // Optional chaining
    expectPrintedTargetMangleMinify(2020, "x?.y", "x?.y;");
    expectPrintedTargetMangleMinify(2020, "x?.()", "x?.();");
    expectPrintedTargetMangleMinify(2020, "x?.[y]", "x?.[y];");

    // Exponentiation
    expectPrintedTargetMangleMinify(2016, "x ** y", "x**y;");

    // Object rest/spread
    expectPrintedTargetMangleMinify(2018, "({...x})", "({...x});");
    expectPrintedTargetMangleMinify(2018, "({...x} = y)", "({...x}=y);");

    // For-of with destructuring
    expectPrintedTargetMangleMinify(2015, "for (const {a} of b) ;", "for(const{a}of b);");

    // Multiple statements minified
    expectPrintedTargetMangleMinify(2015, "let a = 1; let b = 2; a + b", "let a=1,b=2;a+b;");
}

