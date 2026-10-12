// The service: one process, many requests.
//
// The command line runs a build and exits. This runs for as long as its input
// lasts, answering whatever it is sent, and it is how a JavaScript caller gets
// the outputs and the diagnostics as data rather than as files and text.
//
// It is not a second engine. Every command below goes through api::Build,
// api::Transform or api::Context, which is the same code "guchho build" runs,
// and the only difference is what happens to the result. That is the whole
// design and it is worth stating plainly because the alternative — a service
// that reimplements the build for a protocol — would be a second answer to every
// question the bundler answers, and the two would disagree about what "the same
// build" means before long.
//
// The rules the loop keeps, and why each one is here:
//
// The flag list is the grammar. A request carries a flags array and it is handed
// to the same parser the command line uses, as a synthetic argument list. So an
// option is accepted here for exactly the reason it is accepted there, and a
// misspelled one is refused with the same words. The alternative — a second
// mapping from JSON fields to option members — would need updating twice for
// every option and would be the first place the two drifted.
//
// Writing is off unless it was asked for. api::BuildOptions::write defaults to
// false, and the command line turns it on because a person typing "guchho build"
// wants files; a host asking for a build wants the answer. Leaving it alone is
// what makes outputFiles mean anything.
//
// Nothing here prints. A banner, a build summary or a timer line on the standard
// output would land in the middle of a frame, and the symptom would be a
// protocol error a long way from the print that caused it. This calls the api
// layer directly for a second reason too: runBuild forces write on, and prints
// while it does it.

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "guchho/api.hpp"
#include "guchho/cli.hpp"
#include "guchho/service.hpp"
#include "guchho/pack.hpp"

namespace guchho::cli {

    namespace {

        // How much is read from the input at a time.
        //
        // This is a chunk size and not a packet size: the reader is handed
        // whatever arrived and joins it to what it is still holding, so the only
        // requirement on this number is that it be a reasonable buffer. A
        // megabyte is far more than any build needs in one read and small enough
        // that the buffer is never the reason a process is heavy.
        constexpr size_t kReadChunk = 1024 * 1024;

        // The diagnostic a host gets when it sends something this service cannot
        // read. It is a Message and not a thrown exception because it travels
        // back the way every other diagnostic does, so a host has one place to
        // look for problems.
        api::Message ProtocolError(std::string text) {
            api::Message message;
            message.id = "protocol-error";
            message.text = std::move(text);
            return message;
        }

        // A diagnostic for something the host asked for that the engine refused,
        // carrying the engine's own wording. That is the point: a host showing a
        // message from a build should not be showing a different sentence from
        // the one the same mistake produces on the command line.
        api::Message EngineError(std::string id, std::string text) {
            api::Message message;
            message.id = std::move(id);
            message.text = std::move(text);
            return message;
        }

        // How a request was refused, in the two forms there are: a field the
        // protocol does not accept, and a flag the grammar does not. The first
        // is the host's mistake in the shape it was sent, the second is a
        // misspelling of something the engine knows.
        service::Value ErrorResponse(const api::Message& message) {
            return service::Value::Object({
                {"error", service::Value::String(message.text)},
                {"errors", service::Value::Array({service::MessageToValue(message)})},
            });
        }

        // The flags in a request, as the argument list the grammar reads.
        //
        // A request that sent "flags" as something other than an array of strings
        // is refused rather than coerced. The grammar takes a vector of strings
        // and every one of them ends up as a std::string somewhere; a number here
        // would be stringified into a flag like "--target=1" and refused later
        // with a message about a flag, when what was wrong was the type.
        bool FlagsFromRequest(const service::Value& request,
                              std::vector<std::string>& out,
                              service::Value& error) {
            const service::Value* flags = request.Find("flags");
            if (flags == nullptr || flags->IsNull()) return true;

            if (!flags->IsArray()) {
                error = ErrorResponse(ProtocolError("\"flags\" must be an array of strings"));
                return false;
            }

            for (const service::Value& entry : flags->AsArray()) {
                if (!entry.IsString()) {
                    error = ErrorResponse(ProtocolError("every entry in \"flags\" must be a string"));
                    return false;
                }
                out.push_back(entry.AsString());
            }
            return true;
        }

        // The entry points in a request, added to the argument list.
        //
        // They are [out, in] pairs rather than objects, and that follows from the
        // wire format: the protocol's object form is a list of string keys, which
        // says "out -> in" well and "in, with an optional out" badly. A pair is
        // one less thing to translate in both directions.
        //
        // A pair with no output name becomes a bare positional argument, and one
        // with an output name becomes "out=in", which is the spelling the grammar
        // already reads as an advanced entry point. Neither is a new concept on
        // this side: the host is writing the same command line a person would.
        bool EntryPointsFromRequest(const service::Value& request,
                                    std::vector<std::string>& flags,
                                    service::Value& error) {
            const service::Value* entries = request.Find("entries");
            if (entries == nullptr || entries->IsNull()) return true;

            if (!entries->IsArray()) {
                error = ErrorResponse(ProtocolError(
                    "\"entries\" must be an array of [out, in] pairs"));
                return false;
            }

            for (const service::Value& entry : entries->AsArray()) {
                if (!entry.IsArray() || entry.Size() != 2) {
                    error = ErrorResponse(ProtocolError(
                        "each entry point must be an [out, in] pair"));
                    return false;
                }

                const service::Value& out = *entry.At(0);
                const service::Value& in = *entry.At(1);

                if (!in.IsString() || in.AsString().empty()) {
                    error = ErrorResponse(ProtocolError(
                        "an entry point's input must be a non-empty string"));
                    return false;
                }

                if (out.IsNull() || (out.IsString() && out.AsString().empty())) {
                    flags.push_back(in.AsString());
                } else if (out.IsString()) {
                    flags.push_back(out.AsString() + "=" + in.AsString());
                } else {
                    error = ErrorResponse(ProtocolError(
                        "an entry point's output must be a string or null"));
                    return false;
                }
            }
            return true;
        }

        // The stdin source, as a value on the wire rather than as a flag.
        //
        // A flag would have to carry the whole source in one argument, and there
        // is a limit to how much text an argument can be expected to hold. The
        // same reasoning is why the transform command reads its input as a
        // value.
        void StdinDataFromRequest(const service::Value& request,
                                  api::BuildOptions& options) {
            const service::Value* stdin_data = request.Find("stdin");
            if (stdin_data == nullptr || !stdin_data->IsObject()) return;

            api::StdinOptions stdin_options;

            if (const service::Value* contents = stdin_data->Find("contents")) {
                if (contents->IsString()) {
                    stdin_options.contents = contents->AsString();
                } else if (contents->IsBytes()) {
                    const std::vector<uint8_t>& bytes = contents->AsBytes();
                    stdin_options.contents = std::string(
                        reinterpret_cast<const char*>(bytes.data()), bytes.size());
                }
            }

            if (const service::Value* resolve_dir = stdin_data->Find("resolveDir")) {
                if (resolve_dir->IsString()) stdin_options.resolve_dir = resolve_dir->AsString();
            }
            if (const service::Value* sourcefile = stdin_data->Find("sourcefile")) {
                if (sourcefile->IsString()) stdin_options.sourcefile = sourcefile->AsString();
            }

            // Only set when there is a stdin object at all, so that a request
            // naming a resolve directory without any source does not turn into a
            // build fed an empty virtual file. The engine reads stdin as a file,
            // and an empty one is a legitimate thing to ask to build, so the
            // presence of the object rather than the length of the contents is
            // the test.
            options.stdin_data = std::move(stdin_options);
        }

        // The stdin loader, as a flag.
        //
        // The loader is the one part of the stdin request that is a flag, because
        // the name has to be resolved by the grammar's own table. A name that
        // table does not know is then a build error, which is the answer a caller
        // wants, and which is the same error "guchho build --loader=nonsense"
        // gives.
        void StdinFlagsFromRequest(const service::Value& request,
                                   std::vector<std::string>& flags) {
            const service::Value* stdin_data = request.Find("stdin");
            if (stdin_data == nullptr || !stdin_data->IsObject()) return;

            if (const service::Value* loader = stdin_data->Find("loader")) {
                if (loader->IsString() && !loader->AsString().empty()) {
                    flags.push_back("--loader=" + loader->AsString());
                }
            }
        }


        // The response body for a finished build.
        //
        // The outputs travel only when there are some, which is the case when the
        // build did not write them. A build with write on has put the bytes on
        // disk already, and sending them as well would be a copy of every output
        // in the project over a pipe nobody is going to read.
        service::Value BuildResponse(const api::BuildResult& result) {
            std::vector<std::pair<std::string, service::Value>> entries;

            entries.emplace_back("errors",
                service::Value::Array(service::MessagesToValue(result.errors)));
            entries.emplace_back("warnings",
                service::Value::Array(service::MessagesToValue(result.warnings)));

            if (!result.output_files.empty()) {
                std::vector<service::Value> files;
                files.reserve(result.output_files.size());
                for (const api::OutputFile& file : result.output_files) {
                    files.push_back(service::OutputFileToValue(file));
                }
                entries.emplace_back("outputFiles", service::Value::Array(std::move(files)));
            }

            if (!result.metafile.empty()) {
                // The JSON text the engine already produced, rather than a
                // structure rebuilt here. Parsing it into a value and writing it
                // back out would be a second implementation of the metafile
                // format, and the two would drift the first time a field was
                // added to it.
                entries.emplace_back("metafile", service::Value::String(result.metafile));
            }

            if (!result.mangle_cache.empty()) {
                // Carried back so a host building repeatedly can feed it into the
                // next request and get stable names across rebuilds, which is
                // what the field is for.
                std::vector<service::Value> cache;
                cache.reserve(result.mangle_cache.size());
                for (const auto& [name, value] : result.mangle_cache) {
                    cache.push_back(service::Value::Object({
                        {"name", service::Value::String(name)},
                        {"value", service::Value::Bool(value)},
                    }));
                }
                entries.emplace_back("mangleCache", service::Value::Array(std::move(cache)));
            }

            return service::Value::Object(std::move(entries));
        }

        // Builds the options for a build request, or reports why it cannot.
        //
        // The parser is the grammar, called once with the whole argument list and
        // the internal kind. The internal kind is what "--mangle-cache=" and
        // "--metafile=" are gated on; the external kind is for a host asking what
        // a command line means, and the two flags it holds back are both wanted
        // here — the first carries state between builds, which is what a repeated
        // build is for, and the second asks for the metafile in the result rather
        // than as a file.
        //
        // One pass, and the order of the three fields is what makes it one pass:
        // the flags, then the entry points, then the stdin loader all become
        // arguments before anything is parsed. A request does not have to put its
        // entry points in any particular place, because the grammar reads a bare
        // path as a positional argument wherever it appears.
        void PluginsFromRequest(const service::Value& request, api::BuildOptions& options);

        bool BuildOptionsFromRequest(const service::Value& request,
                                     api::BuildOptions& options,
                                     service::Value& error) {
            std::vector<std::string> flags;

            if (!FlagsFromRequest(request, flags, error)) return false;
            if (!EntryPointsFromRequest(request, flags, error)) return false;
            StdinFlagsFromRequest(request, flags);

            options = newBuildOptions();
            ParseOptionsExtras extras;
            const auto complaint = parseOptionsImpl(flags, &options, nullptr,
                ParseOptionsKind::kInternal, extras);
            if (complaint.has_value()) {
                error = ErrorResponse(EngineError("invalid-build-options", complaint->text));
                return false;
            }

            // The writing and metafile switches are applied after the parse,
            // because they are the host's decisions about what to do with the
            // answer rather than flags a person would type, and because the bare
            // "--metafile" the grammar would read is the same thing this sets.
            if (const service::Value* write = request.Find("write")) {
                if (write->IsBool()) options.write = write->AsBool();
            }
            if (const service::Value* metafile = request.Find("metafile")) {
                if (metafile->IsBool() && metafile->AsBool()) options.metafile = true;
            }

            // The host says whether it runs the config file's "plugins" in its
            // own JavaScript process. It does for every request it makes, which
            // is what keeps the engine from warning that a field the host has
            // already claimed was ignored.
            if (const service::Value* handled = request.Find("configPluginsHandled")) {
                if (handled->IsBool()) options.config_plugins_handled = handled->AsBool();
            }

            // The working directory, as the engine's own override rather than as
            // a starting point handed to config discovery.
            //
            // These are two different things and the difference is the whole
            // point of the field. resolveRunOptions is given the directory to
            // look for a config file *in*; abs_working_dir is what every
            // relative path in the request is resolved *against*. Passing the
            // directory to only the first meant that a request naming
            // absWorkingDir read that project's config and then resolved
            // "src/index.js" against the directory the service happened to be
            // started in — so a build of a project that was not the process's own
            // directory failed with "Could not resolve", naming a file that
            // exists.
            if (const service::Value* dir = request.Find("absWorkingDir")) {
                if (dir->IsString() && !dir->AsString().empty()) {
                    options.abs_working_dir = dir->AsString();
                }
            }

            // The mangle cache and the node paths are state and environment rather
            // than settings of one build, and both are round-tripped: the first
            // comes back in the result, the second is what a host resolves
            // against. Neither is a flag the grammar reads, so neither is in the
            // argument list above.
            if (const service::Value* mangle_cache = request.Find("mangleCache")) {
                if (mangle_cache->IsArray()) {
                    for (const service::Value& entry : mangle_cache->AsArray()) {
                        if (!entry.IsObject()) continue;
                        const service::Value* name = entry.Find("name");
                        const service::Value* value = entry.Find("value");
                        if (name != nullptr && name->IsString()
                            && value != nullptr && value->IsBool()) {
                            options.mangle_cache[name->AsString()] = value->AsBool();
                        }
                    }
                }
            }
            if (const service::Value* node_paths = request.Find("nodePaths")) {
                if (node_paths->IsArray()) {
                    for (const service::Value& entry : node_paths->AsArray()) {
                        if (entry.IsString()) options.node_paths.push_back(entry.AsString());
                    }
                }
            }

            StdinDataFromRequest(request, options);
            PluginsFromRequest(request, options);
            return true;
        }

        // ===================================================================
        // The JavaScript plugin bridge.
        // ===================================================================
        //
        // A plugin written in JavaScript does not run in this process. It runs
        // in the host that spawned this one, and reaching it means sending a
        // request back up the pipe and waiting for the answer. That is the one
        // thing the service protocol did not previously do: until now every
        // frame travelled host to service, and the service never spoke first.
        //
        // Everything below exists to make that direction look ordinary to the
        // rest of the engine. A plugin built here is an api::Plugin like any
        // other: it registers the same callbacks, and those callbacks are what
        // the engine calls. The only difference is that the callback's body is
        // a round trip to the host instead of a local function call, and the
        // engine neither knows nor needs to know that.
        //
        // The contract with the host is deliberately shaped like the hooks in
        // the TypeScript types rather than like the engine's internal structs,
        // because the host is where the Rollup-shaped hook lives. A resolve
        // request carries a source, an importer and a kind; an answer carries an
        // id or nothing. The translation between that and api::OnResolveResult
        // happens here, at the one seam, and nowhere else.

        // The kind of a resolve request, as the name the host's hook expects.
        // The names are the ones in the TypeScript types; an unknown kind comes
        // back as "none" rather than as an empty string, so the host can always
        // switch on it.
        std::string ResolveKindName(api::ResolveKind kind) {
            switch (kind) {
                case api::ResolveKind::kEntryPoint: return "entry";
                case api::ResolveKind::kJSImportStatement: return "import-statement";
                case api::ResolveKind::kJSRequireCall: return "require-call";
                case api::ResolveKind::kJSDynamicImport: return "dynamic-import";
                case api::ResolveKind::kJSRequireResolve: return "require-resolve";
                case api::ResolveKind::kCSSImportRule: return "import-rule";
                case api::ResolveKind::kCSSComposesFrom: return "composes-from";
                case api::ResolveKind::kCSSURLToken: return "url-token";
                case api::ResolveKind::kNone: return "none";
            }
            return "none";
        }

        // The name of a loader, back again. The wire carries the name rather
        // than the number because the numbers are this build's enum and would
        // become part of the protocol the moment a host stored one.
        api::Loader LoaderFromName(const std::string& name) {
            static const std::unordered_map<std::string, api::Loader> kLoaders = {
                {"base64", api::Loader::kBase64},   {"binary", api::Loader::kBinary},
                {"copy", api::Loader::kCopy},       {"css", api::Loader::kCSS},
                {"dataurl", api::Loader::kDataURL}, {"default", api::Loader::kDefault},
                {"empty", api::Loader::kEmpty},     {"file", api::Loader::kFile},
                {"global-css", api::Loader::kGlobalCSS}, {"js", api::Loader::kJS},
                {"json", api::Loader::kJSON},       {"jsx", api::Loader::kJSX},
                {"local-css", api::Loader::kLocalCSS}, {"text", api::Loader::kText},
                {"ts", api::Loader::kTS},           {"tsx", api::Loader::kTSX},
            };
            const auto found = kLoaders.find(name);
            return found == kLoaders.end() ? api::Loader::kNone : found->second;
        }

        // The plugin-to-plugin payload, riding as a value.
        //
        // The engine passes a std::any from a resolve hook to the matching load
        // hook, and a JavaScript plugin puts a plain value there. Rather than
        // invent a serialisation for arbitrary C++, the bridge stashes the value
        // it came back with inside a shared_ptr and hands that through as the
        // any. The only code that ever looks inside is this bridge, so the fact
        // that it holds a service::Value rather than whatever the plugin meant
        // by it costs nothing and keeps the value intact across the round trip.
        service::Value PluginMetaValue(const std::any& data) {
            if (!data.has_value()) return service::Value::Null();
            const auto* held = std::any_cast<std::shared_ptr<service::Value>>(&data);
            if (held == nullptr || !*held) return service::Value::Null();
            return **held;
        }

        std::any StoredPluginMeta(service::Value value) {
            return std::any(std::make_shared<service::Value>(std::move(value)));
        }

        // The messages a hook reported, in the shape the engine expects.
        //
        // A host sends "errors" and "warnings" as arrays whose entries are
        // either a plain string — the common case, and all a one-line refusal
        // needs — or an object with "text" and an optional "id". The plugin's
        // name is stamped on every message, so a diagnostic from a JavaScript
        // plugin reads the same as one from a C++ plugin.
        std::vector<api::Message> PluginMessages(const service::Value& response,
                                                 const char* key,
                                                 const std::string& plugin_name) {
            std::vector<api::Message> messages;
            const service::Value* list = response.Find(key);
            if (list == nullptr || !list->IsArray()) return messages;

            for (const service::Value& entry : list->AsArray()) {
                api::Message message;
                message.plugin_name = plugin_name;
                message.id = plugin_name + "-plugin";
                if (entry.IsString()) {
                    message.text = entry.AsString();
                } else if (entry.IsObject()) {
                    if (const service::Value* text = entry.Find("text")) {
                        if (text->IsString()) message.text = text->AsString();
                    }
                    if (const service::Value* id = entry.Find("id")) {
                        if (id->IsString()) message.id = id->AsString();
                    }
                } else {
                    continue;
                }
                messages.push_back(std::move(message));
            }
            return messages;
        }

        // One callback, one round trip.
        //
        // The plugin name, the hook name and the arguments go up; the hook's
        // answer comes back. The engine's own plugin ordering is preserved by
        // the host, which runs its hooks in the order the plugins were listed,
        // so the bridge does not sort anything itself.
        service::Value PluginCall(const std::string& plugin,
                                  const std::string& hook,
                                  service::Value args) {
            return service::InvokeCallback(service::Value::Object({
                {"plugin", service::Value::String(plugin)},
                {"hook", service::Value::String(hook)},
                {"args", std::move(args)},
            }));
        }

        // The hooks, one function each, each a translation rather than a rule.
        //
        // Every one of them begins the same way: a process with no host — a
        // command-line build — has no callback transport, and the correct answer
        // there is to decline rather than to block waiting for a host that will
        // never arrive. That check is why a plugin list attached only over the
        // service stays inert on the command line instead of hanging it.

        api::OnStartResult RunJsStart(const std::string& plugin) {
            api::OnStartResult result;
            if (!service::HasCallbackTransport()) return result;

            const service::Value response = PluginCall(plugin, "buildStart", service::Value::Null());
            result.errors = PluginMessages(response, "errors", plugin);
            result.warnings = PluginMessages(response, "warnings", plugin);
            return result;
        }

        api::OnResolveResult RunJsResolve(const std::string& plugin, const api::OnResolveArgs& args) {
            api::OnResolveResult result;
            result.plugin_name = plugin;
            if (!service::HasCallbackTransport()) return result;

            std::vector<service::Value> with;
            for (const auto& [key, value] : args.with) {
                with.push_back(service::Value::Array(
                    {service::Value::String(key), service::Value::String(value)}));
            }

            const service::Value response = PluginCall(plugin, "resolveId", service::Value::Object({
                {"source", service::Value::String(args.path)},
                {"importer", args.importer.empty() ? service::Value::Null()
                                                   : service::Value::String(args.importer)},
                {"namespace", service::Value::String(args.namespace_)},
                {"resolveDir", service::Value::String(args.resolve_dir)},
                {"kind", service::Value::String(ResolveKindName(args.kind))},
                {"with", service::Value::Array(std::move(with))},
                {"meta", PluginMetaValue(args.plugin_data)},
            }));

            result.errors = PluginMessages(response, "errors", plugin);
            result.warnings = PluginMessages(response, "warnings", plugin);
            if (!result.errors.empty()) return result;

            if (const service::Value* external = response.Find("external")) {
                if (external->IsBool() && external->AsBool()) result.external = true;
            }

            const service::Value* id = response.Find("id");
            if (id != nullptr && id->IsString() && !id->AsString().empty()) {
                result.path = id->AsString();
            } else if (!result.external) {
                // Neither a path nor an external claim: the hook declined, and
                // the next plugin — and finally the built-in rules — get a turn.
                return result;
            }

            if (const service::Value* ns = response.Find("namespace")) {
                if (ns->IsString()) result.namespace_ = ns->AsString();
            }
            if (const service::Value* suffix = response.Find("suffix")) {
                if (suffix->IsString()) result.suffix = suffix->AsString();
            }
            if (const service::Value* side = response.Find("moduleSideEffects")) {
                if (side->IsBool()) {
                    result.side_effects = side->AsBool() ? api::SideEffects::kTrue
                                                         : api::SideEffects::kFalse;
                }
            }
            if (const service::Value* meta = response.Find("meta")) {
                if (!meta->IsNull()) result.plugin_data = StoredPluginMeta(*meta);
            }
            if (const service::Value* files = response.Find("watchFiles")) {
                if (files->IsArray()) {
                    for (const service::Value& file : files->AsArray()) {
                        if (file.IsString()) result.watch_files.push_back(file.AsString());
                    }
                }
            }
            if (const service::Value* dirs = response.Find("watchDirs")) {
                if (dirs->IsArray()) {
                    for (const service::Value& dir : dirs->AsArray()) {
                        if (dir.IsString()) result.watch_dirs.push_back(dir.AsString());
                    }
                }
            }
            return result;
        }

        api::OnLoadResult RunJsLoad(const std::string& plugin, const api::OnLoadArgs& args) {
            api::OnLoadResult result;
            result.plugin_name = plugin;
            if (!service::HasCallbackTransport()) return result;

            std::vector<service::Value> with;
            for (const auto& [key, value] : args.with) {
                with.push_back(service::Value::Array(
                    {service::Value::String(key), service::Value::String(value)}));
            }

            const service::Value response = PluginCall(plugin, "load", service::Value::Object({
                {"id", service::Value::String(args.path)},
                {"namespace", service::Value::String(args.namespace_)},
                {"suffix", service::Value::String(args.suffix)},
                {"with", service::Value::Array(std::move(with))},
                {"meta", PluginMetaValue(args.plugin_data)},
            }));

            result.errors = PluginMessages(response, "errors", plugin);
            result.warnings = PluginMessages(response, "warnings", plugin);
            if (!result.errors.empty()) return result;

            const service::Value* code = response.Find("code");
            if (code == nullptr) return result; // declined; the file system gets a turn
            if (code->IsString()) {
                result.contents = code->AsString();
            } else if (code->IsBytes()) {
                const std::vector<uint8_t>& bytes = code->AsBytes();
                result.contents = std::string(
                    reinterpret_cast<const char*>(bytes.data()), bytes.size());
            } else {
                return result;
            }

            if (const service::Value* dir = response.Find("resolveDir")) {
                if (dir->IsString()) result.resolve_dir = dir->AsString();
            }
            if (const service::Value* loader = response.Find("loader")) {
                if (loader->IsString()) result.loader = LoaderFromName(loader->AsString());
            }
            if (const service::Value* meta = response.Find("meta")) {
                if (!meta->IsNull()) result.plugin_data = StoredPluginMeta(*meta);
            }
            if (const service::Value* files = response.Find("watchFiles")) {
                if (files->IsArray()) {
                    for (const service::Value& file : files->AsArray()) {
                        if (file.IsString()) result.watch_files.push_back(file.AsString());
                    }
                }
            }
            if (const service::Value* dirs = response.Find("watchDirs")) {
                if (dirs->IsArray()) {
                    for (const service::Value& dir : dirs->AsArray()) {
                        if (dir.IsString()) result.watch_dirs.push_back(dir.AsString());
                    }
                }
            }
            return result;
        }

        // A bundle hook: buildEnd, generateBundle or writeBundle.
        //
        // The three share a body because they share a shape — the finished files
        // go up, messages come back — and differ in one thing only: whether the
        // host is allowed to hand a replacement set of files back. Only
        // generateBundle may, because it is the one that runs before the write.
        // A writeBundle that returned files would be handing them to a write
        // that has already happened, and buildEnd is deliberately given the same
        // restriction so that "which hook can change the output" stays one clear
        // answer.
        api::OnEndResult RunJsBundle(const std::string& plugin,
                                     const std::string& hook,
                                     bool may_replace_files,
                                     api::BuildResult& result) {
            api::OnEndResult outcome;
            if (!service::HasCallbackTransport()) return outcome;

            std::vector<service::Value> files;
            files.reserve(result.output_files.size());
            for (const api::OutputFile& file : result.output_files) {
                files.push_back(service::Value::Object({
                    {"path", service::Value::String(file.path)},
                    {"contents", service::Value::Bytes(file.contents)},
                }));
            }

            const service::Value response = PluginCall(plugin, hook, service::Value::Object({
                {"outputFiles", service::Value::Array(std::move(files))},
            }));

            outcome.errors = PluginMessages(response, "errors", plugin);
            outcome.warnings = PluginMessages(response, "warnings", plugin);
            if (!outcome.errors.empty()) return outcome;

            const service::Value* replacement = response.Find("files");
            if (may_replace_files && replacement != nullptr && replacement->IsArray()) {
                std::vector<api::OutputFile> rebuilt;
                for (const service::Value& entry : replacement->AsArray()) {
                    if (!entry.IsObject()) continue;
                    const service::Value* path = entry.Find("path");
                    if (path == nullptr || !path->IsString()) continue;

                    api::OutputFile file;
                    file.path = path->AsString();
                    if (const service::Value* contents = entry.Find("contents")) {
                        if (contents->IsString()) {
                            const std::string& text = contents->AsString();
                            file.contents.assign(text.begin(), text.end());
                        } else if (contents->IsBytes()) {
                            file.contents = contents->AsBytes();
                        }
                    }
                    rebuilt.push_back(std::move(file));
                }
                result.output_files = std::move(rebuilt);
            }
            return outcome;
        }

        // closeBundle, which is the engine's dispose.
        //
        // A dispose has no result to report into and no failure to raise, so the
        // answer is discarded: a host's closeBundle that throws has already had
        // its say in the host's own process, and there is nothing here for the
        // engine to do about it.
        void RunJsDispose(const std::string& plugin) {
            if (!service::HasCallbackTransport()) return;
            PluginCall(plugin, "closeBundle", service::Value::Null());
        }

        // A whole plugin, built from the descriptor the host sent.
        //
        // Only the hooks the host says it implements are registered. That is not
        // an optimisation but a correctness point: registering resolveId for a
        // plugin that does not have one would send a round trip per import for
        // every plugin in the list, and the engine treats the first resolving
        // plugin as the answer, so a hook that always declines is not free — it
        // is noise on the wire for every module in the graph.
        api::Plugin MakeJsPlugin(const std::string& name, const std::vector<std::string>& hooks) {
            auto implements = [hooks](const char* hook) {
                return std::find(hooks.begin(), hooks.end(), std::string(hook)) != hooks.end();
            };

            api::Plugin plugin;
            plugin.name = name;
            plugin.setup = [name, implements](api::PluginBuild& build) {
                if (implements("buildStart")) {
                    build.on_start([name]() { return RunJsStart(name); });
                }
                if (implements("resolveId")) {
                    build.on_resolve(
                        api::OnResolveOptions{.filter = ".*", .namespace_ = ""},
                        [name](const api::OnResolveArgs& args) { return RunJsResolve(name, args); });
                }
                if (implements("load")) {
                    build.on_load(
                        api::OnLoadOptions{.filter = ".*", .namespace_ = ""},
                        [name](const api::OnLoadArgs& args) { return RunJsLoad(name, args); });
                }
                if (implements("buildEnd") || implements("generateBundle")) {
                    build.on_generate_bundle([name, implements](api::BuildResult& result) {
                        // buildEnd runs first and may refuse; if it does, the
                        // bundle is not offered to generateBundle at all.
                        api::OnEndResult outcome;
                        if (implements("buildEnd")) {
                            api::OnEndResult ended =
                                RunJsBundle(name, "buildEnd", false, result);
                            outcome.errors.insert(outcome.errors.end(),
                                                  ended.errors.begin(), ended.errors.end());
                            outcome.warnings.insert(outcome.warnings.end(),
                                                    ended.warnings.begin(), ended.warnings.end());
                            if (!outcome.errors.empty()) return outcome;
                        }
                        if (implements("generateBundle")) {
                            api::OnEndResult generated =
                                RunJsBundle(name, "generateBundle", true, result);
                            outcome.errors.insert(outcome.errors.end(),
                                                  generated.errors.begin(), generated.errors.end());
                            outcome.warnings.insert(outcome.warnings.end(),
                                                    generated.warnings.begin(), generated.warnings.end());
                        }
                        return outcome;
                    });
                }
                if (implements("writeBundle")) {
                    build.on_write_bundle([name](api::BuildResult& result) {
                        return RunJsBundle(name, "writeBundle", false, result);
                    });
                }
                if (implements("closeBundle")) {
                    build.on_dispose([name]() { RunJsDispose(name); });
                }
            };
            return plugin;
        }

        // The plugin list from a request, appended to the options.
        //
        // The host sends descriptors rather than functions, because a function
        // cannot cross a pipe: a descriptor is a name and the names of the hooks
        // the plugin implements, and the bridge turns each one into the
        // Rollup-shaped callbacks above. A descriptor with no name, or one that
        // is not an object, is skipped rather than refused — a malformed plugin
        // is the host's problem, and failing the whole build for one bad entry
        // would be a worse answer than building without it.
        void PluginsFromRequest(const service::Value& request, api::BuildOptions& options) {
            const service::Value* plugins = request.Find("plugins");
            if (plugins == nullptr || !plugins->IsArray()) return;

            for (const service::Value& entry : plugins->AsArray()) {
                if (!entry.IsObject()) continue;

                const service::Value* name = entry.Find("name");
                if (name == nullptr || !name->IsString() || name->AsString().empty()) continue;

                std::vector<std::string> hooks;
                if (const service::Value* list = entry.Find("hooks")) {
                    if (list->IsArray()) {
                        for (const service::Value& hook : list->AsArray()) {
                            if (hook.IsString()) hooks.push_back(hook.AsString());
                        }
                    }
                }

                options.plugins.push_back(MakeJsPlugin(name->AsString(), hooks));
            }
        }

        // Runs one build and returns the response.
        service::Value HandleBuild(const service::Value& request) {
            api::BuildOptions options;
            service::Value error;
            if (!BuildOptionsFromRequest(request, options, error)) return error;

            // Config discovery, the same step a command line goes through, so
            // that a build from Node reads guchho.config.json exactly as
            // "guchho build" would. Without this the two would disagree about
            // every project that has one, which is most of them.
            options = resolveRunOptions(options, options.abs_working_dir);

            const api::BuildResult result = api::Build(options);
            return BuildResponse(result);
        }

        // One of a "run" command's output streams, forwarded to the host. Defined
        // after the descriptor plumbing below; declared here so the handler can
        // name it.
        void ForwardOutput(int fd, const char* stream);

        // The descriptor plumbing below is defined after the handlers that use
        // it, so the pieces a handler names are declared here.
        int DuplicateFd(int fd);
        bool PointFdAt(int source, int target);
        void CloseFd(int fd);
        bool MakePipe(int fds[2]);

        // Runs a command line in this process, with a JavaScript host behind it.
        //
        // This is how the npm launcher runs "guchho build" and friends for a
        // project whose config names plugins: the command line is the same one
        // the binary would run, but it runs here, in the service that can reach
        // the host's plugin functions. The command's own output is forwarded to
        // the host as it is produced — a watch or a server prints for as long as
        // it runs, so holding the output until the end would be a command that
        // looked hung.
        service::Value HandleRun(const service::Value& request) {
            const service::Value* argv_value = request.Find("argv");
            if (argv_value == nullptr || !argv_value->IsArray()) {
                return ErrorResponse(ProtocolError("\"argv\" must be an array of strings"));
            }
            std::vector<std::string> argv;
            for (const service::Value& entry : argv_value->AsArray()) {
                if (!entry.IsString()) {
                    return ErrorResponse(ProtocolError("\"argv\" must be an array of strings"));
                }
                argv.push_back(entry.AsString());
            }

            // The plugins the host will answer hooks for, in the same descriptor
            // shape the build commands use.
            api::BuildOptions options = newBuildOptions();
            PluginsFromRequest(request, options);

            // Point the command's output at pipes so it can be forwarded. The
            // real descriptors are saved and put back before this returns, and
            // the protocol stream is a descriptor of its own, so none of this
            // touches the frames the host is reading.
            const int saved_out = DuplicateFd(1);
            const int saved_err = DuplicateFd(2);
            int out_pipe[2] = {-1, -1};
            int err_pipe[2] = {-1, -1};

            const bool redirected = saved_out >= 0 && saved_err >= 0 &&
                MakePipe(out_pipe) && MakePipe(err_pipe) &&
                PointFdAt(out_pipe[1], 1) && PointFdAt(err_pipe[1], 2);

            std::thread out_forwarder;
            std::thread err_forwarder;
            if (redirected) {
                CloseFd(out_pipe[1]);
                CloseFd(err_pipe[1]);
                out_forwarder = std::thread(ForwardOutput, out_pipe[0], "stdout");
                err_forwarder = std::thread(ForwardOutput, err_pipe[0], "stderr");
            }

            const int code = RunWithPlugins(argv, options.plugins);

            if (redirected) {
                // Everything the command buffered goes out first, then the
                // descriptors go back to where they were. Pointing them back is
                // also what closes each pipe's write end, so the forwarders see
                // end-of-input and finish.
                std::fflush(nullptr);
                PointFdAt(saved_out, 1);
                PointFdAt(saved_err, 2);
                if (out_forwarder.joinable()) out_forwarder.join();
                if (err_forwarder.joinable()) err_forwarder.join();
                CloseFd(out_pipe[0]);
                CloseFd(err_pipe[0]);
            }
            CloseFd(saved_out);
            CloseFd(saved_err);

            return service::Value::Object({
                {"code", service::Value::Number(code)},
            });
        }

        // Runs one transform and returns the response.
        service::Value HandleTransform(const service::Value& request) {
            std::vector<std::string> flags;
            service::Value error;

            if (!FlagsFromRequest(request, flags, error)) return error;

            // The source file is the name a diagnostic blames when there is no
            // file, so it is a flag and not a field: the grammar reads it, and a
            // name the grammar does not accept is refused by the grammar.
            if (const service::Value* sourcefile = request.Find("sourcefile")) {
                if (sourcefile->IsString() && !sourcefile->AsString().empty()) {
                    flags.push_back("--sourcefile=" + sourcefile->AsString());
                }
            }

            // The loader for a transform is a single name, where a build's is a
            // map of extension to name. It still goes through the grammar, for
            // the same reason: one table resolves loader names everywhere.
            if (const service::Value* loader = request.Find("loader")) {
                if (loader->IsString() && !loader->AsString().empty()) {
                    flags.push_back("--loader=" + loader->AsString());
                }
            }

            auto options = newTransformOptions();
            ParseOptionsExtras extras;
            const auto complaint = parseOptionsImpl(flags, nullptr, &options,
                ParseOptionsKind::kInternal, extras);
            if (complaint.has_value()) {
                return ErrorResponse(EngineError("invalid-transform-options", complaint->text));
            }

            // The input is a value rather than a flag, for the reason it is on a
            // build: a megabyte of source does not fit in an argument in any way
            // worth relying on.
            //
            // The field has to be there, but it may be empty: transforming the
            // empty string is a real operation that returns empty output, where a
            // transform with no input field at all is a host that forgot to send
            // one. Honouring the second as the first would answer a malformed
            // request with a successful no-op, which is the failure a host cannot
            // see.
            const service::Value* text = request.Find("input");
            if (text == nullptr || (!text->IsString() && !text->IsBytes())) {
                return ErrorResponse(EngineError("invalid-transform-request",
                    "a transform needs its input as a string or as bytes"));
            }

            std::string input;
            if (text->IsString()) {
                input = text->AsString();
            } else {
                const std::vector<uint8_t>& bytes = text->AsBytes();
                input = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }

            const api::TransformResult result = api::Transform(input, options);

            std::vector<std::pair<std::string, service::Value>> entries;
            entries.emplace_back("code", service::Value::Bytes(result.code));
            entries.emplace_back("map", service::Value::Bytes(result.map));
            entries.emplace_back("errors",
                service::Value::Array(service::MessagesToValue(result.errors)));
            entries.emplace_back("warnings",
                service::Value::Array(service::MessagesToValue(result.warnings)));
            return service::Value::Object(std::move(entries));
        }

        // A whole number of seconds, from the text form a host sends.
        //
        // "date" travels as a string rather than as a number for the same
        // reason a size does: the protocol's number is a 32-bit integer, and
        // an epoch in seconds stops fitting in one a decade from now. A host
        // that sent the number anyway would have it truncated on the way in
        // and would get an archive with a plausible, wrong timestamp — so a
        // value that is not a decimal integer is refused rather than read as
        // one.
        bool ParseInt64(const std::string& text, std::int64_t& out) {
            if (text.empty() || text.size() > 20) return false;

            std::int64_t value = 0;
            size_t       i     = 0;
            bool         minus = false;
            if (text[0] == '-') {
                minus = true;
                i     = 1;
                if (text.size() == 1) return false;
            }
            for (; i < text.size(); ++i) {
                const char digit = text[i];
                if (digit < '0' || digit > '9') return false;
                if (value > (std::numeric_limits<std::int64_t>::max() - (digit - '0')) / 10) {
                    return false;
                }
                value = value * 10 + (digit - '0');
            }
            out = minus ? -value : value;
            return true;
        }

        // Runs one pack request and returns the response.
        //
        // The request carries the fields rather than a flags array, which is
        // the one place this file departs from the rule at the top. The rule
        // exists so an option is accepted for the same reason it is accepted
        // on the command line, but a pack request has no grammar to read:
        // "inputs" is a list of paths, "format" is an archive format, "date"
        // is a timestamp and "mode" is a permission mask, and the grammar this
        // file would have to use is the build's, which knows about none of
        // them. Sending them through the flag parser would mean inventing
        // flags no command line has, for the sake of a rule about flags.
        //
        // Types are checked here and values by the core, so a caller that
        // sent the wrong shape gets an answer about the shape and a caller
        // that sent a shape the engine refuses gets the engine's own words
        // about it — the same words the command line would have printed.
        service::Value HandlePack(const service::Value& request) {
            pack::PackOptions options;

            const service::Value* inputs = request.Find("inputs");
            if (inputs == nullptr || !inputs->IsArray()) {
                return ErrorResponse(ProtocolError("\"inputs\" must be an array of strings"));
            }
            for (const service::Value& entry : inputs->AsArray()) {
                if (!entry.IsString()) {
                    return ErrorResponse(
                        ProtocolError("every entry in \"inputs\" must be a string"));
                }
                options.inputs.push_back(entry.AsString());
            }

            const service::Value* out_file = request.Find("outFile");
            if (out_file == nullptr || !out_file->IsString()) {
                return ErrorResponse(ProtocolError("\"outFile\" must be a string"));
            }
            options.outFile = out_file->AsString();

            if (const service::Value* format = request.Find("format");
                format != nullptr && !format->IsNull()) {
                if (!format->IsString()) {
                    return ErrorResponse(ProtocolError("\"format\" must be a string"));
                }
                options.format = format->AsString();
            }

            if (const service::Value* level = request.Find("level");
                level != nullptr && !level->IsNull()) {
                if (!level->IsNumber()) {
                    return ErrorResponse(ProtocolError("\"level\" must be a number"));
                }
                options.level = level->AsNumber();
            }

            if (const service::Value* overwrite = request.Find("overwrite");
                overwrite != nullptr && !overwrite->IsNull()) {
                if (!overwrite->IsBool()) {
                    return ErrorResponse(ProtocolError("\"overwrite\" must be a boolean"));
                }
                options.overwrite = overwrite->AsBool();
            }

            if (const service::Value* date = request.Find("date");
                date != nullptr && !date->IsNull()) {
                if (!date->IsString()) {
                    return ErrorResponse(ProtocolError(
                        "\"date\" must be seconds since the Unix epoch, sent as a string"));
                }
                std::int64_t seconds = 0;
                if (!ParseInt64(date->AsString(), seconds)) {
                    return ErrorResponse(
                        ProtocolError("\"date\" must be a whole number of seconds"));
                }
                options.date = seconds;
            }

            if (const service::Value* mode = request.Find("mode");
                mode != nullptr && !mode->IsNull()) {
                if (!mode->IsNumber()) {
                    return ErrorResponse(ProtocolError("\"mode\" must be a number"));
                }
                if (mode->AsNumber() < 0) {
                    return ErrorResponse(
                        ProtocolError("\"mode\" must not be negative"));
                }
                options.mode = static_cast<std::uint32_t>(mode->AsNumber());
            }

            const pack::PackResult result = pack::CreatePack(options);

            std::vector<service::Value> warnings;
            warnings.reserve(result.warnings.size());
            for (const std::string& warning : result.warnings) {
                warnings.push_back(service::Value::String(warning));
            }

            if (!result.Ok()) {
                // Reported as an error rather than as a result with an error
                // field in it, so that unwrap() throws the BuildFailure every
                // other failing call in this package throws. The note travels
                // as a note on the message, which is where a caller reading
                // errors[0].notes expects to find it.
                api::Message message = EngineError("pack-failed", result.error);
                if (!result.note.empty()) {
                    api::Note note;
                    note.text = result.note;
                    message.notes.push_back(std::move(note));
                }
                return service::Value::Object({
                    {"error", service::Value::String(result.error)},
                    {"errors", service::Value::Array({service::MessageToValue(message)})},
                    {"warnings", service::Value::Array(std::move(warnings))},
                });
            }

            // The size is a decimal string for the reason "date" is: an
            // archive past two gigabytes is a real thing to have written, and
            // a number field that cannot hold it would report a wrapped one.
            // The host turns it back into a number, where the same limit
            // applies but is at least visible when it is reached.
            return service::Value::Object({
                {"path", service::Value::String(result.path)},
                {"size", service::Value::String(std::to_string(result.size))},
                {"warnings", service::Value::Array(std::move(warnings))},
            });
        }

        // Formats diagnostics the way a terminal would show them, for a host that
        // has messages and wants the sentences rather than the fields.
        service::Value HandleFormatMessages(const service::Value& request) {
            const service::Value* messages = request.Find("messages");
            if (messages == nullptr || !messages->IsArray()) {
                return ErrorResponse(ProtocolError("\"messages\" must be an array"));
            }

            // Every message is read first and the whole list is formatted in one
            // call, rather than one call per message. The formatter writes a
            // summary line at the end — "1 error and 2 warnings" — and a summary
            // of a list of one is not the summary of the list, so formatting them
            // together is the only way to get the line a caller expects.
            std::vector<api::Message> parsed;
            parsed.reserve(messages->Size());
            for (const service::Value& entry : messages->AsArray()) {
                const std::optional<api::Message> message = service::MessageFromValue(entry);
                if (!message.has_value()) {
                    return ErrorResponse(ProtocolError(
                        "every entry in \"messages\" must be a message"));
                }
                parsed.push_back(*message);
            }

            api::FormatMessagesOptions format_options;
            format_options.kind = api::MessageKind::kError;

            if (const service::Value* kind = request.Find("kind")) {
                if (kind->IsString() && kind->AsString() == "warning") {
                    format_options.kind = api::MessageKind::kWarning;
                }
            }
            if (const service::Value* color = request.Find("color")) {
                if (color->IsBool()) format_options.color = color->AsBool();
            }
            if (const service::Value* width = request.Find("terminalWidth")) {
                if (width->IsNumber()) {
                    format_options.terminal_width = static_cast<int>(width->AsNumber());
                }
            }

            std::vector<service::Value> out;
            for (const std::string& line : api::FormatMessages(parsed, format_options)) {
                out.push_back(service::Value::String(line));
            }
            return service::Value::Object({
                {"logs", service::Value::Array(std::move(out))},
            });
        }

        // Pretty-prints a metafile, which is the one analysis the engine can do
        // and the one a build's own "analyze" flag would have produced.
        service::Value HandleAnalyzeMetafile(const service::Value& request) {
            const service::Value* metafile = request.Find("metafile");
            if (metafile == nullptr || !metafile->IsString()) {
                return ErrorResponse(ProtocolError("\"metafile\" must be a string of JSON"));
            }

            api::AnalyzeMetafileOptions analyze_options;
            if (const service::Value* color = request.Find("color")) {
                if (color->IsBool()) analyze_options.color = color->AsBool();
            }
            if (const service::Value* verbose = request.Find("verbose")) {
                if (verbose->IsBool()) analyze_options.verbose = verbose->AsBool();
            }

            const std::string text = api::AnalyzeMetafile(metafile->AsString(), analyze_options);
            return service::Value::Object({
                {"text", service::Value::String(text)},
            });
        }

        // The live builds, keyed by the number the host chose for them.
        //
        // This is a plain map and not something more elaborate because the
        // service is single-threaded: a context is only touched between reading
        // one request and reading the next, so there is nothing to guard. The
        // watcher and server threads a context starts are the engine's business
        // and are already internally synchronised.
        class ContextRegistry {
            public:
                static ContextRegistry& Instance() {
                    static ContextRegistry registry;
                    return registry;
                }

                // The next unused key, so a host that does not care about naming
                // its contexts can leave the key off entirely.
                int32_t NextKey() {
                    while (contexts_.count(next_key_) != 0) next_key_++;
                    return next_key_++;
                }

                void Put(int32_t key, std::unique_ptr<api::BuildContext> context) {
                    Entry entry;
                    entry.context = std::move(context);
                    contexts_[key] = std::move(entry);
                }

                api::BuildContext* Get(int32_t key) {
                    const auto found = contexts_.find(key);
                    if (found == contexts_.end()) return nullptr;
                    return found->second.context.get();
                }

                // The server that a context is serving, if it is serving one.
                //
                // The engine hands back a function that shuts the server down and
                // owns nothing else about it, so the only way a service can end a
                // server is to hold on to that function. It is kept here beside
                // the context rather than in the host, because the host cannot
                // hold a C++ callable and the process must not exit with a
                // listening socket still open.
                void SetServer(int32_t key, std::function<void()> stop) {
                    if (const auto found = contexts_.find(key); found != contexts_.end()) {
                        found->second.server_stop = std::move(stop);
                    }
                }

                // Dispose, in the one order that is safe, and then release.
                //
                // The server is shut down before the context is disposed, because
                // the server is serving what the context produces and disposing
                // first would leave it serving a build that no longer exists.
                // Disposing before releasing is the engine's own requirement: a
                // context must be disposed exactly once, and letting the
                // destructor be that one call is how a half-torn-down watcher
                // thread escapes.
                void Dispose(int32_t key) {
                    const auto found = contexts_.find(key);
                    if (found == contexts_.end()) return;
                    if (found->second.server_stop) found->second.server_stop();
                    found->second.context->Dispose();
                    contexts_.erase(found);
                }

            private:
                struct Entry {
                    std::unique_ptr<api::BuildContext> context;
                    std::function<void()> server_stop;
                };

                std::unordered_map<int32_t, Entry> contexts_;
                int32_t next_key_ = 0;
        };

        // The key a request is talking about, and whether it had one.
        bool ContextKeyFromRequest(const service::Value& request, int32_t& key) {
            const service::Value* value = request.Find("key");
            if (value == nullptr || !value->IsNumber()) return false;
            key = value->AsNumber();
            return true;
        }

        service::Value HandleContext(const service::Value& request) {
            // The same options a "build" request produces, from the same one
            // function, so that a context is built from exactly the options the
            // one-shot build would have used. Anything less and "rebuild" would
            // be rebuilding something other than the build that was asked for.
            api::BuildOptions options;
            service::Value error;
            if (!BuildOptionsFromRequest(request, options, error)) return error;

            options = resolveRunOptions(options, options.abs_working_dir);

            std::vector<api::Message> errors;
            std::unique_ptr<api::BuildContext> context = api::Context(options, errors);

            if (!context) {
                if (errors.empty()) {
                    errors.push_back(EngineError("context-failed",
                        "the build could not be set up and said nothing about why"));
                }
                api::BuildResult result;
                result.errors = std::move(errors);
                return BuildResponse(result);
            }

            const int32_t key = ContextRegistry::Instance().NextKey();
            ContextRegistry::Instance().Put(key, std::move(context));

            return service::Value::Object({
                {"key", service::Value::Number(key)},
                {"errors", service::Value::Array(service::MessagesToValue(errors))},
            });
        }

        service::Value HandleRebuild(const service::Value& request) {
            int32_t key = 0;
            if (!ContextKeyFromRequest(request, key)) {
                return ErrorResponse(ProtocolError("a rebuild must name the context it is for"));
            }

            api::BuildContext* context = ContextRegistry::Instance().Get(key);
            if (context == nullptr) {
                return ErrorResponse(EngineError("unknown-context",
                    "that context has been disposed of, or was never created"));
            }

            // The options are kept by the context, so there is nothing from the
            // request to apply here except the mangle cache, which is state
            // carried between builds rather than a setting of one.
            return BuildResponse(context->Rebuild());
        }

        service::Value HandleDispose(const service::Value& request) {
            int32_t key = 0;
            if (!ContextKeyFromRequest(request, key)) {
                return ErrorResponse(ProtocolError("a dispose must name the context it is for"));
            }

            // Absent either way, so a second dispose of the same context is
            // reported the same way as a rebuild of a disposed one: the caller
            // used a key that is not there.
            ContextRegistry::Instance().Dispose(key);
            return service::Value::Object({});
        }

        service::Value HandleCancel(const service::Value& request) {
            int32_t key = 0;
            if (!ContextKeyFromRequest(request, key)) {
                return ErrorResponse(ProtocolError("a cancel must name the context it is for"));
            }

            api::BuildContext* context = ContextRegistry::Instance().Get(key);
            if (context != nullptr) context->Cancel();
            return service::Value::Object({});
        }

        service::Value HandleWatch(const service::Value& request) {
            int32_t key = 0;
            if (!ContextKeyFromRequest(request, key)) {
                return ErrorResponse(ProtocolError("a watch must name the context it is for"));
            }

            api::BuildContext* context = ContextRegistry::Instance().Get(key);
            if (context == nullptr) {
                return ErrorResponse(EngineError("unknown-context",
                    "that context has been disposed of, or was never created"));
            }

            api::WatchOptions watch_options;
            if (const service::Value* delay = request.Find("delay")) {
                if (delay->IsNumber()) watch_options.delay = static_cast<int>(delay->AsNumber());
            }
            context->Watch(watch_options);
            return service::Value::Object({});
        }

        service::Value HandleServe(const service::Value& request) {
            int32_t key = 0;
            if (!ContextKeyFromRequest(request, key)) {
                return ErrorResponse(ProtocolError("a serve must name the context it is for"));
            }

            api::BuildContext* context = ContextRegistry::Instance().Get(key);
            if (context == nullptr) {
                return ErrorResponse(EngineError("unknown-context",
                    "that context has been disposed of, or was never created"));
            }

            api::ServeOptions serve_options;
            if (const service::Value* port = request.Find("port")) {
                // Checked rather than cast, because a port is a uint16_t and a
                // number that does not fit one is a request that cannot be
                // honoured. Silently keeping the low half of a 70000 would bind
                // a port nobody asked for, and the symptom — connecting to
                // something else on the machine — is a long way from this line.
                if (port->IsNumber()) {
                    const int32_t number = port->AsNumber();
                    if (number < 0 || number > 65535) {
                        return ErrorResponse(ProtocolError(
                            "\"port\" must be between 0 and 65535"));
                    }
                    serve_options.port = static_cast<uint16_t>(number);
                }
            }
            if (const service::Value* host = request.Find("host")) {
                if (host->IsString()) serve_options.host = host->AsString();
            }
            if (const service::Value* servedir = request.Find("servedir")) {
                if (servedir->IsString()) serve_options.servedir = servedir->AsString();
            }

            // The server's "stop" is not a request the host can make later,
            // because the handle lives here. It is kept against the key and
            // called by dispose, which is the only thing that ends a context
            // anyway — so a context that is serving is stopped by being
            // disposed, and a host that wants it stopped earlier disposes it.
            // Serve() throws on a bad configuration — a port already in use, a
            // half-specified TLS pair — and a thrown exception here would take the
            // whole service down over one request. It is the one place in this
            // file that can throw, so it is the one place that is guarded.
            api::ServeResult served;
            try {
                served = context->Serve(serve_options);
            } catch (const std::exception& reason) {
                return ErrorResponse(EngineError("serve-failed", reason.what()));
            }

            // The engine hands back a function that shuts the server down, and a
            // C++ function cannot cross the wire. It is kept here against the key
            // and called by dispose, which is the only thing that ends a context
            // anyway — so a serving context is stopped by being disposed, and a
            // host that wants it stopped earlier disposes it.
            ContextRegistry::Instance().SetServer(key, served.stop);

            std::vector<service::Value> hosts;
            for (const std::string& host_url : served.hosts) {
                hosts.push_back(service::Value::String(host_url));
            }

            return service::Value::Object({
                {"port", service::Value::Number(static_cast<int32_t>(served.port))},
                {"hosts", service::Value::Array(std::move(hosts))},
            });
        }

        // The command table itself, with the throws it may contain left to
        // Dispatch above.
        service::Value DispatchCommand(const std::string& name, const service::Value& request);

        // Answers one request. Split out from the loop so that a test can call it
        // with a decoded request and read the decoded answer, without a process
        // and a pipe in the way.
        service::Value Dispatch(const service::Value& request) {
            const service::Value* command = request.Find("command");
            if (command == nullptr || !command->IsString()) {
                return ErrorResponse(ProtocolError(
                    "a request must have a \"command\" that is a string"));
            }

            const std::string& name = command->AsString();

            // Everything below this line is a build, and a build can fail in ways
            // that are reported as a Message but also in ways the engine reports
            // by throwing — a port already in use, a watcher started twice, a
            // file that vanished between listing and reading.
            //
            // On the command line that is fine: the process was going to exit
            // anyway. Here it is not. This is a long-lived process answering many
            // requests, and one that dies takes every context it was holding
            // down with it. So the catch is here, at the one place every request
            // passes, rather than repeated in each handler: a host that provokes
            // an exception gets a diagnostic on the request it sent and a service
            // that is still there for the next one.
            try {
                return DispatchCommand(name, request);
            } catch (const std::exception& reason) {
                return ErrorResponse(EngineError("internal-error", reason.what()));
            }
        }

        service::Value DispatchCommand(const std::string& name, const service::Value& request) {
            if (name == "build") return HandleBuild(request);
            if (name == "run") return HandleRun(request);
            if (name == "context") return HandleContext(request);
            if (name == "rebuild") return HandleRebuild(request);
            if (name == "watch") return HandleWatch(request);
            if (name == "serve") return HandleServe(request);
            if (name == "cancel") return HandleCancel(request);
            if (name == "dispose") return HandleDispose(request);
            if (name == "transform") return HandleTransform(request);
            if (name == "pack") return HandlePack(request);
            if (name == "format-msgs") return HandleFormatMessages(request);
            if (name == "analyze-metafile") return HandleAnalyzeMetafile(request);

            // The twelve lex/parse/transform/print commands. They are asked
            // first rather than listed here because their table is closed and
            // its names are a naming scheme rather than twelve spellings: a
            // future stage for a future language is a name this recognises
            // without this file learning it.
            if (IsServiceCompileCommand(name)) {
                return RunServiceCompileRequest(name, request);
            }

            return ErrorResponse(ProtocolError("unknown command: " + name));
        }

    } // namespace

    namespace {

        // ===================================================================
        // The protocol stream, and the descriptor it is written to.
        // ===================================================================
        //
        // Frames are written to a descriptor saved at startup rather than to
        // "stdout" itself, because the "run" command below borrows stdout and
        // stderr for the command line it drives: it points them at pipes so the
        // command's own output can be forwarded to the host as it is produced.
        // If the protocol were written through "stdout" it would land in that
        // pipe and corrupt the very stream the host is reading a notice from.
        //
        // The lock is what lets a forwarding thread and the request loop write
        // frames without interleaving their bytes. Every frame is one call.
        std::mutex g_protocol_write_mutex;
        int        g_protocol_fd = -1;

        void WriteProtocolBytes(const uint8_t* data, size_t size) {
            std::lock_guard<std::mutex> lock(g_protocol_write_mutex);
            if (g_protocol_fd < 0) return;
            size_t written = 0;
            while (written < size) {
            #ifdef _WIN32
                const int n = _write(g_protocol_fd, data + written,
                    static_cast<unsigned>(size - written));
            #else
                const ssize_t n = ::write(g_protocol_fd, data + written, size - written);
            #endif
                if (n <= 0) break;
                written += static_cast<size_t>(n);
            }
        }

        void WriteProtocolFrame(const std::vector<uint8_t>& bytes) {
            WriteProtocolBytes(bytes.data(), bytes.size());
        }

        void SetProtocolFd(int fd) {
            std::lock_guard<std::mutex> lock(g_protocol_write_mutex);
            g_protocol_fd = fd;
        }

        // ---- Descriptor plumbing for the "run" command --------------------
        //
        // The operations are spelled differently on Windows ("_dup", "_pipe")
        // and elsewhere, and the differences are the whole reason these exist.
        int DuplicateFd(int fd) {
        #ifdef _WIN32
            return _dup(fd);
        #else
            return ::dup(fd);
        #endif
        }

        bool PointFdAt(int source, int target) {
        #ifdef _WIN32
            return _dup2(source, target) == 0;
        #else
            return ::dup2(source, target) >= 0;
        #endif
        }

        void CloseFd(int fd) {
            if (fd < 0) return;
        #ifdef _WIN32
            _close(fd);
        #else
            ::close(fd);
        #endif
        }

        // A pipe whose buffered end is binary on Windows, so a command's output
        // is forwarded byte for byte rather than with newlines doubled.
        bool MakePipe(int fds[2]) {
        #ifdef _WIN32
            return _pipe(fds, 65536, _O_BINARY) == 0;
        #else
            return ::pipe(fds) == 0;
        #endif
        }

        long ReadFd(int fd, uint8_t* into, size_t capacity) {
        #ifdef _WIN32
            return static_cast<long>(_read(fd, into, static_cast<unsigned>(capacity)));
        #else
            return static_cast<long>(::read(fd, into, capacity));
        #endif
        }

        // One of a "run" command's output streams, forwarded to the host a frame
        // at a time. A frame is a notice rather than a plugin callback: it names
        // no plugin and the host answers it with nothing, which is what the
        // "notice" field tells it. The stream is named so the host can send it
        // back to the descriptor a person is reading.
        void ForwardOutput(int fd, const char* stream) {
            std::vector<uint8_t> buffer(65536);
            while (true) {
                const long n = ReadFd(fd, buffer.data(), buffer.size());
                if (n <= 0) break;

                service::Packet notice;
                notice.is_request = true;
                notice.id = 0;
                notice.value = service::Value::Object({
                    {"notice", service::Value::String("output")},
                    {"stream", service::Value::String(stream)},
                    {"text", service::Value::Bytes(std::vector<uint8_t>(
                        buffer.begin(),
                        buffer.begin() +
                            static_cast<std::vector<uint8_t>::difference_type>(n)))},
                });
                WriteProtocolFrame(service::EncodePacket(notice));
            }
        }

        // Reads whatever has arrived, and returns it.
        //
        // This is deliberately not fread. fread(ptr, 1, n, stream) is specified
        // to keep reading until it has all n bytes or hits end-of-file, so
        // asking it for a chunk on a pipe blocks until a megabyte has arrived or
        // the host has closed its end — a host that writes a small request and
        // waits for the answer would wait forever, and so would the process. The
        // service hung here before this was written, with no output at all,
        // because it was still trying to fill a buffer nobody was going to fill.
        //
        // read(2) returns as soon as there is anything to return, which is the
        // behaviour a framed protocol needs, and is what this calls.
        //
        // A return of 0 means the input has ended. A return of -1 is a read
        // error, and is reported the same way, because there is no useful
        // difference to a host at this point: either way, nothing more is coming
        // on this stream and the loop below ends.
        long ReadSome(uint8_t* into, size_t capacity) {
            #ifdef _WIN32
            return static_cast<long>(_read(_fileno(stdin), into,
                static_cast<unsigned>(capacity)));
            #else
            return static_cast<long>(::read(STDIN_FILENO, into, capacity));
            #endif
        }

        // Puts a stream into binary mode, and says why this is here.
        //
        // On Windows a stream opened as text has two translations on it, and
        // both of them corrupt a binary frame rather than merely making it
        // awkward. A newline byte inside an output file's contents comes back out
        // as two bytes, so every frame the host decodes after the first newline
        // in a JavaScript file is off by however many newlines came before it. And
        // 0x1A is end-of-file in text mode, so a source file containing a
        // Ctrl-Z — a perfectly ordinary byte, and one a string can contain —
        // silently ends the request stream halfway through.
        //
        // Both are properties of the C runtime rather than of anything this
        // service does, which is why they are fixed here once instead of being
        // worked around in the codec.
        void UseBinaryStreams() {
        #ifdef _WIN32
            _setmode(_fileno(stdin), _O_BINARY);
            _setmode(_fileno(stdout), _O_BINARY);
        #else
            // Everywhere else there is no translation to turn off. The
            // stdio<->unistd switch is deliberately not taken: this build uses
            // the C library's own functions below, so there is nothing to
            // switch away from.
        #endif
        }

    } // namespace

} // namespace guchho::cli

// The callback transport, as the rest of the engine sees it.
//
// This is a single global rather than a parameter threaded through every
// plugin, and that is deliberate: a plugin hook is registered once and then
// called by whichever worker thread reaches it, deep inside the build, with
// nothing in its signature that could carry a transport. The transport is a
// property of the process having a host, not of any one build, so a global
// is the honest shape for it. The mutex is what makes it safe to read from
// several worker threads at once and to replace at the start and end of the
// service loop; the transport itself serialises the requests sent through
// it.
namespace guchho::service {

    namespace {

        std::mutex g_callback_mutex;
        CallbackTransport g_callback_transport;

    } // namespace

    void SetCallbackTransport(CallbackTransport transport) {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        g_callback_transport = std::move(transport);
    }

    bool HasCallbackTransport() {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        return static_cast<bool>(g_callback_transport);
    }

    Value InvokeCallback(const Value& request) {
        // The transport is copied out under the lock and called outside it,
        // because the call blocks for a whole round trip and holding the lock
        // across that would stop every other hook in the build from even
        // reaching the point where the transport serialises them.
        CallbackTransport transport;
        {
            std::lock_guard<std::mutex> lock(g_callback_mutex);
            transport = g_callback_transport;
        }
        if (!transport) {
            throw std::runtime_error(
                "a plugin hook was reached in a process with no plugin host");
        }
        return transport(request);
    }

} // namespace guchho::service

namespace guchho::cli {

    int RunService() {
        // Before the greeting, and before anything is read, because every byte
        // this process exchanges from now on is a frame.
        UseBinaryStreams();

        // The descriptor every frame is written to. A duplicate of stdout taken
        // now, before the "run" command ever points stdout at a pipe for a child
        // command: from here on, "stdout" belongs to whatever command line is
        // running, and this is the one path back to the host.
        SetProtocolFd(DuplicateFd(1));
    #ifdef _WIN32
        if (g_protocol_fd >= 0) _setmode(g_protocol_fd, _O_BINARY);
    #endif

        // The version goes out before anything is read, and it goes out as a
        // frame rather than as a packet: a host has to be able to tell whether
        // the process it started is one it can talk to before it decodes
        // anything from it.
        //
        // Written once, into a buffer, and then written out. Calling the encoder
        // twice would work and would be two chances to differ.
        const std::vector<uint8_t> greeting = service::EncodeVersionFrame(service::kVersion);
        WriteProtocolFrame(greeting);

        service::PacketReader reader;

        // On the heap, not the stack. The chunk is a megabyte, and a megabyte is
        // far more than the default stack on Windows hands a thread — the process
        // died with a stack overflow before this was moved, and it died before it
        // printed the greeting, which made it look like the protocol was broken
        // rather than like an allocation was in the wrong place.
        std::vector<uint8_t> chunk(kReadChunk);

        // The state the callback direction needs, and why each piece is here.
        //
        // A plugin hook is answered by an extra exchange in the middle of a
        // request: the service sends a request of its own and waits. The reader
        // is shared with the main loop rather than duplicated, because a frame
        // can arrive split across reads either way and there is only one stream.
        //
        // Requests that arrive while a callback is in flight — a host that sent
        // two builds back to back would do this — cannot be dispatched from
        // inside the callback without recursing a whole request deep, so they go
        // onto a queue and are drained by the main loop once the answer is in.
        //
        // The id counter is the service's own. The host's requests use its ids;
        // this one's callbacks use a separate space, and the direction bit is
        // what keeps the two from ever being confused.
        std::vector<service::Packet> deferred;
        std::mutex callback_mutex;
        uint32_t next_callback_id = 0;

        service::SetCallbackTransport(
            [&](const service::Value& request) -> service::Value {
                // One callback at a time. Two hooks that fired on different
                // worker threads would otherwise interleave their frames and
                // each read the other's answer; the build's plugin order is a
                // sequence, so serialising here is also what makes it real.
                std::lock_guard<std::mutex> lock(callback_mutex);

                const uint32_t id = next_callback_id++;

                service::Packet ask;
                ask.is_request = true;
                ask.id = id;
                ask.value = request;
                const std::vector<uint8_t> encoded = service::EncodePacket(ask);
                WriteProtocolFrame(encoded);

                while (true) {
                    const long read = ReadSome(chunk.data(), chunk.size());
                    if (read <= 0) {
                        throw std::runtime_error(
                            "the plugin host closed while a plugin hook was running");
                    }

                    std::vector<service::Packet> arrived =
                        reader.Feed(chunk.data(), static_cast<size_t>(read));

                    if (reader.IsBroken()) {
                        throw std::runtime_error(reader.Error());
                    }

                    for (service::Packet& packet : arrived) {
                        // The answer, and only the answer, ends the wait. It is
                        // matched by id as well as by direction, so an answer to
                        // some other callback — which cannot happen while this
                        // lock is held, but which a future protocol might allow —
                        // would not be mistaken for this one.
                        if (!packet.is_request && packet.id == id) {
                            return std::move(packet.value);
                        }
                        deferred.push_back(std::move(packet));
                    }
                }
            });

        // The one place a request is answered, so the main loop and the drain of
        // the deferred queue cannot format a reply differently.
        auto answer = [&](const service::Packet& packet) {
            service::Packet reply;
            reply.is_request = false;
            reply.id = packet.id;
            reply.value = Dispatch(packet.value);

            const std::vector<uint8_t> encoded = service::EncodePacket(reply);
            WriteProtocolFrame(encoded);
        };

        while (true) {
            const long read = ReadSome(chunk.data(), chunk.size());
            if (read == 0) {
                // End of input, or a read error. Either way nothing more is
                // coming, and a host that closed its end is a service that has
                // done its work. Every request is answered as it is read, so
                // there is nothing in flight that this abandons.
                break;
            }

            const std::vector<service::Packet> packets = reader.Feed(chunk.data(),
                static_cast<size_t>(read));

            if (reader.IsBroken()) {
                // A frame this version cannot read means the two sides are not
                // speaking the same protocol. Carrying on would turn one bad
                // handshake into a stream of nonsense, so this says what happened
                // and stops — and the host, which is waiting on this request id,
                // gets an answer rather than a closed pipe.
                service::Packet reply;
                reply.is_request = false;
                reply.id = 0;
                reply.value = service::Value::Object({
                    {"error", service::Value::String(reader.Error())},
                });
                const std::vector<uint8_t> encoded = service::EncodePacket(reply);
                WriteProtocolFrame(encoded);
                service::SetCallbackTransport(nullptr);
                return 1;
            }

            for (const service::Packet& packet : packets) {
                // A response to a request this service never made — with the
                // callback direction in use, this is a stray answer, not a
                // request to answer. Answering a response is how a protocol ends
                // up talking to itself.
                if (!packet.is_request) continue;
                answer(packet);
            }

            // Whatever arrived while a callback was being answered, now that no
            // request is in flight and the queue is complete.
            while (!deferred.empty()) {
                service::Packet packet = std::move(deferred.front());
                deferred.erase(deferred.begin());
                if (packet.is_request) answer(packet);
            }
        }

        // The host is gone, so a plugin hook reached later — during a dispose
        // that outlives the loop, say — must not try to reach it.
        service::SetCallbackTransport(nullptr);
        return 0;
    }

    // The dispatch, exposed so a test can drive the service in this process.
    // Everything a request can be is here, and nothing here starts a process or
    // reads a stream, which is what makes the loop above testable at all.
    service::Value RunServiceRequest(const service::Value& request) {
        return Dispatch(request);
    }

} // namespace guchho::cli
