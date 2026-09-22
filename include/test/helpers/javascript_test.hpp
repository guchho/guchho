#pragma once

// ---------------------------------------------------------------------------
// JavaScript parser and printer test helpers for Guchho
//
// This header provides inline helper functions used across the JavaScript test
// suite.  Every helper builds a Guchho Options struct, feeds source code through
// the parser (and optionally the printer), and then asserts against expected
// results using Google Test macros.
//
// The helpers are intentionally minimal — they exist to reduce boilerplate in
// individual test files so each test case can stay short and focused.
// ---------------------------------------------------------------------------

#include "test/guchho_test.hpp"

#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/javascript/js_printer.hpp"
#include "guchho/javascript/js_renamer.hpp"
#include "guchho/logger.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cstdio>

// Short namespace aliases so test code stays concise.
namespace js = guchho::javascript;
namespace compiler = guchho::compiler;
namespace config = guchho::config;
namespace compat = guchho::compat;

using guchho::logger::DeferLogKind;
using guchho::logger::Log;
using guchho::logger::NewDeferLog;
using guchho::logger::OutputOptions;
using guchho::logger::Source;
using guchho::logger::TerminalInfo;


// ---------------------------------------------------------------------------
// Source construction
// ---------------------------------------------------------------------------

// Creates a Source object from an in-memory string, suitable for feeding into
// the Guchho parser.  The source is tagged as "stdin" with a synthetic
// <stdin> path so error messages remain readable in test output.
//
// Example:
//   Source s = SourceForTest("var x = 1;");
//   // s.contents == "var x = 1;"
//   // s.identifier_name == "stdin"
inline Source SourceForTest(std::string contents) {
    Source s;
    s.index = 0;
    s.identifier_name = "stdin";
    s.pretty_paths = {std::string("<stdin>"), std::string("<stdin>")};
    s.key_path = guchho::logger::Path{"<stdin>", {}, {}, {}, {}};
    s.contents = std::move(contents);
    return s;
}

// ---------------------------------------------------------------------------
// Log formatting
// ---------------------------------------------------------------------------

// Concatenates all diagnostic messages in a vector into a single string using
// the default terminal output format.  This makes it easy to compare the full
// diagnostic output of a parse/print pass against an expected string in tests.
//
// Example:
//   auto msgs = log.done();
//   EXPECT_EQ(MessagesToString(msgs), "error: unexpected token\n");
inline std::string MessagesToString(const std::vector<guchho::logger::Msg>& msgs) {
    std::string text;
    for (const auto& msg : msgs) {
        text += msg.String(OutputOptions{}, TerminalInfo{});
    }
    return text;
}

// ---------------------------------------------------------------------------
// Parse-error assertions
// ---------------------------------------------------------------------------

// Parses the given source code with the supplied options and asserts that the
// resulting diagnostic output exactly matches the expected string.  Used as
// the common implementation behind all expectParseError* helpers.
//
// Side effects: the log is consumed and its messages are compared.  A parse
// failure does not cause a test failure on its own — only a mismatch in the
// formatted diagnostics does.
inline void expectParseErrorCommon(const std::string& contents, const std::string& expected, config::Options* options) {
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    js::Parse(log, SourceForTest(contents), js::OptionsFromConfig(options));
    auto msgs = log.done();
    EXPECT_EQ(MessagesToString(msgs), expected);
}


// Parses the source with default options and asserts the diagnostic output.
//
// Example:
//   expectParseError("var x = x", "error: cannot access \"x\" before initialization\n");
inline void expectParseError(const std::string& contents, const std::string& expected) {
    config::Options options{};
    expectParseErrorCommon(contents, expected, &options);
}


// Parses the source as though targeting a specific ECMAScript version and
// asserts the diagnostic output.  The version is converted to a Semver and
// stored in UnsupportedJSFeatures so the parser can reject syntax that is not
// yet available in that version.
//
// Example:
//   expectParseErrorTarget(2015, "let x = 1;", "");
//   expectParseErrorTarget(5, "class Foo {}", "error: classes are not supported in ES5\n");
inline void expectParseErrorTarget(int es_version, const std::string& contents, const std::string& expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    expectParseErrorCommon(contents, expected, &options);
}

// Same as expectParseErrorTarget but also enables ASCII-only output mode.
// Useful for testing that identifier escaping and code generation produce
// correct ASCII-safe output for older target environments.
inline void expectParseErrorTargetASCII(int es_version, const std::string& contents, const std::string& expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    options.ASCIIOnly = true;
    expectParseErrorCommon(contents, expected, &options);
}

// ---------------------------------------------------------------------------
// Print-and-compare assertions
// ---------------------------------------------------------------------------

// Parses the source, prints it back to JavaScript, and asserts the output
// matches the expected string.  This is the core round-trip helper.
//
// Example:
//   expectPrinted("var x = 1 + 2;", "var x = 1 + 2;\n");
inline void expectPrintedCommon(const std::string& contents, const std::string& expected, config::Options* options) {
    options->OmitRuntimeForTests = true;
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    auto parsed = js::Parse(log, SourceForTest(contents), js::OptionsFromConfig(options));
    auto msgs = log.done();

    std::string text;
    for (const auto& msg : msgs) {
        if (msg.kind == guchho::logger::MsgKind::kWarning) {
            continue;
        }
        text += msg.String(OutputOptions{}, TerminalInfo{});
    }
    EXPECT_EQ(text, "");
    ASSERT_TRUE(parsed.second) << "Parse error";

    compiler::SymbolMap symbol_map;
    symbol_map.symbols_for_source.resize(1);
    symbol_map.symbols_for_source[0] = parsed.first.symbols;
    auto renamer = js::NewNoOpRenamer(symbol_map);

    js::PrinterOptions print_options;
    print_options.unsupported_features = options->UnsupportedJSFeatures;
    print_options.ascii_only = options->ASCIIOnly;
    print_options.omit_runtime_for_tests = options->OmitRuntimeForTests;

    js::PrintResult result = js::Print(parsed.first, symbol_map, *renamer, print_options);
    EXPECT_EQ(result.js, expected);
}

// Parses and prints with default options.  The most common helper for
// verifying that valid JavaScript round-trips correctly.
//
// Example:
//   expectPrinted("var x = 1;", "var x = 1;\n");
inline void expectPrinted(const std::string& contents, const std::string& expected) {
    config::Options options{};
    expectPrintedCommon(contents, expected, &options);
}

// Parses and prints with MinifySyntax enabled.  Used for testing that the
// syntax minifier produces the expected compact output.
//
// Example:
//   expectPrintedMangle("if (a) { b; }", "a && b();\n");
inline void expectPrintedMangle(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.MinifySyntax = true;
    expectPrintedCommon(contents, expected, &options);
}


// Convenience helper that verifies both the normal print output and the
// minified output in a single test assertion.
//
// Example:
//   expectPrintedNormalAndMangle("var x = 1;", "var x = 1;\n", "var x=1;\n");
inline void expectPrintedNormalAndMangle(const std::string& contents, const std::string& normal, const std::string& mangle) {
    expectPrinted(contents, normal);
    expectPrintedMangle(contents, mangle);
}

// Parses and prints with specific JS features marked as unsupported.  This
// tests that the printer downgrades or transforms syntax that the target
// environment cannot handle.
//
// Example:
//   expectPrintedWithUnsupportedFeatures(
//       compat::JSFeature::kObjectRestProperty,
//       "var {a, ...b} = x;",
//       "var {a} = x;\nvar b = Object.assign({}, x);\n");
inline void expectPrintedWithUnsupportedFeatures(compat::JSFeature unsupported_features, const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.UnsupportedJSFeatures = unsupported_features;
    expectPrintedCommon(contents, expected, &options);
}

// Like expectPrintedWithUnsupportedFeatures but asserts the parse-error
// diagnostics instead of the printed output.
inline void expectParseErrorWithUnsupportedFeatures(compat::JSFeature unsupported_features, const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.UnsupportedJSFeatures = unsupported_features;
    expectParseErrorCommon(contents, expected, &options);
}


// Parses and prints as though targeting a specific ECMAScript version.
// Useful for testing syntax lowering (e.g. arrow functions → function
// expressions, template literals → string concatenation).
//
// Example:
//   expectPrintedTarget(5, "var x = () => 1;", "var x = function() { return 1; };\n");
inline void expectPrintedTarget(int es_version, const std::string& contents, const std::string& expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    expectPrintedCommon(contents, expected, &options);
}

// Same as expectPrintedTarget but with MinifySyntax also enabled.
inline void expectPrintedMangleTarget(int es_version, const std::string& contents, const std::string& expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    options.MinifySyntax = true;
    expectPrintedCommon(contents, expected, &options);
}

// Parses and prints with ASCII-only output.  Non-ASCII characters in
// identifiers and strings are escaped to \xNN or \uNNNN sequences.
//
// Example:
//   expectPrintedASCII("var x = \"\u00e9\";", "var x = \"\\u00e9\";\n");
inline void expectPrintedASCII(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.ASCIIOnly = true;
    expectPrintedCommon(contents, expected, &options);
}

// Parses and prints targeting a specific ES version with ASCII-only output.
inline void expectPrintedTargetASCII(int es_version, const std::string& contents, const std::string& expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    options.ASCIIOnly = true;
    expectPrintedCommon(contents, expected, &options);
}

// ---------------------------------------------------------------------------
// String utilities for test assertions
// ---------------------------------------------------------------------------

// Replaces every occurrence of `from` in `s` with `to` and returns the result.
// Non-destructive — the original string is not modified.
//
// Example:
//   ReplaceAll("a-b-c", "-", "_")  →  "a_b_c"
//   ReplaceAll("hello", "x", "y")  →  "hello"  (no match, unchanged)
inline std::string ReplaceAll(const std::string& s, const std::string& from, const std::string& to) {
    std::string out;
    size_t prev = 0;
    size_t pos = 0;
    while ((pos = s.find(from, prev)) != std::string::npos) {
        out.append(s, prev, pos - prev);
        out += to;
        prev = pos + from.size();
    }
    out.append(s, prev, std::string::npos);
    return out;
}


// A minimal printf-style format function that replaces a single %s
// placeholder with a string argument.  A literal %% produces a single %.
// No other format specifiers are supported.
//
// Example:
//   SprintfReplace("hello %s!", "world")  →  "hello world!"
//   SprintfReplace("100%%", "")           →  "100%"
inline std::string SprintfReplace(const std::string& format, const std::string& arg) {
    std::string out;
    size_t i = 0;
    while (i < format.size()) {
        if (format[i] == '%' && i + 1 < format.size()) {
            if (format[i + 1] == '%') {
                out += '%';
                i += 2;
                continue;
            }
            if (format[i + 1] == 's') {
                out += arg;
                i += 2;
                continue;
            }
        }
        out += format[i];
        ++i;
    }
    return out;
}


// A minimal printf-style format function that replaces a single %d
// placeholder with an integer argument, or a zero-padded hex placeholder
// (%04x, %02X, etc.) with the argument formatted accordingly.  A literal %%
// produces a single %.
//
// Supported placeholders:
//   %d  — decimal integer
//   %0Nx — zero-padded lowercase hex (N = width)
//   %0NX — zero-padded uppercase hex (N = width)
//
// Example:
//   SprintfReplace("count: %d", 42)      →  "count: 42"
//   SprintfReplace("byte: %02x", 255)    →  "byte: ff"
//   SprintfReplace("hex: %04X", 10)      →  "hex: 000A"
inline std::string SprintfReplace(const std::string& format, int arg) {
    std::string out;
    size_t i = 0;
    while (i < format.size()) {
        if (format[i] == '%' && i + 1 < format.size()) {
            if (format[i + 1] == '%') {
                out += '%';
                i += 2;
                continue;
            }
            if (format[i + 1] == 'd') {
                out += std::to_string(arg);
                i += 2;
                continue;
            }
            if (format[i + 1] == '0') {
                size_t j = i + 1;
                while (j < format.size() && (isdigit(static_cast<unsigned char>(format[j])) || format[j] == 'x' || format[j] == 'X')) {
                    ++j;
                }
                std::string spec = format.substr(i + 1, j - i - 1);
                int width = 0;
                for (size_t k = 0; k < spec.size() && isdigit(static_cast<unsigned char>(spec[k])); ++k) {
                    width = width * 10 + (spec[k] - '0');
                }
                bool upper = spec.find('X') != std::string::npos;
                char buf[32];
                if (upper) {
                    snprintf(buf, sizeof(buf), "%0*X", width, arg);
                } else {
                    snprintf(buf, sizeof(buf), "%0*x", width, arg);
                }
                out += buf;
                i = j;
                continue;
            }
        }
        out += format[i];
        ++i;
    }
    return out;
}


// Decodes a single WTF-8 code point from the leading bytes of a UTF-8-like
// string and returns it as an int.  Returns 0 for empty input.  This is
// needed because Guchho uses WTF-8 internally for string values (which allows
// unpaired surrogates), while test assertions sometimes need to compare
// individual code points.
//
// Edge cases:
//   - Empty string → 0
//   - 1-byte sequence (ASCII) → the byte value itself
//   - 2/3/4-byte sequences → the decoded code point
//   - Truncated sequences → partial decode of available bytes
//
// Example:
//   DecodeWTF8Rune("A")   →  65   (ASCII 'A')
//   DecodeWTF8Rune("\xc3\xa9")  →  233  (é, U+00E9)
//   DecodeWTF8Rune("")    →  0
inline int DecodeWTF8Rune(const std::string& s) {
    if (s.empty()) {
        return 0;
    }
    unsigned char c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) {
        return c;
    }
    if ((c >> 5) == 0x06 && s.size() >= 2) {
        return ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[1]) & 0x3F);
    }
    if ((c >> 4) == 0x0E && s.size() >= 3) {
        return ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[2]) & 0x3F);
    }
    if (s.size() >= 4) {
        return ((c & 0x07) << 18) | ((static_cast<unsigned char>(s[1]) & 0x3F) << 12) | ((static_cast<unsigned char>(s[2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[3]) & 0x3F);
    }
    return c;
}

// ---------------------------------------------------------------------------
// JSX helpers
// ---------------------------------------------------------------------------

// Parses JSX-enabled source code and asserts the diagnostic output.  This is
// the JSX counterpart of expectParseError — it enables JSX.Parse before
// calling expectParseErrorCommon.
//
// Example:
//   expectParseErrorJSX("<div />", "");
inline void expectParseErrorJSX(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.JSX.Parse = true;
    expectParseErrorCommon(contents, expected, &options);
}


// Parses and prints JSX source code in both preserve and transform modes.
// This verifies that JSX is handled correctly regardless of whether the
// output should keep JSX syntax or transform it into function calls.
//
// Parameters:
//   contents          — the JSX source to parse
//   expectedPreserve  — expected output with JSX.Preserve = true
//   expectedTransform — expected output with JSX.Preserve = false
//
// Example:
//   expectPrintedJSX("<div />", "<div />;\n", "React.createElement(\"div\", null);\n");
inline void expectPrintedJSX(const std::string& contents, const std::string& expectedPreserve, const std::string& expectedTransform) {
    config::Options preserve_options{};
    preserve_options.JSX.Parse = true;
    preserve_options.JSX.Preserve = true;
    expectPrintedCommon(contents, expectedPreserve, &preserve_options);
    config::Options transform_options{};
    transform_options.JSX.Parse = true;
    expectPrintedCommon(contents, expectedTransform, &transform_options);
}

// Parses and prints JSX source code in preserve mode only.  This is the
// 2-argument overload used by tests that only care about preserve output.
//
// Example:
//   expectPrintedJSX("<div />", "<div />;\n");
inline void expectPrintedJSX(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.JSX.Parse = true;
    options.JSX.Preserve = true;
    expectPrintedCommon(contents, expected, &options);
}


// Parses and prints JSX with SideEffects enabled.  When SideEffects is true,
// elements that are known to be side-effect-free may be dropped from the
// output.
//
// Example:
//   expectPrintedJSXSideEffects("<div />", "<div />;\n");
inline void expectPrintedJSXSideEffects(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.JSX.Parse = true;
    options.JSX.SideEffects = true;
    expectPrintedCommon(contents, expected, &options);
}


// Parses and prints JSX with MinifySyntax enabled.  Verifies that the syntax
// minifier handles JSX elements correctly (e.g. collapsing whitespace,
// removing unnecessary attributes).
//
// Example:
//   expectPrintedMangleJSX("<div />", "<div />;\n");
inline void expectPrintedMangleJSX(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.MinifySyntax = true;
    options.JSX.Parse = true;
    expectPrintedCommon(contents, expected, &options);
}


// Configuration struct for JSX automatic-runtime tests.  This bundles the
// options that affect how the JSX transform resolves and imports its runtime.
struct JSXAutomaticTestOptions {
    bool Development{};
    std::string ImportSource;
    bool OmitJSXRuntimeForTests{};
    bool SideEffects{};
};

// Parses JSX source with the automatic runtime enabled and asserts the
// diagnostic output.  The automatic runtime uses import statements to
// resolve JSX helpers instead of relying on a global React variable.
//
// Example:
//   expectParseErrorJSXAutomatic({},
//       "<div>{x</div>",
//       "error: Unexpected \"<\" in expression\n");
inline void expectParseErrorJSXAutomatic(const JSXAutomaticTestOptions& options, const std::string& contents, const std::string& expected) {
    config::Options config_options{};
    config_options.OmitJSXRuntimeForTests = options.OmitJSXRuntimeForTests;
    config_options.JSX.AutomaticRuntime = true;
    config_options.JSX.Parse = true;
    config_options.JSX.Development = options.Development;
    config_options.JSX.ImportSource = options.ImportSource;
    config_options.JSX.SideEffects = options.SideEffects;
    expectParseErrorCommon(contents, expected, &config_options);
}

// Parses and prints JSX source with the automatic runtime enabled.  This
// verifies that the transform correctly generates import statements and
// createElement/jsxs/jsxsDEV calls depending on the options.
//
// Example:
//   expectPrintedJSXAutomatic({},
//       "<div />",
//       "import { jsx } from \"react/jsx-runtime\";\njsx(\"div\", null);\n");
inline void expectPrintedJSXAutomatic(const JSXAutomaticTestOptions& options, const std::string& contents, const std::string& expected) {
    config::Options config_options{};
    config_options.OmitJSXRuntimeForTests = options.OmitJSXRuntimeForTests;
    config_options.JSX.AutomaticRuntime = true;
    config_options.JSX.Parse = true;
    config_options.JSX.Development = options.Development;
    config_options.JSX.ImportSource = options.ImportSource;
    config_options.JSX.SideEffects = options.SideEffects;
    expectPrintedCommon(contents, expected, &config_options);
}


// ---------------------------------------------------------------------------
// TypeScript parser test helpers for Guchho
//
// This header provides inline helper functions for testing Guchho's
// TypeScript parser.  Every helper builds a config::Options struct with
// TS.Parse enabled, feeds source code through the parser (and optionally
// the printer), and then asserts against expected results using Google Test.
//
// These helpers are separated from the JavaScript helpers so that
// TypeScript-specific test files can include only what they need without
// pulling in unnecessary dependencies.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// LiteralString helper
// ---------------------------------------------------------------------------

// A helper type that accepts both string literals (via template deduction for
// size) and std::string values.  This allows test helpers to accept either
// compile-time string literals or runtime strings without requiring callers to
// explicitly construct std::string objects.
struct LiteralString {
    std::string value;

    template <size_t N>
    LiteralString(const char (&lit)[N])
        : value(lit, N - 1) {}
    LiteralString(std::string lit)
        : value(std::move(lit)) {}
};

// ---------------------------------------------------------------------------
// Whitespace-only minification
// ---------------------------------------------------------------------------

// Parses and prints with MinifyWhitespace enabled.  Removes unnecessary
// whitespace while preserving semantics.
//
// Example:
//   expectPrintedMinify("var x = 1;", "var x=1;");
inline void expectPrintedMinify(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.MinifyWhitespace = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

// Parses and prints with MinifySyntax and MinifyWhitespace enabled.
//
// Example:
//   expectPrintedMangleMinify("var x = 1;", "var x=1;");
inline void expectPrintedMangleMinify(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.MinifySyntax = true;
    options.MinifyWhitespace = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

// Parses and prints with both MinifyWhitespace and ASCII-only output.
inline void expectPrintedMinifyASCII(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.MinifyWhitespace = true;
    options.ASCIIOnly = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

// ---------------------------------------------------------------------------
// Target-version + minification combinations
// ---------------------------------------------------------------------------

// Parses and prints targeting a specific ES version with MinifyWhitespace.
inline void expectPrintedTargetMinify(int es_version, LiteralString contents, LiteralString expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    options.MinifyWhitespace = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

// Parses and prints targeting a specific ES version with MinifySyntax.
// Named expectPrintedTargetMangle to match the test file convention.
inline void expectPrintedTargetMangle(int es_version, LiteralString contents, LiteralString expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    options.MinifySyntax = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

// Parses and prints targeting a specific ES version with MinifySyntax and
// MinifyWhitespace.
inline void expectPrintedTargetMangleMinify(int es_version, LiteralString contents, LiteralString expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    options.MinifySyntax = true;
    options.MinifyWhitespace = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

// ---------------------------------------------------------------------------
// JSX single-option helpers
// ---------------------------------------------------------------------------

// Parses and prints JSX with preserve mode only (no extra options).
inline void expectPrintedJSXPreserve(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.JSX.Parse = true;
    options.JSX.Preserve = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

// Parses and prints JSX with ASCII-only output.
inline void expectPrintedJSXASCII(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.JSX.Parse = true;
    options.JSX.Preserve = true;
    options.ASCIIOnly = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

// Parses and prints JSX with MinifyWhitespace.
inline void expectPrintedJSXMinify(LiteralString contents, LiteralString expected) {
    config::Options options{};
    options.JSX.Parse = true;
    options.JSX.Preserve = true;
    options.MinifyWhitespace = true;
    expectPrintedCommon(contents.value, expected.value, &options);
}

// ---------------------------------------------------------------------------
// Parse-error assertions (TypeScript)
// ---------------------------------------------------------------------------

// Parses TypeScript source code and asserts that the resulting diagnostic
// output exactly matches the expected string.  This is the most basic
// TypeScript parse helper — it enables TS.Parse with all other options at
// their defaults.
//
// Example:
//   expectParseErrorTS("let x: number = 'str';",
//       "error: Type 'string' is not assignable to type 'number'\n");
inline void expectParseErrorTS(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    expectParseErrorCommon(contents, expected, &options);
}


// Parses TypeScript source with experimental decorators enabled and asserts
// the diagnostic output.  This tests the legacy decorator syntax (the one
// that uses the "experimentalDecorators" tsconfig flag), not the TC39 Stage 3
// decorators.
//
// Example:
//   expectParseErrorExperimentalDecoratorTS(
//       "@sealed\nclass Foo {}",
//       "");
inline void expectParseErrorExperimentalDecoratorTS(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    options.TS.Config.ExperimentalDecorators = config::MaybeBool::kTrue;
    expectParseErrorCommon(contents, expected, &options);
}


// ---------------------------------------------------------------------------
// Print-and-compare assertions (TypeScript)
// ---------------------------------------------------------------------------

// Parses and prints TypeScript source code with specific JS features marked
// as unsupported.  This tests that the printer correctly lowers or transforms
// TypeScript syntax that depends on features unavailable in the target
// environment.
//
// Example:
//   expectPrintedWithUnsupportedFeaturesTS(
//       compat::JSFeature::kTopLevelAwait,
//       "await fetch('/')",
//       ...);
inline void expectPrintedWithUnsupportedFeaturesTS(compat::JSFeature unsupported_features, const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    options.UnsupportedJSFeatures = unsupported_features;
    expectPrintedCommon(contents, expected, &options);
}

// Parses and prints TypeScript source as though targeting a specific
// ECMAScript version.  The version is converted to a Semver and stored in
// UnsupportedJSFeatures so the printer can downgrade syntax accordingly.
//
// Example:
//   expectPrintedTargetTS(2015, "const x = 1;", "const x = 1;\n");
inline void expectParseErrorTargetTS(int es_version, const std::string& contents, const std::string& expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.TS.Parse = true;
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    expectParseErrorCommon(contents, expected, &options);
}

// Parses and prints TypeScript source with default options.  This is the
// TypeScript equivalent of expectPrinted — it verifies that valid TypeScript
// round-trips correctly through the parser and printer.
//
// Example:
//   expectPrintedTS("let x: number = 1;", "let x = 1;\n");
inline void expectPrintedTS(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    expectPrintedCommon(contents, expected, &options);
}

// Parses and prints TypeScript source with UseDefineForClassFields set to
// false.  This simulates the TypeScript "assign" semantics for class fields,
// where class fields are emitted as simple assignments in the constructor
// rather than using Object.defineProperty.
//
// Example:
//   expectPrintedAssignSemanticsTS(
//       "class Foo { x = 1 }",
//       "class Foo {\n  constructor() {\n    this.x = 1;\n  }\n}\n");
inline void expectPrintedAssignSemanticsTS(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    options.TS.Config.UseDefineForClassFields = config::MaybeBool::kFalse;
    expectPrintedCommon(contents, expected, &options);
}

// Same as expectPrintedAssignSemanticsTS but also targeting a specific
// ECMAScript version.  Useful for testing that class field lowering
// interacts correctly with other ES-version-dependent transforms.
//
// Example:
//   expectPrintedAssignSemanticsTargetTS(2015, "class Foo { x = 1 }", ...);
inline void expectPrintedAssignSemanticsTargetTS(int es_version, const std::string& contents, const std::string& expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.TS.Parse = true;
    options.TS.Config.UseDefineForClassFields = config::MaybeBool::kFalse;
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    expectPrintedCommon(contents, expected, &options);
}

// Parses and prints TypeScript source with experimental decorators enabled.
// This tests that the legacy decorator transform produces the expected
// output for decorated classes, methods, and properties.
//
// Example:
//   expectPrintedExperimentalDecoratorTS(
//       "function sealed() {}\n@sealed\nclass Foo {}",
//       "function sealed() {}\nlet Foo = class Foo {};\nFoo = __decorate([sealed], Foo);\n");
inline void expectPrintedExperimentalDecoratorTS(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    options.TS.Config.ExperimentalDecorators = config::MaybeBool::kTrue;
    expectPrintedCommon(contents, expected, &options);
}

// Parses and prints TypeScript source with MinifySyntax enabled.  This
// verifies that the syntax minifier handles TypeScript-specific constructs
// (type annotations, interfaces, etc.) correctly, stripping them and
// producing compact output.
//
// Example:
//   expectPrintedMangleTS("let x: number = 1;", "let x=1;\n");
inline void expectPrintedMangleTS(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    options.MinifySyntax = true;
    expectPrintedCommon(contents, expected, &options);
}


// Parses and prints TypeScript source with both MinifySyntax enabled and
// UseDefineForClassFields set to false.  This tests the interaction between
// syntax minification and the "assign" class field semantics.
//
// Example:
//   expectPrintedMangleAssignSemanticsTS(
//       "class Foo { x = 1 }",
//       "class Foo{constructor(){this.x=1}}\n");
inline void expectPrintedMangleAssignSemanticsTS(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    options.TS.Config.UseDefineForClassFields = config::MaybeBool::kFalse;
    options.MinifySyntax = true;
    expectPrintedCommon(contents, expected, &options);
}

// Parses and prints TypeScript source targeting a specific ECMAScript
// version.  This tests syntax lowering for TypeScript constructs that depend
// on ES features (e.g. enum downlevel, namespace emission).
//
// Example:
//   expectPrintedTargetTS(5, "enum Foo { A, B }", ...);
inline void expectPrintedTargetTS(int es_version, const std::string& contents, const std::string& expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.TS.Parse = true;
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    expectPrintedCommon(contents, expected, &options);
}

// Parses and prints TypeScript source targeting a specific ECMAScript version
// with experimental decorators enabled.  This tests that decorator lowering
// interacts correctly with ES-version-dependent transforms (e.g. decorators
// on ES5 vs ES2015 targets).
//
// Example:
//   expectPrintedTargetExperimentalDecoratorTS(
//       2015, "@sealed class Foo {}", ...);
inline void expectPrintedTargetExperimentalDecoratorTS(int es_version, const std::string& contents, const std::string& expected) {
    compat::Semver semver;
    semver.parts = {static_cast<uint32_t>(es_version)};
    config::Options options{};
    options.TS.Parse = true;
    options.TS.Config.ExperimentalDecorators = config::MaybeBool::kTrue;
    options.UnsupportedJSFeatures = compat::UnsupportedJSFeatures({{compat::Engine::kES, semver}});
    expectPrintedCommon(contents, expected, &options);
}

// ---------------------------------------------------------------------------
// NoAmbiguousLessThan helpers
// ---------------------------------------------------------------------------

// Parses TypeScript source with NoAmbiguousLessThan enabled and asserts the
// diagnostic output.  When NoAmbiguousLessThan is true, the parser resolves
// the ambiguity between the "<" operator and the start of a type parameter
// list in favor of the type parameter.  This can cause parse errors on code
// that would otherwise be valid JavaScript.
//
// Example:
//   expectParseErrorTSNoAmbiguousLessThan(
//       "a < b > (c)",
//       "error: unexpected \"(\" in expression\n");
inline void expectParseErrorTSNoAmbiguousLessThan(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    options.TS.NoAmbiguousLessThan = true;
    expectParseErrorCommon(contents, expected, &options);
}


// Parses and prints TypeScript source with NoAmbiguousLessThan enabled.
// Verifies that expressions containing "<" and ">" that are ambiguous
// between operators and type parameters are correctly resolved and printed.
//
// Example:
//   expectPrintedTSNoAmbiguousLessThan("a < b > c", "a < b > c;\n");
inline void expectPrintedTSNoAmbiguousLessThan(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    options.TS.NoAmbiguousLessThan = true;
    expectPrintedCommon(contents, expected, &options);
}

// ---------------------------------------------------------------------------
// TSX (TypeScript + JSX) helpers
// ---------------------------------------------------------------------------

// Parses TypeScript source with JSX enabled and asserts the diagnostic
// output.  This tests the TSX syntax where JSX elements can appear in
// TypeScript files, combining type checking with JSX transformation.
//
// Example:
//   expectParseErrorTSX("<div>{x</div>",
//       "error: Unexpected \"<\" in expression\n");
inline void expectParseErrorTSX(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    options.JSX.Parse = true;
    expectParseErrorCommon(contents, expected, &options);
}

// Parses and prints TypeScript source with JSX enabled.  This verifies that
// TSX files are correctly parsed, type-stripped, and printed back to
// JavaScript with the JSX syntax preserved.
//
// Example:
//   expectPrintedTSX("let x: number = <div />;", "let x = <div />;\n");
inline void expectPrintedTSX(const std::string& contents, const std::string& expected) {
    config::Options options{};
    options.TS.Parse = true;
    options.JSX.Parse = true;
    expectPrintedCommon(contents, expected, &options);
}
