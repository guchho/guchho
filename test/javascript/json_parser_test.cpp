#include "test/guchho_test.hpp"

#include "guchho/javascript/js_parser.hpp"
#include "guchho/logger.hpp"

#include <string>
#include <utility>
#include <vector>

namespace js = guchho::javascript;

using guchho::logger::DeferLogKind;
using guchho::logger::Log;
using guchho::logger::NewDeferLog;
using guchho::logger::Source;

namespace {

Source MakeSource(std::string contents) {
    Source s;
    s.index = 0;
    s.identifier_name = "stdin";
    s.pretty_paths = {"<stdin>", "<stdin>"};
    s.key_path = guchho::logger::Path{"<stdin>", {}, {}, {}, {}};
    s.contents = std::move(contents);
    return s;
}

std::string ParseJSONError(const std::string& input) {
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    js::JSONOptions options{};
    auto [result, ok] = js::Parser::ParseJSON(log, MakeSource(input), options);
    (void)result;
    (void)ok;
    auto msgs = log.done();
    std::string text;
    for (const auto& msg : msgs) {
        text += msg.String(guchho::logger::OutputOptions{}, guchho::logger::TerminalInfo{});
    }
    return text;
}

bool ParseJSONOk(const std::string& input) {
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    js::JSONOptions options{};
    auto [result, ok] = js::Parser::ParseJSON(log, MakeSource(input), options);
    (void)result;
    return ok;
}

bool ParseJSONC(const std::string& input, std::string& error_output) {
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    js::JSONOptions options{};
    options.flavor = js::JSONFlavor::kTSConfigJSON;
    auto [result, ok] = js::Parser::ParseJSON(log, MakeSource(input), options);
    (void)result;
    auto msgs = log.done();
    error_output.clear();
    for (const auto& msg : msgs) {
        error_output += msg.String(guchho::logger::OutputOptions{}, guchho::logger::TerminalInfo{});
    }
    return ok;
}

} // namespace

TEST(JSONParser, TestStrings) {
    EXPECT_TRUE(ParseJSONOk("\"hello\""));
    EXPECT_TRUE(ParseJSONOk("\"\""));
    EXPECT_TRUE(ParseJSONOk("\"with\\nescape\""));
    EXPECT_TRUE(ParseJSONOk("\"with\\u0041unicode\""));
}

TEST(JSONParser, TestNumbers) {
    EXPECT_TRUE(ParseJSONOk("42"));
    EXPECT_TRUE(ParseJSONOk("0"));
    EXPECT_TRUE(ParseJSONOk("3.14"));
    EXPECT_TRUE(ParseJSONOk("-1"));
    EXPECT_TRUE(ParseJSONOk("-0"));
    EXPECT_TRUE(ParseJSONOk("1e10"));
    EXPECT_TRUE(ParseJSONOk("1.5e-3"));
    EXPECT_TRUE(ParseJSONOk("1E+2"));
}

TEST(JSONParser, TestBooleans) {
    EXPECT_TRUE(ParseJSONOk("true"));
    EXPECT_TRUE(ParseJSONOk("false"));
}

TEST(JSONParser, TestNull) {
    EXPECT_TRUE(ParseJSONOk("null"));
}

TEST(JSONParser, TestArrays) {
    EXPECT_TRUE(ParseJSONOk("[]"));
    EXPECT_TRUE(ParseJSONOk("[1]"));
    EXPECT_TRUE(ParseJSONOk("[1, 2, 3]"));
    EXPECT_TRUE(ParseJSONOk("[\"a\", true, null, 42]"));
    EXPECT_TRUE(ParseJSONOk("[[1, 2], [3, 4]]"));
    EXPECT_TRUE(ParseJSONOk("[{\"key\": \"value\"}]"));
}

TEST(JSONParser, TestObjects) {
    EXPECT_TRUE(ParseJSONOk("{}"));
    EXPECT_TRUE(ParseJSONOk("{\"key\": \"value\"}"));
    EXPECT_TRUE(ParseJSONOk("{\"a\": 1, \"b\": 2, \"c\": 3}"));
    EXPECT_TRUE(ParseJSONOk("{\"nested\": {\"inner\": true}}"));
    EXPECT_TRUE(ParseJSONOk("{\"arr\": [1, 2, 3]}"));
}

TEST(JSONParser, TestNestedStructures) {
    EXPECT_TRUE(ParseJSONOk("{\"users\": [{\"name\": \"Alice\", \"age\": 30}, {\"name\": \"Bob\", \"age\": 25}]}"));
    EXPECT_TRUE(ParseJSONOk("[{\"a\": [1, {\"b\": 2}], \"c\": {\"d\": [3, 4]}}]"));
}

TEST(JSONParser, TestErrors) {
    EXPECT_NE(ParseJSONError(""), "");
    EXPECT_NE(ParseJSONError("{"), "");
    EXPECT_NE(ParseJSONError("}"), "");
    EXPECT_NE(ParseJSONError("["), "");
    EXPECT_NE(ParseJSONError("]"), "");
    EXPECT_NE(ParseJSONError("undefined"), "");
    EXPECT_NE(ParseJSONError("NaN"), "");
    EXPECT_NE(ParseJSONError("Infinity"), "");
    EXPECT_NE(ParseJSONError("1, 2"), "");
    EXPECT_NE(ParseJSONError("{\"key\": }"), "");
    EXPECT_NE(ParseJSONError("{\"key\" }"), "");
}

TEST(JSONParser, TestTrailingCommasJSON) {
    EXPECT_EQ(ParseJSONError("[1, 2, 3,]"), "<stdin>: ERROR: JSON does not support trailing commas\n");
    EXPECT_EQ(ParseJSONError("{\"a\": 1,}"), "<stdin>: ERROR: JSON does not support trailing commas\n");
}

TEST(JSONParser, TestTrailingCommasJSONC) {
    std::string error;
    EXPECT_TRUE(ParseJSONC("[1, 2, 3,]", error));
    EXPECT_EQ(error, "");

    EXPECT_TRUE(ParseJSONC("{\"a\": 1,}", error));
    EXPECT_EQ(error, "");
}

TEST(JSONParser, TestEmptyInput) {
    EXPECT_FALSE(ParseJSONOk(""));
}

TEST(JSONParser, TestParseGlobalName) {
    auto TestGlobal = [](const std::string& input, const std::vector<std::string>& expected_segments, bool expected_ok) {
        Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});
        auto [segments, ok] = js::ParseGlobalName(log, MakeSource(input));
        EXPECT_EQ(ok, expected_ok);
        if (expected_ok) {
            EXPECT_EQ(segments.size(), expected_segments.size());
            for (size_t i = 0; i < std::min(segments.size(), expected_segments.size()); ++i) {
                EXPECT_EQ(segments[i], expected_segments[i]);
            }
        }
    };

    TestGlobal("process", {"process"}, true);
    TestGlobal("process.env", {"process", "env"}, true);
    TestGlobal("process.env.NODE_ENV", {"process", "env", "NODE_ENV"}, true);
    TestGlobal("this", {"this"}, true);
    TestGlobal("import.meta.url", {"import", "meta", "url"}, true);
    TestGlobal("a.b['c']", {"a", "b", "c"}, true);
    TestGlobal("a.b[\"c\"]", {"a", "b", "c"}, true);

    TestGlobal("a.", {}, false);
    TestGlobal("a[", {}, false);
    TestGlobal("[a]", {}, false);
}

TEST(JSONParser, TestComplexValidJSON) {
    std::string input = R"({
        "name": "test",
        "version": "1.0.0",
        "description": "A test package",
        "main": "index.js",
        "scripts": {
            "start": "node index.js",
            "test": "jest"
        },
        "dependencies": {
            "express": "^4.18.0",
            "lodash": "^4.17.21"
        },
        "keywords": ["test", "javascript", "json"],
        "author": {
            "name": "Test Author",
            "email": "test@example.com"
        },
        "license": "MIT",
        "active": true,
        "count": 42,
        "pi": 3.14159,
        "nothing": null
    })";

    EXPECT_TRUE(ParseJSONOk(input));
}
