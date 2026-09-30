// =============================================================================
// include/guchho/runtime.hpp — what runtimes are around, for "init" to decide
// =============================================================================
//
// "guchho init" writes its configuration in one of two forms: a JavaScript
// config (guchho.config.js) that a runtime evaluates, or a JSON config
// (guchho.json) that Guchho reads by itself. Which one a generated project gets
// is decided by whether a JavaScript runtime is installed: when there is a
// runtime the config is a script, because that is the file a person with a
// runtime is most likely to want to edit; with none, the only honest config is
// the one the build can read without one.
//
// That decision is deliberately split from the scaffolder. Detection reads the
// environment and the file system; generation reads a request and writes bytes.
// The two have nothing to say to each other, and the function that turns a
// detection result into a config format is the whole of their relationship —
// which is the point of keeping the enum and the selection here, next to each
// other, and the PATH walking here and nowhere else.
//
// Detection is a lookup and nothing more. It runs no JavaScript, spawns no
// process and reads no directory outside the PATH, because the question it
// answers — "is there a node on this machine" — is answered by whether a file
// of that name is executable, and starting the runtime to find out would be a
// slower and a less honest way to ask it.
//
// The detection result is injectable. A CLI test cannot know whether the
// machine it runs on has Node installed, so the CLI asks for the answer through
// a detector that the tests stand in for; and the PATH walker is pure, taking
// the PATH and an "is this file executable" predicate as arguments, so it can
// be exercised against a synthetic PATH with a fake file system.

#pragma once

#include "guchho/scaffold.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace guchho::runtime {

// What a supported JavaScript runtime is. This is the whole of the detection
// answer as an enum rather than two separate booleans, because the pair "is
// node here" and "is bun here" have a third answer between them — neither —
// and that is precisely the case the init command has to plan for. Adding a
// runtime later is a new enumerator and a new branch in the lookup, and nothing
// else.
enum class JavaScriptRuntime {
    kNone,  // No supported runtime found. The config falls back to the format
            // Guchho can read by itself.
    kNode,  // Node.js, or a node the user pointed at with GUCHHO_NODE_BIN.
    kBun,   // Bun, and no Node.
};

// A function the CLI calls to learn the runtime, so that tests can answer
// instead of the machine. The real answer is RealDetectJavaScriptRuntime,
// installed by default and replaced for the duration of a test.
using JavaScriptRuntimeDetector = std::function<JavaScriptRuntime()>;

// The format a detected runtime selects. Only the fallback needs the decision
// to be a surprise: any runtime that can evaluate a config gets a config for
// it, and the no-runtime case gets the one config that needs no runtime.
//
// Input:  SelectInitConfigFormat(JavaScriptRuntime::kNode)   -> kJavaScript
// Input:  SelectInitConfigFormat(JavaScriptRuntime::kBun)    -> kJavaScript
// Input:  SelectInitConfigFormat(JavaScriptRuntime::kNone)   -> kJson
scaffold::ProjectConfig SelectInitConfigFormat(JavaScriptRuntime runtime);

// The runtime actually present, asked of the environment right now.
JavaScriptRuntime DetectJavaScriptRuntime();

// The real implementation behind the detector above, kept reachable so a test
// that wants the real PATH walk can have it without the default function
// pointer being a secret.
JavaScriptRuntime RealDetectJavaScriptRuntime();

// Reads and replaces the detector. SetRuntimeDetector returns whatever was
// installed before, so a test can hand the previous detector straight back
// when it is done and the next test sees the machine, not the last test.
JavaScriptRuntimeDetector GetRuntimeDetector();
JavaScriptRuntimeDetector SetRuntimeDetector(JavaScriptRuntimeDetector detector);

// The concrete file names that count as "name" inside one PATH directory.
//
// "node" is a command name, not a file name: on Windows it is found as node.exe
// or node.cmd, and on a Unix as a file called node with the executable bit set.
// This expands the command name into the concrete names the platform would look
// for, in the order it would look for them. "pathext" is the PATHEXT list on
// Windows (each extension with its leading dot, separated by ";") and is
// ignored, together with the argument, on other platforms.
std::vector<std::string> RuntimeCandidatesFor(const std::string& dir,
                                              const std::string& name,
                                              std::string_view pathext);

// Whether a command name is on a PATH. "path_env" is the platform's PATH value,
// unmodified — the walker does not overwrite or consult the process-wide PATH —
// and "is_executable" answers whether a concrete file is a usable command, so
// the walker never touches the disk itself and a test can answer from a table.
bool FindExecutableOnPath(
    const std::string& name, std::string_view path_env,
    const std::function<bool(const std::string&)>& is_executable);

// The pure core of detection. It applies the priority rules — a Node override,
// then Node on the PATH, then Bun, then nothing — to synthetic inputs, and is
// what the real detector calls with the environment and what a test calls with
// a table and a crafted PATH.
//
// Input:  RuntimeFromLookups("/usr/bin", "", {"/usr/bin/node" is executable})
// Output: JavaScriptRuntime::kNode
JavaScriptRuntime RuntimeFromLookups(
    std::string_view path_env, std::string_view guchho_node_bin,
    const std::function<bool(const std::string&)>& is_executable);

} // namespace guchho::runtime