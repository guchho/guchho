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

#include <cstdio>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <string>
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
            return true;
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

    int RunService() {
        // Before the greeting, and before anything is read, because every byte
        // this process exchanges from now on is a frame.
        UseBinaryStreams();

        // The version goes out before anything is read, and it goes out as a
        // frame rather than as a packet: a host has to be able to tell whether
        // the process it started is one it can talk to before it decodes
        // anything from it.
        //
        // Written once, into a buffer, and then written out. Calling the encoder
        // twice would work and would be two chances to differ.
        const std::vector<uint8_t> greeting = service::EncodeVersionFrame(service::kVersion);
        std::fwrite(greeting.data(), 1, greeting.size(), stdout);
        std::fflush(stdout);

        service::PacketReader reader;

        // On the heap, not the stack. The chunk is a megabyte, and a megabyte is
        // far more than the default stack on Windows hands a thread — the process
        // died with a stack overflow before this was moved, and it died before it
        // printed the greeting, which made it look like the protocol was broken
        // rather than like an allocation was in the wrong place.
        std::vector<uint8_t> chunk(kReadChunk);

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
                std::fwrite(encoded.data(), 1, encoded.size(), stdout);
                std::fflush(stdout);
                return 1;
            }

            for (const service::Packet& packet : packets) {
                // A response to a request this service never made. There is
                // nothing to answer, and answering a response is how a protocol
                // ends up talking to itself. The callback direction is not in use
                // yet, so this is the only way one arrives.
                if (!packet.is_request) continue;

                service::Packet reply;
                reply.is_request = false;
                reply.id = packet.id;
                reply.value = Dispatch(packet.value);

                const std::vector<uint8_t> encoded = service::EncodePacket(reply);
                std::fwrite(encoded.data(), 1, encoded.size(), stdout);
                std::fflush(stdout);
            }
        }

        return 0;
    }

    // The dispatch, exposed so a test can drive the service in this process.
    // Everything a request can be is here, and nothing here starts a process or
    // reads a stream, which is what makes the loop above testable at all.
    service::Value RunServiceRequest(const service::Value& request) {
        return Dispatch(request);
    }

} // namespace guchho::cli
