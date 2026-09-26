// Tests for the disk-free part of the public API: Transform, FormatMessages,
// AnalyzeMetafile, and the validation that Build and Context perform before they
// touch the disk. Builds that do read or write files run against a real
// temporary directory, because a build has no filesystem parameter to swap out
// and the behaviour worth checking here — where the output lands, what a rebuild
// sees — is the behaviour of the real thing.

#include "test/guchho_test.hpp"

#include "guchho/api.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace guchho;

namespace {

// A directory that exists for the length of one test and is removed with it,
// contents and all. The name is deliberately not derived from anything the test
// controls, so two tests running at once cannot pick the same directory.
class TempDir {
public:
    explicit TempDir(const std::string& label) {
        static int counter = 0;
        path_ = (std::filesystem::temp_directory_path() /
                 ("guchho-api-test-" + label + "-" +
                  std::to_string(counter++) + "-" +
                  std::to_string(static_cast<long long>(
                      std::filesystem::hash_value(path_seed())))))
            .string();
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::string& path() const { return path_; }

    // A path inside the directory, built from a relative path with forward
    // slashes so a test reads the same on every platform.
    std::string At(const std::string& relative) const {
        return (std::filesystem::path(path_) / std::filesystem::path(relative)).string();
    }

    // Writes "contents" to "relative", creating the directories it needs. Returns
    // the absolute path that was written.
    std::string Write(const std::string& relative, const std::string& contents) const {
        const std::string full = At(relative);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(full).parent_path(), ec);
        std::ofstream out(full, std::ios::binary | std::ios::trunc);
        out << contents;
        out.close();
        return full;
    }

    bool Exists(const std::string& relative) const {
        std::error_code ec;
        return std::filesystem::exists(At(relative), ec);
    }

    std::string Read(const std::string& relative) const {
        std::ifstream in(At(relative), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }

private:
    // A little entropy so two processes running the same test at the same moment
    // still get different directories. Failing that, the counter above is enough
    // for the single-process case that matters.
    static std::string path_seed() {
        const std::time_t now = std::time(nullptr);
        return std::to_string(static_cast<long long>(now));
    }

    std::string path_;
};

std::string to_string(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string first_text(const std::vector<api::Message>& msgs) {
    return msgs.empty() ? std::string() : msgs[0].text;
}

// A build that reports nothing but its own output, so a test looking at errors
// is not distracted by a summary on stderr.
api::LogLevel quiet() {
    return api::LogLevel::kSilent;
}

} // namespace

// ===========================================================================
// Transform
// ===========================================================================

TEST(Api, TransformJSErasesTypesAndKeepsArrow) {
    api::TransformOptions opts;
    opts.loader = api::Loader::kTS;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const f = (x: number) => x * 2;", opts);
    EXPECT_TRUE(r.errors.empty());
    const std::string code = to_string(r.code);
    EXPECT_TRUE(contains(code, "x) => x * 2"));
    EXPECT_FALSE(contains(code, "number"));
}

TEST(Api, TransformMinifyWhitespaceRemovesSpacing) {
    api::TransformOptions opts;
    opts.minify_whitespace = true;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const a = 1 + 2;", opts);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_EQ(to_string(r.code), "const a=1+2;\n");
}

TEST(Api, TransformSourceMapPopulatesMapAndSources) {
    api::TransformOptions opts;
    opts.sourcemap = api::SourceMap::kExternal;
    opts.sourcefile = "in.js";
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const a = 1;", opts);
    EXPECT_TRUE(r.errors.empty());
    const std::string map = to_string(r.map);
    ASSERT_FALSE(map.empty());
    EXPECT_TRUE(contains(map, "\"version\""));
    EXPECT_TRUE(contains(map, "in.js"));
    // The map is a separate artefact, so the code must not have a comment
    // pointing at a file that was never written.
    EXPECT_FALSE(contains(to_string(r.code), "sourceMappingURL"));
}

TEST(Api, TransformNoSourceMapLeavesMapEmpty) {
    api::TransformOptions opts;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const a = 1;", opts);
    EXPECT_TRUE(r.map.empty());
    EXPECT_FALSE(contains(to_string(r.code), "sourceMappingURL"));
}

TEST(Api, TransformLegalCommentsExtractedWhenExternal) {
    api::TransformOptions opts;
    opts.legal_comments = api::LegalComments::kExternal;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("/*! keep me */\nconst a = 1;", opts);
    EXPECT_TRUE(r.errors.empty());
    // The comment is handed back as its own artefact instead of staying in the
    // code, which is the whole difference between "external" and "inline".
    EXPECT_TRUE(contains(to_string(r.legal_comments), "keep me"));
    EXPECT_FALSE(contains(to_string(r.code), "keep me"));
}

TEST(Api, TransformLinkedSourceMapIsRejected) {
    api::TransformOptions opts;
    opts.sourcemap = api::SourceMap::kLinked;
    opts.sourcefile = "in.js";
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const a = 1;", opts);
    EXPECT_FALSE(r.errors.empty());
    EXPECT_EQ(first_text(r.errors), "Cannot transform with linked source maps");
}

TEST(Api, TransformLinkedLegalCommentsIsRejected) {
    api::TransformOptions opts;
    opts.legal_comments = api::LegalComments::kLinked;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("/*! keep me */\nconst a = 1;", opts);
    EXPECT_FALSE(r.errors.empty());
    EXPECT_EQ(first_text(r.errors), "Cannot transform with linked legal comments");
}

TEST(Api, TransformSourceMapNamesTheDefaultSourcefile) {
    // A transform is handed a string rather than a path, so "sourcefile" has a
    // default of "<stdin>" and a map can always be produced without one.
    api::TransformOptions opts;
    opts.sourcemap = api::SourceMap::kExternal;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const a = 1;", opts);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_TRUE(contains(to_string(r.map), "<stdin>"));
}

TEST(Api, TransformSyntaxErrorIsReportedNotThrown) {
    api::TransformOptions opts;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const a = ;", opts);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_TRUE(r.code.empty());
    // A syntax error is a place in a file, so the message must say where.
    ASSERT_TRUE(r.errors[0].location.has_value());
    EXPECT_EQ(r.errors[0].location->file, "<stdin>");
    EXPECT_EQ(r.errors[0].location->line, 1);
}

TEST(Api, TransformSyntaxErrorMessageIsNotEmpty) {
    api::TransformOptions opts;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("function ( {", opts);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_FALSE(r.errors[0].text.empty());
}

TEST(Api, TransformTSLoaderStripsInterfaces) {
    api::TransformOptions opts;
    opts.loader = api::Loader::kTS;
    opts.log_level = quiet();
    api::TransformResult r =
        api::Transform("interface A { x: number }\nconst a: A = { x: 1 };", opts);
    EXPECT_TRUE(r.errors.empty());
    const std::string code = to_string(r.code);
    EXPECT_FALSE(contains(code, "interface"));
    EXPECT_TRUE(contains(code, "x: 1"));
}

TEST(Api, TransformTSXLoaderParsesJsx) {
    api::TransformOptions opts;
    opts.loader = api::Loader::kTSX;
    opts.jsx = api::JSX::kPreserve;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const a = <div />;", opts);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_TRUE(contains(to_string(r.code), "<div"));
}

TEST(Api, TransformCSSLoaderMinifiesRules) {
    api::TransformOptions opts;
    opts.loader = api::Loader::kCSS;
    opts.minify_whitespace = true;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform(".a { color: red }", opts);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_EQ(to_string(r.code), ".a{color:red}\n");
}

TEST(Api, TransformDefineSubstitutesIdentifier) {
    api::TransformOptions opts;
    opts.define = {{"DEBUG", "false"}};
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("if (DEBUG) { console.log(1) }", opts);
    EXPECT_TRUE(r.errors.empty());
    const std::string code = to_string(r.code);
    EXPECT_FALSE(contains(code, "if (DEBUG)"));
}

TEST(Api, TransformBannerAndFooterWrapOutput) {
    api::TransformOptions opts;
    opts.banner = "// top\n";
    opts.footer = "\n// bottom\n";
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const a = 1;", opts);
    EXPECT_TRUE(r.errors.empty());
    const std::string code = to_string(r.code);
    EXPECT_EQ(code.rfind("// top\n", 0), 0u);
    EXPECT_NE(code.find("// bottom"), std::string::npos);
}

TEST(Api, TransformMinifyIdentifiersRenamesLocals) {
    api::TransformOptions opts;
    opts.minify_identifiers = true;
    opts.log_level = quiet();
    // A local has no name anyone outside the file can be using, so it is the one
    // kind of binding minification is allowed to shorten.
    api::TransformResult r =
        api::Transform("function outer() { const longName = 1; return longName; }", opts);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_FALSE(contains(to_string(r.code), "longName"));
}

TEST(Api, TransformManglePropsShortensProperties) {
    api::TransformOptions opts;
    opts.minify_identifiers = true;
    opts.mangle_props = ".*";
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const o = {}; o.someProperty = 1; console.log(o);", opts);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_FALSE(contains(to_string(r.code), "someProperty"));
}

TEST(Api, TransformMangleCacheIsReturned) {
    api::TransformOptions opts;
    opts.minify_identifiers = true;
    opts.mangle_props = ".*";
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const o = {}; o.someProperty = 1; console.log(o);", opts);
    EXPECT_TRUE(r.errors.empty());
    // The decisions are handed back so a caller can carry them into the next
    // transform of the same file.
    EXPECT_FALSE(r.mangle_cache.empty());
}

TEST(Api, TransformMangleCacheIsAccepted) {
    api::TransformOptions first;
    first.minify_identifiers = true;
    first.mangle_props = ".*";
    first.log_level = quiet();
    const api::TransformResult r1 =
        api::Transform("const o = {}; o.someProperty = 1; console.log(o);", first);
    ASSERT_FALSE(r1.mangle_cache.empty());

    api::TransformOptions second;
    second.minify_identifiers = true;
    second.mangle_props = ".*";
    second.mangle_cache = r1.mangle_cache;
    second.log_level = quiet();
    const api::TransformResult r2 =
        api::Transform("const o = {}; o.someProperty = 1; console.log(o);", second);
    // Feeding the cache back in is accepted and changes nothing about the build
    // succeeding; it is the caller's copy of the decisions, not a different set
    // of options.
    EXPECT_TRUE(r2.errors.empty());
    EXPECT_FALSE(r2.code.empty());
}

TEST(Api, TransformSourcefileAppearsInDiagnostic) {
    api::TransformOptions opts;
    opts.sourcefile = "src/app.ts";
    opts.loader = api::Loader::kTS;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const a = ;", opts);
    ASSERT_FALSE(r.errors.empty());
    ASSERT_TRUE(r.errors[0].location.has_value());
    EXPECT_EQ(r.errors[0].location->file, "src/app.ts");
}

TEST(Api, TransformLineLimitIsAccepted) {
    // The option asks the printer to keep lines short, which it does by wrapping
    // rather than by refusing: a build is not a linter, and a long line is not a
    // reason to fail one.
    api::TransformOptions opts;
    opts.line_limit = 10;
    opts.minify_whitespace = true;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("const averyveryverylong = 1;", opts);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_FALSE(r.code.empty());
}

// ===========================================================================
// Transform: option validation
// ===========================================================================

TEST(Api, TransformInvalidLogLevelIsReportedNotThrown) {
    api::TransformOptions opts;
    opts.log_level = static_cast<api::LogLevel>(99);
    // A level that is not a level leaves the transform with no idea how chatty to
    // be, so it declines to guess: the mistake is reported and no code comes back,
    // rather than the call escaping as an exception.
    api::TransformResult r = api::Transform("const a = 1;", opts);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_TRUE(contains(r.errors[0].text, "log_level"));
    EXPECT_TRUE(contains(r.errors[0].text, "99"));
    EXPECT_TRUE(r.code.empty());
}

TEST(Api, TransformInvalidLogOverrideIsReportedWithItsKey) {
    api::TransformOptions opts;
    opts.log_override = {{"js-parse-error", static_cast<api::LogLevel>(42)}};
    api::TransformResult r = api::Transform("const a = ;", opts);
    ASSERT_FALSE(r.errors.empty());
    const std::string text = r.errors[0].text;
    EXPECT_TRUE(contains(text, "js-parse-error"));
    EXPECT_TRUE(contains(text, "42"));
}

TEST(Api, TransformEmptyInputProducesEmptyCode) {
    api::TransformOptions opts;
    opts.log_level = quiet();
    api::TransformResult r = api::Transform("", opts);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_TRUE(r.code.empty());
}

// ===========================================================================
// FormatMessages
// ===========================================================================

TEST(Api, FormatMessagesOneStringPerMessage) {
    std::vector<api::Message> msgs(3);
    msgs[0].text = "first";
    msgs[1].text = "second";
    msgs[2].text = "third";
    std::vector<std::string> out = api::FormatMessages(msgs, {});
    ASSERT_EQ(out.size(), 3u);
    EXPECT_TRUE(contains(out[0], "first"));
    EXPECT_TRUE(contains(out[1], "second"));
    EXPECT_TRUE(contains(out[2], "third"));
}

TEST(Api, FormatMessagesEmptyInputIsEmptyOutput) {
    EXPECT_TRUE(api::FormatMessages({}, {}).empty());
}

TEST(Api, FormatMessagesErrorKindTagsError) {
    std::vector<api::Message> msgs(1);
    msgs[0].text = "something went wrong";
    api::FormatMessagesOptions opts;
    opts.kind = api::MessageKind::kError;
    const std::string out = api::FormatMessages(msgs, opts)[0];
    EXPECT_TRUE(contains(out, "ERROR"));
    EXPECT_TRUE(contains(out, "something went wrong"));
}

TEST(Api, FormatMessagesWarningKindTagsWarning) {
    std::vector<api::Message> msgs(1);
    msgs[0].text = "something is odd";
    api::FormatMessagesOptions opts;
    opts.kind = api::MessageKind::kWarning;
    const std::string out = api::FormatMessages(msgs, opts)[0];
    EXPECT_TRUE(contains(out, "WARNING"));
    EXPECT_FALSE(contains(out, "[ERROR]"));
}

TEST(Api, FormatMessagesRendersLocationAndSnippet) {
    std::vector<api::Message> msgs(1);
    msgs[0].text = "Expected \"}\" but found end of file";
    msgs[0].location = api::Location{
        .file = "src/app.js",
        .namespace_ = "",
        .line = 12,
        .column = 4,
        .length = 0,
        .line_text = "  function main() {",
        .suggestion = "",
    };
    const std::string out = api::FormatMessages(msgs, {})[0];
    EXPECT_TRUE(contains(out, "src/app.js:12:4"));
    EXPECT_TRUE(contains(out, "function main() {"));
    EXPECT_TRUE(contains(out, "^"));
}

TEST(Api, FormatMessagesNoLocationIsOneLine) {
    std::vector<api::Message> msgs(1);
    msgs[0].text = "no file to point at";
    const std::string out = api::FormatMessages(msgs, {})[0];
    EXPECT_TRUE(contains(out, "no file to point at"));
    EXPECT_FALSE(contains(out, "│"));
}

TEST(Api, FormatMessagesAppendsNotes) {
    std::vector<api::Message> msgs(1);
    msgs[0].text = "cannot resolve";
    api::Note note;
    note.text = "did you mean ./utils.js?";
    msgs[0].notes.push_back(note);
    const std::string out = api::FormatMessages(msgs, {})[0];
    // A note is a separate, indented paragraph under the message it belongs to,
    // so it must not run into the message text on the same line.
    EXPECT_TRUE(contains(out, "cannot resolve"));
    EXPECT_TRUE(contains(out, "did you mean ./utils.js?"));
    EXPECT_FALSE(contains(out, "cannot resolve did you mean"));
}

TEST(Api, FormatMessagesColorAddsEscapeSequences) {
    std::vector<api::Message> msgs(1);
    msgs[0].text = "colored";
    api::FormatMessagesOptions opts;
    opts.color = true;
    const std::string out = api::FormatMessages(msgs, opts)[0];
    EXPECT_TRUE(contains(out, "\x1b["));
}

TEST(Api, FormatMessagesWithoutColorHasNoEscapes) {
    std::vector<api::Message> msgs(1);
    msgs[0].text = "plain";
    api::FormatMessagesOptions opts;
    opts.color = false;
    opts.terminal_width = 0;
    const std::string out = api::FormatMessages(msgs, opts)[0];
    EXPECT_FALSE(contains(out, "\x1b["));
}

TEST(Api, FormatMessagesTerminalWidthWrapsText) {
    std::vector<api::Message> msgs(1);
    msgs[0].text = "word ";
    for (int i = 0; i < 40; i++) {
        msgs[0].text += "word ";
    }
    api::FormatMessagesOptions opts;
    opts.terminal_width = 40;
    const std::string out = api::FormatMessages(msgs, opts)[0];
    // Wrapping means more than one line for a sentence this long.
    size_t lines = 0;
    for (char c : out) {
        if (c == '\n') lines++;
    }
    EXPECT_GT(lines, 1u);
}

// ===========================================================================
// AnalyzeMetafile
// ===========================================================================

namespace {

// A metafile with one output fed by two inputs, plus a source map that the
// report is expected to leave out.
const char* kMetafileTwoInputs = R"({
  "outputs": {
    "dist/app.js": {
      "entryPoint": "src/app.js",
      "bytes": 3072,
      "inputs": {
        "src/app.js": { "bytesInOutput": 2048 },
        "node_modules/x/index.js": { "bytesInOutput": 1024 }
      }
    },
    "dist/app.js.map": {
      "bytes": 99999,
      "inputs": {
        "src/app.js": { "bytesInOutput": 99999 }
      }
    }
  }
})";

} // namespace

TEST(Api, AnalyzeMetafileRendersOutputAndInputs) {
    const std::string out = api::AnalyzeMetafile(kMetafileTwoInputs, {});
    ASSERT_FALSE(out.empty());
    EXPECT_TRUE(contains(out, "dist/app.js"));
    EXPECT_TRUE(contains(out, "src/app.js"));
    EXPECT_TRUE(contains(out, "node_modules/x/index.js"));
}

TEST(Api, AnalyzeMetafileSkipsSourceMaps) {
    const std::string out = api::AnalyzeMetafile(kMetafileTwoInputs, {});
    EXPECT_FALSE(contains(out, "dist/app.js.map"));
    EXPECT_FALSE(contains(out, "95.4kb")); // 99999 bytes, the map's own size
}

TEST(Api, AnalyzeMetafileTopLevelRowIsOneHundredPercent) {
    const std::string out = api::AnalyzeMetafile(kMetafileTwoInputs, {});
    EXPECT_TRUE(contains(out, "100.0%"));
}

TEST(Api, AnalyzeMetafileChildPercentagesAreWholePercentSigns) {
    // 2048 of 3072 bytes is two thirds, which is 66.7%. The sign used to be
    // doubled, so a test that only looked for "66.7" would have passed while the
    // output read "66.7%%".
    const std::string out = api::AnalyzeMetafile(kMetafileTwoInputs, {});
    EXPECT_TRUE(contains(out, "66.7%"));
    EXPECT_FALSE(contains(out, "%%"));
}

TEST(Api, AnalyzeMetafileAbbreviatesSizes) {
    const std::string out = api::AnalyzeMetafile(kMetafileTwoInputs, {});
    EXPECT_TRUE(contains(out, "3.0kb")); // 3072 bytes
    EXPECT_TRUE(contains(out, "2.0kb")); // 2048 bytes
    EXPECT_TRUE(contains(out, "1.0kb")); // 1024 bytes
}

TEST(Api, AnalyzeMetafileByteCountsBelowOneKilo) {
    const std::string metafile = R"({
      "outputs": {
        "dist/tiny.js": { "bytes": 512, "inputs": {} }
      }
    })";
    const std::string out = api::AnalyzeMetafile(metafile, {});
    EXPECT_TRUE(contains(out, "512b"));
}

TEST(Api, AnalyzeMetafileSkipsInputsThatContributedNothing) {
    const std::string metafile = R"({
      "outputs": {
        "dist/app.js": {
          "bytes": 100,
          "inputs": {
            "src/used.js": { "bytesInOutput": 100 },
            "src/unused.js": { "bytesInOutput": 0 }
          }
        }
      }
    })";
    const std::string out = api::AnalyzeMetafile(metafile, {});
    EXPECT_TRUE(contains(out, "src/used.js"));
    EXPECT_FALSE(contains(out, "src/unused.js"));
}

TEST(Api, AnalyzeMetafileLargestOutputFirst) {
    const std::string metafile = R"({
      "outputs": {
        "dist/small.js": { "bytes": 10, "inputs": {} },
        "dist/large.js": { "bytes": 5000, "inputs": {} }
      }
    })";
    const std::string out = api::AnalyzeMetafile(metafile, {});
    const size_t large = out.find("dist/large.js");
    const size_t small = out.find("dist/small.js");
    ASSERT_NE(large, std::string::npos);
    ASSERT_NE(small, std::string::npos);
    EXPECT_LT(large, small);
}

TEST(Api, AnalyzeMetafileVerboseDrawsHorizontalRule) {
    api::AnalyzeMetafileOptions opts;
    opts.verbose = true;
    const std::string out = api::AnalyzeMetafile(kMetafileTwoInputs, opts);
    // The rule is a three-byte character, so the filler has to be whole copies
    // of it. Repeating its first byte instead produced a run of fragments that
    // no terminal renders.
    EXPECT_TRUE(contains(out, "\xe2\x94\x80\xe2\x94\x80"));
}

TEST(Api, AnalyzeMetafileNonVerbosePadsWithSpaces) {
    api::AnalyzeMetafileOptions opts;
    opts.verbose = false;
    const std::string out = api::AnalyzeMetafile(kMetafileTwoInputs, opts);
    EXPECT_FALSE(contains(out, "\xe2\x94\x80"));
}

TEST(Api, AnalyzeMetafileBothModesProduceSameRows) {
    api::AnalyzeMetafileOptions plain;
    plain.verbose = false;
    api::AnalyzeMetafileOptions rule;
    rule.verbose = true;

    // Verbose only changes the filler, so the sizes and shares must be the same
    // words in both renderings.
    const std::string a = api::AnalyzeMetafile(kMetafileTwoInputs, plain);
    const std::string b = api::AnalyzeMetafile(kMetafileTwoInputs, rule);
    for (const char* token : {"dist/app.js", "3.0kb", "2.0kb", "1.0kb", "100.0%", "66.7%"}) {
        EXPECT_TRUE(contains(a, token));
        EXPECT_TRUE(contains(b, token));
    }
}

TEST(Api, AnalyzeMetafileIgnoresTextThatIsNotAJsonDocument) {
    EXPECT_EQ(api::AnalyzeMetafile("this is not json", {}), "");
}

TEST(Api, AnalyzeMetafileIgnoresJsonWithoutOutputs) {
    EXPECT_EQ(api::AnalyzeMetafile(R"({"inputs": {}})", {}), "");
}

TEST(Api, AnalyzeMetafileIgnoresEmptyOutputsObject) {
    EXPECT_EQ(api::AnalyzeMetafile(R"({"outputs": {}})", {}), "");
}

TEST(Api, AnalyzeMetafileIgnoresOutputWithoutBytes) {
    const std::string metafile = R"({
      "outputs": { "dist/app.js": { "inputs": {} } }
    })";
    EXPECT_EQ(api::AnalyzeMetafile(metafile, {}), "");
}

TEST(Api, AnalyzeMetafileEmptyInputIsEmptyOutput) {
    EXPECT_EQ(api::AnalyzeMetafile("", {}), "");
}

// ===========================================================================
// Build: option validation (no disk involved)
// ===========================================================================

TEST(Api, BuildOutfileAndOutdirTogetherIsRejected) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js"};
    opts.outfile = "out.js";
    opts.outdir = "dist";
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_EQ(first_text(r.errors), "Cannot use both \"outfile\" and \"outdir\"");
    EXPECT_TRUE(r.output_files.empty());
}

TEST(Api, BuildMultipleInputsWithoutOutdirIsRejected) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js", "b.js"};
    opts.outfile = "out.js";
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_EQ(first_text(r.errors), "Must use \"outdir\" when there are multiple input files");
    EXPECT_TRUE(r.output_files.empty());
}

TEST(Api, BuildExternalWithoutBundleIsRejected) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js"};
    opts.external = {"react"};
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    EXPECT_FALSE(r.errors.empty());
    EXPECT_TRUE(r.output_files.empty());
}

TEST(Api, BuildAliasWithoutBundleIsRejected) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js"};
    opts.alias = {{"old", "new"}};
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    EXPECT_FALSE(r.errors.empty());
    EXPECT_TRUE(r.output_files.empty());
}

TEST(Api, BuildPluginWithoutNameIsRejected) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js"};
    opts.plugins = {api::Plugin{.name = "", .setup = [](api::PluginBuild&) {}}};
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    EXPECT_FALSE(r.errors.empty());
    EXPECT_TRUE(r.output_files.empty());
}

TEST(Api, BuildInvalidLogLevelIsReportedNotThrown) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js"};
    opts.log_level = static_cast<api::LogLevel>(99);
    api::BuildResult r = api::Build(opts);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_TRUE(contains(r.errors[0].text, "log_level"));
    EXPECT_TRUE(contains(r.errors[0].text, "99"));
}

TEST(Api, BuildReportsTheOutputPathConflict) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js"};
    opts.outfile = "out.js";
    opts.outdir = "dist";
    opts.external = {"react"};
    opts.log_level = static_cast<api::LogLevel>(99);
    api::BuildResult r = api::Build(opts);
    // The level is reported on its own, because it is what decides whether the
    // rest of the build is chatty enough to say anything at all.
    ASSERT_FALSE(r.errors.empty());
    EXPECT_TRUE(contains(r.errors[0].text, "log_level"));
    // Where the output goes is one question with one answer, so the two
    // contradictory settings it was given collapse into the first mistake found.
    bool reported_paths = false;
    for (const auto& e : r.errors) {
        if (e.text == "Cannot use both \"outfile\" and \"outdir\"") reported_paths = true;
    }
    EXPECT_TRUE(reported_paths);
}

TEST(Api, BuildMissingEntryPointIsReported) {
    api::BuildOptions opts;
    opts.entry_points = {"does-not-exist.js"};
    opts.outfile = "out.js";
    opts.abs_working_dir = TempDir("missing-entry").path();
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    EXPECT_FALSE(r.errors.empty());
    EXPECT_TRUE(r.output_files.empty());
}

// ===========================================================================
// Build: real builds over a temporary directory
// ===========================================================================

TEST(Api, BuildWithoutWriteReturnsOutputInMemory) {
    TempDir dir("in-memory");
    dir.Write("entry.js", "export const answer = 42;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    ASSERT_TRUE(r.errors.empty());
    ASSERT_EQ(r.output_files.size(), 1u);
    EXPECT_TRUE(contains(to_string(r.output_files[0].contents), "42"));
    // Nothing was written, because nothing asked for it.
    EXPECT_FALSE(dir.Exists("entry.js.out"));
}

TEST(Api, BuildWithWriteCreatesTheOutputFile) {
    TempDir dir("write");
    dir.Write("entry.js", "export const answer = 42;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.outfile = "dist/bundle.js";
    opts.write = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_TRUE(dir.Exists("dist/bundle.js"));
    EXPECT_TRUE(contains(dir.Read("dist/bundle.js"), "42"));
}

TEST(Api, BuildWithOutdirWritesEveryEntry) {
    TempDir dir("outdir");
    dir.Write("a.js", "export const a = 1;\n");
    dir.Write("b.js", "export const b = 2;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("a.js"), dir.At("b.js")};
    opts.bundle = true;
    opts.outdir = "dist";
    opts.entry_names = "[name]";
    opts.write = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_TRUE(dir.Exists("dist/a.js"));
    EXPECT_TRUE(dir.Exists("dist/b.js"));
}

TEST(Api, BuildMetafileIsJsonWhenRequested) {
    TempDir dir("metafile");
    dir.Write("entry.js", "export const answer = 42;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.metafile = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    ASSERT_TRUE(r.errors.empty());
    ASSERT_FALSE(r.metafile.empty());
    EXPECT_TRUE(contains(r.metafile, "\"outputs\""));
    // The metafile this build produced is something the analyser can read.
    EXPECT_FALSE(api::AnalyzeMetafile(r.metafile, {}).empty());
}

TEST(Api, BuildMetafileIsEmptyWhenNotRequested) {
    TempDir dir("no-metafile");
    dir.Write("entry.js", "export const answer = 42;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    EXPECT_TRUE(r.metafile.empty());
}

TEST(Api, BuildSourcemapWritesMapNextToOutput) {
    TempDir dir("sourcemap");
    dir.Write("entry.js", "export const answer = 42;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.outfile = "dist/bundle.js";
    opts.sourcemap = api::SourceMap::kExternal;
    opts.write = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    EXPECT_TRUE(r.errors.empty());
    // An external map is a second file beside the output, and the two are both
    // handed back so a caller can write them wherever it likes.
    EXPECT_TRUE(dir.Exists("dist/bundle.js.map"));
    EXPECT_TRUE(dir.Exists("dist/bundle.js"));
    EXPECT_EQ(r.output_files.size(), 2u);
    EXPECT_TRUE(contains(dir.Read("dist/bundle.js.map"), "\"mappings\""));
}

TEST(Api, BuildUnresolvedImportIsReported) {
    TempDir dir("bad-import");
    dir.Write("entry.js", "import './missing.js';\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.outfile = "out.js";
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    EXPECT_FALSE(r.errors.empty());
    EXPECT_TRUE(r.output_files.empty());
}

TEST(Api, BuildExternalStaysOutOfTheGraph) {
    TempDir dir("external");
    dir.Write("entry.js", "import 'react';\nconsole.log(1);\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.external = {"react"};
    opts.outfile = "out.js";
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    EXPECT_TRUE(r.errors.empty());
    ASSERT_EQ(r.output_files.size(), 1u);
    // The import statement survives, because that is what "external" means.
    EXPECT_TRUE(contains(to_string(r.output_files[0].contents), "react"));
}

TEST(Api, BuildOutputFileHasAPathAndAHash) {
    TempDir dir("hash");
    dir.Write("entry.js", "export const answer = 42;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.outfile = "out.js";
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    api::BuildResult r = api::Build(opts);
    ASSERT_EQ(r.output_files.size(), 1u);
    EXPECT_FALSE(r.output_files[0].path.empty());
    EXPECT_FALSE(r.output_files[0].hash.empty());
}

TEST(Api, BuildIdenticalInputsProduceIdenticalHashes) {
    TempDir dir("hash-stable");
    dir.Write("entry.js", "export const answer = 42;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.outfile = "out.js";
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    const api::BuildResult first = api::Build(opts);
    const api::BuildResult second = api::Build(opts);
    ASSERT_EQ(first.output_files.size(), 1u);
    ASSERT_EQ(second.output_files.size(), 1u);
    EXPECT_EQ(first.output_files[0].hash, second.output_files[0].hash);
}

// ===========================================================================
// Context
// ===========================================================================

TEST(Api, ContextBuildsAndRebuilds) {
    TempDir dir("context");
    dir.Write("entry.js", "export const answer = 1;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();

    std::vector<api::Message> errors;
    std::unique_ptr<api::BuildContext> ctx = api::Context(opts, errors);
    ASSERT_TRUE(ctx != nullptr);
    EXPECT_TRUE(errors.empty());

    const api::BuildResult first = ctx->Rebuild();
    EXPECT_TRUE(first.errors.empty());
    ASSERT_EQ(first.output_files.size(), 1u);
    EXPECT_TRUE(contains(to_string(first.output_files[0].contents), "1"));

    // A rebuild after the file changed sees the change, which is the reason a
    // context exists instead of repeated Build() calls.
    dir.Write("entry.js", "export const answer = 222;\n");
    const api::BuildResult second = ctx->Rebuild();
    EXPECT_TRUE(second.errors.empty());
    ASSERT_EQ(second.output_files.size(), 1u);
    const std::string code = to_string(second.output_files[0].contents);
    EXPECT_TRUE(contains(code, "222"));
    // The first build's output is unchanged, so a caller holding it is still
    // looking at what it was handed.
    EXPECT_TRUE(contains(to_string(first.output_files[0].contents), "1"));
}

TEST(Api, ContextReturnsNullWithErrorsOnBadOptions) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js"};
    opts.outfile = "out.js";
    opts.outdir = "dist";
    opts.log_level = quiet();

    std::vector<api::Message> errors;
    std::unique_ptr<api::BuildContext> ctx = api::Context(opts, errors);
    EXPECT_TRUE(ctx == nullptr);
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].text, "Cannot use both \"outfile\" and \"outdir\"");
}

TEST(Api, ContextReturnsNullOnInvalidLogLevel) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js"};
    opts.log_level = static_cast<api::LogLevel>(99);

    std::vector<api::Message> errors;
    std::unique_ptr<api::BuildContext> ctx = api::Context(opts, errors);
    EXPECT_TRUE(ctx == nullptr);
    ASSERT_FALSE(errors.empty());
    EXPECT_TRUE(contains(errors[0].text, "log_level"));
    EXPECT_TRUE(contains(errors[0].text, "99"));
}

TEST(Api, ContextReportsInvalidLogOverrideWithItsKey) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js"};
    opts.log_override = {{"css-syntax-error", static_cast<api::LogLevel>(42)}};
    opts.log_level = quiet();

    std::vector<api::Message> errors;
    std::unique_ptr<api::BuildContext> ctx = api::Context(opts, errors);
    EXPECT_TRUE(ctx == nullptr);
    ASSERT_FALSE(errors.empty());
    EXPECT_TRUE(contains(errors[0].text, "css-syntax-error"));
    EXPECT_TRUE(contains(errors[0].text, "42"));
}

TEST(Api, ContextDisposeMakesRebuildEmpty) {
    TempDir dir("dispose");
    dir.Write("entry.js", "export const answer = 1;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();

    std::vector<api::Message> errors;
    std::unique_ptr<api::BuildContext> ctx = api::Context(opts, errors);
    ASSERT_TRUE(ctx != nullptr);
    ctx->Dispose();

    // A disposed context is inert rather than dangerous: asking it to build
    // again returns an empty result instead of reading a torn-down session.
    const api::BuildResult after = ctx->Rebuild();
    EXPECT_TRUE(after.errors.empty());
    EXPECT_TRUE(after.output_files.empty());
}

TEST(Api, ContextCancelIsSafeBetweenRebuilds) {
    TempDir dir("cancel");
    dir.Write("entry.js", "export const answer = 1;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();

    std::vector<api::Message> errors;
    std::unique_ptr<api::BuildContext> ctx = api::Context(opts, errors);
    ASSERT_TRUE(ctx != nullptr);
    ctx->Cancel();
    const api::BuildResult r = ctx->Rebuild();
    EXPECT_TRUE(r.errors.empty());
    ASSERT_EQ(r.output_files.size(), 1u);
}

TEST(Api, ContextBuildOptionsAreCarriedIntoEveryRebuild) {
    TempDir dir("carried");
    dir.Write("entry.js", "const value = 1 + 1;\nconsole.log(value);\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.minify_whitespace = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();

    std::vector<api::Message> errors;
    std::unique_ptr<api::BuildContext> ctx = api::Context(opts, errors);
    ASSERT_TRUE(ctx != nullptr);

    const api::BuildResult first = ctx->Rebuild();
    const api::BuildResult second = ctx->Rebuild();
    ASSERT_EQ(first.output_files.size(), 1u);
    ASSERT_EQ(second.output_files.size(), 1u);
    // Minification was asked for once, at context creation, and applies to both.
    EXPECT_FALSE(contains(to_string(first.output_files[0].contents), "1 + 1"));
    EXPECT_EQ(to_string(first.output_files[0].contents),
              to_string(second.output_files[0].contents));
}

TEST(Api, ContextWatchOnDisposedContextIsRejected) {
    TempDir dir("watch-disposed");
    dir.Write("entry.js", "export const answer = 1;\n");

    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();

    std::vector<api::Message> errors;
    std::unique_ptr<api::BuildContext> ctx = api::Context(opts, errors);
    ASSERT_TRUE(ctx != nullptr);
    ctx->Dispose();

    // Watch and Serve are the two calls that take the session over, and both
    // document that they throw once it has been disposed. Unlike a build, which
    // reports what went wrong, these have nothing to report into: the caller
    // asked for something the session can no longer do, and the answer is that
    // the session is gone.
    bool threw = false;
    try {
        ctx->Watch({});
    } catch (const std::exception&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

// ===========================================================================
// Plugins: validation level only
// ===========================================================================

TEST(Api, PluginSetupRunsOnceAtContextCreation) {
    TempDir dir("plugin-setup");
    dir.Write("entry.js", "export const answer = 1;\n");

    int setups = 0;
    api::BuildOptions opts;
    opts.entry_points = {dir.At("entry.js")};
    opts.bundle = true;
    opts.abs_working_dir = dir.path();
    opts.log_level = quiet();
    opts.plugins = {api::Plugin{
        .name = "counter",
        .setup = [&setups](api::PluginBuild&) { setups++; },
    }};

    std::vector<api::Message> errors;
    std::unique_ptr<api::BuildContext> ctx = api::Context(opts, errors);
    ASSERT_TRUE(ctx != nullptr);
    // Setup is a chance to register hooks, and it happens before the first build.
    EXPECT_EQ(setups, 1);

    ctx->Rebuild();
    ctx->Rebuild();
    // The hooks registered in setup are what the engine re-runs; setup itself is
    // not a per-build step.
    EXPECT_EQ(setups, 1);
}

TEST(Api, PluginWithEmptyNameIsRejectedByContext) {
    api::BuildOptions opts;
    opts.entry_points = {"a.js"};
    opts.log_level = quiet();
    opts.plugins = {api::Plugin{.name = "", .setup = [](api::PluginBuild&) {}}};

    std::vector<api::Message> errors;
    std::unique_ptr<api::BuildContext> ctx = api::Context(opts, errors);
    EXPECT_TRUE(ctx == nullptr);
    ASSERT_FALSE(errors.empty());
    EXPECT_TRUE(contains(errors[0].text, "name"));
}
