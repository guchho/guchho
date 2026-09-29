// Tests for the service: the engine reached over a pipe rather than over a
// command line.
//
// The service is what the npm package talks to, and the reason it exists is
// that a build's outputs and its diagnostics are wanted as data rather than as
// files and as prose. Getting that wrong is invisible from the outside in a
// particular way: the command line can be completely correct while the service
// answers a request with the wrong field, or with a message the host cannot
// parse, or with nothing at all.
//
// So the framing is tested as framing (a packet written and read back), and
// every command is tested through RunServiceRequest — the dispatch without the
// pipe. That split is deliberate: it means a failure here is either "the
// protocol cannot carry this" or "the command answers wrongly", and never
// "the child process would not start".
//
// The other half of the protocol, the child process and the frames on a real
// pipe, is the npm suite's problem (package/npm/test/api-*.test.*) because the
// other side of it is JavaScript. What is checked here is what this side sends.

#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <string>
#include <vector>

#include "guchho/service.hpp"

namespace cli::test {

using guchho::test::CliWorkspace;

namespace {

using guchho::service::Packet;
using guchho::service::PacketReader;
using guchho::service::Value;

// A request with a command and nothing else.
Value Request(const std::string& command) {
    return Value::Object({{"command", Value::String(command)}});
}

// The "error" string on a response, or "" when there is none. Used to check that
// a refusal is a refusal and says something.
std::string ErrorOf(const Value& response) {
    const Value* error = response.Find("error");
    return error != nullptr && error->IsString() ? error->AsString() : std::string();
}

// The number of items in an array field, or -1 when the field is missing or is
// not an array.
int CountOf(const Value& response, const std::string& key) {
    const Value* field = response.Find(key);
    if (field == nullptr || !field->IsArray()) return -1;
    return static_cast<int>(field->AsArray().size());
}

// The whole of a transform's "code", as text. The service sends bytes because a
// program is bytes before it is text, and the host decodes them.
std::string CodeOf(const Value& response) {
    const Value* code = response.Find("code");
    if (code == nullptr || !code->IsBytes()) return std::string();
    const std::vector<uint8_t>& bytes = code->AsBytes();
    return std::string(bytes.begin(), bytes.end());
}

// The contents of the first output file, as text.
std::string FirstOutputOf(const Value& response) {
    const Value* files = response.Find("outputFiles");
    if (files == nullptr || !files->IsArray() || files->AsArray().empty()) return std::string();
    const Value& first = files->AsArray().front();
    const Value* contents = first.Find("contents");
    if (contents == nullptr || !contents->IsBytes()) return std::string();
    const std::vector<uint8_t>& bytes = contents->AsBytes();
    return std::string(bytes.begin(), bytes.end());
}

} // namespace

// ---------------------------------------------------------------------------
// The protocol itself
// ---------------------------------------------------------------------------

// A packet written and read back is the same packet.
//
// This is the test that would have caught a decoder that forgot to skip the
// request id, which is a bug that shows up as every value being four bytes early
// — and only for values that are not four bytes long, so the smallest examples
// pass.
TEST(CliService, APacketSurvivesAWriteAndARead) {
    const Value sent = Value::Object({
        {"command", Value::String("transform")},
        {"input", Value::String("let a = 1;")},
        {"count", Value::Number(7)},
        {"flag", Value::Bool(true)},
        {"nothing", Value::Null()},
        {"list", Value::Array({Value::Number(1), Value::String("two")})},
        {"blob", Value::Bytes({0x00, 0xFF, 0x1A})},
    });

    Packet packet;
    packet.is_request = true;
    packet.id = 42;
    packet.value = sent;

    const std::vector<uint8_t> encoded = guchho::service::EncodePacket(packet);

    PacketReader reader;
    const std::vector<Packet> decoded = reader.Feed(encoded.data(), encoded.size());

    ASSERT_FALSE(reader.IsBroken()) << reader.Error();
    ASSERT_EQ(decoded.size(), static_cast<size_t>(1));
    EXPECT_EQ(decoded[0].id, 42);
    EXPECT_TRUE(decoded[0].is_request);

    // Field by field rather than with an equality the Value type does not have.
    const Value& got = decoded[0].value;
    EXPECT_EQ(got.Find("input")->AsString(), std::string("let a = 1;"));
    EXPECT_EQ(got.Find("count")->AsNumber(), 7);
    EXPECT_TRUE(got.Find("flag")->AsBool());
    EXPECT_TRUE(got.Find("nothing")->IsNull());
    EXPECT_EQ(got.Find("list")->AsArray().size(), static_cast<size_t>(2));
    EXPECT_EQ(got.Find("blob")->AsBytes().size(), static_cast<size_t>(3));

    // The exact bytes, because a byte that survives as the wrong value is worse
    // than one that does not survive: 0x1A is end-of-file in a text stream on
    // Windows, which is why the service puts both streams in binary mode.
    EXPECT_EQ(got.Find("blob")->AsBytes()[2], static_cast<uint8_t>(0x1A));
}

// A frame split across two reads is reassembled.
//
// A pipe does not deliver frames, it delivers whatever arrived. Feeding a reader
// half a frame and then the other half is the case that a reader which assumes
// one read is one frame gets wrong, and it gets it wrong only under load.
TEST(CliService, AFrameSplitAcrossTwoReadsIsReassembled) {
    Packet packet;
    packet.is_request = true;
    packet.id = 1;
    packet.value = Value::String("a string long enough to be split in the middle");

    const std::vector<uint8_t> encoded = guchho::service::EncodePacket(packet);
    ASSERT_GT(encoded.size(), static_cast<size_t>(4));

    PacketReader reader;
    const size_t half = encoded.size() / 2;

    const std::vector<Packet> first = reader.Feed(encoded.data(), half);
    EXPECT_EQ(first.size(), static_cast<size_t>(0)) << "half a frame is not a frame";

    const std::vector<Packet> second = reader.Feed(encoded.data() + half, encoded.size() - half);
    ASSERT_EQ(second.size(), static_cast<size_t>(1));
    EXPECT_EQ(second[0].value.AsString(),
              std::string("a string long enough to be split in the middle"));
}

// Two frames in one read are two frames.
//
// The other half of the same problem, and the reason the reader is a loop rather
// than an if.
TEST(CliService, TwoFramesInOneReadAreTwoPackets) {
    std::vector<uint8_t> both;

    for (uint32_t id = 0; id < 2; id++) {
        Packet packet;
        packet.is_request = true;
        packet.id = id;
        packet.value = Value::Number(static_cast<int32_t>(id));
        const std::vector<uint8_t> encoded = guchho::service::EncodePacket(packet);
        both.insert(both.end(), encoded.begin(), encoded.end());
    }

    PacketReader reader;
    const std::vector<Packet> decoded = reader.Feed(both.data(), both.size());

    ASSERT_EQ(decoded.size(), static_cast<size_t>(2));
    EXPECT_EQ(decoded[0].value.AsNumber(), 0);
    EXPECT_EQ(decoded[1].value.AsNumber(), 1);
}

// The greeting is a length and the version text, and not a packet.
//
// The host reads it before it decodes anything, because the one thing a host has
// to decide first is whether the process it started speaks its protocol.
TEST(CliService, TheGreetingIsALengthAndTheVersion) {
    const std::vector<uint8_t> frame = guchho::service::EncodeVersionFrame("1");

    ASSERT_EQ(frame.size(), static_cast<size_t>(5));
    EXPECT_EQ(frame[4], static_cast<uint8_t>('1'));

    // Little-endian, so the length is the first four bytes and reads back as the
    // number of bytes that follow.
    PacketReader reader;
    const std::vector<Packet> frames = reader.Feed(frame.data(), frame.size());
    EXPECT_EQ(frames.size(), static_cast<size_t>(0)) << "the greeting is not a packet";

    EXPECT_EQ(guchho::service::kVersion, std::string("1"));
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

// An unknown command is refused rather than ignored.
//
// A service that silently did nothing for a command it did not know would hang
// its host on the request id forever, because a response is the only thing that
// ends a request.
TEST(CliService, AnUnknownCommandIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(Request("not-a-command"));

    EXPECT_FALSE(ErrorOf(response).empty()) << "an unknown command should say so";
}

// A request with no command at all is refused.
TEST(CliService, ARequestWithNoCommandIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(Value::Object({}));

    EXPECT_FALSE(ErrorOf(response).empty());
}

// A request that is not an object is refused.
//
// A host sends what it was given, and a build called with a string is a mistake
// in a host rather than in a project. It has to be refused rather than crash the
// service, because the service is shared by every request that follows.
TEST(CliService, ARequestThatIsNotAnObjectIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(Value::String("build"));

    EXPECT_FALSE(ErrorOf(response).empty());
}

// A refused request still carries "errors", so a host has one shape to read.
//
// The distinction the host cares about is "the code being built is wrong" versus
// "the installation is wrong", and both arrive with errors so that the first
// case can be rendered without a special case.
TEST(CliService, ARefusalCarriesTheErrorsFieldToo) {
    const Value response = guchho::cli::RunServiceRequest(Request("wat"));

    EXPECT_TRUE(response.Find("errors") != nullptr);
    EXPECT_TRUE(response.Find("errors")->IsArray());
}

// ---------------------------------------------------------------------------
// transform
// ---------------------------------------------------------------------------

// Source in, program out.
TEST(CliService, TransformReturnsTheProgramAsBytes) {
    const Value response = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("transform")},
        {"input", Value::String("const answer: number = 42;\n")},
        {"loader", Value::String("ts")},
    }));

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_EQ(CountOf(response, "errors"), 0);

    const std::string code = CodeOf(response);
    EXPECT_TRUE(code.find("answer") != std::string::npos) << code;

    // The loader was read: a type annotation that survived would mean "ts" was
    // ignored and the program was parsed as JavaScript.
    EXPECT_TRUE(code.find(": number") == std::string::npos) << code;
}

// A transform with no input field at all is refused.
//
// The field's absence means the host forgot it, not that the source is empty.
// The two have to be told apart, because a request that says "transform nothing"
// and gets "here is nothing" back is a successful no-op a host cannot see. An
// explicit empty string is a real transform and is tested below.
TEST(CliService, TransformWithNoInputIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(Request("transform"));

    EXPECT_FALSE(ErrorOf(response).empty());
}

// An explicitly empty input is a real transform, not a refusal.
//
// This is the other half of the test above, and the reason the refusal keys on
// the field being absent rather than on it being empty: transforming the empty
// string is what a build system does to a file that happens to have no
// statements, and there is nothing wrong with it.
TEST(CliService, TransformOfEmptyInputSucceeds) {
    const Value response = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("transform")},
        {"input", Value::String("")},
        {"loader", Value::String("js")},
    }));

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_EQ(CountOf(response, "errors"), 0);
    EXPECT_TRUE(CodeOf(response).empty());
}

// An input of the wrong type is refused rather than ignored.
//
// A number where the source should be is a host that built its request wrongly,
// and taking it as the empty string would answer as though the code were empty.
TEST(CliService, TransformWithANonStringInputIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("transform")},
        {"input", Value::Number(42)},
    }));

    EXPECT_FALSE(ErrorOf(response).empty());
}

// A transform that fails reports the failure as errors rather than as an error.
//
// This is the difference the host's whole error handling rests on. A syntax
// error is a diagnostic about the code, with a location a person can act on; it
// is not a protocol failure, and reporting it as one would make a broken file
// look like a broken install.
TEST(CliService, TransformReportsASyntaxErrorAsDiagnostics) {
    Value request = Value::Object({
        {"command", Value::String("transform")},
        {"input", Value::String("const a = (;\n")},
        {"loader", Value::String("js")},
    });

    const Value response = guchho::cli::RunServiceRequest(request);

    EXPECT_TRUE(ErrorOf(response).empty()) << "a syntax error is not a protocol error";
    EXPECT_GT(CountOf(response, "errors"), 0);
}

// ---------------------------------------------------------------------------
// build
// ---------------------------------------------------------------------------

// A build with write:false collects its outputs instead of writing them.
//
// The option the npm package's in-memory build is built on, and the one that
// cannot be checked from the command line at all.
TEST(CliService, BuildWithWriteFalseCollectsOutputs) {
    CliWorkspace ws("svc-build");
    ws.Write("src/entry.js", "export const answer = 42;\n");

    const Value request = Value::Object({
        {"command", Value::String("build")},
        {"write", Value::Bool(false)},
        {"absWorkingDir", Value::String(ws.path())},
        {"entries", Value::Array({Value::Array({Value::Null(), Value::String("src/entry.js")})})},
        {"flags", Value::Array({Value::String("--bundle"), Value::String("--format=esm")})},
    });

    const Value response = guchho::cli::RunServiceRequest(request);

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_EQ(CountOf(response, "errors"), 0);

    EXPECT_EQ(CountOf(response, "outputFiles"), 1);

    const std::string output = FirstOutputOf(response);
    EXPECT_TRUE(output.find("42") != std::string::npos) << output;

    // The whole point of write:false. A build that wrote anyway would make the
    // option a lie that only shows up as a stray directory much later.
    EXPECT_FALSE(std::filesystem::exists(ws.At("dist"))) << "write:false wrote to disk";
}

// A build with no entry points at all falls back to a glob, finds nothing, and
// reports success having built nothing.
//
// This is deliberately not a refusal here. The rule that a build has to name
// something is the JavaScript layer's (package/npm/guchho/lib/build.js), which
// refuses before it sends a request because it is the layer that knows whether
// a "stdin" build was asked for. Pinning the service's lenient behaviour is the
// point of the test: it is what makes the two layers two layers rather than one
// rule written twice, where the second copy is the one that drifts.
TEST(CliService, BuildWithNoEntryPointsFallsBackToAGlob) {
    CliWorkspace ws("svc-build-empty");

    const Value request = Value::Object({
        {"command", Value::String("build")},
        {"write", Value::Bool(false)},
        {"absWorkingDir", Value::String(ws.path())},
    });

    const Value response = guchho::cli::RunServiceRequest(request);

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_EQ(CountOf(response, "errors"), 0);

    // Nothing was built, and the field is absent rather than an empty array
    // because BuildResponse only adds it when there is something in it.
    EXPECT_TRUE(response.Find("outputFiles") == nullptr) << "an empty glob should produce no files";
}

// A build that cannot resolve its entry point reports that as diagnostics.
TEST(CliService, BuildReportsAMissingEntryPointAsDiagnostics) {
    CliWorkspace ws("svc-build-missing");

    const Value request = Value::Object({
        {"command", Value::String("build")},
        {"write", Value::Bool(false)},
        {"absWorkingDir", Value::String(ws.path())},
        {"entries", Value::Array({Value::Array({Value::Null(), Value::String("src/nope.js")})})},
    });

    const Value response = guchho::cli::RunServiceRequest(request);

    EXPECT_GT(CountOf(response, "errors"), 0);
}

// A build asked for a metafile gets one, as text.
//
// It is JSON text rather than a decoded object because the host already has a
// JSON parser and this side does not need a second one. The host parses; the
// service does not serialize a tree it would have to walk anyway.
TEST(CliService, BuildReturnsAMetafileAsJSONText) {
    CliWorkspace ws("svc-build-metafile");
    ws.Write("src/entry.js", "export const answer = 42;\n");

    const Value request = Value::Object({
        {"command", Value::String("build")},
        {"write", Value::Bool(false)},
        {"metafile", Value::Bool(true)},
        {"absWorkingDir", Value::String(ws.path())},
        {"entries", Value::Array({Value::Array({Value::Null(), Value::String("src/entry.js")})})},
    });

    const Value response = guchho::cli::RunServiceRequest(request);

    const Value* metafile = response.Find("metafile");
    ASSERT_TRUE(metafile != nullptr);
    ASSERT_TRUE(metafile->IsString());

    // Not compared whole: the point is that it is JSON, and that it is the
    // metafile rather than a summary of one.
    EXPECT_TRUE(metafile->AsString().find("\"outputs\"") != std::string::npos)
        << metafile->AsString();
}

// Two builds in a row answer two requests.
//
// The service is a loop and the engine keeps caches between requests, which is
// the whole reason it exists. A second build that answered with the first one's
// output would be the failure this is here for, and it is one a stateless
// wrapper cannot have — which is exactly why it needs a test.
TEST(CliService, TwoBuildsAnswerTwoRequests) {
    CliWorkspace ws("svc-build-twice");
    ws.Write("src/entry.js", "export const answer = 1;\n");

    const auto build = [&ws]() {
        const Value request = Value::Object({
            {"command", Value::String("build")},
            {"write", Value::Bool(false)},
            {"absWorkingDir", Value::String(ws.path())},
            {"entries", Value::Array({Value::Array({Value::Null(), Value::String("src/entry.js")})})},
        });
        return guchho::cli::RunServiceRequest(request);
    };

    const std::string first = FirstOutputOf(build());

    ws.Write("src/entry.js", "export const answer = 'changed';\n");
    const std::string second = FirstOutputOf(build());

    EXPECT_TRUE(first.find("changed") == std::string::npos) << first;
    EXPECT_TRUE(second.find("changed") != std::string::npos) << second;
}

// ---------------------------------------------------------------------------
// The contexts
// ---------------------------------------------------------------------------

// A context outlives the request that made it, and rebuilds reflect a change.
//
// This is the reason the service holds state at all. A build is one request and
// throws away what it learned; a context is what makes the second build of an
// edited file cheaper than the first, and the thing being tested is that the
// state is really kept and really used.
TEST(CliService, AContextRebuildsAndPicksUpAChange) {
    CliWorkspace ws("svc-context");
    ws.Write("src/entry.js", "export const answer = 'first';\n");

    const Value made = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("context")},
        {"write", Value::Bool(false)},
        {"absWorkingDir", Value::String(ws.path())},
        {"entries", Value::Array({Value::Array({Value::Null(), Value::String("src/entry.js")})})},
        {"flags", Value::Array({Value::String("--bundle"), Value::String("--format=esm")})},
    }));

    const Value* key = made.Find("key");
    ASSERT_TRUE(key != nullptr) << ErrorOf(made);
    ASSERT_TRUE(key->IsNumber());
    const int32_t context_key = key->AsNumber();

    ws.Write("src/entry.js", "export const answer = 'second';\n");

    const Value rebuilt = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("rebuild")},
        {"key", Value::Number(context_key)},
    }));

    EXPECT_EQ(CountOf(rebuilt, "errors"), 0);
    const std::string output = FirstOutputOf(rebuilt);
    EXPECT_TRUE(output.find("second") != std::string::npos) << output;

    // Released so the watcher and any server stop with the test.
    const Value disposed = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("dispose")},
        {"key", Value::Number(context_key)},
    }));
    EXPECT_TRUE(ErrorOf(disposed).empty()) << ErrorOf(disposed);
}

// A rebuild that names no context is refused.
//
// Rather than treated as "the most recent one", which would make two contexts in
// a host build each other.
TEST(CliService, ARebuildWithNoContextIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(Request("rebuild"));

    EXPECT_FALSE(ErrorOf(response).empty());
}

// A rebuild of a context that does not exist is refused.
//
// The alternative is a crash, and the service is shared: one bad request taking
// the process with it would take every build after it.
TEST(CliService, ARebuildOfAnUnknownContextIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("rebuild")},
        {"key", Value::Number(999999)},
    }));

    EXPECT_FALSE(ErrorOf(response).empty());
}

// Disposing a context twice does not crash.
//
// A host releases a context from a finally block as often as from a caller, and
// a teardown path that can fail is a teardown path that will.
TEST(CliService, DisposingAContextTwiceIsSafe) {
    CliWorkspace ws("svc-context-double");
    ws.Write("src/entry.js", "export const answer = 1;\n");

    const Value made = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("context")},
        {"write", Value::Bool(false)},
        {"absWorkingDir", Value::String(ws.path())},
        {"entries", Value::Array({Value::Array({Value::Null(), Value::String("src/entry.js")})})},
    }));

    const Value* key = made.Find("key");
    ASSERT_TRUE(key != nullptr) << ErrorOf(made);
    const int32_t context_key = key->AsNumber();

    const Value dispose = Value::Object({
        {"command", Value::String("dispose")},
        {"key", Value::Number(context_key)},
    });

    guchho::cli::RunServiceRequest(dispose);
    const Value again = guchho::cli::RunServiceRequest(dispose);
    (void)again;

    // The second dispose is allowed to complain that there is nothing there —
    // what it must not do is take the service with it. The check is that a
    // request after it still gets an answer, because the service is shared and
    // one bad request would otherwise take every build that followed.
    const Value after = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("transform")},
        {"input", Value::String("let a = 1;\n")},
        {"loader", Value::String("js")},
    }));

    EXPECT_TRUE(ErrorOf(after).empty()) << ErrorOf(after);
    EXPECT_TRUE(CodeOf(after).find("a") != std::string::npos);
}

// ---------------------------------------------------------------------------
// format-msgs
// ---------------------------------------------------------------------------

// A message is formatted into the sentence a person reads.
TEST(CliService, FormatMessagesReturnsPrintableText) {
    const Value message = Value::Object({
        {"text", Value::String("something is wrong")},
        {"location", Value::Object({
            {"file", Value::String("src/entry.js")},
            {"namespace", Value::String("file")},
            {"line", Value::Number(1)},
            {"column", Value::Number(0)},
            {"length", Value::Number(1)},
            {"lineText", Value::String("x")},
            {"suggestion", Value::String("")},
        })},
    });

    const Value response = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("format-msgs")},
        {"messages", Value::Array({message})},
        {"kind", Value::String("error")},
        {"color", Value::Bool(false)},
    }));

    const Value* logs = response.Find("logs");
    ASSERT_TRUE(logs != nullptr) << ErrorOf(response);
    ASSERT_TRUE(logs->IsArray());
    ASSERT_EQ(logs->AsArray().size(), static_cast<size_t>(1));

    const std::string text = logs->AsArray().front().AsString();
    EXPECT_TRUE(text.find("something is wrong") != std::string::npos) << text;
    EXPECT_TRUE(text.find("src/entry.js") != std::string::npos) << text;
}

// format-msgs with no messages is refused rather than answering with nothing.
TEST(CliService, FormatMessagesWithNoMessagesIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(Request("format-msgs"));

    EXPECT_FALSE(ErrorOf(response).empty());
}

// ---------------------------------------------------------------------------
// analyze-metafile
// ---------------------------------------------------------------------------

// A metafile is pretty-printed, and the result is text.
TEST(CliService, AnalyzeMetafileReturnsText) {
    const Value response = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("analyze-metafile")},
        {"metafile", Value::String("{\"inputs\":{},\"outputs\":{}}")},
    }));

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);

    const Value* text = response.Find("text");
    ASSERT_TRUE(text != nullptr);
    EXPECT_TRUE(text->IsString());
}

// A metafile that is not JSON is not an error; it produces no report.
//
// The API is documented this way (include/guchho/api.hpp:1883): the text is not
// a metafile, so there is nothing to analyze, and an empty report is the answer
// rather than a failure. A caller reading a metafile off disk and handing it
// over should not have to tell "wrong file" apart from "build broke" by
// catching.
TEST(CliService, AnalyzeMetafileOfNonsenseProducesNoText) {
    const Value response = guchho::cli::RunServiceRequest(Value::Object({
        {"command", Value::String("analyze-metafile")},
        {"metafile", Value::String("this is not json")},
    }));

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);

    const Value* text = response.Find("text");
    ASSERT_TRUE(text != nullptr);
    EXPECT_TRUE(text->IsString());
    EXPECT_TRUE(text->AsString().empty()) << text->AsString();
}

// A request with no metafile at all is refused.
//
// An absent field is a host that forgot it, which the empty report above would
// otherwise hide as "this metafile has no outputs".
TEST(CliService, AnalyzeMetafileWithNoMetafileIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(Request("analyze-metafile"));

    EXPECT_FALSE(ErrorOf(response).empty());
}

} // namespace cli::test
