// =============================================================================
// src/cli/wasm_abi.cpp — the C ABI the JavaScript package calls
// =============================================================================
//
// The WebAssembly flavor keeps the command line as its primary interface: the
// npm package's bin entry maps a host process's arguments onto callMain() and
// every flag, message and exit code behaves exactly as the native binary's
// does. These three exports exist for the other half of the package — the
// embedder API, where a host holds the engine as a module object rather than
// spawning it and wants a one-call build without assembling an argument list.
//
// The design is deliberately thin. guchho_build builds the same argv the
// command line would have received and calls cli::Run, so flag parsing,
// project discovery, diagnostics and the exit-code contract are the CLI's
// own rather than a second implementation that would drift from it.
// Diagnostics reach the host through the print callbacks Emscripten already
// routes to console.log/console.error (or to whatever the host registered on
// the module); guchho_last_error is only the coarse answer to "did it work",
// which for the CLI path is the exit code plus one sentence of context.
//
// Every export is EMSCRIPTEN_KEEPALIVE, so the symbols survive linking and
// land on the module object as _guchho_build / _guchho_last_error /
// _guchho_version. Strings cross the boundary as UTF-8 pointers: arguments
// are read with UTF8ToString on the JS side, and returned pointers refer to
// static storage that stays valid until the next call — a host that wants to
// keep a copy must copy it before calling again.

#include "guchho/cli.hpp"
#include "guchho/service.hpp"

#include <optional>
#include <string>
#include <vector>

// The keep-alive attribute and its header only exist under Emscripten. A
// native build configured with -DGUCHHO_WASM=ON still compiles this file —
// that is how the feature gates themselves get tested — so the attribute
// degrades to nothing there and the exports simply go unused.
#if defined(__EMSCRIPTEN__)
    #include <emscripten/emscripten.h>
    #define GUCHHO_WASM_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
    #define GUCHHO_WASM_KEEPALIVE
#endif

namespace {

    // The last failure's one-line description. Written by guchho_build and
    // read by guchho_last_error; static so the pointer handed to JavaScript
    // never dangles.
    std::string g_last_error;

    // Reads one string-valued field out of the flags object, if it is there
    // and is a string. Anything else — absent, null, a number where a string
    // was expected — is ignored rather than diagnosed: this JSON is a set of
    // convenience extras, and the command line remains the authority on
    // anything it does not cover.
    std::string StringFlag(const guchho::service::Value* flags, const char* key) {
        if (flags == nullptr) return {};
        const guchho::service::Value* field = flags->Find(key);
        if (field == nullptr || !field->IsString()) return {};
        return field->AsString();
    }

    // Reads one boolean field, using the same "ignore anything unexpected"
    // rule as StringFlag. A flag that is present and true adds its switch;
    // false and absent both add nothing, which is what --minify=false would
    // also say.
    bool BoolFlag(const guchho::service::Value* flags, const char* key) {
        if (flags == nullptr) return false;
        const guchho::service::Value* field = flags->Find(key);
        return field != nullptr && field->IsBool() && field->AsBool();
    }

} // namespace

// Runs "build entry --outdir=... [extras]" through the command line itself.
//
// Input:  entry  — the path the CLI would take, inside the module's file
//                  system (for Node, a path under the NODEFS mount).
//         outdir — the --outdir value; may be null to leave the default.
//         flags_json — an optional flat JSON object. Recognised keys:
//                  "minify": true  -> --minify
//                  "name": "lib"   -> --name=lib
//                  "quiet": true   -> --quiet
//                  "verbose": true -> --log-level=verbose
//                  Anything else is ignored; pass remaining flags through
//                  engine.callMain for the full grammar.
// Output: the CLI's exit code — 0 on success, 1 on a failed build, 2 on a
//         rejected command line. Diagnostics have already been printed by
//         the time this returns.
extern "C" GUCHHO_WASM_KEEPALIVE int guchho_build(const char* entry,
                                                 const char* outdir,
                                                 const char* flags_json) {
    g_last_error.clear();

    if (entry == nullptr || entry[0] == '\0') {
        g_last_error = "guchho_build: an entry point is required";
        return 2;
    }

    const guchho::service::Value* flags = nullptr;
    guchho::service::Value        parsed;
    if (flags_json != nullptr && flags_json[0] != '\0') {
        const std::optional<guchho::service::Value> document =
            guchho::service::JSONToValue(flags_json);
        if (!document.has_value() || !document->IsObject()) {
            g_last_error = "guchho_build: flags_json must be a flat JSON object";
            return 2;
        }
        parsed  = *document;
        flags   = &parsed;
    }

    std::vector<std::string> args;
    args.reserve(8);
    args.push_back("build");
    args.push_back(entry);
    if (outdir != nullptr && outdir[0] != '\0') {
        args.push_back(std::string("--outdir=") + outdir);
    }
    if (BoolFlag(flags, "minify")) args.push_back("--minify");
    if (BoolFlag(flags, "quiet")) args.push_back("--quiet");
    if (BoolFlag(flags, "verbose")) args.push_back("--log-level=verbose");
    const std::string name = StringFlag(flags, "name");
    if (!name.empty()) args.push_back("--name=" + name);

    const int exit_code = guchho::cli::Run(args);
    if (exit_code != 0) {
        g_last_error = "guchho_build failed with exit code " + std::to_string(exit_code);
    }
    return exit_code;
}

// The message from the most recent failing guchho_build, or an empty string
// if the last call succeeded. The pointer refers to static storage and stays
// valid until the next guchho_build call.
extern "C" GUCHHO_WASM_KEEPALIVE const char* guchho_last_error() {
    return g_last_error.c_str();
}

// The engine version, as the command line prints it. Static storage, valid
// for the lifetime of the module.
extern "C" GUCHHO_WASM_KEEPALIVE const char* guchho_version() {
    return GUCHHO_VERSION_STRING;
}
