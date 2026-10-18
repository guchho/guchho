// Tests for guchho_service, the C ABI the WebAssembly package calls to run the
// engine's service dispatch in process.
//
// The dispatch itself is cli_service_compile_test.cpp's problem: every command
// there is answered by the same RunServiceRequest this export wraps. What is
// tested here is the layer this file adds — that a JSON request becomes the
// request the service would have received, that the answer comes back as JSON,
// and that a document this layer cannot read is refused in the one shape the
// host already knows how to read. The bytes envelope is here too, because it is
// the one thing JSON cannot carry on its own and the one place a host passing a
// buffer would otherwise get a lex of the literal base64.
//
// The export is only compiled into the WebAssembly flavor, but it is a handful
// of lines over RunServiceRequest and needs no Emscripten to exercise. The test
// target compiles src/cli/wasm_abi.cpp for exactly this reason, so the wrapper
// is checked by the same suite as everything it wraps.

#include "test/guchho_test.hpp"

#include <optional>
#include <string>

#include "guchho/service.hpp"

// Declared here rather than in a header: the export has no public header, the
// way the other C ABI exports do not, because the only caller is generated
// JavaScript that reaches it by name.
extern "C" const char* guchho_service(const char* request_json);

namespace cli::test {

namespace {

using guchho::service::Value;

// The response for a raw request document, parsed back into a value. A
// response that is not a document at all is what JSONToValue returns nullopt
// for, which the tests turn into a failure by reading it further.
std::optional<Value> Send(const std::string& request_json) {
    return guchho::service::JSONToValue(guchho_service(request_json.c_str()));
}

// The "error" string on a response, or "" when there is none.
std::string ErrorOf(const Value& response) {
    const Value* error = response.Find("error");
    return error != nullptr && error->IsString() ? error->AsString() : std::string();
}

// The number of items in an array field, or -1 when the field is missing.
int CountOf(const Value& response, const std::string& key) {
    const Value* field = response.Find(key);
    if (field == nullptr || !field->IsArray()) return -1;
    return static_cast<int>(field->AsArray().size());
}

} // namespace

TEST(WasmAbi, LexHtmlAnswersWithTheTokensTheEngineProduces) {
    const std::optional<Value> response =
        Send(R"({"command":"lex-html","input":"<a>"})");

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->IsObject());
    EXPECT_EQ(ErrorOf(*response), std::string());
    // A start tag and the end-of-file marker: "<a>" is one tag, and the lexer
    // always closes with an eof token.
    EXPECT_EQ(CountOf(*response, "tokens"), 2);

    const Value* tokens = response->Find("tokens");
    ASSERT_TRUE(tokens != nullptr && tokens->IsArray());
    const Value* first = tokens->At(0);
    ASSERT_TRUE(first != nullptr);
    const Value* kind = first->Find("kind");
    ASSERT_TRUE(kind != nullptr && kind->IsString());
    EXPECT_EQ(kind->AsString(), std::string("start-tag"));
}

TEST(WasmAbi, ARequestThatIsNotJsonIsRefusedInTheUsualShape) {
    const std::optional<Value> response = Send("not json");

    ASSERT_TRUE(response.has_value());
    EXPECT_NE(ErrorOf(*response), std::string());
}

TEST(WasmAbi, ARequestThatIsNotAnObjectIsRefused) {
    const std::optional<Value> response = Send("[1,2,3]");

    ASSERT_TRUE(response.has_value());
    EXPECT_NE(ErrorOf(*response), std::string());
}

TEST(WasmAbi, AnEmptyRequestIsRefused) {
    const std::optional<Value> response = Send("");

    ASSERT_TRUE(response.has_value());
    EXPECT_NE(ErrorOf(*response), std::string());
}

TEST(WasmAbi, AnUnknownCommandIsRefusedByTheEngine) {
    const std::optional<Value> response = Send(R"({"command":"lex-klingon"})");

    ASSERT_TRUE(response.has_value());
    EXPECT_NE(ErrorOf(*response), std::string());
}

TEST(WasmAbi, BytesTravelAsAnEnvelopeAndBecomeTheLexersInput) {
    // "<a>" is 0x3C 0x61 0x3E, which base64 spells "PGE+". Sent as an envelope,
    // it has to reach the lexer as bytes and lex to the same two tokens the
    // string form does.
    const std::optional<Value> response = Send(
        R"({"command":"lex-html","input":{"__bytes__":"PGE+"}})");

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(ErrorOf(*response), std::string());
    EXPECT_EQ(CountOf(*response, "tokens"), 2);
}

TEST(WasmAbi, AnEnvelopeThatIsNotBase64IsRefused) {
    const std::optional<Value> response = Send(
        R"({"command":"lex-html","input":{"__bytes__":"not base64!"}})");

    ASSERT_TRUE(response.has_value());
    EXPECT_NE(ErrorOf(*response), std::string());
}

TEST(WasmAbi, ParseAndPrintAnswerThroughTheSameHandle) {
    const std::optional<Value> parsed =
        Send(R"({"command":"parse-js","input":"const a = 1;"})");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(ErrorOf(*parsed), std::string());

    const Value* id = parsed->Find("id");
    ASSERT_TRUE(id != nullptr && id->IsNumber());

    const std::string print_request =
        std::string("{\"command\":\"print-js\",\"ast\":") +
        std::to_string(id->AsNumber()) + "}";
    const std::optional<Value> printed = Send(print_request);

    ASSERT_TRUE(printed.has_value());
    EXPECT_EQ(ErrorOf(*printed), std::string());
    const Value* code = printed->Find("code");
    ASSERT_TRUE(code != nullptr && code->IsString());
    EXPECT_EQ(code->AsString(), std::string("const a = 1;\n"));
}

} // namespace cli::test
