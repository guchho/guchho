#include "test/helpers/javascript_test.hpp"

TEST(JsParser, TestJSONAtom) {
    expectPrintedJSON("false", "false");
    expectPrintedJSON("true", "true");
    expectPrintedJSON("null", "null");
    expectParseErrorJSON("undefined", "<stdin>: ERROR: Unexpected \"undefined\" in JSON\n");
}

TEST(JsParser, TestJSONString) {
    expectPrintedJSON("\"x\"", "\"x\"");
    expectParseErrorJSON("'x'", "<stdin>: ERROR: JSON strings must use double quotes\n");
    expectParseErrorJSON("`x`", "<stdin>: ERROR: Unexpected \"`x`\" in JSON\n");

    // Newlines
    expectPrintedJSON("\"\u2028\"", "\"\\u2028\"");
    expectPrintedJSON("\"\u2029\"", "\"\\u2029\"");
    expectParseErrorJSON("\"\r\"", "<stdin>: ERROR: Unterminated string literal\n");
    expectParseErrorJSON("\"\n\"", "<stdin>: ERROR: Unterminated string literal\n");

    // Control characters
    for (int c = 0; c < 0x20; c++) {
        if (c != '\r' && c != '\n') {
            char message[64];
            std::snprintf(message, sizeof(message), "<stdin>: ERROR: Syntax error \"\\x%02X\"\n", c);
            expectParseErrorJSON("\"" + std::string(1, static_cast<char>(c)) + "\"", message);
        }
    }

    // Valid escapes
    expectPrintedJSON("\"\\\"\"", "'\"'");
    expectPrintedJSON("\"\\\\\"", "\"\\\\\"");
    expectPrintedJSON("\"\\/\"", "\"/\"");
    expectPrintedJSON("\"\\b\"", "\"\\b\"");
    expectPrintedJSON("\"\\f\"", "\"\\f\"");
    expectPrintedJSON("\"\\n\"", "\"\\n\"");
    expectPrintedJSON("\"\\r\"", "\"\\r\"");
    expectPrintedJSON("\"\\t\"", "\"\t\"");
    expectPrintedJSON("\"\\u0000\"", "\"\\0\"");
    expectPrintedJSON("\"\\u0078\"", "\"x\"");
    expectPrintedJSON("\"\\u1234\"", "\"\u1234\"");
    expectPrintedJSON("\"\\uD800\"", "\"\\uD800\"");
    expectPrintedJSON("\"\\uDC00\"", "\"\\uDC00\"");

    // Invalid escapes
    expectParseErrorJSON("\"\\", "<stdin>: ERROR: Unterminated string literal\n");
    expectParseErrorJSON("\"\\0\"", "<stdin>: ERROR: Syntax error \"0\"\n");
    expectParseErrorJSON("\"\\1\"", "<stdin>: ERROR: Syntax error \"1\"\n");
    expectParseErrorJSON("\"\\'\"", "<stdin>: ERROR: Syntax error \"'\"\n");
    expectParseErrorJSON("\"\\a\"", "<stdin>: ERROR: Syntax error \"a\"\n");
    expectParseErrorJSON("\"\\v\"", "<stdin>: ERROR: Syntax error \"v\"\n");
    expectParseErrorJSON("\"\\\n\"", "<stdin>: ERROR: Syntax error \"\\x0A\"\n");
    expectParseErrorJSON("\"\\x78\"", "<stdin>: ERROR: Syntax error \"x\"\n");
    expectParseErrorJSON("\"\\u{1234}\"", "<stdin>: ERROR: Syntax error \"{\"\n");
    expectParseErrorJSON("\"\\uG\"", "<stdin>: ERROR: Syntax error \"G\"\n");
    expectParseErrorJSON("\"\\uDG\"", "<stdin>: ERROR: Syntax error \"G\"\n");
    expectParseErrorJSON("\"\\uDEG\"", "<stdin>: ERROR: Syntax error \"G\"\n");
    expectParseErrorJSON("\"\\uDEFG\"", "<stdin>: ERROR: Syntax error \"G\"\n");
    expectParseErrorJSON("\"\\u\"", "<stdin>: ERROR: Syntax error '\"'\n");
    expectParseErrorJSON("\"\\uD\"", "<stdin>: ERROR: Syntax error '\"'\n");
    expectParseErrorJSON("\"\\uDE\"", "<stdin>: ERROR: Syntax error '\"'\n");
    expectParseErrorJSON("\"\\uDEF\"", "<stdin>: ERROR: Syntax error '\"'\n");
}

TEST(JsParser, TestJSONNumber) {
    expectPrintedJSON("0", "0");
    expectPrintedJSON("-0", "-0");
    expectPrintedJSON("123", "123");
    expectPrintedJSON("123.456", "123.456");
    expectPrintedJSON("123e20", "123e20");
    expectPrintedJSON("123e-20", "123e-20");
    expectParseErrorJSON("123.", "<stdin>: ERROR: Unexpected \"123.\" in JSON\n");
    expectParseErrorJSON("-123.", "<stdin>: ERROR: Unexpected \"123.\" in JSON\n");
    expectParseErrorJSON(".123", "<stdin>: ERROR: Unexpected \".123\" in JSON\n");
    expectParseErrorJSON("-.123", "<stdin>: ERROR: Unexpected \".123\" in JSON\n");
    expectParseErrorJSON("NaN", "<stdin>: ERROR: Unexpected \"NaN\" in JSON\n");
    expectParseErrorJSON("Infinity", "<stdin>: ERROR: Unexpected \"Infinity\" in JSON\n");
    expectParseErrorJSON("-Infinity", "<stdin>: ERROR: Unexpected \"-\" in JSON\n");
    expectParseErrorJSON("+1", "<stdin>: ERROR: Unexpected \"+\" in JSON\n");
    expectParseErrorJSON("- 1", "<stdin>: ERROR: Unexpected \"-\" in JSON\n");
    expectParseErrorJSON("01", "<stdin>: ERROR: Unexpected \"01\" in JSON\n");
    expectParseErrorJSON("0b1", "<stdin>: ERROR: Unexpected \"0b1\" in JSON\n");
    expectParseErrorJSON("0o1", "<stdin>: ERROR: Unexpected \"0o1\" in JSON\n");
    expectParseErrorJSON("0x1", "<stdin>: ERROR: Unexpected \"0x1\" in JSON\n");
    expectParseErrorJSON("0n", "<stdin>: ERROR: Unexpected \"0n\" in JSON\n");
    expectParseErrorJSON("-01", "<stdin>: ERROR: Unexpected \"01\" in JSON\n");
    expectParseErrorJSON("-0b1", "<stdin>: ERROR: Unexpected \"0b1\" in JSON\n");
    expectParseErrorJSON("-0o1", "<stdin>: ERROR: Unexpected \"0o1\" in JSON\n");
    expectParseErrorJSON("-0x1", "<stdin>: ERROR: Unexpected \"0x1\" in JSON\n");
    expectParseErrorJSON("-0n", "<stdin>: ERROR: Expected number in JSON but found \"0n\"\n");
    expectParseErrorJSON("1_2", "<stdin>: ERROR: Unexpected \"1_2\" in JSON\n");
    expectParseErrorJSON("1.e2", "<stdin>: ERROR: Unexpected \"1.e2\" in JSON\n");
}

TEST(JsParser, TestJSONObject) {
    expectPrintedJSON("{\"x\":0}", "({x:0})");
    expectPrintedJSON("{\"x\":0,\"y\":1}", "({x:0,y:1})");
    expectPrintedJSONWithWarning(
        "{\"x\":0,\"x\":1}",
        "<stdin>: WARNING: Duplicate key \"x\" in object literal\n<stdin>: NOTE: The original key \"x\" is here:\n",
        "({x:0,x:1})");
    expectParseErrorJSON("{\"x\":0,}", "<stdin>: ERROR: JSON does not support trailing commas\n");
    expectParseErrorJSON("{x:0}", "<stdin>: ERROR: Expected string in JSON but found \"x\"\n");
    expectParseErrorJSON("{1:0}", "<stdin>: ERROR: Expected string in JSON but found \"1\"\n");
    expectParseErrorJSON("{[\"x\"]:0}", "<stdin>: ERROR: Expected string in JSON but found \"[\"\n");
}

TEST(JsParser, TestJSONArray) {
    expectPrintedJSON("[]", "[]");
    expectPrintedJSON("[1]", "[1]");
    expectPrintedJSON("[1,2]", "[1,2]");
    expectParseErrorJSON("[,]", "<stdin>: ERROR: Unexpected \",\" in JSON\n");
    expectParseErrorJSON("[,1]", "<stdin>: ERROR: Unexpected \",\" in JSON\n");
    expectParseErrorJSON("[1,]", "<stdin>: ERROR: JSON does not support trailing commas\n");
    expectParseErrorJSON("[1,,2]", "<stdin>: ERROR: Unexpected \",\" in JSON\n");
}

TEST(JsParser, TestJSONInvalid) {
    expectParseErrorJSON("({\"x\":0})", "<stdin>: ERROR: Unexpected \"(\" in JSON\n");
    expectParseErrorJSON("{\"x\":(0)}", "<stdin>: ERROR: Unexpected \"(\" in JSON\n");
    expectParseErrorJSON("#!/usr/bin/env node\n{}", "<stdin>: ERROR: Unexpected \"#!/usr/bin/env node\" in JSON\n");
    expectParseErrorJSON("{\"x\":0}{\"y\":1}", "<stdin>: ERROR: Expected end of file in JSON but found \"{\"\n");
}

TEST(JsParser, TestJSONComments) {
    expectParseErrorJSON("/*comment*/{}", "<stdin>: ERROR: JSON does not support comments\n");
    expectParseErrorJSON("//comment\n{}", "<stdin>: ERROR: JSON does not support comments\n");
    expectParseErrorJSON("{/*comment*/}", "<stdin>: ERROR: JSON does not support comments\n");
    expectParseErrorJSON("{//comment\n}", "<stdin>: ERROR: JSON does not support comments\n");
    expectParseErrorJSON("{}/*comment*/", "<stdin>: ERROR: JSON does not support comments\n");
    expectParseErrorJSON("{}//comment\n", "<stdin>: ERROR: JSON does not support comments\n");
}
