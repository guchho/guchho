// =============================================================================
// src/cli/cli_init.cpp — the command that starts a project
// =============================================================================
//
// Everything in this file is about finding out what somebody wants. What to
// build it from, and whether to ask at all, are questions with more than one
// answer, and the answers come from three places: the words on the command
// line, the defaults, and — when there is a person to ask — the terminal.
// Where the project goes is not among them: that is the working directory, and
// nothing on the command line says otherwise. What a starter project contains
// is none of this file's business; that is decided in cli_scaffold.cpp from the
// three values this file arrives at, and this file never looks inside a file it
// writes.
//
// The target is the working directory, and it briefly was not. This command
// once took a directory and then lost it again, which makes the second removal
// the one worth a paragraph: "guchho init my-app" is a refusal, and a person who
// typed it is asking for the thing this tool used to do. A bare "unexpected
// argument" is a dead end for somebody whose command worked yesterday, so the
// refusal names the way out instead — go and make the directory first, and run
// this inside it. It is also the reason a stray argument is stopped rather than
// ignored: a command that writes into the directory it is standing in is a
// command where a mistyped argument lands in a stranger's project.
//
// The division matters for a reason that is not tidiness. A scaffolder that
// also parses a command line can only be tested by running the command line,
// and running the command line is how you get a terminal, a working directory
// and an argument parser into the same assertion. Here, the half worth testing
// hardest is the half that decides what a project is, and it has no terminal in
// it at all.
//
// Two decisions are worth stating before the code, because each of them departs
// from what this command used to do.
//
// It used to ignore arguments it did not recognise. "guchho init --outdir=dist"
// and "guchho init --template=typo" both wrote the same four files, silently,
// and the reasoning was recorded at the top of cli_project.cpp: a starter either
// gets written or it does not, so there was nothing a wrong flag could change.
// That reasoning was sound when there was nothing to get wrong. There are now
// two values — a type and a template — and a misspelt one used to be answered
// with a working project built the wrong way, which is a worse outcome than a
// refusal.
//
// And it used to write four files whose names were hard-coded in the middle of
// the handler. They are now produced from a type and a template, which is why
// this file has three enum values in it that it never inspects beyond passing
// along: the command's own job ends at knowing which of the twelve combinations
// was asked for.
#include "guchho/cli.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/runtime.hpp"
#include "guchho/scaffold.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace guchho::cli {

// =============================================================================
// What was asked for
// =============================================================================

namespace {

// The command line, after it has been read.
//
// The two optionals are there because each one answers a different question:
// "was it given", which is not the same question as "what is it". An optional
// holding a default would be indistinguishable from a flag nobody typed, and
// that distinction is the whole basis for whether to ask somebody.
//
// There is no member for the directory. It is the working directory, so there
// is nothing to store and nothing to ask about.
struct InitOptions {
    std::optional<scaffold::ProjectType>     type;
    std::optional<scaffold::ProjectTemplate> tmpl;
    bool                               yes = false;
    bool                               force = false;
    bool                               help = false;
};

// The two options that take a value, named once because they are named in every
// error message and in the usage line, and three copies of a spelling is three
// chances to disagree with the help.
//
// The short forms are named next to them for the same reason. A short form is a
// spelling the command accepts and the help prints, which makes it one more
// place the two can drift apart, and it is also a place where a diagnostic can
// be wrong: a person who typed "-t" is owed to be told about "--template".
constexpr std::string_view kTypeOption = "--type";
constexpr std::string_view kTemplateOption = "--template";
constexpr std::string_view kShortTemplateOption = "-t";

// The value that follows "--option", whether it was written as "--option value"
// or "--option=value". Both spellings work and neither is preferred, because a
// person who has typed one of them once should not have to remember which.
//
// Returns false when there is no value to take, which is a mistake worth naming
// rather than a value that happens to be empty. Three things all count as no
// value: "--template" with nothing after it, "--template" followed by another
// flag, and "--template=" with nothing after the equals sign. Reading the next
// flag as a template name would turn a clear refusal into a confusing one about
// spelling, and accepting the empty case would let "--template=" fall through to
// the name lookup below and be reported as an unknown name — which sends whoever
// typed it looking for the list of valid names instead of at the space they left
// after the equals sign.
bool TakeOptionValue(const std::vector<std::string>& args, size_t& i,
                     std::string_view option, std::string& value)
{
    if (!args[i].starts_with(option)) return false;

    // Bare "--option": the value is whatever the next argument is.
    if (args[i].size() == option.size()) {
        if (i + 1 >= args.size() || args[i + 1].starts_with("-")) return false;
        value = args[++i];
        return true;
    }

    // "--option=something": the value is on this argument, and anything that
    // follows it belongs to whatever comes next.
    if (args[i][option.size()] != '=') return false;
    value = args[i].substr(option.size() + 1);
    return !value.empty();
}

// Joins names into "a, b, c" for a diagnostic. A list is a list: the whole
// point of printing one is that it is shorter to read than the three lines an
// indented version takes, and every one of them is on the same screen as the
// message that is complaining.
std::string JoinNames(const std::vector<std::string>& names)
{
    std::string out;
    for (size_t i = 0; i < names.size(); i++) {
        if (i > 0) out += ", ";
        out += names[i];
    }
    return out;
}

// ---------------------------------------------------------------------------
// Asking
// ---------------------------------------------------------------------------

// True when there is a person on the other end of both ends.
//
// Both, not either. A command that reads from a terminal but writes somewhere
// else is a command whose question appears in a log file, and one that writes
// to a terminal but reads from a pipe is a command that hangs forever waiting
// for a line that is never going to arrive. Requiring a terminal at both ends
// is what makes "--yes" unnecessary in a script without the command having to
// know it is in one.
bool IsInteractive()
{
    return logger::GetTerminalInfo(0).is_tty && logger::GetTerminalInfo(1).is_tty;
}

// Reads one line. Returns false at the end of the input, which is the one thing
// that has to end a run of questions: a pipe that closed is not going to answer
// the next one either, and a loop that kept asking would spin.
bool ReadLine(std::string& out)
{
    if (!std::getline(std::cin, out)) return false;
    // Stripped rather than trimmed, so that a line ending in a carriage return
    // does not end up inside a directory name on Windows.
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) {
        out.pop_back();
    }
    return true;
}

// The bullet a question is written with. The checkmark family and the diamond
// both exist in the Command Prompt's code page as something else, which is why
// the cross in a diagnostic drops to "X" there; this drops to an arrow for the
// same reason and no further.
std::string_view PromptMarker()
{
    return logger::IsProbablyWindowsCommandPrompt() ? std::string_view(">") : std::string_view("◇");
}

// Asks for a value from a fixed set, starting from what the command line or the
// defaults already decided. Returns false only when the person gave up on it,
// and the caller treats that as a reason to stop asking rather than as a
// failure: there is a value, and it was chosen before anybody started typing.
//
// A rejected answer is repeated, up to a point. Somebody who typed a name that
// does not exist has usually mistyped it rather than decided to stop, and the
// real names are right there under the complaint. It is not repeated forever,
// because a question nobody can answer is a question the command should have
// skipped — on the third refusal the value already in hand is used.
bool AskChoice(std::string_view label, const std::vector<std::string>& valid,
               std::string_view current, const helpers::TypoDetector& typos,
               std::string& out)
{
    out.assign(current);
    for (int attempt = 0; attempt < 3; attempt++) {
        // Every valid name is in the prompt itself, so the accepted set is on
        // the screen before a finger touches a key, and a mistyped answer is
        // answered against the list that already appeared above it.
        std::cout << PromptMarker() << " " << label << " [" << JoinNames(valid) << "]: " << std::flush;
        if (!ReadLine(out)) {
            std::cout << "\n";
            return true;
        }
        if (out.empty()) {
            out.assign(current);
            return true;
        }
        bool known = false;
        for (const std::string& name : valid) {
            if (name == out) {
                known = true;
                break;
            }
        }
        if (known) return true;

        auto suggestion = typos.MaybeCorrectTypo(out);
        std::cout << "    Not a value this command accepts";
        if (suggestion) {
            std::cout << ". Did you mean '" << *suggestion << "'?";
        }
        std::cout << "\n    Choose one of: " << JoinNames(valid) << "\n";
    }
    out.assign(current);
    return false;
}

// Turns a name the command line got wrong into a diagnostic and the code to
// leave with.
//
// The complaint itself is the parse function's wording, and the note carries
// everything needed to fix it: the names that would have worked, and a
// correction when the rejected name is a real name missing one character. The
// correction is only offered when there is one, since a guess printed next to
// the full list is worse than the list on its own.
//
// Both name options go through this, which is the reason it is a function and
// not written out twice: when it was written out twice, one of the two grew a
// suggestion and the other did not.
int RejectName(const std::vector<std::string>& args, const std::string& rejected,
               const std::optional<std::string>& error, const std::string& label,
               const std::vector<std::string>& valid)
{
    const helpers::TypoDetector typos(valid);
    std::string note = label + ": " + JoinNames(valid);
    if (auto suggestion = typos.MaybeCorrectTypo(rejected)) {
        note = std::format("Did you mean '{}'? ", *suggestion) + note;
    }
    logger::PrintErrorWithNoteToStderr(args, *error, note);
    return static_cast<int>(ExitCode::kCLIUsageError);
}

} // namespace

// =============================================================================
// Command: init
// =============================================================================

// Writes a project that builds, into the directory the command is standing in.
//
// The target is the working directory and there is no flag that changes it, so
// a directory that does not exist is not a case this command has: the way to
// start a project somewhere else is to go there first and run this, and a
// directory named on the command line is a refusal rather than a project.
//
// The defaults are an application in plain JavaScript, and they are the same
// defaults whether this runs from a script or from a terminal — "--yes" does not
// change what gets written, it only says the values were not going to be asked
// for. That is the whole difference between the two, and it is why this command
// can be used from a build script at all.
//
// What it writes, and what it refuses to write over, belongs to
// guchho::scaffold. This function reads a command line, fills in the values
// (type, template, and the config format the machine asks for), asks about
// whichever of the two choices were not given when there is somebody to ask,
// and prints what happened.
//
// Returns 0 for help and for a project that was created or was already there.
// Returns 2 for a command line that was not understood, which is worth
// distinguishing from a run that failed: nothing was wrong with the request, as
// opposed to the file system refusing it, which is worth 1.
//
// Input:  { "init" } in an empty directory
// Output: four "  Create" lines, a "  Config:" line naming the format that was
//         chosen, a success line, "Next steps:", "  guchho dev", and 0.
//
// Input:  { "init", "--template=ts" }
// Output: src/index.html, src/main.ts, src/style.css and guchho.config.js (or
//         guchho.json when no runtime is found) in the working directory, a
//         success line, and 0.
//
// Input:  { "init", "--type", "lib", "--template", "ts", "--yes" }
// Output: src/index.ts, guchho.config.js, package.json and README.md in the
//         working directory, a success line, and 0, with nothing read from the
//         terminal even though there is one.
//
// Input:  { "init", "--template=vanila" }
// Output: a complaint about the spelling on the error stream, 2, and nothing
//         written.
//
// Input:  { "init", "my-app" }
// Output: a complaint that this command has no directory argument, 2, and
//         nothing written and no directory created.
int runInit(const std::vector<std::string>& args) {
    InitOptions opts;
    bool bad_usage = false;

    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "--help" || arg == "-h") {
            opts.help = true;
            continue;
        }
        if (arg == "--force" || arg == "-f") {
            opts.force = true;
            continue;
        }
        if (arg == "--yes" || arg == "-y") {
            opts.yes = true;
            continue;
        }

        std::string value;
        if (arg == kTypeOption || arg.starts_with(std::string(kTypeOption) + "=")) {
            if (!TakeOptionValue(args, i, kTypeOption, value)) {
                logger::PrintErrorWithNoteToStderr(
                    args, std::format("Option '{}' requires a value", kTypeOption),
                    "Usage: guchho init --type=<type>");
                return static_cast<int>(ExitCode::kCLIUsageError);
            }
            scaffold::ProjectType parsed;
            if (auto error = scaffold::ParseProjectType(value, parsed)) {
                return RejectName(args, value, error, "Available types",
                                  scaffold::ProjectTypeNames());
            }
            opts.type = parsed;
            continue;
        }

        // Both spellings of the option and both spellings of its value. The long
        // form is tried first because it is the one a diagnostic talks about,
        // and TakeOptionValue cannot mistake the two for each other: "--template"
        // starts with "-t" but does not go on to spell "=something", so the short
        // form's own check rejects it.
        if (arg == kTemplateOption || arg == kShortTemplateOption ||
            arg.starts_with(std::string(kTemplateOption) + "=") ||
            arg.starts_with(std::string(kShortTemplateOption) + "=")) {
            if (!TakeOptionValue(args, i, kTemplateOption, value) &&
                !TakeOptionValue(args, i, kShortTemplateOption, value)) {
                logger::PrintErrorWithNoteToStderr(
                    args, std::format("Option '{}' requires a value", kTemplateOption),
                    "Usage: guchho init --template=<name>, or -t <name>");
                return static_cast<int>(ExitCode::kCLIUsageError);
            }
            scaffold::ProjectTemplate parsed;
            if (auto error = scaffold::ParseProjectTemplate(value, parsed)) {
                return RejectName(args, value, error, "Available templates",
                                  scaffold::TemplateNames());
            }
            opts.tmpl = parsed;
            continue;
        }

        if (!arg.empty() && arg.front() == '-') {
            logger::PrintErrorWithNoteToStderr(
                args, std::format("Unknown option '{}'", arg),
                "Run 'guchho init --help' to see the options this command takes.");
            bad_usage = true;
            continue;
        }

        // Whatever is left is a positional, and this command has none. It had
        // one and lost it, so this is an argument somebody typed on purpose and
        // reasonably expected to work; the note says where the project goes
        // rather than leaving the reader to work out what changed. Refused
        // outright rather than flagged, because unlike an unknown flag there is
        // no version of the rest of this command line that was what they meant.
        logger::PrintErrorWithNoteToStderr(
            args, std::format("Unexpected argument '{}'", arg),
            "guchho init initializes the current working directory. "
            "Create the directory first, then run guchho init inside it.");
        return static_cast<int>(ExitCode::kCLIUsageError);
    }

    if (opts.help) {
        printInitHelp(std::cout);
        return static_cast<int>(ExitCode::kSuccess);
    }
    if (bad_usage) {
        return static_cast<int>(ExitCode::kCLIUsageError);
    }

    // The interface is built the same way the two commands in cli_project.cpp
    // build it — default options, an error string, and a check — and a working
    // directory that cannot be used is worth 1 rather than 2 for the same reason
    // it is there: nothing about what was asked for was wrong.
    filesystem::RealFsOptions fs_opts;
    std::string fs_err;
    auto fs = filesystem::MakeRealFS(fs_opts, fs_err);
    if (!fs) {
        std::cerr << "Error: " << fs_err << "\n";
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    // Read once, here: the working directory does not change while a command
    // runs, and the target and the name are two readings of it.
    const std::string cwd = fs->Cwd();

    scaffold::ProjectType type = opts.type.value_or(scaffold::ProjectType::kApp);
    scaffold::ProjectTemplate tmpl = opts.tmpl.value_or(scaffold::ProjectTemplate::kBasic);

    // A question is asked only for what the command line left unanswered, and
    // only when there is a person to answer it. The values above are what the
    // run ends up with in every case, so a question that never gets asked costs
    // nothing except the one it would have taken to ask.
    //
    // There is no question about the name, and there never should have been one
    // now that the directory is not an argument: the project is named after the
    // directory it is written into, and asking somebody the name of the
    // directory they are standing in is asking them what they are already doing.
    //
    // Answering a flag on the command line is taken as the answer, not as a
    // request to confirm it: somebody who wrote --type=lib in a script, or
    // in a terminal while testing one, did not type it to be asked what they
    // meant by it.
    const bool interactive = !opts.yes && IsInteractive();
    if (interactive) {
        printBanner(std::cout);

        // Each choice is discarded rather than acted on when it is refused: the
        // value that goes into the request is the one from the command line or
        // the default, which is a real name and needs no checking.
        if (!opts.type) {
            const helpers::TypoDetector typos(scaffold::ProjectTypeNames());
            std::string chosen;
            AskChoice("Project type", scaffold::ProjectTypeNames(),
                      scaffold::ProjectTypeName(type), typos, chosen);
            scaffold::ParseProjectType(chosen, type);
        }

        if (!opts.tmpl) {
            const helpers::TypoDetector typos(scaffold::TemplateNames());
            std::string chosen;
            AskChoice("Template", scaffold::TemplateNames(),
                      scaffold::TemplateName(tmpl), typos, chosen);
            scaffold::ParseProjectTemplate(chosen, tmpl);
        }

        // One blank line after the questions, so the answers are read before
        // the list of files, rather than as part of it.
        std::cout << "\n";
    }

    // The config format is not a question, it is an answer the machine already
    // has: a runtime that can evaluate a config means the config is written as
    // one, and the only decision that is a real decision — no runtime to
    // evaluate anything — is the fallback a JSON config exists for. It is asked
    // of the machine once, here, and handed to the scaffolder in the request,
    // which is the only place the file name is ever seen.
    const scaffold::ProjectConfig config =
        SelectInitConfigFormat(runtime::DetectJavaScriptRuntime());

    // The target is the working directory. There is no second mechanism for
    // choosing one, so this is not a decision and there is nothing to ask about
    // it — and the name is the name of this directory rather than a value from
    // anywhere else, which is why a package.json written from it is the same on
    // every machine.
    scaffold::ScaffoldRequest req;
    req.type = type;
    req.tmpl = tmpl;
    req.config = config;
    req.name = fs->Base(cwd);
    req.target_dir = cwd;
    req.force = opts.force;

    scaffold::ScaffoldResult result = scaffold::RunScaffold(*fs, req);

    // The per-file lines come first, because they are the part of the output
    // that is about this run rather than about the project, and the summary is
    // the part somebody scrolls down to find.
    for (const std::string& path : result.created) {
        std::cout << "  Create " << path << "\n";
    }
    for (const std::string& path : result.overwritten) {
        std::cout << "  Overwrite " << path << "\n";
    }
    for (const std::string& path : result.skipped) {
        std::cout << "  Skip " << path << " (already exists)\n";
    }
    // The chosen format is part of the summary of a run that happened, not of a
    // run that was refused. Detection only decides the default for files that
    // are generated, and naming it here is how somebody knows which of the two
    // files they are being shown.
    if (result.Ok()) {
        std::cout << "  Config: " << scaffold::ConfigFileName(config) << "\n";
    }
    std::cout << "\n";

    // The logger writes to the file descriptor and this file writes to the
    // stream, and the two are not the same buffer. Without a flush here the
    // summary would appear above the list it is a summary of, in the order they
    // were written rather than the order they were produced.
    std::cout.flush();

    if (!result.Ok()) {
        if (result.dir_not_empty) {
            logger::PrintErrorWithNoteToStderr(
                args, result.error,
                "Use --force to initialize anyway: guchho init --force");
            // The request was well formed and the answer to it is no.
            return static_cast<int>(ExitCode::kCLIUsageError);
        }
        // Everything else that went wrong is the file system, not the command
        // line, and the two are worth telling apart: one is a mistake in what
        // was asked for, the other is a run that failed.
        logger::PrintErrorToStderr(args, result.error);
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    // The word in the middle of the success line is the one thing that differs
    // between the three kinds of project, and it is worth there: "Project
    // initialized successfully" after writing a library is true and unhelpful,
    // where "Library initialized successfully" says which of the three things
    // just happened.
    const char* what = type == scaffold::ProjectType::kLibrary  ? "Library"
                     : type == scaffold::ProjectType::kPlugin   ? "Plugin"
                                                               : "Project";
    logger::PrintSuccessToStdout(
        args, std::format("{} initialized successfully.", what));

    // There is no "cd" line, and there is nothing to replace it with. The
    // project was written into the directory the command was already standing
    // in, so "cd ." is the advice nobody needs and a name would be a directory
    // this command never chose.
    std::cout << "\nNext steps:\n\n";
    std::cout << (type == scaffold::ProjectType::kApp ? "  guchho dev" : "  guchho build")
              << "\n\n";

    return static_cast<int>(ExitCode::kSuccess);
}

} // namespace guchho::cli
