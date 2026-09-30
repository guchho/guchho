// =============================================================================
// src/cli/runtime.cpp — the PATH detective "guchho init" leans on
// =============================================================================
//
// This file answers one question: is there a supported JavaScript runtime on
// this machine, and which one. It is deliberately small and deliberately pure —
// the detection core takes the PATH, the Node override and an executable check
// as arguments, so the machine's own environment is the last thing it touches
// and a test can feed it a table. The machine's real answers (the environment
// and the file system) are gathered in the two functions at the bottom.
//
// The rules it applies are short enough to state in full:
//
//   - GUCHHO_NODE_BIN, when set, is a Node. The config loader already uses
//     that variable to pick the executable that evaluates guchho.config.js
//     (src/resolver/guchho_json.cpp), so a Node that lives outside the PATH —
//     portable, downloaded, installed to a non-standard prefix — has to count
//     as Node here too, or an init run would write guchho.json and then build
//     with a runtime standing ready to have evaluated a config instead.
//   - Otherwise Node is looked up on the PATH by its own name.
//   - Bun is looked up the same way, and only when Node was not found: when
//     both are installed the answer is Node, because Node is the runtime the
//     config loader resolves to first and a project should be configured for
//     the same runtime the build will actually use.
//   - With neither found the answer is "none", which is the case that matters:
//     it is the only one that changes the config format.
//
// None of it executes JavaScript, because none of it has to: the lookup is a
// file-name computation, and starting the runtime to learn whether it exists
// would trade a stat for a process.

#include "guchho/runtime.hpp"
#include "guchho/scaffold.hpp"

#if defined(_WIN32)
#include <filesystem>
#include <system_error>
#else
#include <unistd.h>
#endif

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace guchho::runtime {

namespace {

// The environment variable that overrides the Node binary, under the same name
// the config loader reads (src/resolver/guchho_json.cpp). One name for one
// thing, so a person who points the loader at a Node points the detector at
// the same Node.
constexpr const char* kNodeEnvName = "GUCHHO_NODE_BIN";

std::optional<std::string> GetEnvValue(const char* name)
{
#if defined(_WIN32)
    char*  value = nullptr;
    size_t size  = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result = value;
    free(value);
    return result;
#else
    if (const char* value = std::getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
#endif
}

// The separator that ends one PATH entry. Windows lists its directories with a
// semicolon, and every other platform with a colon; an empty entry means the
// current directory, which is the convention the lookup honors below.
constexpr char kPathListSeparator()
{
#if defined(_WIN32)
    return ';';
#else
    return ':';
#endif
}

// The separator used when a PATH directory and a command name are joined into
// one path, kept native so the candidate string is the platform's own spelling.
constexpr char kPathSeparator()
{
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
}

std::string JoinDirAndName(const std::string& dir, const std::string& name)
{
    if (dir.empty()) return name;
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + kPathSeparator() + name;
}

// Whether a concrete file is a runnable command, asked of the real file system.
//
// Windows has no executable bit, so "executable" means "exists", and the
// candidate list has already restricted the search to the extensions a command
// may carry. Everywhere else the bit is what execvp looks at, so it is what
// this looks at.
bool RealIsExecutable(const std::string& path)
{
#if defined(_WIN32)
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(path), ec) && !ec;
#else
    return ::access(path.c_str(), X_OK) == 0;
#endif
}

} // namespace

std::vector<std::string> RuntimeCandidatesFor(const std::string& dir,
                                              const std::string& name,
                                              std::string_view pathext)
{
    std::vector<std::string> out;
    out.push_back(JoinDirAndName(dir, name));
#if defined(_WIN32)
    // Windows resolves a command by trying each PATHEXT extension in order.
    // The bare name is tried first anyway, because an extension cannot be
    // assumed for a file somebody made command-shaped by some other mechanism,
    // and PATHEXT entries always have their leading dot when they are useful.
    size_t start = 0;
    while (start <= pathext.size()) {
        size_t       end = pathext.find(';', start);
        std::string_view ext = pathext.substr(
            start, end == std::string_view::npos ? std::string_view::npos
                                                 : end - start);
        if (!ext.empty()) {
            out.push_back(JoinDirAndName(dir, name + std::string(ext)));
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
#else
    (void)pathext;
#endif
    return out;
}

bool FindExecutableOnPath(
    const std::string& name, std::string_view path_env,
    const std::function<bool(const std::string&)>& is_executable)
{
    // PATHEXT is read once, from the environment, because it is a property of
    // the machine rather than of the PATH being searched and there is nothing
    // a test needs to say about it that RuntimeCandidatesFor does not cover.
    std::string pathext;
#if defined(_WIN32)
    if (auto value = GetEnvValue("PATHEXT")) {
        pathext = *value;
    }
#endif

    size_t start = 0;
    while (start <= path_env.size()) {
        size_t       end = path_env.find(kPathListSeparator(), start);
        std::string_view segment = path_env.substr(
            start, end == std::string_view::npos ? std::string_view::npos
                                                 : end - start);
        // An empty entry names the current directory, exactly as it does to the
        // shell that invented the separator convention.
        const std::string dir = segment.empty() ? std::string(".")
                                                : std::string(segment);
        for (const std::string& candidate : RuntimeCandidatesFor(dir, name, pathext)) {
            if (is_executable(candidate)) return true;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return false;
}

JavaScriptRuntime RuntimeFromLookups(
    std::string_view path_env, std::string_view guchho_node_bin,
    const std::function<bool(const std::string&)>& is_executable)
{
    // The override is a Node in its own right, before the PATH is consulted:
    // it names an executable the build will use, so a config must be the one
    // that executable can read whatever else is or is not discoverable.
    if (!guchho_node_bin.empty()) return JavaScriptRuntime::kNode;
    if (FindExecutableOnPath("node", path_env, is_executable)) {
        return JavaScriptRuntime::kNode;
    }
    if (FindExecutableOnPath("bun", path_env, is_executable)) {
        return JavaScriptRuntime::kBun;
    }
    return JavaScriptRuntime::kNone;
}

scaffold::ProjectConfig SelectInitConfigFormat(JavaScriptRuntime runtime)
{
    return runtime == JavaScriptRuntime::kNone
        ? scaffold::ProjectConfig::kJson
        : scaffold::ProjectConfig::kJavaScript;
}

// -----------------------------------------------------------------------------
// The machine under the seam
// -----------------------------------------------------------------------------

JavaScriptRuntime RealDetectJavaScriptRuntime()
{
    const std::string path_env =
        GetEnvValue("PATH").value_or(std::string());
    const std::string node_bin =
        GetEnvValue(kNodeEnvName).value_or(std::string());
    return RuntimeFromLookups(path_env, node_bin, RealIsExecutable);
}

namespace {

// The detector installed by default: the real one. A test replaces this with a
// function answering one of the four values, and hands the real one back when
// it is done.
JavaScriptRuntimeDetector g_detector = &RealDetectJavaScriptRuntime;

} // namespace

JavaScriptRuntime DetectJavaScriptRuntime()
{
    return g_detector();
}

JavaScriptRuntimeDetector GetRuntimeDetector()
{
    return g_detector;
}

JavaScriptRuntimeDetector SetRuntimeDetector(JavaScriptRuntimeDetector detector)
{
    JavaScriptRuntimeDetector previous = g_detector;
    g_detector = detector;
    return previous;
}

} // namespace guchho::runtime