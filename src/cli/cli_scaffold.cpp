// =============================================================================
// src/cli/cli_scaffold.cpp — what a starter project is, as data, and the write
// =============================================================================
//
// The two halves of the scaffolding layer, and the reason they are in one file
// is that they are the same half: deciding what goes in a starter project, and
// putting it there. Nothing above this file decides anything about the shape of
// a project, and nothing in it knows why it is being asked.
//
// The first half is a function of three values and touches nothing. Given a
// project type, a template and a name it returns the same list of files, with
// the same contents, on every machine and in every run. There is no clock in it
// and no randomness, which is the whole reason it is separate from the code
// that opens files: a thing that can be compared against a list of paths is a
// thing that can be tested, and a test of a scaffolder that has to create a
// directory to find out what the scaffolder would have created is a test that
// mostly tests the file system.
//
// The second half walks that list. It is the only code here that writes, and
// the rules it writes by are short enough to state in full:
//
//   - A target that does not exist is created. That is the ordinary case.
//   - A target that exists and holds nothing this scaffold would have written
//     is refused, unless force was asked for. Nothing is written when it is.
//   - A file that is already there is left alone, unless force was asked for.
//   - Nothing outside the generated set is removed. Ever, either way.
//
// The last rule is the one that is easiest to get wrong, because "the
// directory was not empty, so clear it out" is a shorter program than "the
// directory was not empty, so stop". The shorter one is also the one that
// takes somebody's work with it. Force here means the generated files may be
// replaced, and nothing else about the directory is in question.
#include "guchho/scaffold.hpp"
#include "guchho/filesystem.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace guchho::scaffold {

// =============================================================================
// The names
// =============================================================================

namespace {

// The one table each pair of lookups reads. The order is the order they are
// printed in, which is also the order they are offered in when a name is
// rejected, and it is the same in both cases so that the list a person reads as
// an explanation is the list that would have worked.
struct NameEntry {
    std::string_view name;
    std::string_view description;
};

constexpr NameEntry kProjectTypes[] = {
    {"app",     "Web application"},
    {"library", "Reusable JavaScript/TypeScript library"},
    {"plugin",  "Guchho build-system plugin"},
};

constexpr NameEntry kTemplates[] = {
    {"basic", "JavaScript + CSS"},
    {"ts",    "TypeScript + CSS"},
    {"jsx",   "JavaScript + JSX + CSS"},
    {"tsx",   "TypeScript + JSX + CSS"},
};

// The extension of the single source file a template produces. There is no
// separate entry for an application and a library here, and there is not one
// for "vanilla" or "library-ts" either: the template answers what language the
// source is in, and nothing about it answers what the project is for.
constexpr std::string_view kExtensions[] = {
    ".js",  // kBasic
    ".ts",  // kTypeScript
    ".jsx", // kJSX
    ".tsx", // kTSX
};

// True when a character may appear in an unscoped npm package name. This is the
// intersection of "may appear in a URL" and "survives a shell", narrowed to
// what npm actually accepts, and it is deliberately written out rather than
// derived: a name is written into a file that people read, and the rule worth
// having is the one npm enforces, not the one this function could derive.
bool IsNpmNameChar(unsigned char c)
{
    return std::islower(c) != 0 ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '.' || c == '_' || c == '~';
}

} // namespace

std::string_view ProjectTypeName(ProjectType type)
{
    return kProjectTypes[static_cast<size_t>(type)].name;
}

std::string_view ProjectTypeDescription(ProjectType type)
{
    return kProjectTypes[static_cast<size_t>(type)].description;
}

std::string_view TemplateName(ProjectTemplate tmpl)
{
    return kTemplates[static_cast<size_t>(tmpl)].name;
}

std::string_view TemplateDescription(ProjectTemplate tmpl)
{
    return kTemplates[static_cast<size_t>(tmpl)].description;
}

const std::vector<std::string>& ProjectTypeNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const NameEntry& entry : kProjectTypes) {
            out.emplace_back(entry.name);
        }
        return out;
    }();
    return names;
}

const std::vector<std::string>& TemplateNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const NameEntry& entry : kTemplates) {
            out.emplace_back(entry.name);
        }
        return out;
    }();
    return names;
}

std::string_view SourceExtension(ProjectTemplate tmpl)
{
    return kExtensions[static_cast<size_t>(tmpl)];
}

std::string_view ConfigFileName(ProjectConfig config)
{
    return config == ProjectConfig::kJson ? "guchho.json"
                                          : "guchho.config.js";
}

std::optional<std::string> ParseProjectType(std::string_view value, ProjectType& out)
{
    for (size_t i = 0; i < std::size(kProjectTypes); i++) {
        if (kProjectTypes[i].name == value) {
            out = static_cast<ProjectType>(i);
            return std::nullopt;
        }
    }
    return std::format("Unknown project type '{}'", value);
}

std::optional<std::string> ParseProjectTemplate(std::string_view value, ProjectTemplate& out)
{
    for (size_t i = 0; i < std::size(kTemplates); i++) {
        if (kTemplates[i].name == value) {
            out = static_cast<ProjectTemplate>(i);
            return std::nullopt;
        }
    }
    return std::format("Unknown template '{}'", value);
}

// =============================================================================
// The name that goes into a package.json
// =============================================================================
//
// A directory can be called almost anything and a package cannot: no capitals,
// no spaces, nothing that a shell would eat. Rather than refuse the second
// case — which would mean a directory named "My App" failing over a name that
// is perfectly good as a directory — the name is brought into the smaller set.
// What is refused is the case where the smaller set comes out empty, because
// there is no honest package name to write and a generated package.json with
// an empty "name" is worse than an error that says why.
//
// The distinction matters to the caller, which is told nothing about how much
// was changed and can therefore compare the answer against the directory name
// and say so. Silently renaming somebody's project to something they did not
// type is the failure this avoids.

std::optional<std::string> NpmPackageName(std::string_view directory_name)
{
    // The last path segment is the name. The init command passes the base of
    // the working directory, which has no separator in it, but an embedder can
    // pass a path and "a/b/my-lib" is a project called "my-lib", not one called
    // "a/b/my-lib".
    size_t slash = directory_name.find_last_of("/\\");
    std::string_view base = slash == std::string_view::npos
        ? directory_name
        : directory_name.substr(slash + 1);

    std::string out;
    out.reserve(base.size());
    bool last_was_dash = false;
    for (char raw : base) {
        // Lowercased rather than dropped, because "MyLib" and "mylib" are the
        // same name to a person and the package registry only has room for one.
        unsigned char c = static_cast<unsigned char>(
            static_cast<char>(std::tolower(static_cast<unsigned char>(raw))));
        if (!IsNpmNameChar(c)) {
            // Runs of characters that cannot be written become one dash, so
            // "my lib" is "my-lib" rather than "my--lib".
            if (!last_was_dash && !out.empty()) {
                out += '-';
                last_was_dash = true;
            }
            continue;
        }
        out += static_cast<char>(c);
        last_was_dash = c == '-';
    }

    // A leading dot or dash is not allowed, and a trailing dash reads as a name
    // that got cut off rather than as the name. Both are stripped here so the
    // loop above does not have to know about either.
    while (!out.empty() && (out.front() == '.' || out.front() == '-' || out.front() == '_')) {
        out.erase(out.begin());
    }
    while (!out.empty() && (out.back() == '.' || out.back() == '-')) {
        out.pop_back();
    }

    if (out.empty() || out.size() > 214) {
        return std::nullopt;
    }
    return out;
}

// =============================================================================
// The files
// =============================================================================
//
// What follows is the entire content of a starter project. It is worth saying
// out loud what these strings are chosen to be, because the temptation with a
// scaffolder is to make them impressive.
//
// They are minimal, because the point of "init" is a starting point and a
// demonstration is not one. They are valid, because a project that does not
// build on the first try teaches the reader that Guchho is difficult. They
// assume no framework, because a starter that imports React is a starter that
// fails before anyone has installed anything. And they name each other by
// relative path, because that is what makes a generated project work wherever
// it is copied to.
//
// The one place a comment appears is the JSX and TSX starter, and it is there
// because a person who asked for a JSX template and got a file with no markup
// in it would reasonably conclude the command was broken.

namespace {

// Builds one of the documents. The page is the same for all four templates;
// only the script it loads changes, because that is the only thing the template
// is responsible for.
std::string AppHtml(std::string_view script_name)
{
    return std::format(
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
        "  <script type=\"module\" src=\"./{}\"></script>\n"
        "</body>\n"
        "</html>\n",
        script_name);
}

// The script an application starts from.
//
// The four are one program, and each template changes exactly one thing about
// it: the typed templates say what the element is, and the markup templates say
// that they are not using markup yet. Both import the stylesheet rather than
// leaving it to the document alone, so that a build pulls the three files
// together and a change to one of them shows up in the output.
std::string AppScript(ProjectTemplate tmpl)
{
    std::string out;

    // Only the two markup templates get this, and only because of the note that
    // follows it. There is no JSX runtime installed, so a file that used markup
    // here would be a project that could not be built; the honest thing is to
    // write the file the template asked for and to say why it is plain.
    if (tmpl == ProjectTemplate::kJSX || tmpl == ProjectTemplate::kTSX) {
        out +=
            "// This file is loaded as JSX, which is what the extension asks for.\n"
            "// No JSX runtime is installed, so nothing here uses markup yet.\n";
    }

    out += "import './style.css';\n";
    out += "\n";

    const bool typed = tmpl == ProjectTemplate::kTypeScript || tmpl == ProjectTemplate::kTSX;
    out += typed
        ? "const app: Element | null = document.querySelector('h1');\n"
        : "const app = document.querySelector('h1');\n";

    out +=
        "if (app) {\n"
        "  app.textContent = 'Hello from Guchho!';\n"
        "}\n";
    return out;
}

std::string AppStyle()
{
    return
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
        "}\n";
}

// The "build" fields a generated config carries. They are the whole config, in
// the order they are written, and there is exactly one list of them: the two
// formats below share it, so "the same defaults" is true by construction rather
// than by whatever the two renderers happen to agree on. Every key is one the
// parser already accepts (see the "build" list in guchho_json.cpp) and most of
// the values are the defaults a build would use anyway, which is the point: a
// person can open this file, see what a build is being asked for, and change
// one line.
//
// "quote" is the flag that says whether the value is a string, and it is the
// only way the two formats need to differ about a value: both write booleans
// bare, and both quote strings — in the quote the other format uses.
struct ConfigEntry {
    std::string_view key;
    std::string      value;
    bool             quote;
};

std::vector<ConfigEntry> AppConfigFields()
{
    return {
        {"entry", "src/index.html", true},
        {"outdir", "dist", true},
        {"format", "esm", true},
        {"target", "esnext", true},
        {"minify", "true", false},
        {"pretty", "true", false},
        {"minifyHtml", "false", false},
    };
}

// The "build" fields a library or a plugin is built with: the source file is
// the entry, the output is a directory of its own, and the platform is neutral
// because a package is not loaded by a browser. Nothing else is here, and in
// particular there is no "types" field — the build does not produce a
// declaration file, and a package.json that points at one that is never
// written is a broken package rather than a helpful one.
std::vector<ConfigEntry> PackageConfigFields(std::string_view ext)
{
    return {
        {"entry", "src/index" + std::string(ext), true},
        {"outdir", "dist", true},
        {"format", "esm", true},
        {"platform", "neutral", true},
        {"target", "esnext", true},
        {"minify", "true", false},
        {"pretty", "true", false},
    };
}

// The config written for a JavaScript runtime: an ES module carrying the same
// fields as the loader accepts from a .js config file. The layout is fixed —
// one field per line, trailing commas — so that the tests that diff the two
// formats and the tests that diff two runs can rely on byte-for-byte equality.
std::string RenderConfigJavaScript(const std::vector<ConfigEntry>& fields)
{
    std::string out = "export default {\n"
                      "  build: {\n";
    for (const ConfigEntry& field : fields) {
        out += "    ";
        out += field.key;
        out += ": ";
        out += field.quote ? "'" + field.value + "'" : std::string(field.value);
        out += ",\n";
    }
    out += "  },\n"
           "};\n";
    return out;
}

// The config written when there is no runtime to evaluate one: the same fields
// as the JavaScript form, in strict JSON. There are no trailing commas and the
// strings are double-quoted, and nothing else differs — the field list above is
// what makes the two forms say the same thing.
std::string RenderConfigJson(const std::vector<ConfigEntry>& fields)
{
    std::string out = "{\n"
                      "  \"build\": {\n";
    for (size_t i = 0; i < fields.size(); i++) {
        const ConfigEntry& field = fields[i];
        out += "    \"";
        out += field.key;
        out += "\": ";
        out += field.quote ? "\"" + field.value + "\"" : std::string(field.value);
        out += (i + 1 < fields.size()) ? ",\n" : "\n";
    }
    out += "  }\n"
           "}\n";
    return out;
}

// Renders the fields in the format a request asks for.
std::string RenderConfig(ProjectConfig config, const std::vector<ConfigEntry>& fields)
{
    return config == ProjectConfig::kJson ? RenderConfigJson(fields)
                                          : RenderConfigJavaScript(fields);
}

// The one source file of a library: an export, so that importing the built
// package has something to import, and nothing else.
std::string LibrarySource(ProjectTemplate tmpl)
{
    if (tmpl == ProjectTemplate::kTypeScript || tmpl == ProjectTemplate::kTSX) {
        return
            "export function add(a: number, b: number): number {\n"
            "  return a + b;\n"
            "}\n";
    }
    return
        "export function add(a, b) {\n"
        "  return a + b;\n"
        "}\n";
}

// A plugin is a package that extends the build, and the extension point is not
// reachable from JavaScript yet: the config file's "plugins" field is accepted
// and then ignored, with a warning, because nothing reads it. So this is a
// placeholder that builds and says what it is, rather than a scaffold
// describing hooks that would fail to compile or silently do nothing.
//
// It is the same source for all four templates, which is a deliberate choice
// rather than an oversight: a placeholder has nothing to type, and giving the
// TypeScript templates a type that only exists to be a type would make the file
// look like it were doing something.
//
// The export is an empty object and stays that way until there is an API for it
// to export. A name or a version or a setup function written here would be a
// guess at a shape nobody has agreed on, and a wrong guess is worse than
// nothing because it reads as a decision.
std::string PluginSource()
{
    return
        "// Starter only. Guchho has no JavaScript plugin API yet, so nothing\n"
        "// here is called by a build. See README.md.\n"
        "export default {};\n";
}

// The manifest. Three fields and nothing else, because every one of them is a
// fact about the package rather than a setting somebody has to understand, and
// anything invented here would be a claim about Guchho rather than about npm.
std::string PackageJson(std::string_view package_name)
{
    return std::format(
        "{{\n"
        "  \"name\": \"{}\",\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"private\": false\n"
        "}}\n",
        package_name);
}

std::string LibraryReadme(std::string_view display_name)
{
    return std::format(
        "# {}\n"
        "\n"
        "Built with [Guchho](https://github.com/guchho).\n"
        "\n"
        "## Build\n"
        "\n"
        "    guchho build\n"
        "\n"
        "The bundle is written to `dist/`.\n",
        display_name);
}

// The plugin readme says the useful thing first, which is that there is nothing
// to do here yet. A starter that hid that behind a paragraph of instructions
// would be worse than no starter.
std::string PluginReadme(std::string_view display_name)
{
    return std::format(
        "# {}\n"
        "\n"
        "A Guchho build plugin.\n"
        "\n"
        "Guchho does not load plugins written in JavaScript yet — the `plugins`\n"
        "field in a config file is accepted and ignored. This package builds and\n"
        "is a starting point; there are no hooks to implement until that changes.\n"
        "\n"
        "## Build\n"
        "\n"
        "    guchho build\n"
        "\n"
        "The bundle is written to `dist/`.\n",
        display_name);
}

// The first path segment of a generated path, which is the name the scaffold
// occupies in the target directory. "src/index.html" occupies "src".
std::string_view TopLevelName(std::string_view path)
{
    size_t slash = path.find('/');
    return slash == std::string_view::npos ? path : path.substr(0, slash);
}

} // namespace

FileList BuildFileList(const ScaffoldRequest& req)
{
    FileList list;

    const std::string ext = std::string(SourceExtension(req.tmpl));
    const std::string source_path = "src/index" + ext;
    const std::string script_path = "src/main" + ext;

    // The name only matters where it is written down. An application has no
    // package.json and no readme, so its files are identical whatever the
    // project is called — which is what lets a bare "guchho init" produce the
    // same bytes on every machine, in whatever directory it happened to be run.
    std::string package_name;
    if (req.type == ProjectType::kLibrary || req.type == ProjectType::kPlugin) {
        auto name = NpmPackageName(req.name);
        if (!name) {
            list.error =
                std::format("Cannot derive a package name from '{}'", req.name);
            return list;
        }
        package_name = *name;
    }

    // The one config. The fields are the project's, the format is the
    // request's, and the file name is the two of them looking at each other —
    // computed once so the three uses of it (the path, the contents, the
    // message) cannot disagree.
    const std::vector<ConfigEntry> config_fields =
        req.type == ProjectType::kApp ? AppConfigFields()
                                      : PackageConfigFields(ext);
    const std::string config_name = std::string(ConfigFileName(req.config));
    const std::string config_text = RenderConfig(req.config, config_fields);

    switch (req.type) {
        case ProjectType::kApp:
            list.files.push_back({"src/index.html", AppHtml("main" + ext)});
            list.files.push_back({script_path, AppScript(req.tmpl)});
            list.files.push_back({"src/style.css", AppStyle()});
            list.files.push_back({config_name, config_text});
            break;

        case ProjectType::kLibrary:
            list.files.push_back({source_path, LibrarySource(req.tmpl)});
            list.files.push_back({config_name, config_text});
            list.files.push_back({"package.json", PackageJson(package_name)});
            list.files.push_back({"README.md", LibraryReadme(package_name)});
            break;

        case ProjectType::kPlugin:
            list.files.push_back({source_path, PluginSource()});
            list.files.push_back({config_name, config_text});
            list.files.push_back({"package.json", PackageJson(package_name)});
            list.files.push_back({"README.md", PluginReadme(package_name)});
            break;
    }
    return list;
}

// =============================================================================
// Writing
// =============================================================================

namespace {

// Joins a "/" separated relative path onto a target directory using the file
// system's own separator. The generated paths are stored with "/" because they
// are compared against a list and printed in a message, so turning one into a
// native path is this function's job and no other code's.
std::string JoinUnder(filesystem::Fs& fs, const std::string& target, std::string_view rel)
{
    std::vector<std::string> parts;
    for (size_t start = 0; start <= rel.size(); ) {
        size_t slash = rel.find('/', start);
        if (slash == std::string_view::npos) {
            parts.emplace_back(rel.substr(start));
            break;
        }
        parts.emplace_back(rel.substr(start, slash - start));
        start = slash + 1;
    }
    if (parts.size() == 1) return fs.Join({target, parts[0]});
    if (parts.size() == 2) return fs.Join({target, parts[0], parts[1]});
    return fs.Join({target, parts[0], parts[1], parts[2]});
}

// True when a file is already there.
//
// Asked of the directory rather than of the path, because a file is not the only
// thing that can be named by a path and a directory of the same name would
// answer a different question. The lookup is the case-insensitive one the
// interface provides, and only the first half of its answer is used: a name that
// matched under a different spelling is still a file that is there, and whether
// the spelling is the one we would have written is not a question worth
// refusing over.
bool FileExists(filesystem::Fs& fs, const std::string& full_path)
{
    auto entries = fs.ReadDirectory(fs.Dir(full_path));
    if (!entries.Ok()) return false;
    auto [entry, different_case] = entries.value.Get(fs.Base(full_path));
    (void)different_case;
    return entry != nullptr && entry->Kind(fs) == filesystem::EntryKind::kFile;
}

} // namespace

ScaffoldResult RunScaffold(filesystem::Fs& fs, const ScaffoldRequest& req)
{
    ScaffoldResult result;

    FileList list = BuildFileList(req);
    if (!list.Ok()) {
        result.error = list.error;
        return result;
    }

    // A directory that holds only what this scaffold would have written is a
    // project that already exists, and running the command again over it is
    // harmless: every file is found, every file is skipped, and nothing is
    // lost. A directory holding anything else is somebody's work, and is
    // refused before a single byte is written.
    //
    // The comparison is a count of distinct names, and the scaffold's own names
    // are made distinct before they are counted: three files under "src" occupy
    // one name in the target directory, and counting them three times would make
    // a directory holding nothing but "src" look like something else is in it.
    // The lookup goes through the interface rather than a string comparison, so
    // a project whose README was renamed to readme.md on a case-insensitive
    // file system still counts as one this scaffold owns.
    //
    // A directory that cannot be read is not treated as empty. It is left to the
    // write below to fail on its own terms, which is a clearer error than one
    // invented here about a directory the reader never managed to look at.
    //
    // The question is asked of the target directory, and only the target
    // directory. Kind is used here purely as a yes-or-no — does a directory
    // already sit here — and what it hands back as the path is not the thing
    // being read: on the host this was written on it answers with an empty
    // string, and reading that is how a guard against writing into somebody's
    // directory becomes a guard that never fires.
    auto [normalized, kind] = fs.Kind(fs.Dir(req.target_dir), fs.Base(req.target_dir));
    if (kind == filesystem::EntryKind::kDir) {
        auto entries = fs.ReadDirectory(req.target_dir);
        if (entries.Ok() && entries.value.data) {
            std::set<std::string> top_level;
            for (const ProjectFile& file : list.files) {
                top_level.insert(std::string(TopLevelName(file.path)));
            }
            size_t owned = 0;
            for (const std::string& name : top_level) {
                if (entries.value.Get(name).first != nullptr) {
                    owned++;
                }
            }
            if (static_cast<size_t>(entries.value.PeekEntryCount()) > owned && !req.force) {
                result.dir_not_empty = true;
                result.error = std::format("Directory '{}' is not empty", fs.Base(req.target_dir));
                return result;
            }
        }
    }

    // Created rather than assumed, because the first of the four paths is the
    // only one that needs a directory in front of it. A failure here needs no
    // message of its own: the first file that cannot be written reports the
    // same problem in terms of the file that was asked for.
    std::string dir_error;
    if (!filesystem::MkdirAll(fs, req.target_dir, dir_error)) {
        result.error = std::format("Could not create directory '{}': {}",
            fs.Base(req.target_dir), dir_error);
        return result;
    }

    // Every file is attempted whatever happened to the one before it, so a
    // directory that cannot be created does not leave the files that do not
    // need one unwritten, and the summary is the only place a failure is
    // mentioned. `ok` is combined without short-circuiting for the same reason.
    bool ok = true;
    for (const ProjectFile& file : list.files) {
        std::string full_path = JoinUnder(fs, req.target_dir, file.path);

        // Whether the file is already there is only asked when the answer
        // changes something. Without force the answer means skip, and with it
        // the answer does not: a file that exists is a file to be replaced, and
        // that is what was asked for.
        const bool already_there = FileExists(fs, full_path);
        if (already_there && !req.force) {
            result.skipped.push_back(file.path);
            continue;
        }

        std::string file_dir_error;
        filesystem::MkdirAll(fs, fs.Dir(full_path), file_dir_error);

        // The permit pair either side of the open. Four files written one
        // after another cannot exhaust the process's handles, so there is
        // nothing to hold here and the slot is taken back at once.
        filesystem::BeforeFileOpen();
        // Opened through the path conversion rather than from the UTF-8 string
        // directly, so that a project whose directory is not ASCII opens on
        // Windows the same way it does everywhere else.
        std::ofstream ofs(filesystem::PathFromUTF8(full_path), std::ios::binary);
        filesystem::AfterFileClose();
        if (!ofs.is_open()) {
            ok = false;
            continue;
        }
        // Written as bytes, because the contents include a document and a
        // stylesheet and neither should have its line endings rewritten by
        // whatever the platform considers correct.
        ofs.write(file.contents.data(), static_cast<std::streamsize>(file.contents.size()));
        if (!ofs) {
            ok = false;
            continue;
        }
        // Reported as what happened to it rather than as a creation, since a
        // person who asked for an overwrite is owed the knowledge that one
        // happened.
        if (already_there) {
            result.overwritten.push_back(file.path);
        } else {
            result.created.push_back(file.path);
        }
    }

    if (!ok) {
        result.error = "Some files could not be created";
    }
    return result;
}

} // namespace guchho::scaffold
