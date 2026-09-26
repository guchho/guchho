// =============================================================================
// src/cli/cli_flags.cpp — the readers the flag grammar is built from
// =============================================================================
//
// The grammar itself lives in cli_options.cpp: a long walk over the argument
// list that recognises each flag by name and puts its value somewhere. This
// file is the vocabulary that walk is written in — the handful of small
// functions that turn a piece of text into a value, and the tables that fix
// the spelling of the names a flag accepts.
//
// The split is by responsibility rather than by size. cli_options.cpp knows
// what "--target=es2020" means and where the result belongs; it does not know
// that "es2020" and "es6" are the same language level, that an engine name may
// be followed by a version, or how a value is quoted into a message. Those are
// answers that more than one flag needs, and a rule that lives next to the one
// flag that first wanted it is a rule that will be reimplemented slightly
// differently by the next flag that needs it.
//
// Three conventions hold across everything below.
//
// A rejected value is a complaint, not an exception. Every function here
// returns its answer alongside a message rather than throwing, because a
// rejected value is one complaint among many: the person who typed it has
// several other things wrong on the same line, and reporting the first one and
// stopping is the grammar's decision, not theirs. Each function also answers
// for every input, including an empty one, so a caller never has to check for
// a value it did not get.
//
// A message has two halves. The complaint says what is wrong, and the note says
// what to type instead, and they travel together so a caller can print the
// advice only when there is some. Values quoted into either half are escaped
// for ASCII, so a stray control character or a non-ASCII byte in what somebody
// typed comes back out of the message as text rather than as a second problem.
//
// A name that is accepted and a name that is offered as a suggestion come from
// the same table. The loader names, the engine names and the language levels
// below are the only place those spellings exist, which is why the note
// attached to a rejected one is assembled from the table rather than typed
// beside it.
// =============================================================================

#include "guchho/api.hpp"
#include "guchho/cli.hpp"
#include "guchho/helpers.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace guchho::cli {

// =============================================================================
// Loader names
// =============================================================================
//
// Translates one loader name into the value the engine uses, for the flag that
// sets a loader for every file at once and for the form that names a single
// file by extension.
//
// The table is the whole of the answer. A loader decides what a file is before
// anything is compiled from it — a stylesheet, a module, a block of text, a
// resource copied through untouched — so a name that is accepted and a name
// that is suggested have to be the same name, and a table is the only shape in
// which that is true by construction rather than by review. The three
// stylesheet entries are distinct on purpose: "css" is a stylesheet,
// "local-css" is a stylesheet that only applies to the importing file, and
// "global-css" is one that applies everywhere. The two that pass a file
// through without compiling it — "copy" and "file" — differ in what they do
// with the bytes rather than the text.
//
// "out" is written only when the name is known, so a caller that gets a
// complaint back keeps whatever it had stored there instead of a half-written
// value. The complaint is returned without a note, and the caller supplies the
// sentence that lists what is accepted: the list belongs to the flag that
// rejected the name, since two flags that both take a loader do not always
// accept the same set.
//
// Input:  ParseLoader("json", out)
// Output: out = api::Loader::kJSON and an empty optional, which is how success
//         is spelled here and in every function in this file.
//
// Input:  ParseLoader("jsonn", out)
// Output: out untouched, and "Invalid loader \"jsonn\"". The name is echoed
//         back quoted and ASCII-escaped, so whatever was typed is what appears
//         in the message.
std::optional<std::string> ParseLoader(const std::string& value, api::Loader& out) {

    // A map rather than a chain of comparisons, because a chain would be
    // searchable by eye and a map is searchable by the compiler: add a name to
    // this table and it is accepted, and there is no second list to keep in
    // step. Function-local, so the table is built once on first use and has no
    // lifetime beyond the run.
    static const std::unordered_map<std::string, api::Loader> kLoaderMap = {
        {"none",       api::Loader::kNone},
        {"base64",     api::Loader::kBase64},
        {"binary",     api::Loader::kBinary},
        {"copy",       api::Loader::kCopy},
        {"css",        api::Loader::kCSS},
        {"dataurl",    api::Loader::kDataURL},
        {"default",    api::Loader::kDefault},
        {"empty",      api::Loader::kEmpty},
        {"file",       api::Loader::kFile},
        {"global-css", api::Loader::kGlobalCSS},
        {"js",         api::Loader::kJS},
        {"json",       api::Loader::kJSON},
        {"jsx",        api::Loader::kJSX},
        {"local-css",  api::Loader::kLocalCSS},
        {"text",       api::Loader::kText},
        {"ts",         api::Loader::kTS},
        {"tsx",        api::Loader::kTSX},
    };

    auto it = kLoaderMap.find(value);
    if (it != kLoaderMap.end()) {
        out = it->second;
        return std::nullopt;
    }

    return "Invalid loader \"" + value + "\"";
}

// =============================================================================
// Recognising a flag that may carry a value
// =============================================================================

// Answers one question about one argument: is this the flag, or this flag with
// a value attached? The grammar asks it before every switch, and the answer
// decides whether the argument is a bare switch or the front of a value.
//
// The prefix test alone would be wrong, and wrong in a way that shows up
// constantly, because Guchho's switches share prefixes: "--minify" is a
// switch, and so are "--minify-whitespace", "--minify-syntax" and
// "--minify-identifiers". Testing "--minify-whitespace" against "--minify"
// matches the prefix and has to be rejected by something. That something is the
// remainder test: whatever follows the flag name must be nothing at all, or a
// single "=" and then the value. A "-" in that position is a different flag
// that happens to start with the same letters.
//
// Input:  arg = "--bundle", flag = "--bundle"
// Output: true — a bare switch.
//
// Input:  arg = "--bundle=false", flag = "--bundle"
// Output: true, and the caller reads the value out of the argument itself.
//
// Input:  arg = "--minify-whitespace", flag = "--minify"
// Output: false. The prefix matches and the remainder begins with "-", so this
//         is the longer flag and not this one with a value.
bool isBoolFlag(const std::string& arg, std::string_view flag) {

    if (!arg.starts_with(flag)) {
        return false;
    }
    auto remainder = arg.substr(flag.size());
    return remainder.empty() || remainder[0] == '=';
}

// =============================================================================
// The value of a switch
// =============================================================================
//
// Reads a switch that may have been written bare or with an explicit value, and
// returns both the value it means and a complaint if it does not mean one.
//
// The bare form is the common one and it takes the caller's default, not a
// value of its own. That is the point of the parameter: a switch whose bare
// form means "on" and a switch whose bare form means "off" are both spelled
// the same way on the command line, and the grammar is the only place that
// knows which is which. A caller reading "--allow-overwrite" is asking for a
// true; a caller reading "--no-treeshaking=false" is asking for a false, and
// neither the argument nor this function can tell the difference.
//
// The two outputs are both useful, and they are useful in different
// circumstances: the value is what the build would use, and the complaint is
// what the person who typed it is told. A caller that rejects the command line
// uses the complaint and ignores the value; a caller that wants to report
// everything wrong with a line at once uses the value and collects the
// complaint. The value returned alongside a complaint is false, not the
// default, so a caller that ignores the complaint gets a switch that is off
// rather than one that is on because somebody misspelled it.
//
// Input:  arg = "--bundle", default_value = true
// Output: { true, no complaint }.
//
// Input:  arg = "--bundle=false", default_value = true
// Output: { false, no complaint }.
//
// Input:  arg = "--bundle=yes", default_value = true
// Output: { false, a complaint reading "Invalid value \"yes\" in
//         \"--bundle=yes\"" }, whose note is "Valid values are \"true\" or
//         \"false\".". Only the two words are accepted: this is a machine-read
//         flag, and "1" or "yes" would be a spelling to remember on top of the
//         one thing being spelled.
std::pair<bool, std::optional<ErrorWithNote>> parseBoolFlag(
    const std::string& arg, bool default_value)
{
    // No "=" at all is the bare form, and the caller's default is what it
    // means. The switch is not read off the argument here because the argument
    // does not contain it: the caller has already decided which flag it is
    // reading, and a bare "=true" after any switch would be redundant.
    auto eq = arg.find('=');
    if (eq == std::string::npos) {
        return {default_value, std::nullopt};
    }

    auto value = arg.substr(eq + 1);

    // Both spellings are checked explicitly rather than folded or compared
    // case-insensitively, because this value is written into a build's
    // configuration and into scripts, and a flag whose accepted values drift
    // with case is a flag whose recorded commands stop working.
    if (value == "false") return {false, std::nullopt};
    if (value == "true")  return {true,  std::nullopt};

    return {false, MakeErrorWithNote(
        "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
        "Valid values are \"true\" or \"false\".")};
}

// =============================================================================
// Log levels
// =============================================================================
//
// Reads the name of a log level. The names are the ones a person is most likely
// to guess, from "silent" at the bottom to "verbose" at the top, and each maps
// to one level of the engine's own scale rather than to a number.
//
// The complaint lists every accepted name. That note is longer than any other
// in this file on purpose: a log level is the flag people reach for when
// something is not working, and the point of asking for one is usually to be
// told why they are still not seeing anything. Naming the six is cheaper than
// a second round trip.
//
// The value is set to kSilent before the complaint is returned, which is the
// one place in this file where an output is written on the way to failing. It
// is deliberate: a caller that has decided to keep going after the complaint
// should not be left logging at whatever it had before, and silent is the
// level at which a run that was already rejected can do no further harm.
//
// Input:  value = "debug"
// Output: out = api::LogLevel::kDebug and an empty optional.
//
// Input:  value = "loud"
// Output: out = api::LogLevel::kSilent, and a complaint reading "Invalid value
//         \"loud\" in \"--log-level=loud\"" whose note names all six levels.
std::optional<ErrorWithNote> parseLogLevel(
    const std::string& value, const std::string& arg, api::LogLevel& out)
{
    if (value == "verbose")  { out = api::LogLevel::kVerbose;  return std::nullopt; }
    if (value == "debug")    { out = api::LogLevel::kDebug;    return std::nullopt; }
    if (value == "info")     { out = api::LogLevel::kInfo;     return std::nullopt; }
    if (value == "warning")  { out = api::LogLevel::kWarning;  return std::nullopt; }
    if (value == "error")    { out = api::LogLevel::kError;    return std::nullopt; }
    if (value == "silent")   { out = api::LogLevel::kSilent;   return std::nullopt; }

    out = api::LogLevel::kSilent;
    return MakeErrorWithNote(
        "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
        "Valid values are \"verbose\", \"debug\", \"info\", \"warning\", \"error\", or \"silent\".");
}

// =============================================================================
// Comma separated lists
// =============================================================================
//
// Splits a value on a separator, with one special case: an empty value is an
// empty list rather than a list holding one empty element.
//
// That case is the reason this function exists rather than a bare getline. The
// flags that come through here are lists of names — the package.json fields to
// read in order, the export conditions to activate, the extensions to resolve,
// the statement labels to delete, the targets to build for — and in every one
// of them a single empty element is worse than no elements at all. An empty
// field in "main-fields" matches every package.json rather than none, an empty
// condition activates nothing and reads as a mistake, and an empty label names
// no code to delete while looking as though it named some. Writing the flag
// with nothing after the "=" therefore means "none of them", and that is what
// an empty result says.
//
// Two details follow from reading with a stream, and a caller splitting a flag
// by hand should know both. An empty element in the middle of a list is kept,
// because "--conditions=a,,b" is a list of three with the middle one empty and
// a person who typed that meant to write something there. A separator at the
// end does not produce a final empty element, so "a,b," is a list of two: the
// trailing separator is read as the end of the last element rather than as the
// start of an empty one.
//
// Input:  s = ""
// Output: an empty vector, which is the whole point of the function.
//
// Input:  s = "main,module"
// Output: { "main", "module" }.
//
// Input:  s = "a,,b"
// Output: { "a", "", "b" } — the empty middle element survives.
//
// Input:  s = "a,b,"
// Output: { "a", "b" }.
std::vector<std::string> splitWithEmptyCheck(const std::string& s, char sep) {

    // Checked before the stream is built, because an empty string has no
    // separator in it at all and the loop below would decide what to do with
    // that by accident. Deciding it here is the decision this function is named
    // for.
    if (s.empty()) return {};

    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string token;

    // The returned vector is not reserved, because the list arrives from a
    // command line where the common case is one element, and a reservation for
    // a guess would allocate for flags that are read once per run.
    while (std::getline(iss, token, sep)) {
        result.push_back(token);
    }
    return result;
}

// =============================================================================
// Whole numbers
// =============================================================================
//
// Reads a value that has to be a whole number and nothing else, reporting
// rather than throwing. The note is a parameter because only the caller knows
// what the number is for: "The watch delay must be an integer." says something
// a person can act on, and no generic wording would.
//
// Two things count as failure, and they produce the same complaint. A value
// with anything in it that is not a digit is thrown by the conversion — an
// empty value, or one that starts with a letter. A value that converts but
// does not end there, such as "300ms", is caught by comparing the position the
// conversion stopped at against the length of the value, which is why the
// conversion is asked where it stopped rather than simply run. The prefix rule
// that the position check enforces is the strict one: a leading "+" or a
// leading "-" is accepted because both are a whole number, and a leading space
// is not, because it is a typo rather than a number.
//
// The return value on failure is 0, and "err" is set. A caller that treats the
// complaint as fatal returns it and never looks at the number; a caller that
// wants to report everything wrong with a line at once keeps the 0 and carries
// on, which is why this function cannot decide that on its own.
//
// Input:  value = "300", error_hint = "The watch delay must be an integer."
// Output: 300, with "err" untouched.
//
// Input:  value = "300ms"
// Output: 0, and a complaint reading "Invalid value \"300ms\" in
//         \"--watch-delay=300ms\"" whose note is the hint.
//
// Input:  value = ""
// Output: 0 and the same complaint, from the conversion failing rather than
//         from the position check.
int parseInt(const std::string& value, const std::string& arg,
                    const std::string& error_hint, std::optional<ErrorWithNote>& err) {
    try {
        size_t pos = 0;

        // Asked for the stop position so the trailing text can be detected.
        // std::stoi would happily return the leading digits of "300ms" and
        // leave the rest unmentioned, which is the one behaviour a number
        // reader cannot have.
        int result = std::stoi(value, &pos);

        if (pos != value.size()) {
            err = MakeErrorWithNote(
                "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
                error_hint);
            return 0;
        }
        return result;
    } catch (...) {

        // Everything the conversion can throw lands here rather than at the
        // caller: a value that is not a number at all, and a number too large
        // or too small to be an int. Both are the same mistake as far as the
        // person who typed it is concerned, and the caller's hint is the right
        // advice for both.
        err = MakeErrorWithNote(
            "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
            error_hint);
        return 0;
    }
}

// =============================================================================
// Deciding that a command line is a build's
// =============================================================================
//
// The flags below are the ones the grammar accepts for a build and not for a
// transform, and they are here as prefixes so isArgForBuild can recognise one
// without knowing whether it arrived bare or with a value attached. Every entry
// corresponds to a clause in parseOptionsImpl (cli_options.cpp) carrying a
// "&& build_opts" guard, and the two lists are meant to be kept in step: a flag
// added to the grammar under that guard and not added here is a flag that turns
// a build into a complaint about a transform.
//
// The spelling of each entry is the one the grammar expects, "=" or ":" included,
// and a near miss is rejected rather than trimmed — "--outdirs=dist" is not
// "--outdir=" and is a flag nobody has heard of, exactly as it would be if the
// grammar were asked directly.
//
// Nothing both halves accept is listed, and that is the point of the list rather
// than an omission: "--minify" and "--format=" are equally at home in a build
// and a transform, so they are no more evidence of one than of the other.
static const char* const kBuildOnlyFlagPrefixes[] = {
    "--bundle",
    "--mangle-cache=",
    "--metafile",
    "--outfile=",
    "--outdir=",
    "--outbase=",
    "--resolve-extensions=",
    "--main-fields=",
    "--conditions=",
    "--public-path=",
    "--tsconfig=",
    "--entry-names=",
    "--chunk-names=",
    "--asset-names=",
    "--loader:",
    "--out-extension:",
    "--packages=",
    "--external:",
    "--inject:",
    "--alias:",
    "--banner:",
    "--footer:",
};
//
// Answers whether one argument is evidence that the command line as a whole is
// about building something, which is the question the two callers ask before
// they act on the whole list: whether to strip the analyze flags out of it, and
// whether to read it into a build's options or a transform's.
//
// Two kinds of argument are evidence. Anything that does not begin with a dash
// is a path, and a path is what a build is for. And any flag that only the build
// grammar accepts is evidence on its own, because a transform has no use for an
// output directory, a metafile or a package rule: the argument says which half
// of the grammar was meant, whatever else is on the line.
//
// That second kind is a list, and the list below is the same list the grammar
// keeps. Every entry here corresponds to a clause in parseOptionsImpl that reads
// "... && build_opts", and the two are meant to be read side by side: a flag
// added to the grammar with that guard and not added here is a flag that turns a
// build into a complaint about a transform, which is the failure this table
// exists to prevent. The test "CliBuild.BuildOnlyFlagsAreNotMistakenForTransform
// Flags" is the other half of that promise, and fails if the two drift apart.
//
// The table holds prefixes, not whole arguments, and each is spelled the way the
// grammar spells it — with the "=" or the ":" the grammar expects — because a
// prefix is also what stops a near miss from being accepted: "--outdirs=dist" is
// not "--outdir=" and is rejected here as firmly as the grammar rejects it.
//
// What is deliberately absent is every flag both halves accept: --minify,
// --watch, --format=, --platform=, --target=, --loader=, --sourcefile= and the
// rest. Those say nothing about which half was meant, so a command line carrying
// only those is left to the grammar, and a transform keeps accepting them.
//
// Input:  arg = "src/index.js"
// Output: true. This is the case the whole function exists for: a bare path
//         with no command in front of it is a build.
//
// Input:  arg = "--outdir=dist"
// Output: true. A transform cannot write a directory, so this argument is a
//         build's even with no path anywhere on the line.
//
// Input:  arg = "--bundle"
// Output: true.
//
// Input:  arg = "--bundle=false"
// Output: false. The bare switch is the evidence; this is not it, which is what
//         isArgBoolFlag is for and what the caller uses it for.
//
// Input:  arg = "--minify"
// Output: false. Both grammars take it, so it is not evidence of either.
bool isArgForBuild(const std::string& arg) {

    // The two clauses are the two kinds of evidence, and neither is a
    // conversation with the grammar: this function is asked before the grammar
    // has decided anything, and has to answer from the shape of the argument
    // alone.
    if (!arg.starts_with("-")) {
        return true;
    }

    for (const char* prefix : kBuildOnlyFlagPrefixes) {
        if (arg.starts_with(prefix)) {
            return true;
        }
    }

    return false;
}

// =============================================================================
// Language levels and engines
// =============================================================================
//
// The names that "--target" accepts, and nothing else that knows them.
//
// Both tables are static and file-local: they are constant for the lifetime of
// the process, they are not part of anything an embedder can extend, and
// keeping them here means the note attached to a rejected name can be built
// from the same table that accepts it.
static const std::unordered_map<std::string, api::EngineName> kValidEngines = {
    {"chrome",  api::EngineName::kChrome},
    {"deno",    api::EngineName::kDeno},
    {"edge",    api::EngineName::kEdge},
    {"firefox", api::EngineName::kFirefox},
    {"hermes",  api::EngineName::kHermes},
    {"ie",      api::EngineName::kIE},
    {"ios",     api::EngineName::kIOS},
    {"node",    api::EngineName::kNode},
    {"opera",   api::EngineName::kOpera},
    {"rhino",   api::EngineName::kRhino},
    {"safari",  api::EngineName::kSafari},
};

// A language level is a single edition, and two names may mean the same one:
// "es6" and "es2015" are the same edition, and both are in the table pointing
// at the same value, because both are what people type. The naming is otherwise
// the year, with "esnext" standing for the edition that has no year yet and
// "es5" for the one that is older than the convention.
static const std::unordered_map<std::string, api::Target> kValidTargets = {
    {"esnext", api::Target::kESNext},
    {"es5",    api::Target::kES5},
    {"es6",    api::Target::kES2015},
    {"es2015", api::Target::kES2015},
    {"es2016", api::Target::kES2016},
    {"es2017", api::Target::kES2017},
    {"es2018", api::Target::kES2018},
    {"es2019", api::Target::kES2019},
    {"es2020", api::Target::kES2020},
    {"es2021", api::Target::kES2021},
    {"es2022", api::Target::kES2022},
    {"es2023", api::Target::kES2023},
    {"es2024", api::Target::kES2024},
    {"es2025", api::Target::kES2025},
};

// =============================================================================
// Reading a --target list
// =============================================================================
//
// Reads the values of one "--target" flag, which accepts two different kinds of
// thing in the same comma-separated list: a language level, which says what
// output to produce, and an engine with a version, which says what has to keep
// working. A person writes both, in one flag, in either order.
//
// The language level is looked up first because it is a whole word, and an
// engine is a word plus a version. The engine is then matched by prefix, and
// everything after the name is the version — which is why the case is folded
// first and why the version keeps whatever characters it has: "Chrome120",
// "chrome120" and "CHROME120" are the same request, and a version is a
// free-form string the engine interprets rather than something parsed here.
//
// An engine name with no version is a complaint, and the only one in this
// function with an empty note. That is not an oversight: the complaint already
// says exactly what is missing, so a note would either repeat it or say
// something less useful. It is the one case where the missing half is a value
// rather than a spelling.
//
// The note for a name that is neither is assembled from the tables on the way
// out, sorted so the message is the same whichever table order it was built
// from, and phrased as a list of shapes rather than a list of names: a language
// level, or an engine followed by N. The placeholders carry the quoting so a
// shell that copies one of them cannot misread it.
//
// Nothing here depends on the order the engine table is walked in, which is
// worth stating because the walk is over a hash map. It works because no engine
// name is a prefix of another, so at most one of them can match a given value.
// A future engine whose name starts with an existing one would break that, and
// this is the place to notice.
//
// Input:  targets = { "es2020" }
// Output: out_target = api::Target::kES2020, no engines added, no complaint.
//
// Input:  targets = { "es2020", "chrome120", "safari17" }
// Output: out_target = api::Target::kES2020 and two engines appended in the
//         order they were written, and no complaint.
//
// Input:  targets = { "es6" }
// Output: out_target = api::Target::kES2015 — the two names are the same
//         edition, and the caller is told which one it got.
//
// Input:  targets = { "chrome" }
// Output: a complaint reading "Target \"chrome\" is missing a version number
//         in \"--target=chrome\"" with an empty note. The engine is not added.
//
// Input:  targets = { "es2013" }
// Output: a complaint reading "Invalid target \"es2013\" in
//         \"--target=es2013\"" whose note lists "esN" and every engine followed
//         by N.
std::optional<ErrorWithNote> parseTargets(
    const std::vector<std::string>& targets,
    const std::string& arg,
    api::Target& out_target,
    std::vector<api::Engine>& out_engines)
{
    // One value at a time, and the first one that is not understood ends the
    // whole flag. A partially applied target list would be a build that
    // silently targets something other than what was asked for, which is the
    // one outcome a flag is not allowed to produce quietly.
    for (const auto& value : targets) {

        // ASCII only, so a name typed in any ASCII case is found, and no
        // assumption is made about the case rules of a script the version might
        // be written in.
        std::string lower = helpers::ToLowerASCII(value);

        auto t_it = kValidTargets.find(lower);
        if (t_it != kValidTargets.end()) {
            out_target = t_it->second;
            continue;
        }

        // Not a language level, so it has to be an engine followed by a
        // version. "found_engine" separates "this was an engine with a version,
        // carry on" from "this was an engine name with nothing after it",
        // because both leave this loop early for different reasons.
        bool found_engine = false;
        for (const auto& [engine_name, engine_id] : kValidEngines) {
            if (lower.starts_with(engine_name)) {
                std::string version = lower.substr(engine_name.size());

                if (version.empty()) {
                    return MakeErrorWithNote(
                        "Target " + helpers::QuoteSingle(value, true) + " is missing a version number in " + helpers::QuoteSingle(arg, true),
                        "");
                }

                out_engines.push_back(api::Engine{.name = engine_id, .version = version});
                found_engine = true;
                break;
            }
        }
        if (found_engine) continue;

        // Nothing matched, so the message has to say what would have. It is
        // built here rather than kept as a string beside the tables because the
        // two would then be free to disagree, and a rejected name is exactly
        // when a person is reading the list of what they could have written.
        std::vector<std::string> valid;
        valid.push_back("\"esN\"");
        for (const auto& [key, _] : kValidEngines) {
            valid.push_back(helpers::QuoteSingle(key + "N", true));
        }
        std::sort(valid.begin(), valid.end());

        // Rendered as an English list — "a, b, or c" — because this is the one
        // message in the file that a person reads with their whole list of
        // engines in mind rather than looking up one name. The final separator
        // is spelled out rather than appended blindly, so the message does not
        // read "a, b, c," when there is nothing after the comma.
        std::string valid_str;
        for (size_t i = 0; i < valid.size(); i++) {
            if (i > 0) {
                valid_str += (i + 1 == valid.size()) ? ", or " : ", ";
            }
            valid_str += valid[i];
        }

        return MakeErrorWithNote(
            "Invalid target " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
            "Valid values are " + valid_str + " where N is a version number.");
    }
    return std::nullopt;
}

// =============================================================================
// The starting options for a run
// =============================================================================
//
// A run begins from the defaults the engine itself defines, and from nothing
// else. Every member of api::BuildOptions has a default that describes what
// that option means when nobody has an opinion about it, and those defaults are
// the ones a build with no flags at all gets.
//
// That is a deliberate division. The defaults in api::BuildOptions are the
// engine's opinion and are the same for every caller, including a host program
// that never touches the command line. The defaults that belong to the command
// line — how many messages to report, how chatty the log should be, whether the
// output is written to disk — are applied by the command that starts the run,
// in the place that knows what kind of run it is. A build, a transform and a
// watch session each set those differently, and none of them is right for the
// other two.
//
// The two functions exist separately because there are two structures and they
// are not interchangeable: TransformOptions is the per-file subset, and the
// options that only mean something for a graph are absent from it by
// construction, so a transform cannot come to depend on an entry point or an
// output directory. One function taking a flag saying which to produce would
// have to return one or the other, and the compiler could not check that the
// caller took the one it meant.
//
// Returned by value so a caller can move the result straight into place. A run
// that assembled its options and then handed a pointer to them elsewhere would
// have to keep the structure alive itself.
//
// Input:  newBuildOptions()
// Output: an api::BuildOptions with every member at its default — bundling
//         off, no entry points, no output location, write off, and the log at
//         info level with no cap.
//
// Input:  newTransformOptions()
// Output: an api::TransformOptions with every member at its default, and no
//         field for an entry point or an output directory to have been set in.
api::BuildOptions newBuildOptions() {

    api::BuildOptions opts;
    return opts;
}

api::TransformOptions newTransformOptions() {

    api::TransformOptions opts;
    return opts;
}

// =============================================================================
// The HTML-first default
// =============================================================================
//
// Switches bundling on when what is being built is a document, and only when
// nobody has said otherwise.
//
// A document cannot be served as one copied file, because it refers to the
// scripts and styles it loads: a build that left bundling off would hand back a
// directory of untouched assets where a browser expects a single file. So the
// request cannot mean anything else, and it is honoured here rather than
// refused.
//
// Three things are checked before the switch is thrown, and each of them is a
// decision about who wins. Bundling is only turned on while it is still off, so
// an explicit choice on the command line is never overridden — including a
// choice to leave it off. Only the plain entry point list is inspected, since
// an entry written as "output=input" already says what its output is and a
// suffix test on its input would be reading the wrong half. And the suffix is
// compared exactly, so ".HTML" does not count: the comparison is a case
// sensitive one on purpose, because a file called that is not something a
// document loader recognises either, and a case-insensitive test here would
// enable a mode the rest of the build does not agree with.
//
// The five in the size check is the length of the suffix, and it is there so
// that a path shorter than the suffix cannot be asked to compare against itself.
//
// Input:  entry_points = { "index.html" }, bundle off
// Output: the same structure with bundle on, nothing else touched.
//
// Input:  entry_points = { "main.js" }, bundle off
// Output: unchanged. Nothing was switched on.
//
// Input:  entry_points = { "index.html" }, bundle already on
// Output: unchanged, and the loop is not entered at all.
void ApplyHtmlBundleDefault(api::BuildOptions& opts) {

    if (!opts.bundle) {
        for (const auto& ep : opts.entry_points) {
            if (ep.size() >= 5 && ep.compare(ep.size() - 5, 5, ".html") == 0) {
                opts.bundle = true;
                return;
            }
        }
    }
}

} // namespace guchho::cli
