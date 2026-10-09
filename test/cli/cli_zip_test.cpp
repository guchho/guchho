// =============================================================================
// test/cli/cli_zip_test.cpp — the command that writes an archive
// =============================================================================
//
// Two halves, tested in the two ways the harness offers.
//
// The command line half runs runZip through RunCli and checks what a person
// would see: the exit code, which stream each line went to, and what was left
// on disk. The argument handling is where a zip command has the decisions a
// build does not — which word is an input, what the archive is called when
// nobody named it, and whether a mistake in the spelling is worth 2 or worth 1
// — and none of that is reachable from the core layer.
//
// The service half reaches RunServiceRequest, because a request that arrives
// as a value has its own kind of mistake: a field of the wrong type, a date
// that is a number where the protocol has no room for one, an archive that
// could not be written. Those are answered with an error value rather than
// with a line on a terminal, and the two have to say the same thing about the
// same request.
//
// The archive's own contents are not checked here beyond a name and a method.
// Whether an entry lands under the right name, with the right permissions and
// the right timestamp, is test/core/zip_test.cpp's subject and it reads every
// one of those back with miniz. What is checked here is that the options a
// person typed arrived there: one archive read back with enough in it to prove
// the command did not swallow a flag.
// =============================================================================

#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include "guchho/miniz.hpp"
#include "guchho/service.hpp"

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace cli::test {

using guchho::test::CliWorkspace;
using guchho::test::kBuildFailure;
using guchho::test::kSuccess;
using guchho::test::kUsageError;
using guchho::test::OutputContains;
using guchho::test::RunCli;

namespace {

namespace fs = std::filesystem;

// One entry, read back from a finished archive.
struct Entry {
    std::string   name;
    std::string   contents;
    std::uint16_t method = 0;
    bool          is_dir = false;
    std::time_t   mtime  = 0;
};

// Reads an archive with miniz's reader. Returns false and fills "error" when
// the file is not one, which is itself what several of these tests want to
// know.
bool ReadArchive(const std::string& path, std::vector<Entry>& out, std::string& error)
{
    out.clear();

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
        return false;
    }

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            error = "cannot stat entry " + std::to_string(i);
            mz_zip_reader_end(&zip);
            return false;
        }

        Entry entry;
        entry.name   = stat.m_filename;
        entry.is_dir = stat.m_is_directory != 0;
        entry.method = stat.m_method;
        entry.mtime  = stat.m_time;
        if (!entry.is_dir) {
            size_t          size = 0;
            void*           data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
            if (data == nullptr) {
                error = "cannot extract " + entry.name;
                mz_zip_reader_end(&zip);
                return false;
            }
            entry.contents.assign(static_cast<const char*>(data), size);
            mz_free(data);
        }
        out.push_back(std::move(entry));
    }

    mz_zip_reader_end(&zip);
    return true;
}

// The names in an archive, in the order the central directory lists them.
std::vector<std::string> Names(const std::vector<Entry>& entries)
{
    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const Entry& entry : entries) names.push_back(entry.name);
    return names;
}

bool Contains(const std::vector<std::string>& items, const std::string& value)
{
    for (const std::string& item : items) {
        if (item == value) return true;
    }
    return false;
}

// A workspace with one file in it, which is what most of these tests ask for.
void WriteTree(CliWorkspace& ws, const std::string& dir = "dist")
{
    ws.Write(dir + "/index.html", "<html></html>");
    ws.Write(dir + "/app.js", "let a = 1;\n");
}

// The error string on a service response, or "" when there is none.
std::string ErrorOf(const guchho::service::Value& response)
{
    const guchho::service::Value* error = response.Find("error");
    return error != nullptr && error->IsString() ? error->AsString() : std::string();
}

// One field of a service response, as text. Nothing but "" when it is not a
// string, which for the assertions below reads as "the field is not what it
// should be" rather than crashing on the way to saying so.
std::string TextOf(const guchho::service::Value& response, const std::string& key)
{
    const guchho::service::Value* field = response.Find(key);
    return field != nullptr && field->IsString() ? field->AsString() : std::string();
}

} // namespace

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

TEST(ZipHelpTest, AnswersWithItsOwnUsageAndFlags)
{
    CliWorkspace ws("zip-help");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"zip", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho zip <input...>")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "Create a zip archive")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "-o, --outfile=<path>")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "--level=<0-9>")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "--allow-overwrite")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "guchho zip dist/ -o release.zip")) << result.out;
    // Help is an answer, not a diagnostic, so it belongs on the standard
    // output where a person can pipe it to a pager.
    EXPECT_TRUE(result.err.empty()) << result.err;
}

TEST(ZipHelpTest, HelpWinsOverARestThatIsWrong)
{
    CliWorkspace ws("zip-help-first");

    // --help is read before the arguments are, so a command line that is
    // otherwise refused still explains itself. The alternative — parsing first
    // and answering afterwards — would make help depend on getting the rest of
    // the line right, which is backwards.
    const guchho::test::CliResult result = RunCli({"zip", "--allow-ovewrite", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho zip <input...>")) << result.out;
}

// ---------------------------------------------------------------------------
// What is refused before anything is read from disk
// ---------------------------------------------------------------------------

TEST(ZipUsageTest, IsListedAlongsideTheOtherCommands)
{
    const guchho::test::CliResult result = RunCli({"--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "zip <input...>")) << result.out;
}

TEST(ZipUsageTest, NoInputsIsRefused)
{
    CliWorkspace ws("zip-no-inputs");

    const guchho::test::CliResult result = RunCli({"zip"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "No input files were given")) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "Name at least one file or directory")) << result.err;
    EXPECT_TRUE(result.out.empty()) << result.out;
}

TEST(ZipUsageTest, TwoInputsWithoutAnOutputFileAreRefused)
{
    CliWorkspace ws("zip-two-inputs");
    WriteTree(ws, "one");
    WriteTree(ws, "two");

    const guchho::test::CliResult result = RunCli({"zip", "one", "two"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Multiple inputs need an output file")) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "-o/--outfile")) << result.err;
}

TEST(ZipUsageTest, AFlagThisCommandDoesNotHaveIsRefused)
{
    CliWorkspace ws("zip-unknown-flag");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"zip", "dist", "--allow-ovewrite"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Unknown zip flag: \"--allow-ovewrite\"")) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "guchho zip --help")) << result.err;
    // Refused rather than read as a path: the alternative is an archive with a
    // strange entry in it and no complaint at all.
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

TEST(ZipUsageTest, ALevelOutsideTheRangeIsRefused)
{
    CliWorkspace ws("zip-level-range");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"zip", "dist", "--level=11"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Invalid compression level: \"11\"")) << result.err;
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

TEST(ZipUsageTest, ALevelThatIsNotWholeNumberIsRefused)
{
    CliWorkspace ws("zip-level-text");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"zip", "dist", "--level=6x"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Invalid compression level: \"6x\"")) << result.err;
}

TEST(ZipUsageTest, ALevelWithNoValueIsRefused)
{
    CliWorkspace ws("zip-level-no-value");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"zip", "dist", "--level"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Missing value for --level")) << result.err;
}

TEST(ZipUsageTest, AnOutfileWithNoValueIsRefused)
{
    CliWorkspace ws("zip-outfile-no-value");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"zip", "dist", "-o"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Missing value for -o")) << result.err;
}

TEST(ZipUsageTest, AnEmptyOutfileIsRefused)
{
    CliWorkspace ws("zip-outfile-empty");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"zip", "dist", "-o", ""});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Missing value for --outfile")) << result.err;
    // Nothing was written, because the request could never have named a
    // destination and everything below this point is reached through one.
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

// ---------------------------------------------------------------------------
// What it does when it works
// ---------------------------------------------------------------------------

TEST(ZipRunTest, NamesTheArchiveAfterTheSingleInput)
{
    CliWorkspace ws("zip-default-outfile");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"zip", "dist"});

    EXPECT_EQ(result.exit_code, kSuccess) << result.err;
    EXPECT_TRUE(OutputContains(result.out, "Created dist.zip")) << result.out;
    EXPECT_TRUE(ws.Exists("dist.zip"));
    // The result is the archive, so it belongs on the standard output rather
    // than mixed into the diagnostics on stderr.
    EXPECT_TRUE(result.err.empty()) << result.err;
}

TEST(ZipRunTest, WritesToTheNamedOutfile)
{
    CliWorkspace ws("zip-named-outfile");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"zip", "dist", "-o", "release.zip"});

    EXPECT_EQ(result.exit_code, kSuccess) << result.err;
    EXPECT_TRUE(OutputContains(result.out, "Created release.zip")) << result.out;
    EXPECT_TRUE(ws.Exists("release.zip"));
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

TEST(ZipRunTest, AcceptsBothSpellingsOfTheOutfileFlag)
{
    CliWorkspace ws("zip-outfile-spellings");
    WriteTree(ws);

    const guchho::test::CliResult spaced = RunCli({"zip", "dist", "--outfile", "a.zip"});
    EXPECT_EQ(spaced.exit_code, kSuccess) << spaced.err;
    EXPECT_TRUE(ws.Exists("a.zip"));

    const guchho::test::CliResult joined = RunCli({"zip", "dist", "--outfile=b.zip"});
    EXPECT_EQ(joined.exit_code, kSuccess) << joined.err;
    EXPECT_TRUE(ws.Exists("b.zip"));
}

TEST(ZipRunTest, PutsTheTreeUnderItsOwnName)
{
    CliWorkspace ws("zip-entry-names");
    WriteTree(ws);
    ws.Write("dist/assets/logo.svg", "<svg/>");

    const guchho::test::CliResult result = RunCli({"zip", "dist", "-o", "out.zip"});
    ASSERT_EQ(result.exit_code, kSuccess) << result.err;

    std::vector<Entry> entries;
    std::string        error;
    ASSERT_TRUE(ReadArchive(ws.At("out.zip"), entries, error)) << error;

    const std::vector<std::string> names = Names(entries);
    EXPECT_TRUE(Contains(names, "dist/"));
    EXPECT_TRUE(Contains(names, "dist/index.html"));
    EXPECT_TRUE(Contains(names, "dist/app.js"));
    EXPECT_TRUE(Contains(names, "dist/assets/logo.svg"));
}

TEST(ZipRunTest, CompressionLevelReachesTheArchive)
{
    CliWorkspace ws("zip-level");
    // Repetitive, so that the two ends of the range differ by more than the
    // headers around them.
    ws.Write("dist/bundle.js", std::string(4096, 'a'));

    ASSERT_EQ(RunCli({"zip", "dist", "-o", "stored.zip", "--level=0"}).exit_code, kSuccess);
    ASSERT_EQ(RunCli({"zip", "dist", "-o", "packed.zip", "--level=9"}).exit_code, kSuccess);

    std::vector<Entry> stored;
    std::string        error;
    ASSERT_TRUE(ReadArchive(ws.At("stored.zip"), stored, error)) << error;

    std::vector<Entry> packed;
    ASSERT_TRUE(ReadArchive(ws.At("packed.zip"), packed, error)) << error;

    // 0 means stored rather than deflated, which is the difference a caller
    // asking for it is asking for.
    ASSERT_FALSE(stored.empty());
    EXPECT_EQ(stored.size(), packed.size());
    EXPECT_EQ(stored.back().method, 0);
    EXPECT_EQ(packed.back().method, 8);

    std::error_code ec;
    const auto stored_size = fs::file_size(CliWorkspace::Native(ws.At("stored.zip")), ec);
    const auto packed_size = fs::file_size(CliWorkspace::Native(ws.At("packed.zip")), ec);
    EXPECT_GT(stored_size, packed_size);
}

TEST(ZipRunTest, AnExistingOutputIsAFailure)
{
    CliWorkspace ws("zip-exists");
    WriteTree(ws);
    ws.Write("out.zip", "not an archive");

    const guchho::test::CliResult result = RunCli({"zip", "dist", "-o", "out.zip"});

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "Output file already exists: \"out.zip\""))
        << result.err;
    EXPECT_TRUE(OutputContains(result.err, "--allow-overwrite")) << result.err;
    // Refused, not replaced: a command that overwrote a file nobody said it
    // could would be a command nobody could safely point at a directory with.
    EXPECT_EQ(ws.Read("out.zip"), "not an archive");
}

TEST(ZipRunTest, AllowOverwriteReplacesIt)
{
    CliWorkspace ws("zip-overwrite");
    WriteTree(ws);
    ws.Write("out.zip", "not an archive");

    const guchho::test::CliResult result =
        RunCli({"zip", "dist", "-o", "out.zip", "--allow-overwrite"});

    EXPECT_EQ(result.exit_code, kSuccess) << result.err;

    std::vector<Entry> entries;
    std::string        error;
    EXPECT_TRUE(ReadArchive(ws.At("out.zip"), entries, error)) << error;
}

TEST(ZipRunTest, AnInputThatIsNotThereIsAFailure)
{
    CliWorkspace ws("zip-missing-input");

    const guchho::test::CliResult result = RunCli({"zip", "absent", "-o", "out.zip"});

    // The spelling was right and the path was not, which is the line between
    // 1 and 2 for this command.
    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "Input does not exist")) << result.err;
    EXPECT_FALSE(ws.Exists("out.zip"));
}

TEST(ZipRunTest, TwoInputsWithTheSameNameAreRefused)
{
    CliWorkspace ws("zip-colliding-roots");
    WriteTree(ws, "one/dist");
    WriteTree(ws, "two/dist");

    const guchho::test::CliResult result =
        RunCli({"zip", "one/dist", "two/dist", "-o", "out.zip"});

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "Two inputs would produce the same archive path"))
        << result.err;
    EXPECT_FALSE(ws.Exists("out.zip"));
}

TEST(ZipRunTest, LeavesNoTemporaryFileBehind)
{
    CliWorkspace ws("zip-temp-file");
    WriteTree(ws);

    // A failure after the archive has been named, which is the point at which
    // a temporary file would exist if one were ever created.
    ASSERT_EQ(RunCli({"zip", "absent", "-o", "out.zip"}).exit_code, kBuildFailure);
    EXPECT_EQ(ws.Tree().find(".tmp"), std::string::npos) << ws.Tree();

    // And the same on the way through a success, so a rename that quietly
    // failed to remove its own is caught rather than compounding.
    ASSERT_EQ(RunCli({"zip", "dist", "-o", "out.zip"}).exit_code, kSuccess);
    EXPECT_EQ(ws.Tree().find(".tmp"), std::string::npos) << ws.Tree();
}

TEST(ZipRunTest, WarnsWhenTheOutputIsInsideAnInputTree)
{
    CliWorkspace ws("zip-output-inside");
    WriteTree(ws);

    // The first run creates the archive; the second finds it standing inside
    // the tree it is about to read.
    ASSERT_EQ(RunCli({"zip", "dist", "-o", "dist/out.zip"}).exit_code, kSuccess);

    const guchho::test::CliResult second =
        RunCli({"zip", "dist", "-o", "dist/out.zip", "--allow-overwrite"});

    EXPECT_EQ(second.exit_code, kSuccess) << second.err;
    EXPECT_TRUE(OutputContains(second.out, "Created dist/out.zip")) << second.out;
    // On stderr, next to the other diagnostics, and not merged into the line
    // that says the archive was made.
    EXPECT_TRUE(OutputContains(second.err, "which is the archive being written")) << second.err;
}

// ---------------------------------------------------------------------------
// The same request, over the service
// ---------------------------------------------------------------------------

TEST(ZipServiceTest, AnswersWithAPathAndASize)
{
    CliWorkspace ws("zip-service-ok");
    WriteTree(ws);

    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
        }));

    EXPECT_EQ(ErrorOf(response), "");
    EXPECT_EQ(TextOf(response, "path"), "out.zip");

    // A string rather than a number: the protocol's number is a 32-bit integer
    // and an archive past two gigabytes is a real thing to have written.
    const guchho::service::Value* size = response.Find("size");
    ASSERT_TRUE(size != nullptr && size->IsString()) << "size should be a decimal string";
    EXPECT_GT(std::stoull(size->AsString()), 0ull);

    const guchho::service::Value* warnings = response.Find("warnings");
    ASSERT_TRUE(warnings != nullptr && warnings->IsArray());
    EXPECT_TRUE(warnings->AsArray().empty());

    EXPECT_TRUE(ws.Exists("out.zip"));
}

TEST(ZipServiceTest, NeedsAnInputList)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"outFile", guchho::service::Value::String("out.zip")},
        }));

    EXPECT_EQ(ErrorOf(response), "\"inputs\" must be an array of strings");
}

TEST(ZipServiceTest, RefusesAnEntryThatIsNotAPath)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::Number(3)})},
            {"outFile", guchho::service::Value::String("out.zip")},
        }));

    EXPECT_EQ(ErrorOf(response), "every entry in \"inputs\" must be a string");
}

TEST(ZipServiceTest, NeedsAnOutfile)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
        }));

    EXPECT_EQ(ErrorOf(response), "\"outFile\" must be a string");
}

TEST(ZipServiceTest, RefusesALevelThatIsNotANumber)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"level", guchho::service::Value::String("9")},
        }));

    EXPECT_EQ(ErrorOf(response), "\"level\" must be a number");
}

TEST(ZipServiceTest, RefusesAnOverwriteThatIsNotABoolean)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"overwrite", guchho::service::Value::String("yes")},
        }));

    EXPECT_EQ(ErrorOf(response), "\"overwrite\" must be a boolean");
}

TEST(ZipServiceTest, RefusesADateThatIsNotWholeSeconds)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"date", guchho::service::Value::String("yesterday")},
        }));

    EXPECT_EQ(ErrorOf(response), "\"date\" must be a whole number of seconds");
}

TEST(ZipServiceTest, RefusesADateSentAsANumber)
{
    // The protocol's number is a 32-bit integer, so a date cannot travel as
    // one — a host that sent it anyway would have it truncated rather than
    // refused, and would get an archive with a plausible, wrong timestamp.
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"date", guchho::service::Value::Number(1500000000)},
        }));

    EXPECT_EQ(ErrorOf(response),
              "\"date\" must be seconds since the Unix epoch, sent as a string");
}

TEST(ZipServiceTest, RefusesANegativeMode)
{
    // A negative number would become an enormous unsigned one on the way into
    // the core, and the complaint would be about a permission mask nobody asked
    // for rather than about the value that was sent.
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"mode", guchho::service::Value::Number(-1)},
        }));

    EXPECT_EQ(ErrorOf(response), "\"mode\" must not be negative");
}

TEST(ZipServiceTest, TakesADateAndAModeAndWritesTheArchive)
{
    CliWorkspace ws("zip-service-metadata");
    WriteTree(ws);

    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"date", guchho::service::Value::String("1500000000")},
            {"mode", guchho::service::Value::Number(0755)},
        }));

    ASSERT_EQ(ErrorOf(response), "") << ErrorOf(response);
    EXPECT_TRUE(ws.Exists("out.zip"));

    std::vector<Entry> entries;
    std::string        error;
    ASSERT_TRUE(ReadArchive(ws.At("out.zip"), entries, error)) << error;
    ASSERT_FALSE(entries.empty());

    // The only way to tell the date arrived: a request that dropped the field
    // would write each file's own time instead, and every file here has one
    // that is not this.
    constexpr std::int64_t kDate = 1500000000;
    for (const Entry& entry : entries) {
        EXPECT_EQ(static_cast<std::int64_t>(entry.mtime), kDate) << entry.name;
    }
}

TEST(ZipServiceTest, ReportsAFailureWithTheEnginesOwnWords)
{
    CliWorkspace ws("zip-service-failure");

    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("absent")})},
            {"outFile", guchho::service::Value::String("out.zip")},
        }));

    EXPECT_NE(ErrorOf(response), "");
    EXPECT_TRUE(OutputContains(ErrorOf(response), "Input does not exist")) << ErrorOf(response);

    // As an error value rather than as a result with an error field in it, so
    // that unwrap() throws the same BuildFailure every other failing call
    // throws.
    const guchho::service::Value* errors = response.Find("errors");
    ASSERT_TRUE(errors != nullptr && errors->IsArray());
    ASSERT_EQ(errors->AsArray().size(), 1u);

    const guchho::service::Value& message = errors->AsArray().front();
    const guchho::service::Value* id      = message.Find("id");
    ASSERT_TRUE(id != nullptr && id->IsString());
    EXPECT_EQ(id->AsString(), "zip-failed");

    const guchho::service::Value* notes = message.Find("notes");
    ASSERT_TRUE(notes != nullptr && notes->IsArray());

    EXPECT_FALSE(ws.Exists("out.zip"));
}

TEST(ZipServiceTest, CarriesWarningsBack)
{
    CliWorkspace ws("zip-service-warnings");
    WriteTree(ws);

    guchho::cli::RunServiceRequest(guchho::service::Value::Object({
        {"command", guchho::service::Value::String("zip")},
        {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
        {"outFile", guchho::service::Value::String("dist/out.zip")},
    }));

    const guchho::service::Value second =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("zip")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("dist/out.zip")},
            {"overwrite", guchho::service::Value::Bool(true)},
        }));

    ASSERT_EQ(ErrorOf(second), "") << ErrorOf(second);

    const guchho::service::Value* warnings = second.Find("warnings");
    ASSERT_TRUE(warnings != nullptr && warnings->IsArray());
    ASSERT_EQ(warnings->AsArray().size(), 1u);
    EXPECT_TRUE(OutputContains(warnings->AsArray().front().AsString(),
                               "which is the archive being written"));
}

} // namespace cli::test
