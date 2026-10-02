// =============================================================================
// include/guchho/scaffold.hpp — what "guchho init" writes, and nothing else
// =============================================================================
//
// This header is the boundary between the init command and the thing that
// actually produces a project. Everything above the boundary — parsing
// arguments, asking questions, choosing where to write, saying what happened —
// is the command's own problem. Everything below it is a function of three
// values: the kind of project, the template, and the name it was given.
//
// That split is the point of the file, and it exists for two reasons. The first
// is that a command which both asks questions and writes files has no way to be
// tested against a question: every check has to invent a whole run to get to
// it. Split out, the half that decides what a starter project is can be called
// with three arguments and compared to a list of paths, which is what a test of
// a scaffold should be asserting anyway.
//
// The second is that generation is deterministic and I/O is not. Given the same
// three values this layer returns the same bytes every time, on every machine,
// with no clock and no randomness in it. That is what makes "did the template
// change?" answerable by diffing two runs, and it is why nothing below this
// boundary is allowed to read a file, a timestamp, or an environment
// variable. The only part that touches the disk is RunScaffold, and all it does
// with what it is handed is write it.
//
// Nothing here knows that a command line exists. There is no "flag", no usage
// string and no exit code in this header, because the one caller that has all
// three is a command and the other callers — the tests, and anything embedding
// the engine that wants to write a starter of its own — do not.
//
// The names are the ones a person types, and they are deliberately the same
// names the command accepts: "app", "library", "plugin", "basic", "ts", "jsx",
// "tsx". The language and the shape of the project are kept apart on purpose. A
// library written in TypeScript is the same language template as an application
// written in TypeScript, and it is a different project type, and the two
// answers are recorded in two different enums because they answer two different
// questions. Merging them would mean either a name for every combination or a
// flag that means one thing sometimes and another thing other times, and both
// are worse to read a year from now than four short words and an enum.
#pragma once

#include "guchho/filesystem.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace guchho::scaffold {

// What the project is for. It decides the shape of the tree, not the language.
enum class ProjectType {
    kApp,      // A page: a document that loads a script and a stylesheet.
    kLibrary,  // A package other projects import. No document, and no stylesheet.
    kPlugin,   // A package that extends the build. A library that knows what it
               // is for, and is written down as such before the API exists.
};

// Which dialect the source is written in. It decides one thing — the extension
// of the one source file — and the config that goes with it.
//
// "basic" is JavaScript and no markup. "jsx" and "tsx" select the loader that
// understands markup, and nothing more: neither implies that a JSX runtime is
// installed, and a starter that assumed one would be a project that could not
// be built the moment it was created.
enum class ProjectTemplate {
    kBasic,
    kTypeScript,
    kJSX,
    kTSX,
};

// Which configuration file the project is built from. It is the last of the
// three choices, and the one that is decided by the machine rather than chosen
// by the person: "guchho init" asks a type and a template and never a format,
// because which format works is an answer the machine already has (a runtime to
// evaluate a config means a config the runtime can evaluate, and no runtime
// means the config Guchho can read by itself). The default keeps the file this
// project has always written, so a caller that fills in nothing else gets a
// JavaScript config exactly as before.
enum class ProjectConfig {
    kJavaScript,  // guchho.config.js, evaluated by an external JS runtime.
    kJson,        // guchho.json, read directly by Guchho.
};

// One file the scaffold wants written, and where it goes.
//
// The path is relative to the target directory and always uses "/" as its
// separator, whatever the host is, because it is compared against a list in a
// test and printed in a message. It is turned into a native path by the one
// function that writes it, and nowhere else.
struct ProjectFile {
    std::string path;
    std::string contents;
};

// Everything the scaffold needs to know, and nothing it can be talked out of.
//
// The defaults are the command's defaults: an application, in the plain
// template, named after the directory it is being written into. A caller that
// fills in nothing but a target and a name gets the same tree a bare
// "guchho init" gets.
//
// target_dir is a parameter rather than a convention because this layer is not
// only the init command's. The command passes the working directory and has no
// way to pass anything else, but a caller embedding the engine can point this
// anywhere, and the layer does not need to know which of the two it is serving.
struct ScaffoldRequest {
    ProjectType     type = ProjectType::kApp;
    ProjectTemplate tmpl = ProjectTemplate::kBasic;

    // What the project is called. It reaches package.json and README.md and
    // nowhere else — an application writes neither, which is why the command has
    // no flag for it. For a library or a plugin the init command fills it in
    // with the name of the directory being written into.
    std::string     name;

    // Absolute. Where the files are written, created if it is not already there.
    std::string     target_dir;

    // Which of the two config files to write. The init command fills it in from
    // its runtime detection; the default is the JavaScript config, which is what
    // every caller that predates the choice expects, byte for byte.
    ProjectConfig   config = ProjectConfig::kJavaScript;

    bool            force = false;
};

// The files a request turns into, or the reason there are none.
//
// A request can fail before a single byte is generated — a name that cannot be
// written into a package.json is the case that actually happens — and when it
// does, the reason is here rather than in an exception, because the caller has
// to print it as a diagnostic and return an exit code from it.
struct FileList {
    std::vector<ProjectFile> files;
    std::string              error;  // Empty when the list is valid.

    bool Ok() const { return error.empty(); }
    explicit operator bool() const { return Ok(); }
};

// What actually happened on disk.
//
// The two lists are separate because the difference between them is the whole
// answer to "did that overwrite anything", and a caller that has to count a
// combined list to find out has been handed the wrong shape. "skipped" is not a
// failure: a directory that already holds the whole project is a project that
// was already there, and reporting that as an error would make running the
// command twice the kind of thing people stop doing.
struct ScaffoldResult {
    std::vector<std::string> created;
    std::vector<std::string> overwritten;

    // Files that were already there and were left alone. This is the answer to
    // the question a second run asks, and it is a list of its own rather than
    // the absence of a Create line, because "it did not write my file" and "it
    // wrote nothing" are different outcomes and the output has to tell them
    // apart.
    std::vector<std::string> skipped;

    std::string              error;

    // Set when the only thing wrong was that the directory had files in it that
    // this scaffold would not have written, and force was not asked for. It is
    // separated from the other errors because it is the one a person can do
    // something about by typing another word, and the message has to name that
    // word.
    bool dir_not_empty = false;

    bool Ok() const { return error.empty(); }
    explicit operator bool() const { return Ok(); }
};

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

// The name of a type or a template as a person spells it on the command line,
// and the sentence the help puts next to it.
std::string_view ProjectTypeName(ProjectType type);
std::string_view ProjectTypeDescription(ProjectType type);
std::string_view TemplateName(ProjectTemplate tmpl);
std::string_view TemplateDescription(ProjectTemplate tmpl);

// The accepted names, in the order they are printed. These are the same strings
// ParseProjectType and ParseProjectTemplate accept, and the error messages are
// built from them, so a name that is offered and a name that works cannot drift
// apart.
const std::vector<std::string>& ProjectTypeNames();
const std::vector<std::string>& TemplateNames();

// Reads a type or a template name. "out" is written only when the name is one
// this build knows, so a caller that gets a message back can keep whatever it
// had stored there.
//
// Input:  ParseProjectType("lib", out)
// Output: out = ProjectType::kLibrary and an empty optional, meaning success.
//
// Input:  ParseProjectTemplate("vanila", out)
// Output: out untouched, and the message "Unknown template 'vanila'". The caller
//         decides what to do with it, and is the one that attaches the list of
//         names it will also have printed.
std::optional<std::string> ParseProjectType(std::string_view value, ProjectType& out);
std::optional<std::string> ParseProjectTemplate(std::string_view value, ProjectTemplate& out);

// The extension of the one source file a template produces, with the dot.
//
// Input:  SourceExtension(ProjectTemplate::kTSX)  ->  ".tsx"
// Input:  SourceExtension(ProjectTemplate::kBasic) -> ".js"
std::string_view SourceExtension(ProjectTemplate tmpl);

// The name of the configuration file a config format is written to.
//
// Input:  ConfigFileName(ProjectConfig::kJavaScript) -> "guchho.config.js"
// Input:  ConfigFileName(ProjectConfig::kJson)       -> "guchho.json"
std::string_view ConfigFileName(ProjectConfig config);

// The name to write into a package.json, or nothing when there is no honest one.
//
// A directory can be called almost anything and a package cannot, so this is
// where the two meet. It is deliberately separate from the rest: an application
// never needs the answer, and a library whose name cannot be written down has
// to be told so rather than handed a package.json that npm will reject.
//
// Returns the derived name on success and no value on failure, for the same
// reason ParseProjectType reports an unknown template: an empty "name" would
// be a lie, and the caller phrases the refusal.
std::optional<std::string> NpmPackageName(std::string_view directory_name);

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

// The files a request turns into, in the order they should be written.
//
// This is the whole of what a starter project is, as data, and it reads nothing
// and writes nothing. Two calls with equal requests return equal lists, which is
// the property the tests lean on and the reason this function is allowed to
// exist separately from the one that writes.
//
// Input:  a request naming an application in TypeScript
// Output: src/index.html, src/main.ts, src/style.css and guchho.config.js, in
//         that order, with a document that loads "./main.ts" and a script that
//         imports "./style.css". A request that asks for the JSON config writes
//         guchho.json in its place, with the same build fields.
//
// Input:  the same request with the name "My App"
// Output: the same files, byte for byte. A name only reaches the files
//         that carry it, which for an application is none of them.
FileList BuildFileList(const ScaffoldRequest& req);

// Writes what BuildFileList returns, creating the target directory if it is not
// there, and reports what it did.
//
// A directory that does not exist is not a problem: that is the normal case, and
// the command creates the directory a person named. A directory that exists and
// holds something else is, and is the one thing this function refuses to do
// without force — not because writing over a stranger's work is dangerous in
// general, but because a person who typed "init" in the wrong directory should
// be stopped before anything is lost rather than afterwards.
//
// Refusing is also the only case in which nothing is written at all. Every other
// outcome is a best effort: each file is attempted whatever happened to the one
// before it, because a permissions problem on the second file should not leave
// the first one unwritten and the run looking like it did nothing.
//
// Nothing outside the generated set is ever removed, with or without force.
// Force means "these files may be replaced", and the difference between that
// and "clear the directory out" is the difference between a command that
// overwrites a scaffold and one that destroys a project.
//
// Input:  a request whose target does not exist
// Output: created = every generated path, and Ok().
//
// Input:  the same request with force, over a directory holding a generated
//         project and a file called notes.txt
// Output: the generated files replaced, notes.txt untouched and not mentioned,
//         and Ok().
//
// Input:  a request without force, over a directory holding notes.txt
// Output: nothing written, an error naming the directory, dir_not_empty true,
//         and created and skipped both empty.
ScaffoldResult RunScaffold(filesystem::Fs& fs, const ScaffoldRequest& req);

} // namespace guchho::scaffold
