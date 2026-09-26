// =============================================================================
// src/cli/cli_project.cpp — the three commands that manage a project
// =============================================================================
//
// Everything here is about the shape of a project rather than about its output.
// One command writes a starting point, one removes what a build left behind,
// and one answers questions about the machine. None of them builds anything,
// and none of them hands a set of options to the engine.
//
// That is why this file does not use the flag grammar. The grammar in
// cli_options.cpp exists to fill in api::BuildOptions and
// api::TransformOptions, and each of these three commands has nothing to fill
// in: "guchho init" has no build settings, and "guchho clean" has exactly one
// switch. So each command reads its own arguments, in a few lines, looking for
// the two or three spellings it answers to.
//
// One consequence is worth knowing before using them. Because there is no
// grammar here, an argument these commands do not recognise is not a mistake
// they will tell you about: "guchho init --outdir=dist" creates the same four
// files as "guchho init", and the flag is never looked at. The build commands
// are strict about this because a misspelt build flag would otherwise produce a
// build that quietly differs from what was asked for. A starter project either
// gets written or it does not, so there is nothing here for a wrong flag to
// change.
//
// Two of the three write to the file system and one only reads, and all three
// reach it through the filesystem interface rather than around it, so the same
// code can be run against an in-memory tree. Each builds the interface the same
// way
// — default options, an error string, and a check — and treats a failure to
// create it as a failure of the run rather than as a rejected command line.
// That distinction shows up in the exit code: a command line that was not
// understood is worth 2, and a working directory that cannot be used is worth
// 1, because nothing about what was asked for was wrong.
// =============================================================================
#include "guchho/cli.hpp"
#include "guchho/filesystem.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace guchho::cli {
// =============================================================================
// Command: init
// =============================================================================
//
// Writes a project that builds: a document, a script, a stylesheet and a
// configuration file, each small enough to be read at a glance and each
// referring to the others by relative path.
//
// The generated layout is the one the rest of the command line expects. The
// document goes to src/index.html rather than to the top of the project, and
// that is not a matter of taste: it is the second place applyDefaultHtmlEntry
// looks when a run is given no entry point, so a project created here builds
// and serves with no arguments at all, which is the point of the command.
//
// Nothing is overwritten unless it is asked for. A file that already exists is
// reported and left alone, and the command still counts as a success, because
// running "guchho init" twice should be harmless. The one switch that changes
// this is the one that says so out loud.
//
// All four files are attempted even if one of them fails, and the outcome is
// reported once at the end. The operator used to combine the four results is
// the one that always evaluates both sides, so a directory that cannot be
// created does not stop the three files that do not need it — and the summary
// at the end is the only place the failure is mentioned.

// Creates a starter project in the working directory: src/index.html,
// src/main.js, src/style.css and guchho.config.js.
//
// Reads "--help" and "-h" before doing anything, and "--force" to overwrite.
// Both are found by scanning the arguments from the second one onwards, since
// the first is the command word; everything else on the line is ignored.
//
// Returns 0 for help, for a project that was created, and for a project where
// every file already existed. Returns 1 if the filesystem could not be reached,
// or if at least one file could not be written.
//
// Input:  { "init" } in an empty directory
// Output: a blank line, "  Initializing project...", four "  Create" lines, a
//         blank line, "  Done! Get started with:", "    guchho dev", and 0.
//
// Input:  { "init" } in a directory that already has src/main.js
// Output: the same, with "  Skip src/main.js (already exists)" where that file
//         would have been created, and 0.
//
// Input:  { "init", "--force" } where src/main.js exists and differs
// Output: "  Create src/main.js", the file replaced with the starter contents,
//         and 0.
//
// Input:  { "init", "--help" }
// Output: the init help text, nothing written, and 0.
//
// Input:  { "init" } where the working directory cannot be used
// Output: "Error: " followed by the reason on the error stream, and 1.
int runInit(const std::vector<std::string>& args) {
    bool force = false;

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            printInitHelp(std::cout);
            return 0;
        }
        if (args[i] == "--force") {
            force = true;
            continue;
        }
    }

    filesystem::RealFsOptions fs_opts;
    std::string fs_err;
    auto fs = filesystem::MakeRealFS(fs_opts, fs_err);
    if (!fs) {
        // A build failure rather than a usage error, and the wording of the
        // message is the reason: it names what went wrong rather than what was
        // typed, because nothing was. The run failed to start.
        std::cerr << "Error: " << fs_err << "\n";
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    // Read once, here, rather than per file: the working directory does not
    // change while a command runs, and every path below is built from it.
    auto cwd = fs->Cwd();

    // Writes one file, creating whatever directories it needs on the way, and
    // reports what it did. Returns whether this one file succeeded, and the
    // caller combines the four answers.
    auto createFile = [&](const std::string& rel_path, const std::string& content) -> bool {
        std::string full_path = fs->Join({cwd, rel_path});

        // Only asked when overwriting was not requested, since the answer
        // cannot change what happens next if it is going to be ignored.
        if (!force) {
            // The file is not asked about directly. Its directory is listed and
            // its name looked up in the listing, which is how the interface
            // answers "is there such a file" without a separate call for it.
            auto entries = fs->ReadDirectory(fs->Dir(full_path));
            if (entries.Ok()) {
                // The listing also carries what it found under a different
                // spelling, for file systems that do not distinguish case. It is
                // unpacked because the pair is the interface's shape, and only
                // the first half is of any use here: a name that matched
                // differently is still a file that is there.
                auto [entry, diff] = entries.value.Get(fs->Base(full_path));
                // Checked to be a file and not merely to exist, because a
                // directory of the same name is a different thing entirely — and
                // the write below is what discovers that it cannot be one.
                if (entry && entry->Kind(*fs) == filesystem::EntryKind::kFile) {
                    std::cout << "  Skip " << rel_path << " (already exists)\n";
                    return true;
                }
            }
        }

        // Created before the file rather than assumed, so the caller does not
        // have to know that the first of the four paths is the only one with a
        // directory in front of it. A failure here needs no message of its own:
        // the open that follows reports the same problem in terms of the file
        // the person asked for.
        std::string dir_err;
        filesystem::MkdirAll(*fs, fs->Dir(full_path), dir_err);

        // The permit pair either side of the open. Taking a permit before an
        // open is what keeps a process from exceeding the number of handles the
        // operating system will give it, and here it is given back as soon as
        // the stream exists — the stream is still open, and the write below
        // happens after the permit is back. Four files written one after
        // another cannot exhaust anything, so there was nothing to hold here.
        filesystem::BeforeFileOpen();
        std::ofstream ofs(full_path, std::ios::binary);
        filesystem::AfterFileClose();
        if (!ofs.is_open()) {
            std::cerr << "  Error: Could not create " << rel_path << "\n";
            return false;
        }
        // Written as bytes rather than as text, because the contents include a
        // stylesheet and a document and neither of them should have its line
        // endings rewritten by whatever the platform considers correct. The
        // length is cast because the count is a size and the stream takes a
        // count.
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        std::cout << "  Create " << rel_path << "\n";
        return true;
    };

    // Announced before anything is written rather than after, so that a run
    // which is about to touch the file system says so first. The blank lines
    // are what separate this from the banner the commands above it have already
    // printed, and from the list that follows.
    std::cout << "\n  Initializing project...\n\n";

    bool ok = true;
    // The document. It loads the script as a module and links the stylesheet by
    // relative path, so the two files below are found from this one alone and
    // the project works wherever it is copied to.
    ok &= createFile("src/index.html",
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>My App</title>\n"
        "  <link rel=\"stylesheet\" href=\"./style.css\">\n"
        "</head>\n"
        "<body>\n"
        "  <h1>Hello from Guchho!</h1>\n"
        "  <script type=\"module\" src=\"./main.js\"></script>\n"
        "</body>\n"
        "</html>\n");

    // The script. It imports the stylesheet as well, which is what makes a
    // build of this project pull all three files into one output rather than
    // copying them side by side, and it touches the document's heading so that
    // the effect of a rebuild is visible without opening a console.
    ok &= createFile("src/main.js",
        "import './style.css';\n"
        "\n"
        "const app = document.querySelector('h1');\n"
        "if (app) {\n"
        "  app.textContent = 'Hello from Guchho!';\n"
        "}\n");

    // The stylesheet. It is here to be imported by the script above rather than
    // only linked from the document, which is the same file reached by a
    // different route and is what a person would expect to happen to it.
    ok &= createFile("src/style.css",
        "* {\n"
        "  margin: 0;\n"
        "  padding: 0;\n"
        "  box-sizing: border-box;\n"
        "}\n"
        "\n"
        "body {\n"
        "  font-family: system-ui, -apple-system, sans-serif;\n"
        "  display: flex;\n"
        "  justify-content: center;\n"
        "  align-items: center;\n"
        "  min-height: 100vh;\n"
        "  background: #0a0a0a;\n"
        "  color: #fafafa;\n"
        "}\n"
        "\n"
        "h1 {\n"
        "  font-size: 2rem;\n"
        "}\n");

    // The configuration. Every value in it is the default a build would use
    // anyway, which is the point: a person can open this file, see what a build
    // is being asked for, and change one line. The entry matches the document
    // above and the output directory is the one "guchho clean" looks for.
    ok &= createFile("guchho.config.js",
        "export default {\n"
        "  build: {\n"
        "    entry: 'src/index.html',\n"
        "    outdir: 'dist',\n"
        "    format: 'esm',\n"
        "    target: 'es2020',\n"
        "    minify: true,\n"
        "    sourcemap: true,\n"
        "  },\n"
        "};\n");

    // Closes the list. On the failure side the detail has already been printed
    // by whichever file could not be written, so this says only that it
    // happened and the exit code carries it — a run that created three of four
    // files has not done what was asked and should not report success.
    std::cout << "\n";
    if (ok) {
        std::cout << "  Done! Get started with:\n\n"
                  << "    guchho dev\n\n";
    } else {
        std::cout << "  Some files could not be created.\n";
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    return static_cast<int>(ExitCode::kSuccess);
}

// =============================================================================
// Command: clean
// =============================================================================
//
// Removes what a build put in the project, and nothing else.
//
// The list of what that is has three names in it, and only the first is a
// directory the rest of the command line currently writes: "dist" is the
// default output location, and the other two are names kept here for a
// project's own state and for a cache on disk. Neither is referred to anywhere
// else in the project today, so in practice this command finds one directory
// and stays quiet about the other two — which is why a missing target produces
// no line of output at all rather than a line saying it was missing.
//
// The switch makes the command safe to try. Deleting a directory is the one
// thing on the command line that cannot be undone by running it again, and
// "--dry-run" answers the only question a person has before running it: what
// would be removed.

// Removes the generated directories from the working directory: "dist",
// ".guchho" and "cache", in that order.
//
// Reads "--help" and "-h" before doing anything, and "--dry-run" to list what
// would be removed without removing it.
//
// A target that is not there produces no output, because a project that has
// never been built has nothing to clean and there is nothing to tell the person
// who asked. A target that is there and cannot be removed prints the reason on
// the error stream and the run still succeeds: the command was understood and
// did what it could, and a directory held open by something else is not a
// reason to call the whole command wrong.
//
// Input:  { "clean" } with a dist directory holding four entries
// Output: a blank line, "  Removed dist/ (4 items)", a blank line, and 0.
//
// Input:  { "clean", "--dry-run" } in the same directory
// Output: a blank line, "  Would remove dist/", a blank line, and 0, with
//         nothing removed.
//
// Input:  { "clean" } in an empty directory
// Output: two blank lines and 0.
//
// Input:  { "clean", "--help" }
// Output: the clean help text, nothing removed, and 0.
int runClean(const std::vector<std::string>& args) {
    bool dry_run = false;

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            printCleanHelp(std::cout);
            return 0;
        }
        if (args[i] == "--dry-run") {
            dry_run = true;
            continue;
        }
    }

    filesystem::RealFsOptions fs_opts;
    std::string fs_err;
    auto fs = filesystem::MakeRealFS(fs_opts, fs_err);
    if (!fs) {
        std::cerr << "Error: " << fs_err << "\n";
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    auto cwd = fs->Cwd();

    // Fixed, and in this order, so that two runs of the command say the same
    // things in the same order. Nothing is discovered by looking at the project:
    // a command whose deletions depended on what it found could delete a
    // directory a person had created themselves, and the only safe way to choose
    // what to remove is to have decided beforehand.
    std::vector<std::string> targets = {"dist", ".guchho", "cache"};

    // Opens the list, and is also what a run with nothing to remove prints.
    std::cout << "\n";

    for (const auto& target : targets) {
        std::string full_path = fs->Join({cwd, target});
        // Existence is decided by listing the directory itself. A target that
        // is a file rather than a directory also fails this, and is left alone:
        // this command removes directories it recognises by name, and has no
        // opinion about files.
        auto entries = fs->ReadDirectory(full_path);
        if (entries.Ok()) {
            if (dry_run) {
                std::cout << "  Would remove " << target << "/\n";
            } else {
                // The standard removal, with the error code supplied rather
                // than thrown: one target failing should not end the command
                // before the remaining two have been considered.
                std::error_code ec;
                auto count = std::filesystem::remove_all(
                    filesystem::PathFromUTF8(full_path), ec);
                if (!ec) {
                    std::cout << "  Removed " << target << "/ (" << count << " items)\n";
                } else {
                    // On the error stream and without changing the exit code,
                    // which is the decision described above: this is a report
                    // about one directory, not a failure of the command.
                    std::cerr << "  Failed to remove " << target << "/: " << ec.message() << "\n";
                }
            }
        }
    }

    // Closes the list. The trailing separator in every message is added here
    // for the reader and is not part of the path, so that the three lines
    // below each other line up as a column.
    std::cout << "\n";
    return static_cast<int>(ExitCode::kSuccess);
}

// =============================================================================
// Command: info
// =============================================================================
//
// Three facts about the machine and the project, and the first thing anybody
// pastes into a bug report.
//
// It is the shortest of the eight commands and the only one with no settings at
// all, and that is a deliberate answer to the question of what to print. A
// person who has typed "guchho info" has usually just seen something they did
// not expect, and the useful thing to tell them is which machine they are on
// and which build they are running — not a list of things that could be
// configured. Anything configurable is the flag that configures it, and a
// report that repeated the defaults would be three more lines of nothing.
//
// Nothing here is detected. The platform and the build type are decided when
// this file is compiled, because both are properties of the binary rather than
// of the project, and a report that asked the operating system at run time
// could disagree with the binary it was printed by.

// Prints the platform, the build type and the configuration file in use.
//
// Reads "--help" and "-h" and nothing else. There is no flag to change what is
// printed, because there is no version of this report that is more or less
// detailed: the three lines are the three facts that are useful.
//
// Prints its own banner. The commands that build print theirs before they run,
// and this one is not one of them, so a run of it produces exactly one banner
// rather than none or two.
//
// The filesystem is created only to look for the configuration file, and unlike
// the two commands above, a failure to create it is not reported and does not
// affect the result: the report is still true, and "none" is a fair thing to
// say about a configuration file that could not be looked for.
//
// Input:  { "info" } in a project holding guchho.config.js
// Output: the banner, "  Platform:", "  Build:" and "  Config:" lines each
//         naming one thing, a blank line, and 0.
//
// Input:  { "info" } in a directory with no configuration file
// Output: the same, with "none" where the configuration would be, and 0.
//
// Input:  { "info", "--help" }
// Output: the info help text, two lines, and 0 — no banner, because help is
//         answered before anything is printed.
int runInfo(const std::vector<std::string>& args) {
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            printInfoHelp(std::cout);
            return 0;
        }
    }

    printBanner(std::cout);

    // One of three names, chosen when this file was compiled. The last arm is
    // not a list of the systems that are not Windows or macOS; it is what is
    // left, and it is what a binary built anywhere else will say.
#ifdef _WIN32
    std::string platform = "Windows";
#elif __APPLE__
    std::string platform = "macOS";
#else
    std::string platform = "Linux";
#endif

    // The same kind of decision: a build with its assertions compiled out says
    // Release whether or not it was optimised, and a build with them in says
    // Debug however it was configured. Of the two, this is the one worth
    // knowing when a run behaves differently from a release build.
    std::string build_type =
#ifdef NDEBUG
        "Release";
#else
        "Debug";
#endif

    // Which of the three names is a file that is actually there, in the order
    // they are listed. The first is the one this command's own sibling writes,
    // so a project created by "guchho init" reports the file it was given.
    filesystem::RealFsOptions fs_opts;
    std::string fs_err;
    auto fs = filesystem::MakeRealFS(fs_opts, fs_err);
    std::string config = "none";
    if (fs) {
        auto cwd = fs->Cwd();
        std::vector<std::string> config_names = {"guchho.config.js", "guchho.json", "guchho.config.json"};
        for (const auto& name : config_names) {
            std::string full_path = fs->Join({cwd, name});
            // The same probe the init command uses to decide whether to skip a
            // file: list the directory, find the name in the listing, and ask
            // what kind of entry it is. A directory with a configuration file's
            // name on it is not a configuration file.
            auto entries = fs->ReadDirectory(fs->Dir(full_path));
            if (entries.Ok()) {
                auto [entry, diff] = entries.value.Get(fs->Base(full_path));
                if (entry && entry->Kind(*fs) == filesystem::EntryKind::kFile) {
                    config = name;
                    break;
                }
            }
        }
    }

    // The names are padded to a common width so that the values line up as a
    // column, which is what makes three lines readable as one fact each rather
    // than as three sentences.
    std::cout << "  Platform:      " << platform << "\n"
              << "  Build:         " << build_type << "\n"
              << "  Config:        " << config << "\n"
              << "\n";

    return static_cast<int>(ExitCode::kSuccess);
}

} // namespace guchho::cli
