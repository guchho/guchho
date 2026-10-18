// =============================================================================
// src/cli/wasm_abi.cpp — the C ABI the JavaScript package calls
// =============================================================================
//
// The WebAssembly flavor keeps the command line as its primary interface: the
// npm package's bin entry maps a host process's arguments onto callMain() and
// every flag, message and exit code behaves exactly as the native binary's
// does. The exports below exist for the other half of the package — the
// embedder API, where a host holds the engine as a module object rather than
// spawning it and wants one call without assembling an argument list.
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
// guchho_service is the same idea for the service protocol: it reads the JSON
// a host would have framed over the pipe, runs the engine's own
// cli::RunServiceRequest in this process, and writes the answer back as JSON.
// That is what gives the browser the compiler stages (lex, parse, print and
// transform, per language) and the other service commands without a second
// lexer or a second dispatch — the engine answers exactly as it answers the
// npm package's pipe. One thing the pipe carries and JSON does not is raw
// bytes, so a request value written as {"__bytes__":"<base64>"} is decoded
// into bytes before dispatch; a host that passes a Uint8Array input uses that
// spelling, which is why lexer(source) accepts bytes on the wasm path too.
//
// A build that names plugins cannot cross this boundary: plugin hooks are
// callbacks from the engine to the host, and there is no callback transport
// in process. The npm wrapper refuses such requests before they get here,
// the same way the wasm CLI entry refuses a plugin project.
//
// Every export is EMSCRIPTEN_KEEPALIVE, so the symbols survive linking and
// land on the module object as _guchho_build / _guchho_last_error /
// _guchho_version / _guchho_service. Strings cross the boundary as UTF-8
// pointers: arguments are read with UTF8ToString on the JS side, and returned
// pointers refer to static storage that stays valid until the next call — a
// host that wants to keep a copy must copy it before calling again.

#include "guchho/cli.hpp"
#include "guchho/service.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

    // The last answer guchho_service wrote. Static for the same reason
    // g_last_error is: the pointer handed to JavaScript must outlive the call
    // and is only replaced on the next one.
    std::string g_service_response;

    // The base64 value of one character, or -1 for anything outside the
    // standard alphabet. Padding is not a character and is handled by the
    // caller, which stops at the first "=".
    int Base64Value(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    // Decodes standard base64 into bytes. False for a character outside the
    // alphabet, which is the only way a host can get this wrong; an empty
    // input decodes to empty bytes and is a real answer.
    bool Base64Decode(std::string_view src, std::string& out) {
        int    buffer = 0;
        int    bits   = 0;
        for (char c : src) {
            if (c == '=') break;
            const int value = Base64Value(c);
            if (value < 0) return false;
            buffer = (buffer << 6) | value;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
            }
        }
        return true;
    }

    // Rewrites a request value so that an object of the form
    // {"__bytes__":"<base64>"} becomes bytes. JSON has no bytes kind, but the
    // service does, and a lex or a parse can be handed a buffer; this is the
    // one spelling that carries one across. A malformed encoding clears "ok"
    // rather than being treated as text, so a caller that sent bytes gets a
    // refusal instead of a lex of the literal base64.
    guchho::service::Value DecodeBytesValue(const guchho::service::Value& value, bool& ok) {
        using Kind = guchho::service::Value::Kind;

        if (value.GetKind() == Kind::kArray) {
            std::vector<guchho::service::Value> items;
            items.reserve(value.Size());
            for (const guchho::service::Value& item : value.AsArray()) {
                items.push_back(DecodeBytesValue(item, ok));
            }
            return guchho::service::Value::Array(std::move(items));
        }

        if (value.GetKind() == Kind::kObject) {
            const std::vector<std::pair<std::string, guchho::service::Value>>& entries =
                value.AsObject();

            if (entries.size() == 1 && entries[0].first == "__bytes__") {
                const guchho::service::Value& encoded = entries[0].second;
                std::string                   bytes;
                if (encoded.IsString() && Base64Decode(encoded.AsString(), bytes)) {
                    return guchho::service::Value::Bytes(
                        std::vector<uint8_t>(bytes.begin(), bytes.end()));
                }
                ok = false;
                return value;
            }

            std::vector<std::pair<std::string, guchho::service::Value>> out;
            out.reserve(entries.size());
            for (const std::pair<std::string, guchho::service::Value>& entry : entries) {
                out.emplace_back(entry.first, DecodeBytesValue(entry.second, ok));
            }
            return guchho::service::Value::Object(std::move(out));
        }

        return value;
    }

    // The refusal shape every service command uses, so a host reads one thing
    // for "this request was not a request". The text is the engine's own for
    // anything it answers; this is only for a document this layer could not
    // turn into a request at all.
    const char* ServiceRefusal(const char* text) {
        g_service_response = std::string("{\"error\":\"") + text + "\",\"errors\":[]}";
        return g_service_response.c_str();
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

// Answers one service request, in this process, from JSON to JSON.
//
// Input:  request_json — a JSON object whose "command" names a service command
//         (guchho/service.hpp): a build or context command, or one of the
//         twelve lex/parse/print/transform commands. A value written as
//         {"__bytes__":"<base64>"} is decoded into bytes first.
// Output: the response for it, as JSON, in the shape the pipe would have
//         framed. Static storage, valid until the next guchho_service call.
//
// A malformed document, or a request that is not an object, comes back as the
// same {"error":...} refusal every command uses, so the host reads a refusal
// the same way whichever side of the boundary produced it.
extern "C" GUCHHO_WASM_KEEPALIVE const char* guchho_service(const char* request_json) {
    if (request_json == nullptr || request_json[0] == '\0') {
        return ServiceRefusal("guchho_service: a JSON request is required");
    }

    const std::optional<guchho::service::Value> document =
        guchho::service::JSONToValue(request_json);
    if (!document.has_value() || !document->IsObject()) {
        return ServiceRefusal("guchho_service: the request must be a JSON object");
    }

    bool                         ok      = true;
    const guchho::service::Value request = DecodeBytesValue(*document, ok);
    if (!ok) {
        return ServiceRefusal("guchho_service: __bytes__ was not valid base64");
    }

    g_service_response = guchho::service::ValueToJSON(guchho::cli::RunServiceRequest(request));
    return g_service_response.c_str();
}
