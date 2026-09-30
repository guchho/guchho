// Tests for the scaffolding layer, and for the one thing about the init command
// that the other init tests cannot see.
//
// The tests in cli_project_test.cpp ask whether a project appeared and whether it
// builds. Those are the right questions and they stay there. This file asks the
// questions that only exist once there is more than one project to make: that
// every combination of the two choices produces a project, that each of those
// projects builds, and that the command refuses to write into a working
// directory that holds somebody else's work.
//
// The command has no way to name a target any more — the working directory is
// the target, and the two tests that used to name one are gone with it. The
// scaffold layer still creates a target that does not exist, but nothing can
// point it at one but itself, so that behaviour is covered only where the layer
// is tested directly. What has replaced the two on the command line is the
// other side of the same boundary: a mistake in the working directory is still
// worth a non-zero exit and a diagnostic, and it is the file system's mistake
// rather than the command's, which is the difference exit 1 and exit 2 exist
// for.
//
// The twelve combinations are tested as twelve rather than as four, because the
// reason there are two choices at all is that they multiply, and a table that
// only checks the diagonal is a table that would not have noticed the off-diagonal
// entries being wrong. A library is a different set of files from an app, a
// TypeScript library is a different set again, and the only way to know each is
// right is to have made it and built it.
//
// What is not tested here, and why: the questions init asks when it is run from a
// terminal. They are reached only when both ends of the terminal are a console,
// which is decided by logger::GetTerminalInfo with no seam to stand in for it
// (src/core/logger/terminal.cpp:43), and the test harness points both at files
// (include/test/helpers/cli_test.hpp:234). What is tested about that path is the
// part that can hurt somebody: that a run without a terminal, and a run given
// --yes, never ask anything and never block on input — a command that hangs a
// build script is a worse outcome than a command that asks an extra question. It
// is the same position include/test/helpers/cli_test.hpp takes for watch, dev and
// serve: covered up to the last line before the park.
//
// The fixture is a CliWorkspace per test rather than one shared, because these
// tests are about what ends up on disk and a shared directory would let one
// test's project be the next test's "not empty" case.

#include "guchho/runtime.hpp"
#include "guchho/scaffold.hpp"
#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace cli::test {

using guchho::test::CliResult;
using guchho::test::CliWorkspace;
using guchho::test::kBuildFailure;
using guchho::test::kSuccess;
using guchho::test::kUsageError;
using guchho::test::OutputContains;
using guchho::test::RunCli;
using guchho::test::RunCliWithStdin;

namespace {

// The two choices, as the four names the command line accepts for each. Written
// out rather than ranged over the scaffold layer's own tables on purpose: if a
// table gained a name and the test loop followed it, the test would cover the new
// name without anybody having decided the new name is supposed to work, and a
// template that was never finished would be reported as covered.
const std::vector<std::string> kTypes     = {"app", "library", "plugin"};
const std::vector<std::string> kTemplates = {"basic", "ts", "jsx", "tsx"};

// The files a project of this type and template is made of, as a sorted list so
// two orders of the same set are not reported as two different projects.
std::vector<std::string> Sorted(std::vector<std::string> paths) {
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::vector<std::string> Listed(const CliWorkspace& ws) {
    const std::filesystem::path root = CliWorkspace::Native(ws.path());
    std::vector<std::string>         paths;
    std::error_code                  ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        paths.push_back(CliWorkspace::ToUTF8(
            std::filesystem::relative(entry.path(), root, ec).generic_string()));
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

// A list of paths on one line, so that a failure says which file was wrong. The
// assertion macros cannot print a vector, and "the wrong set of files" with no
// names in it is a failure nobody can act on.
std::string Join(const std::vector<std::string>& paths) {
    std::string out;
    for (const std::string& path : paths) {
        if (!out.empty()) out += ", ";
        out += path;
    }
    return out;
}

std::string Tree(const CliWorkspace& ws) {
    return Join(Listed(ws));
}

// The path of the one source file in a project, which is the file whose
// extension is the whole difference between the four templates.
std::string SourceExtension(const std::string& tmpl) {
    return tmpl == "basic" ? ".js" : "." + tmpl;
}

// Pins the runtime detector to one answer for the duration of a test, and puts
// the machine back when the test is done. The config file a run writes depends
// on what a run sees on the PATH, which is the developer's machine — not a
// variable a test controls — so every test that looks at a config file name
// pins the answer it wants in advance. The previous detector is handed back
// straight on the way out, so the next test sees the machine, not this one.
class RuntimeDetectorScope {
public:
    explicit RuntimeDetectorScope(guchho::runtime::JavaScriptRuntime runtime)
        : previous_(guchho::runtime::SetRuntimeDetector([runtime] {
              return runtime;
          })) {}

    ~RuntimeDetectorScope() {
        guchho::runtime::SetRuntimeDetector(previous_);
    }

    RuntimeDetectorScope(const RuntimeDetectorScope&) = delete;
    RuntimeDetectorScope& operator=(const RuntimeDetectorScope&) = delete;

private:
    guchho::runtime::JavaScriptRuntimeDetector previous_;
};

// One build field out of a generated config, whichever of the two formats wrote
// it. "json" says which format, and the only difference is the spelling of the
// delimiters: a key and a value on their own line. Lines that are not a field —
// the braces and the "build" opener — yield nothing, so the map a test builds
// below is exactly the set of fields, whichever format produced them.
struct Field {
    std::string key;
    std::string value;
};

std::optional<Field> ReadConfigField(std::string_view line, bool json)
{
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') i++;
    if (i >= line.size()) return std::nullopt;

    std::string key;
    if (json) {
        if (line[i] != '"') return std::nullopt;
        i++;
        while (i < line.size() && line[i] != '"') key += line[i++];
        if (i >= line.size()) return std::nullopt;
        i++;
    } else {
        while (i < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) {
            key += line[i++];
        }
        if (key.empty()) return std::nullopt;
    }
    while (i < line.size() && line[i] == ' ') i++;
    if (i >= line.size() || line[i] != ':') return std::nullopt;
    i++;
    while (i < line.size() && line[i] == ' ') i++;

    std::string value;
    if (i < line.size() && (line[i] == '\'' || line[i] == '"')) {
        const char quote = line[i++];
        while (i < line.size() && line[i] != quote) value += line[i++];
    } else {
        while (i < line.size() && line[i] != ',' && line[i] != ' ' && line[i] != '\t') {
            value += line[i++];
        }
    }
    if (key.empty() || value.empty()) return std::nullopt;
    return Field{key, value};
}

// The build fields of a generated config as key -> value, so the two formats
// can be compared field for field without caring how each one spells its
// quotes.
std::map<std::string, std::string> ConfigFields(const std::string& text, bool json)
{
    std::map<std::string, std::string> out;
    std::istringstream                 lines(text);
    std::string                        line;
    while (std::getline(lines, line)) {
        if (auto field = ReadConfigField(line, json)) {
            out[field->key] = field->value;
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// The twelve combinations
// ---------------------------------------------------------------------------

// Every combination writes a project, and writes the right number of files for
// it. A project type is not a label on the same four files: an app has a page to
// load, and a library has nothing to load it with.
//
// The target is the working directory, and the workspace a fresh one per
// combination, so the files a combination produces are the workspace's own
// files rather than a set under a directory the command was asked to make.
TEST(CliInit, EveryTypeAndTemplateProducesItsOwnFiles) {
    for (const std::string& type : kTypes) {
        for (const std::string& tmpl : kTemplates) {
            CliWorkspace ws("matrix");

            RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNode);
            const CliResult result = RunCli(
                {"init", "--type=" + type, "--template=" + tmpl, "--yes"});

            EXPECT_EQ(result.exit_code, kSuccess)
                << type << "/" << tmpl << " said: " << result.err;

            const bool is_app = type == "app";
            // An app is entered by a page, so its script is the one the page
            // loads and is called main. A library and a plugin are entered by
            // whatever imports them, so theirs is the package's entry point.
            const std::string ext = SourceExtension(tmpl);
            std::vector<std::string> want = {
                is_app ? "src/main" + ext : "src/index" + ext,
                "guchho.config.js"};
            if (is_app) {
                want.push_back("src/index.html");
                want.push_back("src/style.css");
            } else {
                want.push_back("package.json");
                want.push_back("README.md");
            }

            // The two sides are named in the message rather than left to the
            // harness, so that a failure here is a list of files rather than a
            // boolean.
            const std::string got  = Tree(ws);
            const std::string want_text = Join(Sorted(want));
            EXPECT_TRUE(got == want_text)
                << type << "/" << tmpl << " wrote [" << got << "] and not [" << want_text << "]";
        }
    }
}

// The template is the file extension and nothing else, and it is the extension
// the resolver is asked to handle — a TypeScript template that wrote ".js" would
// look right in a listing and be compiled as JavaScript.
TEST(CliInit, TheTemplateIsTheSourceExtension) {
    struct Expect {
        std::string tmpl;
        std::string ext;
    };
    const std::vector<Expect> expects = {
        {"basic", ".js"}, {"ts", ".ts"}, {"jsx", ".jsx"}, {"tsx", ".tsx"}};

    for (const Expect& expect : expects) {
        CliWorkspace ws("ext");

        EXPECT_EQ(RunCli({"init", "--template=" + expect.tmpl, "--yes"}).exit_code, kSuccess);

        EXPECT_TRUE(ws.Exists("src/main" + expect.ext))
            << expect.tmpl << " did not write src/main" << expect.ext;
    }
}

// The reason the twelve above are worth running. A starter project that does not
// build is worse than one that was never written, because somebody has to find
// out which it is before they can use it — and the commonest way a generated
// project fails to build is a configuration file that points at a file the
// scaffold never created.
TEST(CliInit, EveryCombinationBuilds) {
    for (const std::string& type : kTypes) {
        for (const std::string& tmpl : kTemplates) {
            CliWorkspace ws("builds");

            RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNode);
            ASSERT_EQ(RunCli({"init", "--type=" + type, "--template=" + tmpl, "--yes"}).exit_code,
                      kSuccess)
                << type << "/" << tmpl;
            const CliResult build = RunCli({"build"});

            EXPECT_EQ(build.exit_code, kSuccess)
                << type << "/" << tmpl << " did not build: " << build.out << build.err;
            if (type == "app") {
                EXPECT_TRUE(ws.Exists("dist/index.html")) << type << "/" << tmpl;
                EXPECT_TRUE(ws.Exists("dist/main.js")) << type << "/" << tmpl;
            } else {
                EXPECT_TRUE(ws.Exists("dist/index.js")) << type << "/" << tmpl;
            }
        }
    }
}

// The library configuration points at the source the library scaffold wrote, and
// not at a filename from the app scaffold. This is asserted on the file rather
// than through the build above because a build that resolved the entry by falling
// back to a default would pass that test and fail this one.
TEST(CliInit, TheConfigEntryIsTheFileThatWasWritten) {
    CliWorkspace ws("entry");

    RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNode);
    ASSERT_EQ(RunCli({"init", "--type=library", "--template=ts", "--yes"}).exit_code, kSuccess);

    EXPECT_TRUE(OutputContains(ws.Read("guchho.config.js"), "src/index.ts"));
    EXPECT_TRUE(ws.Exists("src/index.ts"));
}

// The app keeps its page under src rather than at the root, and the page loads
// the script by the name the app scaffold actually wrote. A .tsx project whose
// page asked for ./main.js would build and then load nothing.
TEST(CliInit, ThePageLoadsTheSourceThatWasWritten) {
    for (const std::string& tmpl : kTemplates) {
        CliWorkspace ws("page");

        ASSERT_EQ(RunCli({"init", "--template=" + tmpl, "--yes"}).exit_code, kSuccess);

        const std::string ext = tmpl == "basic" ? "js" : tmpl;
        EXPECT_TRUE(OutputContains(ws.Read("src/index.html"), "./main." + ext))
            << tmpl << " page does not load ./main." << ext;
    }
}

// A generated package.json is exactly the three fields it needs to be. Anything
// more is a claim about the project that nothing in the scaffold checked.
TEST(CliInit, TheGeneratedPackageJsonSaysOnlyWhatItKnows) {
    CliWorkspace ws("pkg");

    ASSERT_EQ(RunCli({"init", "--type=library", "--yes"}).exit_code, kSuccess);

    const std::string pkg = ws.Read("package.json");
    EXPECT_TRUE(OutputContains(pkg, "\"name\""));
    EXPECT_TRUE(OutputContains(pkg, "\"version\""));
    EXPECT_TRUE(OutputContains(pkg, "\"private\""));
    EXPECT_FALSE(OutputContains(pkg, "\"dependencies\""));
    EXPECT_FALSE(OutputContains(pkg, "\"scripts\""));
    EXPECT_FALSE(OutputContains(pkg, "\"main\""));
}

// The name in package.json is the directory the project was made in, because that
// is the only name the command is ever told. Somebody who publishes a library
// will see the directory name wherever it is published, so that is what the
// package has to say.
TEST(CliInit, ThePackageNameIsTheDirectoryName) {
    CliWorkspace ws("pkgname");

    ASSERT_EQ(RunCli({"init", "--type=library", "--yes"}).exit_code, kSuccess);

    // The workspace is a temporary directory whose name the harness invented
    // rather than a test, so the assertion asks the scaffolder for the name it
    // is meant to write and checks that the file carries it. Everything the
    // harness's name is made of is a valid npm name already, so the normalized
    // form is the name itself.
    const std::string dir  = std::filesystem::path(ws.path()).filename().string();
    const auto        name = guchho::scaffold::NpmPackageName(dir);
    ASSERT_TRUE(name) << "the harness directory should have a package name";

    EXPECT_TRUE(OutputContains(ws.Read("package.json"), "\"" + *name + "\""));
}

// The plugin scaffold is a placeholder, and it says so in the file itself rather
// than only in a README nobody reads before assuming it works.
TEST(CliInit, ThePluginSaysItIsNotLoadableYet) {
    CliWorkspace ws("plugin");

    ASSERT_EQ(RunCli({"init", "--type=plugin", "--yes"}).exit_code, kSuccess);

    EXPECT_TRUE(OutputContains(ws.Read("README.md"), "does not load plugins written in JavaScript"));
    EXPECT_TRUE(OutputContains(ws.Read("src/index.js"), "no JavaScript plugin API"));
}

// ---------------------------------------------------------------------------
// The names on the command line
// ---------------------------------------------------------------------------

// Both spellings, because somebody who has typed one of them once should not have
// to remember which one it was.
TEST(CliInit, BothSpellingsOfAnOptionValueWork) {
    CliWorkspace eq("eq");
    EXPECT_EQ(RunCli({"init", "--type=library", "--yes"}).exit_code, kSuccess);
    EXPECT_TRUE(eq.Exists("README.md"));

    CliWorkspace sep("sep");
    EXPECT_EQ(RunCli({"init", "--type", "library", "--yes"}).exit_code, kSuccess);
    EXPECT_TRUE(sep.Exists("README.md"));
}

// The last one wins, because a command line built up in a loop is the normal way
// a wrapper script produces one, and refusing to run is the wrong answer to a
// repeated flag.
TEST(CliInit, TheLastOfARempeatedOptionWins) {
    CliWorkspace ws("repeat");

    EXPECT_EQ(RunCli({"init", "--type=library", "--type=app", "--yes"}).exit_code, kSuccess);

    EXPECT_TRUE(ws.Exists("src/index.html")) << "the earlier --type was not replaced";
    EXPECT_FALSE(ws.Exists("README.md"));
}

// A name that is nearly right is answered with the name that is right, because
// the list of valid names does not tell somebody which of them they meant.
TEST(CliInit, ANameMissingOneCharacterIsCorrected) {
    CliWorkspace ws("typo");

    const CliResult result = RunCli({"init", "--type=librry", "--yes"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Did you mean 'library'?"))
        << "no correction offered: " << result.err;
    EXPECT_FALSE(ws.Exists("README.md"));
}

// And the correction is offered for templates too, which is the same question
// asked of a different option and used to have a different answer.
TEST(CliInit, ATemplateNameMissingOneCharacterIsCorrected) {
    CliWorkspace ws("typo2");

    const CliResult result = RunCli({"init", "--template=basc", "--yes"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Did you mean 'basic'?"))
        << "no correction offered: " << result.err;
}

// A name that is wrong by more than a character is not guessed at. A wrong guess
// printed above the list of real names is worse than the list on its own.
TEST(CliInit, ANameThatIsNotCloseIsNotGuessedAt) {
    CliWorkspace ws("typo3");

    const CliResult result = RunCli({"init", "--type=framework", "--yes"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(OutputContains(result.err, "Did you mean"));
    EXPECT_TRUE(OutputContains(result.err, "app, library, plugin"))
        << "the list of real names is what somebody needs: " << result.err;
}

// Every name the command rejects has to name the ones it accepts, and every one
// of them has to be a name that works.
TEST(CliInit, ARejectedNameListsTheOnesThatWork) {
    CliWorkspace ws("list");

    const CliResult type = RunCli({"init", "--type=nope", "--yes"});
    EXPECT_EQ(type.exit_code, kUsageError);
    for (const std::string& name : kTypes) {
        EXPECT_TRUE(OutputContains(type.err, name)) << "did not offer " << name;
    }

    const CliResult tmpl = RunCli({"init", "--template=nope", "--yes"});
    EXPECT_EQ(tmpl.exit_code, kUsageError);
    for (const std::string& name : kTemplates) {
        EXPECT_TRUE(OutputContains(tmpl.err, name)) << "did not offer " << name;
    }
}

// A flag with no value is a half-typed command line, and reading the next flag as
// a name turns a clear refusal into a complaint about spelling.
TEST(CliInit, AFlagWithNoValueIsRefusedRatherThanGuessedAt) {
    CliWorkspace ws("novalue");

    const CliResult alone = RunCli({"init", "--template", "--yes"});
    EXPECT_EQ(alone.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(alone.err, "requires a value"));

    const CliResult trailing = RunCli({"init", "--type="});
    EXPECT_EQ(trailing.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(trailing.err, "requires a value"));
}

// The empty case is separate from the missing case because the argument after an
// empty value belongs to something else. "--type=" followed by "foo" is a
// missing value and a stray positional; the missing value is the one reported,
// because it is the one that made the rest of the command line undecidable, and
// "foo" is never treated as a directory.
TEST(CliInit, AnEmptyValueDoesNotSwallowTheNextArgument) {
    CliWorkspace ws("swallow");

    const CliResult result = RunCli({"init", "--type=", "foo"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "requires a value"));
    EXPECT_FALSE(ws.Exists("foo"));
}

// A positional argument is not accepted, and "guchho init my-app" is the
// command that used to work. Refusing it names the way out — the command writes
// where it is standing, so a project directory is made first and this is run
// inside it — and writes nothing on the strength of a command line that was not
// understood.
TEST(CliInit, AStrayArgumentIsRefused) {
    CliWorkspace ws("stray");

    const CliResult result = RunCli({"init", "my-app"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "my-app"));
    EXPECT_TRUE(OutputContains(result.err, "current working directory"));
    EXPECT_FALSE(ws.Exists("my-app"));
    EXPECT_FALSE(ws.Exists("guchho.config.js"));
}

// ---------------------------------------------------------------------------
// Refusing to write into somebody else's directory
// ---------------------------------------------------------------------------

// The one outcome this command must never have: a directory with a file in it that
// the scaffold would not have written is somebody's work, and it is left alone
// until they say otherwise.
TEST(CliInit, ADirectoryHoldingSomethingElseIsRefused) {
    CliWorkspace ws("foreign");
    ws.Write("notes.txt", "mine\n");

    const CliResult result = RunCli({"init"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "not empty"));
    EXPECT_EQ(ws.Read("notes.txt"), std::string("mine\n"));
    EXPECT_FALSE(ws.Exists("src/index.html"));
    EXPECT_FALSE(ws.Exists("guchho.config.js"));
}

// The refusal has to name the word that changes it, or it is a dead end. The
// file is inside the directory being initialized — the working directory — and
// that is the whole of the rule now: with no second directory to point at, the
// one directory there is holds this one refusal case.
TEST(CliInit, TheRefusalNamesTheFlagThatChangesIt) {
    CliWorkspace ws("hint");
    ws.Write("notes.txt", "mine\n");

    const CliResult result = RunCli({"init"});

    EXPECT_EQ(result.exit_code, kUsageError) << result.out << result.err;
    EXPECT_TRUE(OutputContains(result.err, "--force"))
        << "the only way forward is not in the message: " << result.err;
    EXPECT_FALSE(ws.Exists("src/index.html"));
}

// And the word works: the file that was in the way is still there afterwards,
// because --force means write the project here, not replace the directory.
TEST(CliInit, ForceKeepsTheFilesThatWereAlreadyThere) {
    CliWorkspace ws("force-keep");
    ws.Write("notes.txt", "mine\n");

    EXPECT_EQ(RunCli({"init", "--force", "--yes"}).exit_code, kSuccess);

    EXPECT_EQ(ws.Read("notes.txt"), std::string("mine\n"));
    EXPECT_TRUE(ws.Exists("src/index.html"));
}

// A directory holding part of a project is somebody partway through making one,
// and it is a project this command can finish rather than somebody's work.
TEST(CliInit, ADirectoryHoldingPartOfAProjectIsFinishedNotRefused) {
    CliWorkspace ws("partial");
    ws.MakeDir("src");

    RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNode);
    EXPECT_EQ(RunCli({"init", "--yes"}).exit_code, kSuccess);

    EXPECT_TRUE(ws.Exists("src/index.html"));
    EXPECT_TRUE(ws.Exists("guchho.config.js"));
}

// The boundary of the rule above, and the side of it that is deliberately
// permissive. What is compared is the top level of the directory and nothing
// deeper: a directory that already has a "src" in it is somebody partway through
// making a project, and the file they put inside it is theirs and stays theirs —
// but the scaffold writes beside it rather than refusing, because refusing there
// would break the ordinary case of editing a starter and re-running it. The
// existing file is still not touched.
TEST(CliInit, AFileInsideOurOwnDirectoryIsWrittenBesideNotRefused) {
    CliWorkspace ws("inside");
    ws.Write("src/keep.txt", "mine\n");

    const CliResult result = RunCli({"init", "--yes"});

    EXPECT_EQ(result.exit_code, kSuccess) << result.out << result.err;
    EXPECT_EQ(ws.Read("src/keep.txt"), std::string("mine\n"));
    EXPECT_TRUE(ws.Exists("src/index.html")) << "the project was not written beside it";
}

// And the other side of the same boundary: one more name at the top level, and
// the directory stops being a project this command may write into.
TEST(CliInit, OneMoreNameAtTheTopLevelIsEnoughToRefuse) {
    CliWorkspace ws("toplevel");
    ws.Write("src/keep.txt", "mine\n");
    ws.Write("notes.txt", "mine\n");

    const CliResult result = RunCli({"init", "--yes"});

    EXPECT_EQ(result.exit_code, kUsageError) << result.out << result.err;
    EXPECT_EQ(ws.Read("notes.txt"), std::string("mine\n"));
    EXPECT_EQ(ws.Read("src/keep.txt"), std::string("mine\n"));
    EXPECT_FALSE(ws.Exists("src/index.html"));
}

// Running the command again over its own work is the normal way this gets used —
// somebody adds a file to a starter and re-runs to get the new template — and it
// must not cost them the file they added.
TEST(CliInit, ARerunOverItsOwnProjectIsHarmless) {
    CliWorkspace ws("rerun");

    ASSERT_EQ(RunCli({"init", "--yes"}).exit_code, kSuccess);
    const std::string first = ws.Read("src/main.js");

    for (int run = 0; run < 3; run++) {
        const CliResult again = RunCli({"init", "--yes"});
        EXPECT_EQ(again.exit_code, kSuccess) << "run " << run;
        EXPECT_EQ(ws.Read("src/main.js"), first) << "run " << run << " changed the file";
    }
}

// A file added to a finished project makes the directory somebody's, and the
// third run is the one that notices.
TEST(CliInit, AFileAddedAfterwardsMakesTheProjectSomebodyElses) {
    CliWorkspace ws("added");
    ASSERT_EQ(RunCli({"init", "--yes"}).exit_code, kSuccess);

    ws.Write("my-work.txt", "mine\n");
    const CliResult result = RunCli({"init", "--yes"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_EQ(ws.Read("my-work.txt"), std::string("mine\n"));
}

// The three names the scaffold writes are counted once each, not once per file.
// Three files live under src, and a directory holding nothing but src is a
// project this command can finish.
TEST(CliInit, OneDirectoryCountsOnceHoweverManyFilesAreUnderIt) {
    CliWorkspace ws("count");
    ws.Write("src/main.js", "// mine\n");

    EXPECT_EQ(RunCli({"init", "--yes"}).exit_code, kSuccess);

    EXPECT_EQ(ws.Read("src/main.js"), std::string("// mine\n"));
    EXPECT_TRUE(ws.Exists("src/index.html"));
}

// A path the scaffold wants to write where a directory sits is a file-system
// no, and the answer has to be the run's, not a new refusal invented about the
// command line. This used to point init at a directory that was a file; the
// command no longer takes a directory, so the same refusal is produced by a
// subdirectory that is named like a file: "src/main.js" is a directory, which
// is a name the scaffold owns, so it gets past the not-empty check and then
// cannot be opened as a file.
TEST(CliInit, AFileInTheWayOfTheScaffoldIsAFailureNotAUsageError) {
    CliWorkspace ws("infront");
    ws.MakeDir("src/main.js");

    const CliResult result = RunCli({"init", "--yes"});

    EXPECT_EQ(result.exit_code, kBuildFailure)
        << "the request was fine and the file system refused: " << result.err;
}

// The other half of the same boundary, and the true successor of the old
// "target is a file" test: a *subdirectory* the scaffold needs — "src" — is a
// regular file, so no file can be opened underneath it even though the top
// level is entirely the scaffold's own.
TEST(CliInit, ASubdirectoryThatIsAFileIsAFailureNotAUsageError) {
    CliWorkspace ws("srcfile");
    ws.Write("src", "a file, not a directory\n");

    const CliResult result = RunCli({"init", "--yes"});

    EXPECT_EQ(result.exit_code, kBuildFailure)
        << "the request was fine and the file system refused: " << result.err;
    EXPECT_EQ(ws.Read("src"), std::string("a file, not a directory\n"));
}

// A package name cannot be derived from a directory named entirely out of
// characters npm rejects, and a library written there must be refused rather
// than handed a package.json that npm would not accept. The command no longer
// takes a directory, so this is the one way a run can be pointed at such a
// name: by standing in it.
TEST(CliInit, ANameNpmCannotUseIsRefusedForMetadata) {
    CliWorkspace ws("badname");
    ws.MakeDir("@@");

    // The workspace moved the process into the workspace root; this moves it
    // one directory further, into a name NpmPackageName has no answer for. The
    // workspace restores the process directory when it is torn down, so nothing
    // else in the test observes the move.
    std::error_code ec;
    std::filesystem::current_path(CliWorkspace::Native(ws.At("@@")), ec);
    ASSERT_FALSE(ec) << "could not enter the ill-named directory";

    const CliResult result = RunCli({"init", "--type=library", "--yes"});

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_FALSE(ws.Exists("@@/package.json"));
    EXPECT_TRUE(OutputContains(result.err, "package name"));
}

// ---------------------------------------------------------------------------
// Never asking, and never waiting
// ---------------------------------------------------------------------------

// The run a build script makes. It has no terminal, so there is nobody to ask,
// and the worst thing this command could do is sit there.
TEST(CliInit, ARunWithNoTerminalUsesTheDefaultsAndDoesNotWait) {
    CliWorkspace ws("notty");

    RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNode);
    const CliResult result = RunCli({"init"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("guchho.config.js"));
    EXPECT_FALSE(OutputContains(result.out, "Project name"));
}

// --yes is the same promise made in advance, and it holds whether or not there is
// a terminal to ask on. The text waiting on the input is not an answer to
// anything, so the project lands in the working directory and nowhere else.
TEST(CliInit, YesReadsNothingFromStandardInput) {
    CliWorkspace ws("yes");

    RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNode);
    const CliResult result = RunCliWithStdin({"init", "--yes"}, "a different answer\n");

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("guchho.config.js"))
        << "the project did not land in the working directory";
    EXPECT_FALSE(ws.Exists("a different answer"))
        << "an answer was read from the input and used as a directory name";
    const std::string got = Tree(ws);
    EXPECT_TRUE(got == Join(Sorted({"guchho.config.js", "src/index.html", "src/main.js",
                                    "src/style.css"})))
        << "wrote [" << got << "]";
}

// And the same target seen from the other side: the files are the workspace's
// own, at its root, and nothing is nested under a directory the command was
// given. A project that landed somewhere else — under a name, under a path —
// would show up here as a tree that is not exactly this one.
TEST(CliInit, TheWorkingDirectoryIsTheTarget) {
    CliWorkspace ws("cwd");

    RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNode);
    ASSERT_EQ(RunCli({"init", "--yes"}).exit_code, kSuccess);

    const std::string got = Tree(ws);
    EXPECT_TRUE(got == Join(Sorted({"guchho.config.js", "src/index.html", "src/main.js",
                                    "src/style.css"})))
        << "wrote [" << got << "]";
}

// And the two together: a terminal that has been redirected is not a terminal,
// and the text waiting on the input is not an answer to anything.
TEST(CliInit, StandardInputIsNotReadWhenThereIsNoTerminal) {
    CliWorkspace ws("nostdin");

    RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNode);
    const CliResult result = RunCliWithStdin({"init"}, "something-else\n");

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("guchho.config.js"));
    EXPECT_FALSE(OutputContains(result.out, "something-else"));
}

// ---------------------------------------------------------------------------
// What the command says
// ---------------------------------------------------------------------------

// A file it did not write is a different outcome from a file it did, and the two
// are told apart by name rather than by the absence of a line.
TEST(CliInit, AFileItLeftAloneIsNamedAsSkipped) {
    CliWorkspace ws("skip");
    ws.Write("src/main.js", "// mine\n");

    const CliResult result = RunCli({"init", "--yes"});

    EXPECT_TRUE(OutputContains(result.out, "Skip src/main.js"));
    EXPECT_TRUE(OutputContains(result.out, "Create src/index.html"));
}

// A file it replaced is not a file it created, and somebody who asked for an
// overwrite is owed the knowledge that one happened.
TEST(CliInit, AFileItReplacedIsNamedAsOverwritten) {
    CliWorkspace ws("over");
    ws.Write("src/main.js", "// mine\n");

    const CliResult result = RunCli({"init", "--force", "--yes"});

    EXPECT_TRUE(OutputContains(result.out, "Overwrite src/main.js"))
        << "the overwrite was not reported: " << result.out;
    EXPECT_NE(ws.Read("src/main.js"), std::string("// mine\n"));
}

// The word in the middle of the summary is the one thing that differs between the
// three kinds of project, and "Project" after writing a library is true and says
// nothing.
TEST(CliInit, TheSummarySaysWhichOfTheThreeThingsHappened) {
    for (const std::string& type : kTypes) {
        CliWorkspace ws("summary");

        const CliResult result = RunCli({"init", "--type=" + type, "--yes"});

        // The three names as the summary spells them, which is not the spelling
        // on the command line: a sentence starts with a capital.
        const std::string word = type == "app" ? "Project"
                                  : type == "library" ? "Library"
                                                      : "Plugin";
        EXPECT_TRUE(OutputContains(result.out, word + " initialized successfully."))
            << type << ": " << result.out;
    }
}

// The advice is the next thing to run, and it depends on what was made: an app is
// served, and the other two are built.
TEST(CliInit, TheNextStepDependsOnTheType) {
    CliWorkspace app("next-app");
    EXPECT_TRUE(OutputContains(RunCli({"init", "--yes"}).out, "guchho dev"));

    CliWorkspace lib("next-lib");
    EXPECT_TRUE(OutputContains(RunCli({"init", "--type=library", "--yes"}).out, "guchho build"));
}

// There is no directory to change into, because the project was written into
// the one the command was standing in. A "cd ." would be advice nobody needs,
// and a named directory would be one this command never chose.
TEST(CliInit, TheNextStepNeverSaysToChangeDirectory) {
    CliWorkspace app("next-app");
    const CliResult as_app = RunCli({"init", "--yes"});
    EXPECT_EQ(as_app.exit_code, kSuccess);
    EXPECT_FALSE(OutputContains(as_app.out, "cd ")) << as_app.out;

    CliWorkspace lib("next-lib");
    const CliResult as_lib = RunCli({"init", "--type=library", "--yes"});
    EXPECT_EQ(as_lib.exit_code, kSuccess);
    EXPECT_FALSE(OutputContains(as_lib.out, "cd ")) << as_lib.out;
}

// The help has to carry the two lists, because they are the only two things a
// person has to choose and each of the twelve combinations is a different project.
TEST(CliInit, TheHelpListsEveryTypeAndTemplate) {
    CliWorkspace ws("help");

    const CliResult result = RunCli({"init", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    for (const std::string& type : kTypes) {
        EXPECT_TRUE(OutputContains(result.out, type)) << "the help does not offer " << type;
    }
    for (const std::string& tmpl : kTemplates) {
        EXPECT_TRUE(OutputContains(result.out, tmpl)) << "the help does not offer " << tmpl;
    }
    EXPECT_TRUE(OutputContains(result.out, "--force"));
    EXPECT_TRUE(OutputContains(result.out, "--yes"));
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho init [options]"));
    EXPECT_FALSE(OutputContains(result.out, "[directory]"))
        << "the help offers a directory interface this command does not have";
}

// -h is the short spelling of the same question and gets the same answer, and
// asking does not make a project.
//
// The two outputs are not compared for equality. The first capture in a test
// picks up the test runner's own "[ RUN ]" line, which was still sitting in the
// stdio buffer for the descriptor when the capture was installed
// (include/test/helpers/cli_test.hpp:276) — so the two would differ by exactly
// that line and by nothing about the command. What is compared is the part that
// answers the question.
TEST(CliInit, HelpIsTheSameWithEitherSpelling) {
    CliWorkspace ws("h");

    const CliResult long_form  = RunCli({"init", "--help"});
    const CliResult short_form = RunCli({"init", "-h"});

    EXPECT_EQ(long_form.exit_code, kSuccess);
    EXPECT_EQ(short_form.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(long_form.out, "Usage: guchho init [options]"))
        << long_form.out;
    EXPECT_TRUE(OutputContains(short_form.out, "Usage: guchho init [options]"))
        << short_form.out;
    EXPECT_FALSE(OutputContains(long_form.out, "[directory]")) << long_form.out;
    EXPECT_FALSE(ws.Exists("guchho.config.js")) << "asking for help wrote a project";
}

// A flag nobody has heard of is refused rather than ignored, and the refusal
// points at the place where the real list is.
TEST(CliInit, AnUnknownFlagIsRefusedAndPointsAtTheHelp) {
    CliWorkspace ws("unknown");

    const CliResult result = RunCli({"init", "--nope", "--yes"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "--nope"));
    EXPECT_TRUE(OutputContains(result.err, "guchho init --help"));
    EXPECT_FALSE(ws.Exists("guchho.config.js"));
}

// A flag nobody has heard of is refused rather than ignored, and the refusal
// points at the place where the real list is.
//
// The whole command line is abandoned, not just the bad flag. A flag that is not
// a flag means the request is not the one that was written down, and writing four
// files on the strength of the rest of it would leave a project on disk that
// nobody asked for and cannot tell the shape of. Saying what is wrong and writing
// nothing is the recoverable outcome; the files are the part that is not.
TEST(CliInit, AnUnknownFlagStopsTheCommandLineBeingUsedAtAll) {
    CliWorkspace ws("unknown2");

    const CliResult result = RunCli({"init", "--type=library", "--nope", "--yes"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "--nope"));
    EXPECT_EQ(Tree(ws), "");
}

// ---------------------------------------------------------------------------
// The config format
// ---------------------------------------------------------------------------

// The four combinations of the detection table end in exactly one of the two
// config files, and it is the one the table names: node or bun in an empty
// directory writes the JavaScript config, and neither writes the JSON one. The
// detector is pinned by each loop rather than left to the developer's machine.
TEST(CliInit, TheConfigFileFollowsTheRuntimeThatWasFound) {
    const struct {
        guchho::runtime::JavaScriptRuntime runtime;
        const char*                        file;
    } cases[] = {
        {guchho::runtime::JavaScriptRuntime::kNode, "guchho.config.js"},
        {guchho::runtime::JavaScriptRuntime::kBun, "guchho.config.js"},
        {guchho::runtime::JavaScriptRuntime::kNone, "guchho.json"},
    };

    for (const auto& cs : cases) {
        CliWorkspace        ws("fmt");
        RuntimeDetectorScope scope(cs.runtime);

        const CliResult result = RunCli({"init", "--yes"});
        ASSERT_EQ(result.exit_code, kSuccess) << cs.file;
        EXPECT_TRUE(ws.Exists(cs.file)) << cs.file;
        const std::string other = cs.file == std::string("guchho.config.js")
            ? std::string("guchho.json")
            : std::string("guchho.config.js");
        EXPECT_FALSE(ws.Exists(other)) << "a run for " << cs.file << " also wrote " << other;
    }
}

// The command says which format it chose, in the same indentation as the file
// list, so the decision is never a guess made from whichever file turns up in
// the listing.
TEST(CliInit, TheOutputNamesTheConfigFileThatWasChosen) {
    CliWorkspace ws("fmt-out");
    {
        RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNode);
        const CliResult     result = RunCli({"init", "--yes"});
        EXPECT_EQ(result.exit_code, kSuccess) << result.out << result.err;
        EXPECT_TRUE(OutputContains(result.out, "Config: guchho.config.js"))
            << result.out;
    }

    CliWorkspace ws2("fmt-out2");
    {
        RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNone);
        const CliResult     result = RunCli({"init", "--yes"});
        EXPECT_EQ(result.exit_code, kSuccess) << result.out << result.err;
        EXPECT_TRUE(OutputContains(result.out, "Config: guchho.json"))
            << result.out;
    }
}

// The JSON fallback is not a config that parses in principle only: a project
// configured by guchho.json builds, which is the acceptance check for "both
// formats are equivalent" that would notice a field the two forms read
// differently.
TEST(CliInit, AJsonConfiguredProjectBuilds) {
    CliWorkspace        ws("json-build");
    RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNone);

    ASSERT_EQ(RunCli({"init", "--type=library", "--template=ts", "--yes"}).exit_code,
              kSuccess);

    EXPECT_TRUE(ws.Exists("guchho.json"));

    const CliResult build = RunCli({"build"});
    EXPECT_EQ(build.exit_code, kSuccess) << build.out << build.err;
    EXPECT_TRUE(ws.Exists("dist/index.js"));
}

// Detection picks the default for files that need to be generated, and nothing
// else. A directory holding a config of the other format is somebody's
// directory — exactly as if it held notes.txt — and Node being installed does
// not hand that file to the scaffold. Without force the directory is refused;
// with force the generated config is added beside it, and the other format's
// file is never touched in either case.
TEST(CliInit, AnExistingConfigOfTheOtherFormatIsLeftAlone) {
    CliWorkspace ws("keep-format");
    const std::string theirs = "{ \"build\": { \"outdir\": \"out\" } }\n";
    ws.Write("guchho.json", theirs);

    RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNode);

    const CliResult refused = RunCli({"init", "--yes"});
    EXPECT_EQ(refused.exit_code, kUsageError) << refused.out << refused.err;
    EXPECT_TRUE(OutputContains(refused.err, "not empty")) << refused.err;
    EXPECT_EQ(ws.Read("guchho.json"), theirs);
    EXPECT_FALSE(ws.Exists("guchho.config.js"));

    const CliResult forced = RunCli({"init", "--force", "--yes"});
    EXPECT_EQ(forced.exit_code, kSuccess) << forced.out << forced.err;
    EXPECT_TRUE(ws.Exists("guchho.config.js"));
    EXPECT_EQ(ws.Read("guchho.json"), theirs)
        << "--force wrote over the other format's file";
}

// And the other direction: a directory holding somebody's guchho.config.js is
// just as much somebody's when the detected format is the JSON one.
TEST(CliInit, AJavaScriptConfigOfItsOwnIsAlsoLeftAlone) {
    CliWorkspace ws("keep-format2");
    const std::string theirs = "export default { build: { outdir: 'out' } };\n";
    ws.Write("guchho.config.js", theirs);

    RuntimeDetectorScope scope(guchho::runtime::JavaScriptRuntime::kNone);

    const CliResult refused = RunCli({"init", "--yes"});
    EXPECT_EQ(refused.exit_code, kUsageError) << refused.out << refused.err;

    const CliResult forced = RunCli({"init", "--force", "--yes"});
    EXPECT_EQ(forced.exit_code, kSuccess) << forced.out << forced.err;
    EXPECT_TRUE(ws.Exists("guchho.json"));
    EXPECT_EQ(ws.Read("guchho.config.js"), theirs);
}

// ---------------------------------------------------------------------------
// The scaffolding layer on its own
// ---------------------------------------------------------------------------

// The file list is a function of three values and nothing else, which is what
// makes it testable without a file system. Two calls with the same request have
// to produce the same bytes, on this machine and on the next.
TEST(Scaffold, TheSameRequestProducesTheSameFiles) {
    guchho::scaffold::ScaffoldRequest req;
    req.type = guchho::scaffold::ProjectType::kLibrary;
    req.tmpl = guchho::scaffold::ProjectTemplate::kTypeScript;
    req.name = "same";

    const guchho::scaffold::FileList first  = guchho::scaffold::BuildFileList(req);
    const guchho::scaffold::FileList second = guchho::scaffold::BuildFileList(req);

    ASSERT_EQ(first.files.size(), second.files.size());
    for (size_t i = 0; i < first.files.size(); i++) {
        EXPECT_EQ(first.files[i].path, second.files[i].path);
        EXPECT_EQ(first.files[i].contents, second.files[i].contents);
    }
}

// Two runs, two machines, one list. There is no clock and no randomness in the
// scaffolder, and this is the test that would notice either being added.
TEST(Scaffold, TheListHasNoTimestampInIt) {
    guchho::scaffold::ScaffoldRequest req;
    req.name = "fixed";

    const guchho::scaffold::FileList list = guchho::scaffold::BuildFileList(req);

    for (const guchho::scaffold::ProjectFile& file : list.files) {
        EXPECT_FALSE(OutputContains(file.contents, "20")) << file.path;
    }
}

// The scaffold writes into the directory it is given and nowhere else, which is
// what makes it safe to point at a working directory.
TEST(Scaffold, EveryPathIsRelativeAndInsideTheTarget) {
    guchho::scaffold::ScaffoldRequest req;
    req.name = "n";

    const guchho::scaffold::FileList list = guchho::scaffold::BuildFileList(req);

    for (const guchho::scaffold::ProjectFile& file : list.files) {
        EXPECT_FALSE(file.path.empty()) << "a file with no path";
        EXPECT_NE(file.path.front(), '/') << "an absolute path: " << file.path;
        EXPECT_NE(file.path.front(), '\\') << "a rooted path: " << file.path;
        EXPECT_EQ(file.path.find(".."), std::string::npos) << "a path that climbs out: " << file.path;
    }
}

// The two formats are written from one list of fields, and a field dropped or
// renamed in only one of the two renderers is a field the project stops
// carrying in that format alone. Parsing both texts and comparing them field
// by field is the test that would notice that drift, for each project type.
TEST(Scaffold, TheTwoConfigFormatsCarryTheSameBuildFields) {
    const struct {
        guchho::scaffold::ProjectType     type;
        guchho::scaffold::ProjectTemplate tmpl;
    } cases[] = {
        {guchho::scaffold::ProjectType::kApp, guchho::scaffold::ProjectTemplate::kBasic},
        {guchho::scaffold::ProjectType::kLibrary, guchho::scaffold::ProjectTemplate::kTypeScript},
        {guchho::scaffold::ProjectType::kPlugin, guchho::scaffold::ProjectTemplate::kBasic},
    };

    for (const auto& cs : cases) {
        guchho::scaffold::ScaffoldRequest req;
        req.type = cs.type;
        req.tmpl = cs.tmpl;
        req.name = "equiv";

        req.config = guchho::scaffold::ProjectConfig::kJavaScript;
        const guchho::scaffold::FileList js =
            guchho::scaffold::BuildFileList(req);

        req.config = guchho::scaffold::ProjectConfig::kJson;
        const guchho::scaffold::FileList json =
            guchho::scaffold::BuildFileList(req);

        ASSERT_EQ(js.files.size(), json.files.size());

        const std::string js_name = std::string(guchho::scaffold::ConfigFileName(
            guchho::scaffold::ProjectConfig::kJavaScript));
        const std::string json_name = std::string(guchho::scaffold::ConfigFileName(
            guchho::scaffold::ProjectConfig::kJson));

        const guchho::scaffold::ProjectFile* js_file = nullptr;
        const guchho::scaffold::ProjectFile* json_file = nullptr;
        for (const guchho::scaffold::ProjectFile& file : js.files) {
            if (file.path == js_name) js_file = &file;
        }
        for (const guchho::scaffold::ProjectFile& file : json.files) {
            if (file.path == json_name) json_file = &file;
        }
        ASSERT_TRUE(js_file != nullptr) << "the JavaScript request wrote no " << js_name;
        ASSERT_TRUE(json_file != nullptr) << "the JSON request wrote no " << json_name;

        const std::map<std::string, std::string> js_fields =
            ConfigFields(js_file->contents, /*json=*/false);
        const std::map<std::string, std::string> json_fields =
            ConfigFields(json_file->contents, /*json=*/true);

        EXPECT_EQ(js_fields.size(), json_fields.size())
            << "the two formats list different numbers of build fields";
        for (const auto& [key, value] : js_fields) {
            auto it = json_fields.find(key);
            EXPECT_TRUE(it != json_fields.end())
                << "the JSON format lost a field: " << key;
            if (it != json_fields.end()) {
                EXPECT_EQ(it->second, value)
                    << "a build field differs between the formats: " << key;
            }
        }
    }
}

} // namespace cli::test
