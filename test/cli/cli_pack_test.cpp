// =============================================================================
// test/cli/cli_pack_test.cpp — the command that writes an archive
// =============================================================================
//
// Two halves, tested in the two ways the harness offers.
//
// The command line half runs runPack through RunCli and checks what a person
// would see: the exit code, which stream each line went to, and what was left
// on disk. The argument handling is where a pack command has the decisions a
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
// the right timestamp, is test/core/pack_test.cpp's subject and it reads every
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

TEST(PackHelpTest, AnswersWithItsOwnUsageAndFlags)
{
    CliWorkspace ws("pack-help");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho pack <input...>")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "Create an archive")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "--format=<format>")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "only zip is supported")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "-o, --outfile=<path>")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "--level=<0-9>")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "--allow-overwrite")) << result.out;
    EXPECT_TRUE(OutputContains(result.out, "guchho pack dist/ -o release.zip")) << result.out;
    // Help is an answer, not a diagnostic, so it belongs on the standard
    // output where a person can pipe it to a pager.
    EXPECT_TRUE(result.err.empty()) << result.err;
}

TEST(PackHelpTest, HelpWinsOverARestThatIsWrong)
{
    CliWorkspace ws("pack-help-first");

    // --help is read before the arguments are, so a command line that is
    // otherwise refused still explains itself. The alternative — parsing first
    // and answering afterwards — would make help depend on getting the rest of
    // the line right, which is backwards.
    const guchho::test::CliResult result = RunCli({"pack", "--allow-ovewrite", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho pack <input...>")) << result.out;
}

// ---------------------------------------------------------------------------
// What is refused before anything is read from disk
// ---------------------------------------------------------------------------

TEST(PackUsageTest, IsListedAlongsideTheOtherCommands)
{
    const guchho::test::CliResult result = RunCli({"--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "pack <input...>")) << result.out;

    // The old word is not listed: a command that is gone must not be half
    // present in the one text a person reads to find out what they can type.
    EXPECT_FALSE(OutputContains(result.out, "zip <input...>")) << result.out;
}

TEST(PackUsageTest, NoInputsIsRefused)
{
    CliWorkspace ws("pack-no-inputs");

    const guchho::test::CliResult result = RunCli({"pack"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "No input files were given")) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "Name at least one file or directory")) << result.err;
    EXPECT_TRUE(result.out.empty()) << result.out;
}

TEST(PackUsageTest, TwoInputsWithoutAnOutputFileAreRefused)
{
    CliWorkspace ws("pack-two-inputs");
    WriteTree(ws, "one");
    WriteTree(ws, "two");

    const guchho::test::CliResult result = RunCli({"pack", "one", "two"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Multiple inputs need an output file")) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "-o/--outfile")) << result.err;
}

TEST(PackUsageTest, AFlagThisCommandDoesNotHaveIsRefused)
{
    CliWorkspace ws("pack-unknown-flag");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist", "--allow-ovewrite"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Unknown pack flag: \"--allow-ovewrite\"")) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "guchho pack --help")) << result.err;
    // Refused rather than read as a path: the alternative is an archive with a
    // strange entry in it and no complaint at all.
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

TEST(PackUsageTest, ALevelOutsideTheRangeIsRefused)
{
    CliWorkspace ws("pack-level-range");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist", "--level=11"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Invalid compression level: \"11\"")) << result.err;
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

TEST(PackUsageTest, ALevelThatIsNotWholeNumberIsRefused)
{
    CliWorkspace ws("pack-level-text");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist", "--level=6x"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Invalid compression level: \"6x\"")) << result.err;
}

TEST(PackUsageTest, ALevelWithNoValueIsRefused)
{
    CliWorkspace ws("pack-level-no-value");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist", "--level"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Missing value for --level")) << result.err;
}

TEST(PackUsageTest, AnOutfileWithNoValueIsRefused)
{
    CliWorkspace ws("pack-outfile-no-value");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist", "-o"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Missing value for -o")) << result.err;
}

TEST(PackUsageTest, AnEmptyOutfileIsRefused)
{
    CliWorkspace ws("pack-outfile-empty");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist", "-o", ""});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Missing value for --outfile")) << result.err;
    // Nothing was written, because the request could never have named a
    // destination and everything below this point is reached through one.
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

TEST(PackUsageTest, AnUnsupportedFormatIsRefused)
{
    CliWorkspace ws("pack-format-unsupported");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist", "--format=tar"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Unsupported archive format: \"tar\""))
        << result.err;
    EXPECT_TRUE(OutputContains(result.err, "Only \"zip\" is supported")) << result.err;
    // Refused at the spelling, so nothing was read and nothing was written.
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

TEST(PackUsageTest, AFormatWithNoValueIsRefused)
{
    CliWorkspace ws("pack-format-no-value");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist", "--format"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Missing value for --format")) << result.err;
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

TEST(PackUsageTest, AnEmptyFormatIsRefused)
{
    CliWorkspace ws("pack-format-empty");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist", "--format="});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Missing value for --format")) << result.err;
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

TEST(PackUsageTest, TheFormerZipWordSaysWhereItWent)
{
    CliWorkspace ws("zip-word-retired");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"zip", "dist"});

    // Answered rather than falling through as a path. Without this the run
    // would be a build whose entry point is a file called "zip", and the
    // message would be about a file rather than about a spelling.
    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "'guchho zip' has been replaced by 'guchho pack'"))
        << result.err;
    EXPECT_TRUE(OutputContains(result.err, "guchho pack --help")) << result.err;
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

// ---------------------------------------------------------------------------
// What it does when it works
// ---------------------------------------------------------------------------

TEST(PackRunTest, NamesTheArchiveAfterTheSingleInput)
{
    CliWorkspace ws("pack-default-outfile");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist"});

    EXPECT_EQ(result.exit_code, kSuccess) << result.err;
    EXPECT_TRUE(OutputContains(result.out, "Created dist.zip")) << result.out;
    EXPECT_TRUE(ws.Exists("dist.zip"));
    // The result is the archive, so it belongs on the standard output rather
    // than mixed into the diagnostics on stderr.
    EXPECT_TRUE(result.err.empty()) << result.err;
}

TEST(PackRunTest, WritesToTheNamedOutfile)
{
    CliWorkspace ws("pack-named-outfile");
    WriteTree(ws);

    const guchho::test::CliResult result = RunCli({"pack", "dist", "-o", "release.zip"});

    EXPECT_EQ(result.exit_code, kSuccess) << result.err;
    EXPECT_TRUE(OutputContains(result.out, "Created release.zip")) << result.out;
    EXPECT_TRUE(ws.Exists("release.zip"));
    EXPECT_FALSE(ws.Exists("dist.zip"));
}

TEST(PackRunTest, AcceptsBothSpellingsOfTheOutfileFlag)
{
    CliWorkspace ws("pack-outfile-spellings");
    WriteTree(ws);

    const guchho::test::CliResult spaced = RunCli({"pack", "dist", "--outfile", "a.zip"});
    EXPECT_EQ(spaced.exit_code, kSuccess) << spaced.err;
    EXPECT_TRUE(ws.Exists("a.zip"));

    const guchho::test::CliResult joined = RunCli({"pack", "dist", "--outfile=b.zip"});
    EXPECT_EQ(joined.exit_code, kSuccess) << joined.err;
    EXPECT_TRUE(ws.Exists("b.zip"));
}

TEST(PackRunTest, AcceptsBothSpellingsOfTheFormatFlag)
{
    CliWorkspace ws("pack-format-spellings");
    WriteTree(ws);

    const guchho::test::CliResult joined = RunCli({"pack", "dist", "--format=zip"});
    ASSERT_EQ(joined.exit_code, kSuccess) << joined.err;

    const guchho::test::CliResult spaced =
        RunCli({"pack", "dist", "--format", "zip", "-o", "other.zip"});
    ASSERT_EQ(spaced.exit_code, kSuccess) << spaced.err;

    // Naming the default format out loud is not a different request, so the
    // archive is the one the flag-less run would have written.
    std::vector<Entry> entries;
    std::string        error;
    ASSERT_TRUE(ReadArchive(ws.At("other.zip"), entries, error)) << error;
    EXPECT_TRUE(Contains(Names(entries), "dist/app.js"));
    EXPECT_TRUE(Contains(Names(entries), "dist/index.html"));
}

TEST(PackRunTest, PutsTheTreeUnderItsOwnName)
{
    CliWorkspace ws("pack-entry-names");
    WriteTree(ws);
    ws.Write("dist/assets/logo.svg", "<svg/>");

    const guchho::test::CliResult result = RunCli({"pack", "dist", "-o", "out.zip"});
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

TEST(PackRunTest, CompressionLevelReachesTheArchive)
{
    CliWorkspace ws("pack-level");
    // Repetitive, so that the two ends of the range differ by more than the
    // headers around them.
    ws.Write("dist/bundle.js", std::string(4096, 'a'));

    ASSERT_EQ(RunCli({"pack", "dist", "-o", "stored.zip", "--level=0"}).exit_code, kSuccess);
    ASSERT_EQ(RunCli({"pack", "dist", "-o", "packed.zip", "--level=9"}).exit_code, kSuccess);

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

TEST(PackRunTest, AnExistingOutputIsAFailure)
{
    CliWorkspace ws("pack-exists");
    WriteTree(ws);
    ws.Write("out.zip", "not an archive");

    const guchho::test::CliResult result = RunCli({"pack", "dist", "-o", "out.zip"});

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "Output file already exists: \"out.zip\""))
        << result.err;
    EXPECT_TRUE(OutputContains(result.err, "--allow-overwrite")) << result.err;
    // Refused, not replaced: a command that overwrote a file nobody said it
    // could would be a command nobody could safely point at a directory with.
    EXPECT_EQ(ws.Read("out.zip"), "not an archive");
}

TEST(PackRunTest, AllowOverwriteReplacesIt)
{
    CliWorkspace ws("pack-overwrite");
    WriteTree(ws);
    ws.Write("out.zip", "not an archive");

    const guchho::test::CliResult result =
        RunCli({"pack", "dist", "-o", "out.zip", "--allow-overwrite"});

    EXPECT_EQ(result.exit_code, kSuccess) << result.err;

    std::vector<Entry> entries;
    std::string        error;
    EXPECT_TRUE(ReadArchive(ws.At("out.zip"), entries, error)) << error;
}

TEST(PackRunTest, AnInputThatIsNotThereIsAFailure)
{
    CliWorkspace ws("pack-missing-input");

    const guchho::test::CliResult result = RunCli({"pack", "absent", "-o", "out.zip"});

    // The spelling was right and the path was not, which is the line between
    // 1 and 2 for this command.
    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "Input does not exist")) << result.err;
    EXPECT_FALSE(ws.Exists("out.zip"));
}

TEST(PackRunTest, TwoInputsWithTheSameNameAreRefused)
{
    CliWorkspace ws("pack-colliding-roots");
    WriteTree(ws, "one/dist");
    WriteTree(ws, "two/dist");

    const guchho::test::CliResult result =
        RunCli({"pack", "one/dist", "two/dist", "-o", "out.zip"});

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "Two inputs would produce the same archive path"))
        << result.err;
    EXPECT_FALSE(ws.Exists("out.zip"));
}

TEST(PackRunTest, LeavesNoTemporaryFileBehind)
{
    CliWorkspace ws("pack-temp-file");
    WriteTree(ws);

    // A failure after the archive has been named, which is the point at which
    // a temporary file would exist if one were ever created.
    ASSERT_EQ(RunCli({"pack", "absent", "-o", "out.zip"}).exit_code, kBuildFailure);
    EXPECT_EQ(ws.Tree().find(".tmp"), std::string::npos) << ws.Tree();

    // And the same on the way through a success, so a rename that quietly
    // failed to remove its own is caught rather than compounding.
    ASSERT_EQ(RunCli({"pack", "dist", "-o", "out.zip"}).exit_code, kSuccess);
    EXPECT_EQ(ws.Tree().find(".tmp"), std::string::npos) << ws.Tree();
}

TEST(PackRunTest, WarnsWhenTheOutputIsInsideAnInputTree)
{
    CliWorkspace ws("pack-output-inside");
    WriteTree(ws);

    // The first run creates the archive; the second finds it standing inside
    // the tree it is about to read.
    ASSERT_EQ(RunCli({"pack", "dist", "-o", "dist/out.zip"}).exit_code, kSuccess);

    const guchho::test::CliResult second =
        RunCli({"pack", "dist", "-o", "dist/out.zip", "--allow-overwrite"});

    EXPECT_EQ(second.exit_code, kSuccess) << second.err;
    EXPECT_TRUE(OutputContains(second.out, "Created dist/out.zip")) << second.out;
    // On stderr, next to the other diagnostics, and not merged into the line
    // that says the archive was made.
    EXPECT_TRUE(OutputContains(second.err, "which is the archive being written")) << second.err;
}

// ---------------------------------------------------------------------------
// The same request, over the service
// ---------------------------------------------------------------------------

TEST(PackServiceTest, AnswersWithAPathAndASize)
{
    CliWorkspace ws("pack-service-ok");
    WriteTree(ws);

    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
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

TEST(PackServiceTest, NeedsAnInputList)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
            {"outFile", guchho::service::Value::String("out.zip")},
        }));

    EXPECT_EQ(ErrorOf(response), "\"inputs\" must be an array of strings");
}

TEST(PackServiceTest, RefusesAnEntryThatIsNotAPath)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::Number(3)})},
            {"outFile", guchho::service::Value::String("out.zip")},
        }));

    EXPECT_EQ(ErrorOf(response), "every entry in \"inputs\" must be a string");
}

TEST(PackServiceTest, NeedsAnOutfile)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
        }));

    EXPECT_EQ(ErrorOf(response), "\"outFile\" must be a string");
}

TEST(PackServiceTest, RefusesAFormatThatIsNotAString)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"format", guchho::service::Value::Number(9)},
        }));

    EXPECT_EQ(ErrorOf(response), "\"format\" must be a string");
}

TEST(PackServiceTest, RefusesAFormatTheEngineCannotWrite)
{
    CliWorkspace ws("pack-service-format");
    WriteTree(ws);

    // The shape is right and the value is not, so this is the engine's answer
    // rather than a protocol one — the same words the command line prints,
    // carrying the same note, arriving as a BuildFailure the host can throw.
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"format", guchho::service::Value::String("tar")},
        }));

    EXPECT_TRUE(OutputContains(ErrorOf(response), "Unsupported archive format"))
        << ErrorOf(response);

    const guchho::service::Value* errors = response.Find("errors");
    ASSERT_TRUE(errors != nullptr && errors->IsArray());
    ASSERT_EQ(errors->AsArray().size(), 1u);

    const guchho::service::Value* id = errors->AsArray().front().Find("id");
    ASSERT_TRUE(id != nullptr && id->IsString());
    EXPECT_EQ(id->AsString(), "pack-failed");

    const guchho::service::Value* notes = errors->AsArray().front().Find("notes");
    ASSERT_TRUE(notes != nullptr && notes->IsArray());
    ASSERT_FALSE(notes->AsArray().empty());

    EXPECT_FALSE(ws.Exists("out.zip"));
}

TEST(PackServiceTest, RefusesALevelThatIsNotANumber)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"level", guchho::service::Value::String("9")},
        }));

    EXPECT_EQ(ErrorOf(response), "\"level\" must be a number");
}

TEST(PackServiceTest, RefusesAnOverwriteThatIsNotABoolean)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"overwrite", guchho::service::Value::String("yes")},
        }));

    EXPECT_EQ(ErrorOf(response), "\"overwrite\" must be a boolean");
}

TEST(PackServiceTest, RefusesADateThatIsNotWholeSeconds)
{
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"date", guchho::service::Value::String("yesterday")},
        }));

    EXPECT_EQ(ErrorOf(response), "\"date\" must be a whole number of seconds");
}

TEST(PackServiceTest, RefusesADateSentAsANumber)
{
    // The protocol's number is a 32-bit integer, so a date cannot travel as
    // one — a host that sent it anyway would have it truncated rather than
    // refused, and would get an archive with a plausible, wrong timestamp.
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"date", guchho::service::Value::Number(1500000000)},
        }));

    EXPECT_EQ(ErrorOf(response),
              "\"date\" must be seconds since the Unix epoch, sent as a string");
}

TEST(PackServiceTest, RefusesANegativeMode)
{
    // A negative number would become an enormous unsigned one on the way into
    // the core, and the complaint would be about a permission mask nobody asked
    // for rather than about the value that was sent.
    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
            {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
            {"outFile", guchho::service::Value::String("out.zip")},
            {"mode", guchho::service::Value::Number(-1)},
        }));

    EXPECT_EQ(ErrorOf(response), "\"mode\" must not be negative");
}

TEST(PackServiceTest, TakesADateAndAModeAndWritesTheArchive)
{
    CliWorkspace ws("pack-service-metadata");
    WriteTree(ws);

    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
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

TEST(PackServiceTest, ReportsAFailureWithTheEnginesOwnWords)
{
    CliWorkspace ws("pack-service-failure");

    const guchho::service::Value response =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
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
    EXPECT_EQ(id->AsString(), "pack-failed");

    const guchho::service::Value* notes = message.Find("notes");
    ASSERT_TRUE(notes != nullptr && notes->IsArray());

    EXPECT_FALSE(ws.Exists("out.zip"));
}

TEST(PackServiceTest, CarriesWarningsBack)
{
    CliWorkspace ws("pack-service-warnings");
    WriteTree(ws);

    guchho::cli::RunServiceRequest(guchho::service::Value::Object({
        {"command", guchho::service::Value::String("pack")},
        {"inputs", guchho::service::Value::Array({guchho::service::Value::String("dist")})},
        {"outFile", guchho::service::Value::String("dist/out.zip")},
    }));

    const guchho::service::Value second =
        guchho::cli::RunServiceRequest(guchho::service::Value::Object({
            {"command", guchho::service::Value::String("pack")},
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
