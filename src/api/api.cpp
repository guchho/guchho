// The public Guchho API surface, implemented on top of the internal bundler,
// resolver, logger and file-system layers.
//
// Everything a host program can ask Guchho to do enters through this file:
//
//   * Build()          — one complete bundle-and-print pass, then tear down.
//   * Transform()      — compile one string that already lives in memory.
//   * Context()        — a reusable build session for watch mode and editors.
//   * FormatMessages() — render Message values as text for a terminal or UI.
//   * AnalyzeMetafile()— turn metafile JSON into a size report.
//
// The file is organised in the order the work happens. First come the five
// public entry points, which are thin forwarders. Then comes option
// validation, which is by far the largest section: every public enum is checked,
// every relative path is made absolute, and every string that Guchho will
// interpret is parsed once here rather than deep inside the pipeline. After
// that come the message converters that translate between the public Message
// type and the internal log, the plugin adapters, and finally the build,
// transform, formatting and metafile implementations.
//
// A note on errors: nothing in this file throws for bad user input. Problems
// found while validating options are collected into a deferred log and handed
// back to the caller as a vector of Message values, so one pass can report
// every bad option at once instead of stopping at the first one. Exceptions
// are reserved for programmer errors and for plugin callbacks that misbehave.

#include "guchho/api.hpp"
#include "guchho/bundler.hpp"
#include "guchho/compat.hpp"
#include "guchho/config.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/resolver.hpp"


#include <algorithm>
#include <any>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace guchho::api {

namespace logger = guchho::logger;

// ===========================================================================
// Build API
// ===========================================================================
//
// The one-shot entry point. Everything a full build needs travels in as a
// BuildOptions value and comes back out as a BuildResult.

// Runs one complete build: entry points are resolved, the module graph is
// walked, every file is compiled, the output is printed, and the session is
// torn down again. Nothing is written to disk unless "write" was set, so a
// host that wants the bytes in memory can leave it off and read them straight
// out of the result.
//
// Prefer Context() when more than one build is wanted. A one-shot build cannot
// be cancelled, cannot be watched, and re-runs all of its setup work every time
// it is called.
//
// Input:  entry_points = {"src/index.js"}, bundle = true, format = kESModule,
//         write = false, minify_whitespace = true
// Output: one OutputFile per emitted chunk in "output_files" holding the exact
//         bytes that a write would have produced, "errors" empty on success,
//         "metafile" empty because it was not requested, disk untouched.
BuildResult Build(const BuildOptions& options) {
    return build_impl(options);
}

// ===========================================================================
// Transform API
// ===========================================================================
//
// The single-string half of the API. No module graph, no package resolution,
// no file system: the caller already has the source in memory and wants
// compiled output back.

// Compiles one string of source that the caller already has in hand. The text
// arrives through the ordinary build machinery as an in-memory entry point, so
// transforms get the same loaders, defines, target environment and minification
// as a real build, but without ever touching the disk for input.
//
// Input:  input = "const x = 1", loader = kJS, format = kESModule
// Output: result.code holding the compiled text, "errors" empty on success, and
//         "map" filled in as well whenever a source map was requested.
TransformResult Transform(const std::string& input,
                          const TransformOptions& options) {
    return transform_impl(input, options);
}

// ===========================================================================
// Context API
// ===========================================================================
//
// A long-lived build session. The expensive part of a build — reading option
// values, checking them, running plugin setup, allocating the caches — happens
// once, and every later rebuild reuses it.

// Creates a reusable build session from a set of build options. Plugin setup
// runs immediately, so any error a plugin wants to report about the whole
// configuration surfaces here rather than on the first rebuild.
//
// Returns nullptr and fills "errors" when the options themselves are invalid;
// the caller gets no session to dispose of in that case. An empty error list
// with a non-null context means setup succeeded, not that a build has run yet.
//
// Input:  entry_points = {"src/index.js"}, bundle = true
// Output: a BuildContext whose first Rebuild() produces the same result Build()
//         would have returned for the same options, plus a context that can be
//         watched, cancelled and disposed.
std::unique_ptr<BuildContext> Context(const BuildOptions& options,
                                     std::vector<Message>& errors) {
    return create_build_context(options, errors);
}

// ===========================================================================
// FormatMessages API
// ===========================================================================
//
// Rendering, not building. A host that collects Message values over time — an
// editor, a language server, a log viewer — can turn them into text here
// instead of duplicating Guchho's layout logic.

// Renders a list of Message values as finished text, one string per message.
// This is the same layout the build log uses, including the caret under the
// offending column and the "did you mean" notes, so a host that forwards these
// strings shows the user exactly what a terminal build would have shown.
//
// Input:  one Message with text "Could not resolve \"lodash\"", no location
// Output: {"Could not resolve \"lodash\""}
std::vector<std::string> FormatMessages(
    const std::vector<Message>& msgs, const FormatMessagesOptions& options) {
    return format_msgs_impl(msgs, options);
}

// ===========================================================================
// AnalyzeMetafile API
// ===========================================================================
//
// Reporting on a build that already happened. A build can be asked to emit a
// metafile — JSON describing every output and what went into it — and this
// entry point turns that JSON into a table a human can read.

// Reads the metafile JSON produced by a build and returns a size report: one
// block per output file, listing that file's total size, then the inputs that
// contributed to it, largest first, each with its share as a percentage.
//
// Source maps are left out of the report on purpose. They are generated
// alongside the real output and would otherwise appear as a second entry with
// a size that has nothing to do with the program's own code.
//
// Input:  metafile = {"outputs": {"dist/app.js": {"bytes": 2048,
//                  "entryPoint": "src/index.js", "inputs": { ... }}}}
// Output: a plain-text tree of the form
//
//           dist/app.js    2.0kb   100.0%
//             src/lib.js   1.5kb   75.0%
//
//         and an empty string when the text is not parseable JSON or has no
//         "outputs" object.
std::string AnalyzeMetafile(const std::string& metafile,
                            const AnalyzeMetafileOptions& options) {
    return analyze_metafile_impl(metafile, options);
}

// Removes a directory prefix from a path, accepting either slash style. The
// prefix has to line up on a separator boundary, otherwise "foo" would be
// stripped out of "foobar", so a match is only reported when what follows the
// prefix either starts at a separator or is empty.
//
// This is how a path that Guchho resolved to an absolute location is turned
// back into something relative and stable enough to show in a log line or to
// compare against a caller-supplied base directory.
//
// Input:  path = "/project/src/app.js", prefix = "/project", separators = "/"
// Output: "src/app.js", with the return value true; out is cleared and false is
//         returned when the prefix is not a whole leading path component
//
// Input:  path = "/project/src/app.js", prefix = "/other"
// Output: out = "", return value false
bool strip_dir_prefix(const std::string& path, const std::string& prefix,
                      std::string_view separators, std::string& out) {
    if (path.rfind(prefix, 0) == 0) {
        size_t path_len = path.size();
        size_t prefix_len = prefix.size();

        // An empty prefix matches everything, so the whole path is the suffix.
        if (prefix_len == 0) {
            out = path;
            return true;
        }

        // The path is the prefix itself and nothing more, so the suffix is empty.
        if (path_len == prefix_len) {
            out.clear();
            return true;
        }

        if (separators.find(prefix[prefix_len - 1]) != std::string_view::npos) {
            // The prefix already ends in a separator, so it can be cut off as-is:
            //
            //   strip_dir_prefix(`/foo`, `/`, `/`) => `foo`
            //   strip_dir_prefix(`C:\foo`, `C:\`, `\/`) => `foo`
            out = path.substr(prefix_len);
            return true;
        } else if (separators.find(path[prefix_len]) != std::string_view::npos) {
            // The prefix stops just short of a separator, which has to be dropped
            // along with it so the result never begins with one:
            //
            //   strip_dir_prefix(`/foo/bar`, `/foo`, `/`) => `bar`
            //   strip_dir_prefix(`C:\foo\bar`, `C:\foo`, `\/`) => `bar`
            out = path.substr(prefix_len + 1);
            return true;
        }
    }

    out.clear();
    return false;
}

// Splits a naming template into literal chunks and placeholders. A template
// such as "assets/[name]-[hash]" becomes a list of alternating literal and
// placeholder parts, which the linker then fills in for each emitted file.
//
// A leading "./" is added before parsing so that a template that begins with a
// placeholder — "[name].js" — produces a non-empty first chunk and the part
// list always starts at the same place. Backslashes are folded to forward
// slashes first, because templates name output locations and those are always
// written in one canonical style regardless of the host platform.
//
// A bracket that does not begin a known placeholder is left alone as literal
// text, so a name that legitimately contains one still round-trips.
//
// Input:  "static/[name]-[hash].js"
// Output: ["./static/", kDir, "", kName, "-", kHash, ".js", kNoPlaceholder]
//         where each entry is a (literal text, placeholder kind) pair
//
// Input:  "" => an empty list, which tells the linker to fall back to its own
//         default naming scheme.
std::vector<config::PathTemplate> validate_path_template(std::string_view value) {
    std::vector<config::PathTemplate> result;
    if (value.empty()) {
        return result;
    }

    std::string template_str("./");
    template_str.append(value);
    std::replace(template_str.begin(), template_str.end(), '\\', '/');

    size_t search = 0;

    // Walk the template one placeholder at a time, emitting the literal text in
    // front of each one as its own part.
    while (search < template_str.size()) {
        // Find the next candidate placeholder start.
        size_t found = template_str.find('[', search);
        if (found == std::string::npos) {
            break;
        }
        search = found;

        std::string head = template_str.substr(0, search);
        std::string_view tail = std::string_view(template_str).substr(search);
        config::PathPlaceholder placeholder = config::PathPlaceholder::kNoPlaceholder;

        // Recognise the four placeholders Guchho fills in per output file. The
        // match is anchored at the bracket, so "[dirname]" is not a prefix match
        // for "[dir]" and instead stays literal.
        if (tail.compare(0, 5, "[dir]") == 0) {
            placeholder = config::PathPlaceholder::kDir;
            search += 5;
        } else if (tail.compare(0, 6, "[name]") == 0) {
            placeholder = config::PathPlaceholder::kName;
            search += 6;
        } else if (tail.compare(0, 6, "[hash]") == 0) {
            placeholder = config::PathPlaceholder::kHash;
            search += 6;
        } else if (tail.compare(0, 5, "[ext]") == 0) {
            placeholder = config::PathPlaceholder::kExt;
            search += 5;
        } else {
            // Not a placeholder after all: step over the bracket and keep
            // looking, so literal brackets in a file name survive intact.
            search++;
            continue;
        }

        // Everything up to and including this placeholder is one template part.
        result.push_back(config::PathTemplate{
            head,
            placeholder,
        });

        // Consume the placeholder, then restart the search on the remainder.
        template_str = template_str.substr(search);
        search = 0;
    }

    // Whatever is left after the final placeholder is a trailing literal part.
    if (search < template_str.size()) {
        result.push_back(config::PathTemplate{
            template_str,
            config::PathPlaceholder::kNoPlaceholder,
        });
    }

    return result;
}

// Renders a string the way a log message wants to see it: wrapped in double
// quotes, with embedded quotes and backslashes escaped. Every user-supplied
// string that ends up in a diagnostic goes through here first, which is what
// keeps a stray quote or newline in a path from breaking the layout of the
// message that is reporting it.
//
// Input:  "src/a\"b.js"  =>  "\"src/a\\\"b.js\""
// Input:  "plain"        =>  "\"plain\""
static std::string quoted_for_log(const std::string& s) {
    std::string out;
    out.push_back('"');
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

// ===========================================================================
// Option validation: enums, paths and strings
// ===========================================================================
//
// Everything below this banner turns a public option value into something the
// rest of the pipeline can use without checking again. Three things happen:
//
//   * Public enums are mapped to their internal counterparts. A public "no
//     preference" value becomes whatever the internal build wants, and any
//     value the host never should have produced throws.
//   * Relative paths are made absolute against the working directory, and a
//     path that cannot be made absolute is reported and dropped.
//   * Strings Guchho will later interpret — global names, regular expressions,
//     naming templates, defines — are parsed once here, so the parsers running
//     during a build never have to deal with a malformed input.
//
// The functions are deliberately small and independent. Each one appends to a
// shared log and returns the best value it could produce, which is why a single
// call into validate_build_options can report a dozen unrelated mistakes in one
// round trip instead of forcing the caller to fix them one build at a time.

// Maps a public platform onto the internal one. The public "no preference"
// value means the browser platform, which is what a host gets unless it says
// otherwise. Throws for a value outside the enumeration, which can only happen
// if a caller cast a raw integer to the enum.
//
// Input:  api::Platform::kDefault  =>  config::Platform::kBrowser
// Input:  api::Platform::kNode     =>  config::Platform::kNode
// Input:  an out-of-range value    =>  throws std::runtime_error
static config::Platform validate_platform(api::Platform value) {
    switch (value) {
        case api::Platform::kDefault:
        case api::Platform::kBrowser:
            return config::Platform::kBrowser;
        case api::Platform::kNode:
            return config::Platform::kNode;
        case api::Platform::kNeutral:
            return config::Platform::kNeutral;
        default:
            throw std::runtime_error("Invalid platform");
    }
}

// Maps a public output format onto the internal one. "No preference" means the
// input format is preserved, which only makes sense for a build that is not
// concatenating anything; validate_build_options later replaces it with a
// concrete format once it knows what the build is actually doing.
//
// Input:  api::Format::kDefault  =>  config::Format::kPreserve
// Input:  api::Format::kESModule =>  config::Format::kESModule
static config::Format validate_format(api::Format value) {
    switch (value) {
        case api::Format::kDefault:
            return config::Format::kPreserve;
        case api::Format::kIIFE:
            return config::Format::kIIFE;
        case api::Format::kCommonJS:
            return config::Format::kCommonJS;
        case api::Format::kESModule:
            return config::Format::kESModule;
        case api::Format::kUMD:
            return config::Format::kUMD;
        case api::Format::kAMD:
            return config::Format::kAMD;
        case api::Format::kSystem:
            return config::Format::kSystem;
        default:
            throw std::runtime_error("Invalid format");
    }
}

// Maps a public source-map choice onto the internal one. The two "linked"
// and "external" public values split into pairs internally because the linker
// needs to know separately whether to leave a comment in the generated file
// pointing at the map and whether to emit a side file at all.
//
// Input:  api::SourceMap::kLinked  =>  config::SourceMap::kLinkedWithComment
// Input:  api::SourceMap::kInline  =>  config::SourceMap::kInline
static config::SourceMap validate_source_map(api::SourceMap value) {
    switch (value) {
        case api::SourceMap::kNone:
            return config::SourceMap::kNone;
        case api::SourceMap::kLinked:
            return config::SourceMap::kLinkedWithComment;
        case api::SourceMap::kInline:
            return config::SourceMap::kInline;
        case api::SourceMap::kExternal:
            return config::SourceMap::kExternalWithoutComment;
        case api::SourceMap::kInlineAndExternal:
            return config::SourceMap::kInlineAndExternal;
        default:
            throw std::runtime_error("Invalid source map");
    }
}

// Decides what happens to legal comments, which are the license and copyright
// banners a dependency carries. The default depends on whether output is being
// concatenated: a bundle has exactly one place to put a banner that applies to
// the whole file, while a non-bundled transform leaves the comment where the
// author's code put it.
//
// Input:  kDefault, bundle = true   =>  kEndOfFile
// Input:  kDefault, bundle = false  =>  kInline
// Input:  kNone                     =>  kNone, wherever it appears
static config::LegalComments validate_legal_comments(api::LegalComments value, bool bundle) {
    switch (value) {
        case api::LegalComments::kDefault:
            return bundle ? config::LegalComments::kEndOfFile : config::LegalComments::kInline;
        case api::LegalComments::kNone:
            return config::LegalComments::kNone;
        case api::LegalComments::kInline:
            return config::LegalComments::kInline;
        case api::LegalComments::kEndOfFile:
            return config::LegalComments::kEndOfFile;
        case api::LegalComments::kLinked:
            return config::LegalComments::kLinkedWithComment;
        case api::LegalComments::kExternal:
            return config::LegalComments::kExternalWithoutComment;
        default:
            throw std::runtime_error("Invalid legal comments");
    }
}

// Maps the color option onto the logger's own. "If terminal" lets the logger
// decide at print time, which is what a host embedding Guchho in a GUI usually
// wants: nothing colourful is written unless a real terminal is on the other
// end.
//
// Input:  api::StderrColor::kColorNever  =>  logger::UseColor::kColorNever
// Input:  api::StderrColor::kColorAlways =>  logger::UseColor::kColorAlways
static logger::UseColor validate_color(api::StderrColor value) {
    switch (value) {
        case api::StderrColor::kColorIfTerminal:
            return logger::UseColor::kColorIfTerminal;
        case api::StderrColor::kColorNever:
            return logger::UseColor::kColorNever;
        case api::StderrColor::kColorAlways:
            return logger::UseColor::kColorAlways;
        default:
            throw std::runtime_error("Invalid color");
    }
}

// The options a validator could not make sense of, kept until there is a log to
// add them to.
//
// Validation of these settings happens while the log itself is being built — the
// log level is one of the things being validated — so a validator has nowhere to
// report a bad value to. It records the message here instead, and the entry point
// reports the lot as soon as it has a log. A build handed several unusable
// options therefore hears about all of them, rather than about whichever one
// happened to be validated first.
//
// The default each validator falls back to is the one that keeps a build
// possible: the level is information, the charset is a preference, and neither
// has a safe answer that is worth refusing the whole build over once the mistake
// has been reported.
class PendingErrors {
public:
    // Records one already-formatted message.
    void Add(std::string message) {
        messages_.push_back(std::move(message));
    }

    bool empty() const {
        return messages_.empty();
    }

    // Adds everything recorded so far to "log" and forgets it, so a build that
    // validates the same options twice does not report the same mistake twice.
    void ReportTo(logger::Log& log) {
        for (const std::string& message : messages_) {
            log.AddError(nullptr, logger::Range{}, message);
        }
        messages_.clear();
    }

private:
    std::vector<std::string> messages_;
};

// Maps the public log level onto the logger's. Used both for the log a build
// writes to stderr and for the per-message override table.
//
// A value that is not a level is a mistake in the caller's program rather than
// in the code being built, so it is reported as an error alongside every other
// bad option instead of throwing out of an API that promises not to. The level
// falls back to kInfo, which is the level a build reports its own summary at.
//
// Input:  api::LogLevel::kWarning  =>  logger::LogLevel::kWarning
// Input:  api::LogLevel::kSilent  =>  logger::LogLevel::kSilent
// Input:  99  =>  logger::LogLevel::kInfo, plus one error naming the value
static logger::LogLevel validate_log_level(api::LogLevel value, PendingErrors& pending) {
    switch (value) {
        case api::LogLevel::kVerbose:
            return logger::LogLevel::kVerbose;
        case api::LogLevel::kDebug:
            return logger::LogLevel::kDebug;
        case api::LogLevel::kInfo:
            return logger::LogLevel::kInfo;
        case api::LogLevel::kWarning:
            return logger::LogLevel::kWarning;
        case api::LogLevel::kError:
            return logger::LogLevel::kError;
        case api::LogLevel::kSilent:
            return logger::LogLevel::kSilent;
        default:
            pending.Add(logger::FormatMsg(logger::MsgCat::kAPI_InvalidLogLevel,
                                          static_cast<int>(value)));
            return logger::LogLevel::kInfo;
    }
}

// Resolves the output charset to a single bit: whether non-ASCII characters
// have to be escaped. Escaping is the default because the safe answer for an
// unknown consumer is a file that is valid no matter what encoding the reader
// assumed.
//
// Input:  api::Charset::kASCII => true  (escape non-ASCII in string literals)
// Input:  api::Charset::kUTF8  => false (pass UTF-8 through untouched)
static bool validate_ascii_only(api::Charset value) {
    switch (value) {
        case api::Charset::kDefault:
        case api::Charset::kASCII:
            return true;
        case api::Charset::kUTF8:
            return false;
        default:
            throw std::runtime_error("Invalid charset");
    }
}

// Decides whether bare package specifiers are bundled or left for the host to
// resolve. Bundling is the default: if the caller did not say otherwise, they
// want the dependency in the output file.
//
// Input:  api::Packages::kDefault  => false (bundle it)
// Input:  api::Packages::kExternal => true  (emit the import unchanged)
static bool validate_external_packages(api::Packages value) {
    switch (value) {
        case api::Packages::kDefault:
        case api::Packages::kBundle:
            return false;
        case api::Packages::kExternal:
            return true;
        default:
            throw std::runtime_error("Invalid packages");
    }
}

// Decides whether unused top-level declarations may be dropped from the output.
//
// The default is true in the two situations where nothing can legally be
// appended to the result afterwards. An IIFE is a single self-invoking
// expression, so a host cannot tack extra code onto the end of it without
// breaking the file. A bundle is the output of the whole graph, so anything a
// host might want to add should have been reachable from an entry point
// instead. In every other case — a single file being converted, say — the
// result may be concatenated with more code later, so unused declarations are
// kept unless asked otherwise.
//
// Input:  kDefault, bundle = true, format = kESModule  => true
// Input:  kDefault, bundle = false, format = kESModule => false
// Input:  kDefault, bundle = false, format = kIIFE     => true
// Input:  kTrue,  bundle = false, format = kESModule  => true  (forced)
static bool validate_tree_shaking(api::TreeShaking value, bool bundle, api::Format format) {
    switch (value) {
        case api::TreeShaking::kDefault:
            return bundle || format == api::Format::kIIFE;
        case api::TreeShaking::kFalse:
            return false;
        case api::TreeShaking::kTrue:
            return true;
        default:
            throw std::runtime_error("Invalid tree shaking");
    }
}

// Maps a public loader onto the internal one. The public "no loader" value is
// kept distinct from the public "use the default" value on purpose: the first
// means the caller has said nothing and the resolver will guess from the file
// extension, while the second explicitly requests Guchho's default loader.
//
// Input:  api::Loader::kDefault => config::Loader::kDefault
// Input:  api::Loader::kNone    => config::Loader::kNone
// Input:  api::Loader::kCSS     => config::Loader::kCSS
static config::Loader validate_loader(api::Loader value) {
    switch (value) {
        case api::Loader::kBase64:
            return config::Loader::kBase64;
        case api::Loader::kBinary:
            return config::Loader::kBinary;
        case api::Loader::kCopy:
            return config::Loader::kCopy;
        case api::Loader::kCSS:
            return config::Loader::kCSS;
        case api::Loader::kDataURL:
            return config::Loader::kDataURL;
        case api::Loader::kDefault:
            return config::Loader::kDefault;
        case api::Loader::kEmpty:
            return config::Loader::kEmpty;
        case api::Loader::kFile:
            return config::Loader::kFile;
        case api::Loader::kGlobalCSS:
            return config::Loader::kGlobalCSS;
        case api::Loader::kJS:
            return config::Loader::kJS;
        case api::Loader::kJSON:
            return config::Loader::kJSON;
        case api::Loader::kJSX:
            return config::Loader::kJSX;
        case api::Loader::kLocalCSS:
            return config::Loader::kLocalCSS;
        case api::Loader::kNone:
            return config::Loader::kNone;
        case api::Loader::kText:
            return config::Loader::kText;
        case api::Loader::kTS:
            return config::Loader::kTS;
        case api::Loader::kTSX:
            return config::Loader::kTSX;
        default:
            throw std::runtime_error("Invalid loader");
    }
}

// Turns the three "print absolute paths" flags into the path style each part of
// the build wants. They are separate flags because a host often wants absolute
// paths in the log but relative ones inside a source map, so that the map stays
// portable between machines.
//
// Input:  abs_paths with kLogAbsPath set     => kAbsPath for the log
// Input:  abs_paths without kCodeAbsPath     => kRelPath inside the output
static logger::PathStyle extract_path_style(api::AbsPathsFlags abs_paths, api::AbsPathsFlags flag) {
    return api::Has(abs_paths, flag) ? logger::PathStyle::kAbsPath : logger::PathStyle::kRelPath;
}

// Name of an engine as it appears in a target string, e.g. "chrome115" or
// "es2020". A target string is the engine name with the version appended
// directly to it, which is what lets one string describe a whole compatibility
// requirement.
//
// Input:  compat::Engine::kChrome  =>  "chrome"
// Input:  compat::Engine::kES      =>  "es"
static std::string_view engine_to_string(compat::Engine engine) {
    switch (engine) {
        case compat::Engine::kChrome: return "chrome";
        case compat::Engine::kDeno: return "deno";
        case compat::Engine::kEdge: return "edge";
        case compat::Engine::kES: return "es";
        case compat::Engine::kFirefox: return "firefox";
        case compat::Engine::kHermes: return "hermes";
        case compat::Engine::kIE: return "ie";
        case compat::Engine::kIOS: return "ios";
        case compat::Engine::kNode: return "node";
        case compat::Engine::kOpera: return "opera";
        case compat::Engine::kRhino: return "rhino";
        case compat::Engine::kSafari: return "safari";
    }
    return "";
}

// Maps a public engine name onto the internal one. The public enumeration has
// no "standard language" entry, because a bare language target is expressed
// through the "target" option instead of the engine list; an unmatched value
// therefore falls back to that internal default.
//
// Input:  api::EngineName::kFirefox  =>  compat::Engine::kFirefox
// Input:  an out-of-range value      =>  compat::Engine::kES
static compat::Engine convert_engine_name(api::EngineName name) {
    switch (name) {
        case api::EngineName::kChrome: return compat::Engine::kChrome;
        case api::EngineName::kDeno: return compat::Engine::kDeno;
        case api::EngineName::kEdge: return compat::Engine::kEdge;
        case api::EngineName::kFirefox: return compat::Engine::kFirefox;
        case api::EngineName::kHermes: return compat::Engine::kHermes;
        case api::EngineName::kIE: return compat::Engine::kIE;
        case api::EngineName::kIOS: return compat::Engine::kIOS;
        case api::EngineName::kNode: return compat::Engine::kNode;
        case api::EngineName::kOpera: return compat::Engine::kOpera;
        case api::EngineName::kRhino: return compat::Engine::kRhino;
        case api::EngineName::kSafari: return compat::Engine::kSafari;
    }
    return compat::Engine::kES;
}

// The shape of every engine version Guchho accepts: an optional one, two or
// three dot-separated numbers, optionally followed by a pre-release tag. The
// trailing tag is what lets a caller say "as of the next release" and still
// produce a constraint the version comparison understands.
//
// A version that does not match is reported and dropped, which leaves that one
// engine unconstrained rather than failing the whole build over a typo.
static const std::regex kVersionRegex(
    R"(^([0-9]+)(?:\.([0-9]+))?(?:\.([0-9]+))?(-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?$)");

// What a target decision turns into: the feature bits that the chosen engines
// cannot handle, the prefix data needed to make styles work on them anyway, and
// a human-readable rendering of the target for diagnostics.
//
// The three feature sets are computed together because they are three views of
// the same answer. A language feature and the syntax that lowers to it are
// looked up against the same version constraint, so computing them separately
// would mean walking the version table several times for no benefit.
struct ValidatedFeatures {
    compat::JSFeature unsupported_js{};
    compat::CSSFeature unsupported_css{};
    std::unordered_map<guchho::css::Declarations, compat::CSSPrefix> css_prefix_data;
    std::string target_env;
};

// Works out what the build has to avoid doing and what it has to rewrite, given
// a language-level target and a list of named engines with versions.
//
// The two options are additive: asking for a language target of ES2018 and
// listing an engine at version 115 produces a constraint set covering both, and
// a feature is only allowed through if every engine in the set supports it.
// Requesting neither is the common case for a modern build and returns an empty
// result immediately, so the expensive lookups are skipped entirely.
//
// An engine version that does not parse is reported and then ignored, which
// leaves that engine unconstrained instead of aborting the build.
//
// Input:  target = kES2018, engines = {{kChrome, "115"}, {kFirefox, "115"}}
// Output: unsupported_js holding every language feature added after ES2018,
//         unsupported_css holding the matching style features,
//         target_env = "\"chrome115,es2018,firefox115\""
//
// Input:  target = kDefault, engines = {}
// Output: an all-zero result with an empty target_env
static ValidatedFeatures validate_features(logger::Log& log, api::Target target, const std::vector<api::Engine>& engines) {
    if (target == api::Target::kDefault && engines.empty()) {
        return {};
    }

    std::unordered_map<compat::Engine, compat::Semver> constraints;
    std::vector<std::string> targets;
    targets.reserve(1 + engines.size());

    switch (target) {
        case api::Target::kES5:
            constraints[compat::Engine::kES] = compat::Semver{{5}, ""};
            break;
        case api::Target::kES2015:
            constraints[compat::Engine::kES] = compat::Semver{{2015}, ""};
            break;
        case api::Target::kES2016:
            constraints[compat::Engine::kES] = compat::Semver{{2016}, ""};
            break;
        case api::Target::kES2017:
            constraints[compat::Engine::kES] = compat::Semver{{2017}, ""};
            break;
        case api::Target::kES2018:
            constraints[compat::Engine::kES] = compat::Semver{{2018}, ""};
            break;
        case api::Target::kES2019:
            constraints[compat::Engine::kES] = compat::Semver{{2019}, ""};
            break;
        case api::Target::kES2020:
            constraints[compat::Engine::kES] = compat::Semver{{2020}, ""};
            break;
        case api::Target::kES2021:
            constraints[compat::Engine::kES] = compat::Semver{{2021}, ""};
            break;
        case api::Target::kES2022:
            constraints[compat::Engine::kES] = compat::Semver{{2022}, ""};
            break;
        case api::Target::kES2023:
            constraints[compat::Engine::kES] = compat::Semver{{2023}, ""};
            break;
        case api::Target::kES2024:
            constraints[compat::Engine::kES] = compat::Semver{{2024}, ""};
            break;
        case api::Target::kES2025:
            constraints[compat::Engine::kES] = compat::Semver{{2025}, ""};
            break;
        case api::Target::kESNext:
        case api::Target::kDefault:
        default:
            break;
    }

    for (const api::Engine& engine : engines) {
        std::smatch match;
        if (std::regex_match(engine.version, match, kVersionRegex) && match[1].matched) {
            unsigned long major = std::strtoul(match[1].str().c_str(), nullptr, 10);
            std::vector<uint32_t> parts;
            parts.push_back(static_cast<uint32_t>(major));
            if (match[2].matched) {
                parts.push_back(static_cast<uint32_t>(std::strtoul(match[2].str().c_str(), nullptr, 10)));
                if (match[3].matched) {
                    parts.push_back(static_cast<uint32_t>(std::strtoul(match[3].str().c_str(), nullptr, 10)));
                }
            }
            constraints[convert_engine_name(engine.name)] = compat::Semver{parts, match[4].str()};
            continue;
        }

        std::string text = logger::FormatMsg(logger::MsgCat::kAPI_InvalidVersionNote);
        log.AddErrorWithNotes(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_InvalidVersion, quoted_for_log(engine.version)),
            std::vector<logger::MsgData>{{nullptr, nullptr, text, false}});
    }

    for (auto& [engine, version] : constraints) {
        targets.push_back(std::string(engine_to_string(engine)) + version.ToString());
    }
    if (target == api::Target::kESNext) {
        targets.push_back("esnext");
    }

    std::sort(targets.begin(), targets.end());
    std::string target_env = helpers::StringArrayToQuotedCommaSeparatedString(targets);

    return {
        compat::UnsupportedJSFeatures(constraints),
        compat::UnsupportedCSSFeatures(constraints),
        compat::CSSPrefixData(constraints),
        target_env,
    };
}

// What the caller explicitly claimed about feature support. The two masks
// matter as much as the feature sets: a feature that is named at all is
// remembered even when its value is true, so that a target-driven restriction
// and an explicit "this one works" can be told apart later.
struct SupportedFeatures {
    compat::JSFeature unsupported_js{};
    compat::JSFeature js_mask{};
    compat::CSSFeature unsupported_css{};
    compat::CSSFeature css_mask{};
};

// Applies the caller's per-feature claims on top of what the target already
// decided. Naming a feature the target would have forbidden is what makes an
// override, while naming a feature as supported records that the caller vouched
// for it even if the version table would have said otherwise.
//
// A name that matches neither a language feature nor a style feature is
// reported and skipped, since it can only be a typo.
//
// Input:  {"arrow-function": true, "const-and-let": false}
// Output: js_mask with both bits set, unsupported_js with only the
//         "const-and-let" bit set, css_mask and unsupported_css untouched
static SupportedFeatures validate_supported(logger::Log& log, const std::unordered_map<std::string, bool>& supported) {
    SupportedFeatures result;
    for (auto& [key, value] : supported) {
        auto js = compat::StringToJSFeature.find(std::string_view(key));
        if (js != compat::StringToJSFeature.end()) {
            result.js_mask = static_cast<compat::JSFeature>(static_cast<uint64_t>(result.js_mask) | static_cast<uint64_t>(js->second));
            if (!value) {
                result.unsupported_js = static_cast<compat::JSFeature>(static_cast<uint64_t>(result.unsupported_js) | static_cast<uint64_t>(js->second));
            }
        } else {
            auto css = compat::StringToCSSFeature.find(std::string_view(key));
            if (css != compat::StringToCSSFeature.end()) {
                result.css_mask = static_cast<compat::CSSFeature>(static_cast<uint16_t>(result.css_mask) | static_cast<uint16_t>(css->second));
                if (!value) {
                    result.unsupported_css = static_cast<compat::CSSFeature>(static_cast<uint16_t>(result.unsupported_css) | static_cast<uint16_t>(css->second));
                }
            } else {
                log.AddError(nullptr, logger::Range{},
                    logger::FormatMsg(logger::MsgCat::kAPI_NotValidFeatureName, quoted_for_log(key)));
            }
        }
    }
    return result;
}

// Parses a dotted name into the chain of identifiers it is made of. The text is
// run through the real parser as a synthetic source file, so a name is only
// accepted if it would be legal as code. An empty string is not an error: it
// simply means the option was left unset, and an empty result comes back.
//
// Input:  "MyNamespace.Thing"   =>  {"MyNamespace", "Thing"}
// Input:  ""                    =>  {}
// Input:  "1nvalid"              =>  a parse error in "log" and {}
static std::vector<std::string> validate_global_name(logger::Log& log, const std::string& text, const std::string& path) {
    if (!text.empty()) {
        logger::Source source;
        source.pretty_paths = logger::PrettyPaths{path, path};
        source.key_path = logger::Path{path};
        source.contents = text;

        auto [result, ok] = javascript::ParseGlobalName(log, source);
        if (ok) {
            return result;
        }
    }
    return {};
}

// Checks a regular expression option by compiling it once, then discarding the
// compiled object. The point is not the result but the validation: a pattern
// that does not compile would otherwise fail much later, once per file, in the
// middle of a build. An unset option comes back as nullopt rather than as an
// empty pattern, so the caller can leave the corresponding feature off.
//
// Input:  "^[a-z]+$", what = "mangle props"  =>  the same string back
// Input:  "[unclosed", what = "mangle props"  =>  a log error and nullopt
// Input:  ""                                  =>  nullopt, no error
static std::optional<std::string> validate_regex(logger::Log& log, const std::string& what, const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        std::regex pattern(value, std::regex::ECMAScript);
        (void)pattern;
    } catch (const std::regex_error&) {
        log.AddError(nullptr, logger::Range{},
            logger::FormatMsg(logger::MsgCat::kAPI_NotValidRegexp, quoted_for_log(what), value));
        return std::nullopt;
    }
    return std::optional<std::string>(value);
}

// Makes a caller-supplied path absolute against the working directory, and
// reports the ones that cannot be. Doing this once, up front, is what lets the
// rest of the pipeline compare paths by string equality without worrying about
// what the caller's current directory happened to be.
//
// An empty option is a normal case, not an error: it means the option was left
// out, so an empty string comes back and the corresponding feature stays off.
//
// Input:  "dist/app.js"  =>  "/project/dist/app.js"
// Input:  ""             =>  "", no error
// Input:  a path that cannot be made absolute
//                        =>  a log error naming "path_kind" and the path, and ""
static std::string validate_path(logger::Log& log, filesystem::Fs& fs, const std::string& rel_path, const std::string& path_kind) {
    if (rel_path.empty()) {
        return "";
    }
    std::optional<std::string> abs_path = fs.Abs(rel_path);
    if (!abs_path) {
        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_InvalidPath, path_kind, rel_path));
    }
    return abs_path.value_or("");
}

// Splits the list of external specifiers into the two match tables the
// resolver consults. The split is by stage, not by kind: "before" applies to
// the text as it was written in the source, and "after" applies to whatever the
// resolver made of it. A host that wants to exclude a file has to be able to
// say so for both spellings, which is why each entry usually lands in both
// tables.
//
// Bare package names get a trailing-slash pattern on the "before" side, since
// "left-pad" as a specifier should cover "left-pad/index.js" without also
// catching a package whose name merely starts with those letters.
//
// A specifier with two wildcards is reported and dropped: there is no sensible
// reading of a pattern like "a*b*c" as a single prefix and suffix.
//
// Input:  {"react", "./vendor/*.js"}
// Output: before: exact {"react"}, pattern {"left-pad/"-style} entries;
//         after: exact {"/project/vendor/foo.js"} for the resolved path and a
//         "/project/vendor/*.js" pattern
static config::ExternalSettings validate_externals(logger::Log& log, filesystem::Fs& fs, const std::vector<std::string>& paths) {
    config::ExternalSettings result;
    result.PreResolve.Exact = std::unordered_map<std::string, bool>{};
    result.PostResolve.Exact = std::unordered_map<std::string, bool>{};

    for (const std::string& path : paths) {
        size_t index = path.find('*');
        if (index != std::string::npos) {
            // A single wildcard becomes a prefix and a suffix match. A package
            // name has no absolute form to match against, so for those only the
            // "before" side can be built.
            if (path.find('*', index + 1) != std::string::npos) {
                log.AddError(nullptr, logger::Range{},
                    logger::FormatMsg(logger::MsgCat::kAPI_MoreThanOneWildcard, quoted_for_log(path)));
            } else {
                result.PreResolve.Patterns.push_back(config::WildcardPattern{path.substr(0, index), path.substr(index + 1)});
                if (!resolver::IsPackagePath(path)) {
                    std::string abs_path = validate_path(log, fs, path, "external path");
                    if (!abs_path.empty()) {
                        size_t abs_index = abs_path.find('*');
                        if (abs_index != std::string::npos && abs_path.find('*', abs_index + 1) == std::string::npos) {
                            result.PostResolve.Patterns.push_back(config::WildcardPattern{abs_path.substr(0, abs_index), abs_path.substr(abs_index + 1)});
                        }
                    }
                }
            }
        } else {
            // No wildcard: an exact match on the specifier, plus a package-name
            // entry that also covers everything underneath it.
            result.PreResolve.Exact[path] = true;
            if (resolver::IsPackagePath(path)) {
                result.PreResolve.Patterns.push_back(config::WildcardPattern{path + "/", ""});
            } else {
                std::string abs_path = validate_path(log, fs, path, "external path");
                if (!abs_path.empty()) {
                    result.PostResolve.Exact[abs_path] = true;
                }
            }
        }
    }

    return result;
}

// Validates the alias table, where each key is a specifier to rewrite and each
// value is what to rewrite it to.
//
// A key has to be a bare module specifier: no leading dot, no leading
// separator, and no interior "." or ".." segments, and no trailing separator.
// Those rules are checked by cleaning the key and requiring the result to be
// byte-identical to the input, which rejects every unusable form in one step
// without needing a list of exceptions. An empty value is always rejected,
// since rewriting something to nothing would silently delete the import.
//
// Keys are accepted in either separator style, but only the forward-slash form
// is stored, so a host that builds paths on either platform can use one table.
//
// Accepted keys:
//   "foo", "foo/bar", "@foo", "@foo/bar", "@foo/bar/baz"
//
// Rejected keys:
//   "./foo", "../foo", "/foo", "C:\\foo", ".foo", "foo/", "@foo/", "foo/../bar"
//
// Input:  {"react": "preact/compat"}
// Output: the same pair, accepted; one log error per rejected key
static std::unordered_map<std::string, std::string> validate_alias(logger::Log& log, filesystem::Fs& fs, const std::unordered_map<std::string, std::string>& alias) {
    std::unordered_map<std::string, std::string> valid;

    for (auto& [old_path, new_path] : alias) {
        if (new_path.empty()) {
            log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_InvalidAliasSubstitution, quoted_for_log(new_path)));
            continue;
        }

        // The clean-path round trip is the whole check: anything that survives
        // being cleaned unchanged is a bare specifier, and anything else is not.
        filesystem::GoFilepath clean_path;
        std::string slashed = old_path;
        std::replace(slashed.begin(), slashed.end(), '\\', '/');
        if (!old_path.empty() && old_path[0] != '.' && old_path[0] != '/' && !fs.IsAbs(old_path) &&
            clean_path.Clean(slashed) == old_path) {
            valid[old_path] = new_path;
            continue;
        }

        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_InvalidAliasName, quoted_for_log(old_path)));
    }

    return valid;
}

// What counts as a usable file extension for an option value: at least a dot
// and a character, and a dot that is not also the last character. That rejects
// "" , ".", and "js" in one comparison.
//
// Input:  ".js"  => true
// Input:  "."    => false, because the last character is the dot
// Input:  "js"   => false, because there is no leading dot
static bool is_valid_extension(const std::string& ext) {
    return ext.size() >= 2 && ext[0] == '.' && ext[ext.size() - 1] != '.';
}

// Validates the ordered list of extensions the resolver tries when a specifier
// has none. Order is significant — the first file that exists wins — so the
// caller's list is passed through untouched, and a malformed entry is reported
// without removing it, to keep the position of every other entry intact.
//
// An empty list means the caller wants Guchho's own order, which is returned
// rather than treated as an error.
//
// Input:  {}                        =>  Guchho's own six-entry default order
// Input:  {".mjs", ".bad"}          =>  the list unchanged, plus one log error
static std::vector<std::string> validate_resolve_extensions(logger::Log& log, const std::vector<std::string>& order) {
    if (order.empty()) {
        return {".tsx", ".ts", ".jsx", ".js", ".css", ".json"};
    }
    for (const std::string& ext : order) {
        if (!is_valid_extension(ext)) {
            log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_InvalidFileExtension, quoted_for_log(ext)));
        }
    }
    return order;
}

// Builds the extension-to-loader table: Guchho's defaults, with the caller's
// entries laid over the top. Starting from the defaults rather than from an
// empty table means a host can override one extension without having to restate
// the other twenty.
//
// An extension with a bad shape is reported, and the entry is still stored, so
// a caller who meant ".mjs" and wrote "mjs" sees both the diagnostic and the
// loader that will actually be used.
//
// Input:  {".mjs": kJS, "svg": kText}
// Output: the default table with ".mjs" mapped to kJS and "svg" mapped to kText,
//         plus one log error about "svg"
static std::unordered_map<std::string, config::Loader> validate_loaders(logger::Log& log, const std::unordered_map<std::string, api::Loader>& loaders) {
    std::unordered_map<std::string, config::Loader> result = bundler::DefaultExtensionToLoaderMap();
    for (auto& [ext, loader] : loaders) {
        if (!ext.empty() && !is_valid_extension(ext)) {
            log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_InvalidFileExtension, quoted_for_log(ext)));
        }
        result[ext] = validate_loader(loader);
    }
    return result;
}

// Parses one of the two JSX options — the factory that creates elements or the
// fragment marker. Both are the same kind of thing: a dotted name that the
// output will call, so both use the same parse.
//
// The fragment name is the one exception to the emptiness check, because a
// fragment is legitimately written as a plain identifier. Any other option that
// parses to nothing is reported, since the caller clearly meant to set it.
//
// Input:  "React.createElement", name = "factory"
// Output: a define expression naming that call
//
// Input:  "", name = "factory"       =>  an empty expression, no error
// Input:  "", name = "fragment"      =>  an empty expression, no error
// Input:  "not a name", name = "factory"
//                                    =>  a log error and an empty expression
static config::DefineExpr validate_jsx_expr(logger::Log& log, const std::string& text, const std::string& name) {
    if (!text.empty()) {
        auto [expr, inject_expr] = javascript::ParseDefineExpr(text);
        (void)inject_expr;
        if (!expr.Parts.empty() || (name == "fragment" && expr.HasConstant())) {
            return expr;
        }
        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_InvalidJSX, name, quoted_for_log(text)));
    }
    return config::DefineExpr{};
}

// Builds a lookup key for a define name. The name arrives as a list of
// identifier parts, and what the rest of the pipeline needs is a single opaque
// key, so each part is stored length-prefixed. The length prefix is what makes
// the key unique: without it, {"ab", "c"} and {"a", "bc"} would collide.
//
// The key is an implementation detail — nothing outside this file interprets it
// — so it only has to be consistent within a build.
//
// Input:  {"process", "env", "NODE_ENV"}
// Output: 7 bytes of lengths, then the three names, then "processenvNODE_ENV"
//         as one string
static std::string map_key_for_define(const std::vector<std::string>& parts) {
    std::string result;
    for (const std::string& part : parts) {
        uint32_t n = static_cast<uint32_t>(part.size());
        result.push_back(static_cast<char>(n & 0xFF));
        result.push_back(static_cast<char>((n >> 8) & 0xFF));
        result.push_back(static_cast<char>((n >> 16) & 0xFF));
        result.push_back(static_cast<char>((n >> 24) & 0xFF));
        result += part;
    }
    return result;
}

// Turns the caller's define and pure-name tables into the two structures the
// parsers consume: a set of processed defines, and a list of extra modules that
// have to be injected into every file that uses one.
//
// A define value can take three forms, and the choice of form decides how it is
// stored:
//
//   * A constant or a dotted part chain is inlined directly into the output
//     wherever the name appears, which is the cheap case and by far the common
//     one. {"DEBUG": "false"} becomes a literal in the code.
//   * Anything more complicated — a call, a concatenation, an arrow function —
//     cannot be pasted into the middle of an expression safely. The value is
//     instead treated as a module, imported under a generated name, and the
//     name is rewritten to that import. {"HELPER": "a ? b : c"} works this way.
//   * A value that is neither is reported and dropped.
//
// Three special cases are folded in here rather than left to the caller, because
// they depend on options only this function can see. Browser builds get a
// default value for the conventional environment variable, dropped console
// calls get replaced with a marker the printer understands, and pure names get
// flagged so an unused call can be unwrapped.
//
// The injected modules are returned in a stable order, sorted by define name,
// because they are prepended to every file that needs them: an unstable order
// would make two builds of the same input produce byte-different output.
//
// Input:  define = {"DEBUG": "false", "HELPER": "a ? b : c"}, pure = {"memo"}
// Output: "DEBUG" inlined as the literal false, "HELPER" rewritten to an import
//         of an injected module, "memo" flagged as safely unwrappable, and one
//         entry in the injected list holding the "a ? b : c" module
static std::pair<config::ProcessedDefines, std::vector<config::InjectedDefine>> validate_defines(
    logger::Log& log,
    const std::unordered_map<std::string, std::string>& defines,
    const std::vector<std::string>& pure_fns,
    config::Platform platform,
    bool is_build_api,
    bool minify,
    api::DropFlags drop) {
    // Visit the caller's defines in name order. The map itself has no order, and
    // the injected modules produced below are emitted in this order into every
    // file that needs them, so sorting is what keeps builds reproducible.
    std::vector<std::string> sorted_keys;
    sorted_keys.reserve(defines.size());
    for (auto& [key, value] : defines) {
        sorted_keys.push_back(key);
    }
    std::sort(sorted_keys.begin(), sorted_keys.end());

    std::unordered_map<std::string, config::DefineData> raw_defines;
    std::vector<std::string> node_env_parts{"process", "env", "NODE_ENV"};
    std::string node_env_map_key = map_key_for_define(node_env_parts);
    std::vector<config::InjectedDefine> injected_defines;

    for (const std::string& key : sorted_keys) {
        const std::string& value = defines.at(key);
        std::vector<std::string> key_parts = validate_global_name(log, key, "(define name)");
        if (key_parts.empty()) {
            continue;
        }
        std::string map_key = map_key_for_define(key_parts);

        // Parse the value and see which of the two supported forms it takes.
        auto [define_expr, inject_expr] = javascript::ParseDefineExpr(value);

        // Form one: a constant or a chain of parts, inlined into the output.
        if (define_expr.HasConstant() || !define_expr.Parts.empty()) {
            config::DefineData define;
            define.KeyParts = key_parts;
            define.DefineExprData = std::make_shared<config::DefineExpr>(define_expr);
            raw_defines[map_key] = define;

            // A bare word where a string was probably meant is a common enough
            // slip to be worth a warning, and the message can point at the exact
            // characters in the caller's own declaration.
            if (define_expr.Parts.size() == 1 && map_key == node_env_map_key) {
                logger::MsgData data;
                data.text = logger::FormatMsg(logger::MsgCat::kAPI_DefinedAsIdentifier, quoted_for_log(key), quoted_for_log(value));
                const std::string& part = define_expr.Parts[0];

                // Synthesise a one-line source file that shows the declaration
                // the caller would have written, and point at the value inside
                // it, so the caret and the suggested replacement both land in a
                // sensible place.
                auto location = std::make_shared<logger::MsgLocation>();
                location->file = logger::PrettyPaths{"<go>", "<go>"};
                location->line = 1;
                location->column = 50;
                location->length = static_cast<int>(part.size()) + 2;
                location->line_text = "Define: map[string]string{\"process.env.NODE_ENV\": \"" + part + "\"}";
                location->suggestion = "\"\\\"" + part + "\\\"\"";
                data.location = location;

                logger::Msg msg;
                msg.kind = logger::MsgKind::kWarning;
                msg.data = data;
                log.AddMsgID(logger::MsgID::kJS_SuspiciousDefine, msg);
            }
            continue;
        }

        // Form two: a value too complex to paste in, so it becomes a module
        // that is imported instead. The define now expands to the import's
        // name, and the index into the injected list travels with it.
        if (inject_expr) {
            uint32_t index = static_cast<uint32_t>(injected_defines.size());
            injected_defines.push_back(config::InjectedDefine{
                *inject_expr,
                key,
            });
            injected_defines.back().SourceData.contents = value;

            auto define = std::make_shared<config::DefineExpr>();
            define->InjectedDefineIndex = compiler::Index32::Make(index);

            config::DefineData result;
            result.KeyParts = key_parts;
            result.DefineExprData = define;
            raw_defines[map_key] = result;
            continue;
        }

        // Neither form: the value cannot be represented, so say so and move on.
        log.AddError(nullptr, logger::Range{},
            logger::FormatMsg(logger::MsgCat::kAPI_InvalidDefineValue, value));
    }

    // A browser build gets a default for the conventional environment flag that
    // most UI libraries read, choosing "production" when minifying and
    // "development" otherwise. Without it, those libraries take the
    // development path in a build that was meant for production and ship their
    // development code. Any of the three spellings counts as the caller having
    // set it, so an explicit value always wins.
    if (is_build_api && platform == config::Platform::kBrowser) {
        bool already_defined = false;
        for (const std::vector<std::string>& parts : {
                 std::vector<std::string>{"process"},
                 std::vector<std::string>{"process", "env"},
                 node_env_parts,
             }) {
            if (raw_defines.find(map_key_for_define(parts)) != raw_defines.end()) {
                already_defined = true;
                break;
            }
        }
        if (!already_defined) {
            auto define = std::make_shared<config::DefineExpr>();
            define->Constant = std::make_shared<javascript::EString>(
                javascript::EString{helpers::StringToUTF16(minify ? "production" : "development")});

            config::DefineData result;
            result.KeyParts = node_env_parts;
            result.DefineExprData = define;
            raw_defines[node_env_map_key] = result;
        }
    }

    // When console calls are being dropped, the name is flagged so that each
    // use is replaced with a marker the printer erases, instead of being
    // printed as a bare "undefined" call.
    if (api::Has(drop, api::DropFlags::kDropConsole)) {
        std::vector<std::string> console_parts{"console"};
        std::string console_map_key = map_key_for_define(console_parts);
        config::DefineData define = raw_defines[console_map_key];
        define.KeyParts = console_parts;
        define.Flags = define.Flags | config::DefineFlags::kMethodCallsMustBeReplacedWithUndefined;
        raw_defines[console_map_key] = define;
    }

    for (const std::string& key : pure_fns) {
        std::vector<std::string> key_parts = validate_global_name(log, key, "(pure name)");
        if (key_parts.empty()) {
            continue;
        }
        std::string map_key = map_key_for_define(key_parts);

        // Fold the purity flag into whatever define already occupies this name,
        // so naming something both ways keeps both meanings.
        config::DefineData define = raw_defines[map_key];
        define.KeyParts = key_parts;
        define.Flags = define.Flags | config::DefineFlags::kCallCanBeUnwrappedIfUnused;
        raw_defines[map_key] = define;
    }

    // Folding the table together is the expensive part of setting up defines,
    // and the parsers will all share one result, so it is done once here rather
    // than per file.
    std::vector<config::DefineData> defines_array;
    defines_array.reserve(raw_defines.size());
    for (auto& [key, define] : raw_defines) {
        defines_array.push_back(define);
    }
    return {config::ProcessDefines(defines_array), std::move(injected_defines)};
}

// Expands the caller's per-message log level overrides into the flat table the
// logger wants. Keys are patterns, and one pattern can name several message
// kinds, which is why the expansion is delegated to the logger's own matcher.
// A value that is not a level is reported against the key that carried it, since
// the key is what a caller has to edit to fix it.
//
// Input:  {"css-syntax-error": kSilent}
// Output: every message id that pattern covers mapped to kSilent
static std::unordered_map<logger::MsgID, logger::LogLevel> validate_log_overrides(
    const std::unordered_map<std::string, api::LogLevel>& input, PendingErrors& pending) {
    std::unordered_map<logger::MsgID, logger::LogLevel> output;
    for (auto& [key, value] : input) {
        PendingErrors one;
        const logger::LogLevel level = validate_log_level(value, one);
        if (!one.empty()) {
            // The key belongs in the message rather than the bare value, so the
            // mistake is reported as "this override" and not "some level".
            pending.Add(logger::FormatMsg(logger::MsgCat::kAPI_InvalidLogOverride, key,
                                          static_cast<int>(value)));
            continue;
        }
        logger::StringToMsgIDs(key, level, output);
    }
    return output;
}

// Picks the output extensions for scripts and styles out of a flat map, and
// rejects keys that are neither. Only those two kinds of output can be renamed,
// so a host asking for a third is told so rather than being quietly ignored.
//
// Input:  {".js": ".mjs", ".css": ".min.css"}
// Output: {".mjs", ".min.css"}
// Input:  {".json": ".json5"}  =>  a log error about the key, and {".", ""}
static std::pair<std::string, std::string> validate_output_extensions(logger::Log& log, const std::unordered_map<std::string, std::string>& out_extensions) {
    std::string js;
    std::string css;
    for (auto& [key, value] : out_extensions) {
        if (!is_valid_extension(value)) {
            log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_InvalidOutputExtension, quoted_for_log(value)));
        }
        if (key == ".js") {
            js = value;
        } else if (key == ".css") {
            css = value;
        } else {
            log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_InvalidOutputExtensionValid, quoted_for_log(key)));
        }
    }
    return {js, css};
}

// Extracts the script and style halves of a banner or footer option. Both live
// in one map keyed by output kind, so the same shape is used for text placed
// before the output and text placed after it, and only the two known keys are
// accepted.
//
// Input:  {"js": "/* build */", "css": "/* build */"}
// Output: {"/* build */", "/* build */"}
// Input:  {"wasm": "..."}  =>  a log error naming the option, and {"", ""}
static std::pair<std::string, std::string> validate_banner_or_footer(logger::Log& log, const std::string& name, const std::unordered_map<std::string, std::string>& values) {
    std::string js;
    std::string css;
    for (auto& [key, value] : values) {
        if (key == "js") {
            js = value;
        } else if (key == "css") {
            css = value;
        } else {
            log.AddError(nullptr, logger::Range{},
                logger::FormatMsg(logger::MsgCat::kAPI_InvalidFileType, name, quoted_for_log(key)));
        }
    }
    return {js, css};
}

// Rejects the one option combination that cannot be satisfied. Preserving
// function and class names means keeping the original names, while lowering
// anything configurable means rewriting those same names, so asking for both at
// once asks for two opposite things at the same time.
//
// The check happens after the options are assembled because the answer depends
// on the target that was chosen, which is only known by this point. The message
// names the target so the caller can tell which engine caused the conflict.
//
// Input:  keep_names = true with a target that cannot configure function names
// Output: an error naming the target, with a note explaining the conflict
static void validate_keep_names(logger::Log& log, const config::Options& options) {
    if (options.KeepNames && (static_cast<uint64_t>(options.UnsupportedJSFeatures) &
        static_cast<uint64_t>(compat::JSFeature::kFunctionNameConfigurable)) != 0) {
        std::string where = config::PrettyPrintTargetEnvironment(options.OriginalTargetEnv, options.UnsupportedJSFeatureOverridesMask);
        log.AddErrorWithNotes(nullptr, logger::Range{},
            logger::FormatMsg(logger::MsgCat::kAPI_KeepNamesCannotBeUsed, where),
            std::vector<logger::MsgData>{{
                nullptr,
                nullptr,
                logger::FormatMsg(logger::MsgCat::kAPI_KeepNamesCannotBeUsedNote),
                false,
            }});
    }
}

// Converts one internal message location into the public form. The internal
// location holds both a pretty path for humans and a key path for identity;
// only one of them is published, chosen by the path style the build was
// configured with, so a host that asked for relative log paths does not
// suddenly receive absolute ones inside a message.
//
// A message with no location at all — a configuration problem, say — converts
// to an absent location rather than to a location with empty fields, so a host
// can tell "no position" from "position zero".
//
// Input:  an internal location with file {"/project/a.js", "a.js"}
// Output: a public Location whose file is whichever of the two the path style
//         selected
static std::optional<api::Location> convert_location_to_public(const logger::MsgLocation* loc, logger::PathStyle path_style) {
    if (loc) {
        return api::Location{
            loc->file.Select(path_style),
            loc->namespace_,
            loc->line,
            loc->column,
            loc->length,
            loc->line_text,
            loc->suggestion,
        };
    }
    return std::nullopt;
}

// ===========================================================================
// Messages: the boundary between the public API and the internal log
// ===========================================================================
//
// Guchho reports problems in one internal type and hands them to hosts in
// another. The two are deliberately not the same shape: internally a message
// carries a numeric id and a path pair, while publicly it carries a string id
// and a single path, because that is what a host can act on.
//
// Everything in this section is a translation in one direction or the other,
// and none of it invents or drops information. A message that goes out and comes
// back keeps its id, its text, its position and its notes.
//
// Translation in the outbound direction happens once per build, at the end.
// Translation in the inbound direction happens whenever a host supplies its own
// messages — a plugin returning errors, or a caller asking for a message list
// to be rendered.

// Keeps the boxes alive for the opaque detail payloads that travel on messages
// in the inbound direction.
//
// The internal log stores a raw pointer to a caller-supplied payload rather than
// the payload itself. That keeps the log's own messages small and movable, which
// matters because a build collects thousands of them. In exchange, something has
// to own the payloads, and that is this arena: it holds a shared pointer to each
// box, so the pointer stays valid for as long as the arena does.
//
// One arena is used per conversion batch rather than per message, and it lives
// on the stack next to the messages it belongs to, so the payloads cannot
// outlive the batch they were converted with.
struct DetailArena {
    std::vector<std::shared_ptr<std::any>> details;
};

// Converts a public location into the internal form, filling in the two fields
// the public type does not carry. A message that came from a host has only one
// path, so it is used for both the pretty path and the key path, and an absent
// namespace defaults to the ordinary file one, which is the only kind a host can
// meaningfully be describing.
//
// Input:  Location{file = "a.js", line = 3, column = 5}
// Output: an internal location with file {"a.js", "a.js"} and namespace "file"
static std::shared_ptr<logger::MsgLocation> convert_location_to_internal(
    const std::optional<api::Location>& loc) {
    if (!loc) return nullptr;
    std::string ns = loc->namespace_;
    if (ns.empty()) ns = "file";
    auto out = std::make_shared<logger::MsgLocation>();
    out->file = logger::PrettyPaths{loc->file, loc->file};
    out->namespace_ = std::move(ns);
    out->line = loc->line;
    out->column = loc->column;
    out->length = loc->length;
    out->line_text = loc->line_text;
    out->suggestion = loc->suggestion;
    return out;
}

// Converts a batch of internal messages to the public form, keeping only the
// ones of one severity. Splitting by severity here rather than returning
// everything means a caller cannot accidentally show an error in a list that is
// supposed to contain only warnings.
//
// The message id becomes its string form on the way out, and a message carrying
// an opaque detail payload has that payload copied into the public message, so a
// host that wants to inspect it can without holding a reference to the build.
//
// Input:  an error and a warning, kind = kError
// Output: one Message, holding the error's text, location, notes and detail
static std::vector<api::Message> convert_messages_to_public(
    logger::MsgKind kind,
    const std::vector<logger::Msg>& msgs,
    logger::PathStyle path_style) {
    std::vector<api::Message> filtered;
    for (const logger::Msg& msg : msgs) {
        if (msg.kind != kind) continue;
        std::vector<api::Note> notes;
        for (const logger::MsgData& note : msg.notes) {
            notes.push_back(api::Note{
                note.text,
                convert_location_to_public(note.location.get(), path_style),
            });
        }
        api::Message message;
        message.id = std::string(logger::MsgIDToString(msg.id));
        message.plugin_name = msg.plugin_name;
        message.text = msg.data.text;
        message.location = convert_location_to_public(msg.data.location.get(), path_style);
        message.notes = std::move(notes);
        if (msg.data.user_detail != nullptr) {
            message.detail = *static_cast<std::any*>(msg.data.user_detail);
        }
        filtered.push_back(std::move(message));
    }
    return filtered;
}

// Appends public messages to an existing batch of internal ones, tagging each
// with the severity it was given. Appending rather than replacing lets a plugin
// contribute messages to a batch that already has some, which is what the
// end-of-build callbacks rely on.
//
// The public string id is looked up in the reverse direction, so a host that
// echoes back a message it received earlier gets the same internal message
// rather than an unrecognised one. A detail payload is boxed into the arena and
// referenced by pointer, which is the arrangement the arena exists to support.
//
// Input:  Message{text = "boom", id = ""} with kind = kWarning
// Output: an internal message with the maximum id, the warning kind, and the text
static std::vector<logger::Msg> convert_messages_to_internal(
    std::vector<logger::Msg> msgs,
    logger::MsgKind kind,
    const std::vector<api::Message>& messages,
    DetailArena& arena) {
    for (const api::Message& message : messages) {
        std::vector<logger::MsgData> notes;
        for (const api::Note& note : message.notes) {
            logger::MsgData d;
            d.text = note.text;
            d.location = convert_location_to_internal(note.location);
            notes.push_back(std::move(d));
        }
        logger::Msg msg;
        msg.id = logger::StringToMaximumMsgID(message.id);
        msg.plugin_name = message.plugin_name;
        msg.kind = kind;
        msg.data.text = message.text;
        msg.data.location = convert_location_to_internal(message.location);
        if (message.detail.has_value()) {
            auto boxed = std::make_shared<std::any>(message.detail);
            arena.details.push_back(boxed);
            msg.data.user_detail = boxed.get();
        }
        msg.notes = std::move(notes);
        msgs.push_back(std::move(msg));
    }
    return msgs;
}

// Converts a host's error and warning lists into one internal batch.
//
// The two lists arrive separately because that is how the public API presents
// them, and they are merged into a single batch ordered by message id, with the
// error before the warning for the same id. That ordering is what the log prints
// in, and it is a stable sort so that messages sharing an id keep the order the
// caller gave them — otherwise reordering host messages would shuffle the
// caller's own list for no reason.
//
// An empty pair short-circuits to an empty batch, which is the common case for a
// plugin that has nothing to report.
//
// Input:  errors = {Message{text = "a"}}, warnings = {Message{text = "b"}}
// Output: two internal messages, both carrying the maximum id, the error first
static std::vector<logger::Msg> convert_errors_and_warnings_to_internal(
    const std::vector<api::Message>& errors,
    const std::vector<api::Message>& warnings,
    DetailArena& arena) {
    if (errors.empty() && warnings.empty()) return {};
    std::vector<logger::Msg> msgs;
    msgs.reserve(errors.size() + warnings.size());
    msgs = convert_messages_to_internal(std::move(msgs), logger::MsgKind::kError, errors, arena);
    msgs = convert_messages_to_internal(std::move(msgs), logger::MsgKind::kWarning, warnings, arena);
    std::stable_sort(msgs.begin(), msgs.end(), [](const logger::Msg& a, const logger::Msg& b) {
        if (a.id != b.id) return static_cast<uint8_t>(a.id) < static_cast<uint8_t>(b.id);
        return static_cast<uint8_t>(a.kind) < static_cast<uint8_t>(b.kind);
    });
    return msgs;
}

// Copies a caller's property-mangling cache into a build's own copy. The
// caller keeps theirs, so a host can hand the same cache to several builds, or
// hold on to the last one it received, without any of them observing the
// others' renamings.
//
// Input:  {"someLongName": true}
// Output: an identical map, owned by the build
static std::unordered_map<std::string, bool> clone_mangle_cache(
    const std::unordered_map<std::string, bool>& mangle_cache) {
    return mangle_cache;
}

// Digest of an output file's bytes, for deciding whether anything on disk needs
// rewriting. The eight bytes of a 64-bit hash are base64-encoded without padding,
// which keeps the string short enough to sit in a URL and free of characters that
// would need escaping in one.
//
// This is a change detector, not a security digest: it decides whether a file
// changed, so two files that hash the same are treated as identical, and a
// collision would mean a stale file survives a rebuild.
//
// Input:  the bytes of a generated file
// Output: 11 base64 characters, e.g. "aBcDeFgHiJk"
static std::string hash_for_build_mangling_hash(std::span<const uint8_t> contents) {
    helpers::Xxh64 hasher;
    hasher.Write(contents.data(), contents.size());
    uint64_t sum = hasher.Sum64();
    uint8_t bytes[8];
    for (int i = 0; i < 8; i++) bytes[i] = static_cast<uint8_t>(sum >> (i * 8));
    std::string encoded = helpers::Base64StdEncode({reinterpret_cast<const char*>(bytes), 8});
    while (!encoded.empty() && encoded.back() == '=') encoded.pop_back();
    return encoded;
}

// Renders a byte count the way a person reads a file size: binary units, one
// decimal place, and a trailing space on the smallest unit so that column of a
// size table lines up. The smallest unit keeps a trailing space because it is
// the only one whose width is predictable from its digits.
//
// Input:  0       => "0b "
// Input:  2048    => "2.0kb"
// Input:  5242880 => "5.0mb"
static std::string pretty_print_byte_count(uint64_t n) {
    if (n < 1024) return std::format("{}b ", n);
    if (n < 1024 * 1024) return std::format("{:.1f}kb", static_cast<double>(n) / 1024.0);
    if (n < 1024u * 1024u * 1024u) return std::format("{:.1f}mb", static_cast<double>(n) / (1024.0 * 1024.0));
    return std::format("{:.1f}gb", static_cast<double>(n) / (1024.0 * 1024.0 * 1024.0));
}

// Repeats a one-glyph filler "count" times. The filler is a string rather than a
// char because the rule drawn between columns is a three-byte character, and
// taking its first byte would fill the gap with a run of partial characters that
// no terminal can render.
static std::string repeat_unit(int count, const std::string& unit) {
    if (count <= 0) return "";
    std::string out;
    out.reserve(unit.size() * static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        out += unit;
    }
    return out;
}

// ===========================================================================
// Plugin API
// ===========================================================================
//
// A plugin is an ordinary object in the public API: a name, and a setup function
// that registers callbacks. Guchho owns no plugin type of its own — what the
// build actually runs is a list of internal callbacks with compiled filters —
// so everything in this section is a translation from "a host registered a
// callback" to "the resolver will call this function when a path matches".
//
// The two directions of resolution and loading are the heart of the plugin
// system. A resolve callback decides where a specifier points; a load callback
// decides how a file's bytes are read and interpreted. Both can decline, and a
// decline is not an error — it is how the next plugin, and eventually Guchho's
// own rules, get their turn.

// Maps the public description of a resolve request onto the internal one. The
// public list is longer because it names the language constructs a host cares
// about, while the internal list is what the compiler actually distinguishes
// when it walks an expression.
//
// Input:  api::ResolveKind::kJSImportStatement => compiler::ImportKind::kStmt
// Input:  api::ResolveKind::kCSSComposesFrom     => compiler::ImportKind::kComposesFrom
static compiler::ImportKind resolve_kind_to_import_kind(api::ResolveKind kind) {
    switch (kind) {
        case api::ResolveKind::kEntryPoint: return compiler::ImportKind::kEntryPoint;
        case api::ResolveKind::kJSImportStatement: return compiler::ImportKind::kStmt;
        case api::ResolveKind::kJSRequireCall: return compiler::ImportKind::kRequire;
        case api::ResolveKind::kJSDynamicImport: return compiler::ImportKind::kDynamic;
        case api::ResolveKind::kJSRequireResolve: return compiler::ImportKind::kRequireResolve;
        case api::ResolveKind::kCSSImportRule: return compiler::ImportKind::kAt;
        case api::ResolveKind::kCSSComposesFrom: return compiler::ImportKind::kComposesFrom;
        case api::ResolveKind::kCSSURLToken: return compiler::ImportKind::kUrl;
        default: return compiler::ImportKind::kEntryPoint;
    }
}

// The reverse of resolve_kind_to_import_kind, used when the build asks a
// callback about a request it is currently servicing. An internal kind with no
// public counterpart comes back as "unspecified", which a callback can detect
// and treat as a decline.
//
// Input:  compiler::ImportKind::kRequire   => api::ResolveKind::kJSRequireCall
// Input:  an unrecognised kind            => api::ResolveKind::kNone
static api::ResolveKind import_kind_to_resolve_kind(compiler::ImportKind kind) {
    switch (kind) {
        case compiler::ImportKind::kEntryPoint: return api::ResolveKind::kEntryPoint;
        case compiler::ImportKind::kStmt: return api::ResolveKind::kJSImportStatement;
        case compiler::ImportKind::kRequire: return api::ResolveKind::kJSRequireCall;
        case compiler::ImportKind::kDynamic: return api::ResolveKind::kJSDynamicImport;
        case compiler::ImportKind::kRequireResolve: return api::ResolveKind::kJSRequireResolve;
        case compiler::ImportKind::kAt: return api::ResolveKind::kCSSImportRule;
        case compiler::ImportKind::kComposesFrom: return api::ResolveKind::kCSSComposesFrom;
        case compiler::ImportKind::kUrl: return api::ResolveKind::kCSSURLToken;
        default: return api::ResolveKind::kNone;
    }
}

// The build-side state of one registered plugin: the internal plugin record the
// build will run, plus the log and file system that record validation needs, and
// an arena owning any detail payloads that travel out on the plugin's messages.
//
// The three registration methods below append to the internal lists rather than
// replacing them, because a plugin may register as many callbacks of each kind
// as it likes and they run in registration order — which is also the order the
// host's setup function registered them in, so a plugin that registers two
// resolve callbacks can rely on seeing them in the order it wrote them.
//
// Every registration is fallible in the same way: a bad filter is reported and
// the callback is dropped, leaving the rest of the plugin's registrations intact.
struct PluginImpl {
    logger::Log& log;
    filesystem::Fs& fs;
    config::Plugin plugin;
    DetailArena arena;
    bool is_build_api = false;

    // Makes a plugin-supplied list of paths absolute, reporting the ones that
    // cannot be. A plugin gets the same treatment as the host's own path options
    // for the same reason: from here on, paths are compared as strings, and two
    // spellings of the same file would compare unequal.
    //
    // Input:  {"./src/generated.js"} from plugin "gen"
    // Output: {"/project/src/generated.js"}
    std::vector<std::string> validate_paths_array(const std::vector<std::string>& paths_in, const std::string& name) {
        std::vector<std::string> paths_out;
        if (!paths_in.empty()) {
            std::string path_kind = name + " path for plugin " + quoted_for_log(plugin.Name);
            for (const std::string& rel_path : paths_in) {
                std::string abs_path = validate_path(log, fs, rel_path, path_kind);
                if (!abs_path.empty()) paths_out.push_back(std::move(abs_path));
            }
        }
        return paths_out;
    }

    // Registers a callback that runs once, before any file is read. This is the
    // cheapest place for a plugin to reject a whole configuration, since nothing
    // has been loaded yet and an error here stops the build before it starts.
    //
    // Input:  a callback returning OnStartResult with one error
    // Output: an internal start callback that produces that error before the
    //         first file is read
    void on_start(std::function<api::OnStartResult()> callback) {
        plugin.OnStartList.push_back(config::OnStart{
            [this, cb = std::move(callback)]() -> config::OnStartResult {
                config::OnStartResult result;
                api::OnStartResult response = cb();
                result.Msgs = convert_errors_and_warnings_to_internal(
                    response.errors, response.warnings, arena);
                return result;
            },
            plugin.Name,
        });
    }

    // Registers a resolve callback behind a path filter. The filter is compiled
    // once, here, rather than matched as a string on every import in the graph.
    // An empty filter means "every path", which compiles successfully and matches
    // everything; a filter that does not compile is reported and the callback is
    // not registered, so one bad registration cannot silently take over every
    // import in the build.
    //
    // Input:  filter = "*.svg", namespace_ = "file", callback returning
    //         ResolveResult{path = "/project/icon.svg"}
    // Output: an internal resolve callback invoked for specifiers ending in
    //         ".svg", resolving them through the plugin
    void on_resolve(const api::OnResolveOptions& options,
                    std::function<api::OnResolveResult(const api::OnResolveArgs&)> callback) {
        auto filter = config::CompileFilterForPlugin(plugin.Name, "onResolve", options.filter);
        if (filter == nullptr && !options.filter.empty()) {
            log.AddError(nullptr, logger::Range{},
                logger::FormatMsg(logger::MsgCat::kAPI_InvalidPluginFilterOnResolve, quoted_for_log(options.filter), quoted_for_log(plugin.Name)));
            return;
        }
        if (filter == nullptr) return;

        std::string ns = options.namespace_;
        auto on_resolve_cb = [this, cb = std::move(callback), plugin_name = plugin.Name, ns_str = ns](
                config::OnResolveArgs args) -> config::OnResolveResult {
                config::OnResolveResult result;
                api::OnResolveArgs public_args;
                public_args.path = args.PathData;
                public_args.importer = args.Importer.text;
                public_args.namespace_ = args.Importer.namespace_;
                public_args.resolve_dir = args.ResolveDir;
                public_args.kind = import_kind_to_resolve_kind(args.Kind);
                public_args.plugin_data = args.PluginData;
                public_args.with = args.With.DecodeIntoMap();

                api::OnResolveResult response;
                try {
                    response = cb(public_args);
                } catch (const std::exception& e) {
                    result.ThrownError = e.what();
                    return result;
                } catch (...) {
                    result.ThrownError = "Unknown error";
                    return result;
                }

                result.PluginName = response.plugin_name.empty() ? plugin_name : response.plugin_name;
                result.AbsWatchFiles = validate_paths_array(response.watch_files, "watch file");
                result.AbsWatchDirs = validate_paths_array(response.watch_dirs, "watch directory");

                if (!response.suffix.empty() && response.suffix[0] != '?' && response.suffix[0] != '#') {
                    result.ThrownError = "Invalid path suffix " + quoted_for_log(response.suffix) +
                        " returned from plugin (must start with \"?\" or \"#\")";
                    return result;
                }

                result.ResultPath = logger::Path{
                    .text = std::move(response.path),
                    .namespace_ = std::move(response.namespace_),
                    .ignored_suffix = std::move(response.suffix),
                };
                result.External = response.external;
                result.IsSideEffectFree = (response.side_effects == api::SideEffects::kFalse);
                result.PluginData = std::move(response.plugin_data);

                result.Msgs = convert_errors_and_warnings_to_internal(
                    response.errors, response.warnings, arena);

                if (result.ResultPath.text.empty() && !result.External) {
                    std::string what;
                    if (!response.namespace_.empty()) what = "Namespace";
                    else if (!response.suffix.empty()) what = "Suffix";
                    else if (response.plugin_data.has_value()) what = "PluginData";
                    else if (!response.watch_files.empty()) what = "WatchFiles";
                    else if (!response.watch_dirs.empty()) what = "WatchDirs";
                    if (!what.empty()) {
                        result.Msgs.push_back(logger::Msg{
                            .data = logger::MsgData{
                                .text = "Returning " + quoted_for_log(what) + " doesn't do anything when " + quoted_for_log("Path") + " is empty",
                            },
                            .kind = logger::MsgKind::kWarning,
                        });
                    }
                }
                return result;
            };
        plugin.OnResolveList.push_back(config::OnResolve{
            std::move(*filter),
            std::move(on_resolve_cb),
            plugin.Name,
            std::move(ns),
        });
    }

    // Registers a load callback behind a path filter. The filter is the same
    // path pattern a resolve callback takes, compiled once here; a bad filter is
    // reported and the registration is dropped.
    //
    // A load callback may return file contents it did not read from disk, which
    // is how a plugin serves a virtual file. It may also override the loader for
    // the file, so a callback can return plain text and have it compiled, or
    // return a stylesheet and have it treated as one, regardless of extension.
    //
    // Input:  filter = "virtual:*", callback returning OnLoadResult with
    //         contents = "export default 1" and loader = kJS
    // Output: an internal load callback that intercepts matching paths and
    //         supplies those contents under the script loader
    void on_load(const api::OnLoadOptions& options,
                 std::function<api::OnLoadResult(const api::OnLoadArgs&)> callback) {
        auto filter = config::CompileFilterForPlugin(plugin.Name, "onLoad", options.filter);
        if (filter == nullptr && !options.filter.empty()) {
            log.AddError(nullptr, logger::Range{},
                logger::FormatMsg(logger::MsgCat::kAPI_InvalidPluginFilterOnLoad, quoted_for_log(options.filter), quoted_for_log(plugin.Name)));
            return;
        }
        if (filter == nullptr) return;

        std::string ns = options.namespace_;
        auto on_load_cb = [this, cb = std::move(callback), ns_str = ns](
                config::OnLoadArgs args) -> config::OnLoadResult {
                config::OnLoadResult result;
                api::OnLoadArgs public_args;
                public_args.path = args.LoadPath.text;
                public_args.namespace_ = args.LoadPath.namespace_;
                public_args.plugin_data = args.PluginData;
                public_args.suffix = args.LoadPath.ignored_suffix;
                public_args.with = args.LoadPath.import_attributes.DecodeIntoMap();

                api::OnLoadResult response;
                try {
                    response = cb(public_args);
                } catch (const std::exception& e) {
                    result.ThrownError = e.what();
                    return result;
                } catch (...) {
                    result.ThrownError = "Unknown error";
                    return result;
                }

                result.PluginName = response.plugin_name;
                result.AbsWatchFiles = validate_paths_array(response.watch_files, "watch file");
                result.AbsWatchDirs = validate_paths_array(response.watch_dirs, "watch directory");

                if (response.contents.has_value()) result.Contents = response.contents;
                result.ResultLoader = validate_loader(response.loader);
                result.PluginData = std::move(response.plugin_data);

                std::string path_kind = "resolve directory path for plugin " + quoted_for_log(plugin.Name);
                std::string abs_path = validate_path(log, fs, response.resolve_dir, path_kind);
                if (!abs_path.empty()) result.AbsResolveDir = abs_path;

                result.Msgs = convert_errors_and_warnings_to_internal(
                    response.errors, response.warnings, arena);
                return result;
            };
        plugin.OnLoadList.push_back(config::OnLoad{
            std::move(*filter),
            std::move(on_load_cb),
            plugin.Name,
            std::move(ns),
        });
    }
};

// A plugin's end-of-build callback, held separately from the other registrations
// because it runs at a different time: after the output exists, with the result
// still open for the callback to inspect and add to.
struct OnEndCallback {
    std::string plugin_name;
    std::function<api::OnEndResult(api::BuildResult&)> fn;
};

// Runs every registered plugin's setup function, in order, and collects the
// internal callbacks they append.
//
// The host's options are passed by pointer because a plugin's setup function is
// allowed to read them — that is how a plugin learns the output format, the
// working directory and everything else it was not explicitly told. Setup runs
// before options are validated, which is deliberate: a plugin's own registered
// callbacks participate in validation, and its view of the options is the raw
// public form.
//
// Plugins with no name are reported and skipped, because a nameless plugin could
// not be identified in any message it produced.
//
// Each plugin is handed a facade — resolve, on_start, on_end, on_resolve, on_load,
// on_dispose — that forwards to the corresponding registration above. The facade
// is per plugin and owned by a shared pointer, because the callbacks it creates
// capture it and must stay valid for as long as the build runs.
//
// Input:  one plugin named "svg" whose setup registers a resolve callback for
//         "*.svg"
// Output: one internal plugin record holding that callback, ready for the
//         resolver to run
static void load_plugins(
    api::BuildOptions* initial_options,
    filesystem::Fs& real_fs,
    logger::Log& log,
    cache::CacheSet& caches,
    std::vector<OnEndCallback>& on_end_callbacks,
    std::vector<std::function<void()>>& on_dispose_callbacks,
    std::vector<std::shared_ptr<PluginImpl>>& plugin_impls,
    std::function<void(config::Options&)>& finalize_build_options) {

    std::vector<api::Plugin> clone;
    clone.reserve(initial_options->plugins.size());
    for (const auto& p : initial_options->plugins) clone.push_back(p);

    config::Options* options_for_resolve = nullptr;
    std::vector<config::Plugin> plugins;

    finalize_build_options = [&](config::Options& options) {
        options.Plugins = plugins;
        options_for_resolve = &options;
    };

    for (size_t i = 0; i < clone.size(); i++) {
        api::Plugin& item = clone[i];
        if (item.name.empty()) {
            log.AddError(nullptr, logger::Range{},
                logger::FormatMsg(logger::MsgCat::kAPI_PluginMissingName, std::to_string(i)));
            continue;
        }

        auto impl = std::make_shared<PluginImpl>(PluginImpl{
            log, real_fs, config::Plugin{item.name}, DetailArena{}, true});
        plugin_impls.push_back(impl);

        auto resolve_fn = [&real_fs, &caches, impl, &item, initial_options,
                           options_for_resolve_ptr = &options_for_resolve,
                           &plugins](const std::string& path,
                                     const api::ResolveOptions& options) -> api::ResolveResult {
            if (*options_for_resolve_ptr == nullptr) {
                return api::ResolveResult{
                    .errors = {api::Message{
                        .id = {},
                        .plugin_name = {},
                        .text = "Cannot call \"resolve\" before plugin setup has completed",
                        .location = std::nullopt,
                        .notes = {},
                        .detail = {},
                    }},
                    .warnings = {},
                    .path = {},
                    .external = {},
                    .side_effects = {},
                    .namespace_ = {},
                    .suffix = {},
                    .plugin_data = {},
                };
            }
            if (options.kind == api::ResolveKind::kNone) {
                return api::ResolveResult{
                    .errors = {api::Message{
                        .id = {},
                        .plugin_name = {},
                        .text = "Must specify \"kind\" when calling \"resolve\"",
                        .location = std::nullopt,
                        .notes = {},
                        .detail = {},
                    }},
                    .warnings = {},
                    .path = {},
                    .external = {},
                    .side_effects = {},
                    .namespace_ = {},
                    .suffix = {},
                    .plugin_data = {},
                };
            }

            PendingErrors pending;
            logger::Log resolve_log = logger::NewDeferLog(
                logger::DeferLogKind::kDeferLogNoVerboseOrDebug,
                validate_log_overrides(initial_options->log_override, pending));
            pending.ReportTo(resolve_log);

            config::Options options_clone = **options_for_resolve_ptr;
            auto resolver = resolver::NewResolver(
                config::APICall::kBuildCall, real_fs, resolve_log, caches, &options_clone);

            std::string abs_resolve_dir = validate_path(resolve_log, real_fs, options.resolve_dir, "resolve directory");
            if (resolve_log.has_errors()) {
                auto msgs = resolve_log.done();
                api::ResolveResult result;
                result.errors = convert_messages_to_public(logger::MsgKind::kError, msgs, options_clone.LogPathStyle);
                result.warnings = convert_messages_to_public(logger::MsgKind::kWarning, msgs, options_clone.LogPathStyle);
                return result;
            }

            compiler::ImportKind kind = resolve_kind_to_import_kind(options.kind);
            logger::ImportAttributes attrs = logger::EncodeImportAttributes(options.with);

            auto outcome = bundler::RunOnResolvePlugins(
                plugins, resolver.get(), resolve_log, real_fs, caches.fs_cache,
                nullptr, logger::Range{},
                logger::Path{.text = options.importer, .namespace_ = options.namespace_},
                path, attrs, kind, abs_resolve_dir,
                options.plugin_data, options_clone.LogPathStyle);

            auto msgs = resolve_log.done();
            api::ResolveResult result;
            result.errors = convert_messages_to_public(logger::MsgKind::kError, msgs, options_clone.LogPathStyle);
            result.warnings = convert_messages_to_public(logger::MsgKind::kWarning, msgs, options_clone.LogPathStyle);

            if (outcome.resolve_result.has_value()) {
                const auto& rr = *outcome.resolve_result;
                result.path = rr.path_pair.primary.text;
                result.external = rr.path_pair.is_external;
                result.side_effects = (rr.primary_side_effects_data == nullptr);
                result.namespace_ = rr.path_pair.primary.namespace_;
                result.suffix = rr.path_pair.primary.ignored_suffix;
                result.plugin_data = rr.plugin_data;
            } else if (result.errors.empty()) {
                std::string plugin_name = item.name;
                if (!options.plugin_name.empty()) plugin_name = options.plugin_name;
                auto info = bundler::ResolveFailureErrorTextSuggestionNotes(
                    *resolver, path, kind, plugin_name, real_fs, abs_resolve_dir,
                    options_for_resolve_ptr ? (*options_for_resolve_ptr)->OutputPlatform : config::Platform::kBrowser,
                    logger::PrettyPaths{}, "", options_clone.LogPathStyle);
                api::Message msg;
                msg.text = info.text;
                for (const auto& n : info.notes) {
                    api::Note note;
                    note.text = n.text;
                    msg.notes.push_back(std::move(note));
                }
                result.errors.push_back(std::move(msg));
            }
            return result;
        };

        auto on_end_fn = [&on_end_callbacks, &item](std::function<api::OnEndResult(api::BuildResult&)> fn) {
            on_end_callbacks.push_back(OnEndCallback{item.name, std::move(fn)});
        };

        auto on_dispose_fn = [&on_dispose_callbacks](std::function<void()> fn) {
            on_dispose_callbacks.push_back(std::move(fn));
        };

        api::PluginBuild build{
            .initial_options = initial_options,
            .resolve = std::move(resolve_fn),
            .on_start = [impl](std::function<api::OnStartResult()> cb) { impl->on_start(std::move(cb)); },
            .on_end = std::move(on_end_fn),
            .on_resolve = [impl](const api::OnResolveOptions& opts, std::function<api::OnResolveResult(const api::OnResolveArgs&)> cb) { impl->on_resolve(opts, std::move(cb)); },
            .on_load = [impl](const api::OnLoadOptions& opts, std::function<api::OnLoadResult(const api::OnLoadArgs&)> cb) { impl->on_load(opts, std::move(cb)); },
            .on_dispose = std::move(on_dispose_fn),
        };

        item.setup(build);
        plugins.push_back(std::move(impl->plugin));
    }
}

// ===========================================================================
// Build sessions: one context, many rebuilds
// ===========================================================================
//
// Everything from here to the end of the Transform section is about running
// builds rather than describing them. A build comes in three layers:
//
//   * The arguments, assembled once when a session is created and unchanged
//     afterwards. Everything expensive that does not depend on file contents
//     lives here.
//   * One pass over the graph, run per rebuild, producing a result and the set
//     of files and directories that pass touched.
//   * The session, which owns the arguments, serialises concurrent rebuilds,
//     feeds watch mode, and can be cancelled part-way through.
//
// Forward declaration because plugin setup above needs the validated options
// only after the plugins have registered against them.
static std::pair<std::pair<config::Options, std::vector<config::EntryPoint>>,
                  std::pair<std::shared_ptr<config::ProcessedDefines>, std::optional<config::StdinInfo>>>
validate_build_options(api::BuildOptions& build_opts, logger::Log& log, filesystem::Fs& real_fs);

// The immutable half of a build: everything decided once when the session was
// created. The two owner members are what keep the pointers inside the validated
// options alive — the processed define table and the in-memory entry point are
// held by shared and optional owners respectively, because the options only hold
// references to them.
//
// "write" is recorded rather than inferred from the options, because whether this
// build touches the disk is a property of the session, not of the build itself.
struct RebuildArgs {
    std::shared_ptr<cache::CacheSet> caches;
    std::vector<OnEndCallback> on_end_callbacks;
    std::vector<std::function<void()>> on_dispose_callbacks;
    logger::OutputOptions log_options;
    std::vector<logger::Msg> log_warnings;
    std::vector<config::EntryPoint> entry_points;
    config::Options options;
    std::unordered_map<std::string, bool> mangle_cache;
    std::string abs_working_dir;
    bool write = false;
    std::shared_ptr<config::ProcessedDefines> defines_owner;
    std::optional<config::StdinInfo> stdin_owner;
};

// What one pass produced: the public result, the files and directories that pass
// read, and a copy of the options as they were when the pass ran. The options
// travel with the result because watch mode and the end-of-build callbacks may
// change them between passes, and a caller holding an old result should still
// see the options that produced it.
struct RebuildState {
    api::BuildResult result;
    filesystem::WatchData watch_data;
    config::Options options;
};

// A build that is currently running, shared between the thread doing the work
// and every thread waiting on it.
//
// Waiting is done on this object rather than on the session so that a second
// caller asking for a rebuild while one is in flight joins the build already
// running instead of starting a competing one. That is what makes concurrent
// callers safe without a queue: the second caller is a participant, not a
// competitor.
//
// The cancel flag lives here too, because it is the one piece of state a waiting
// thread may need to reach — Cancel() signals the running build and then waits
// here for it to finish.
struct BuildInProgress {
    std::shared_ptr<config::CancelFlag> cancel;
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    RebuildState state;
};

// One pass over the graph: scan, compile, write, collect messages.
//
// The pass is self-contained. It takes the already-validated arguments, produces
// a result, and touches nothing the caller owns except the caches it was given,
// which is why a session can run as many passes as it likes without any of them
// depending on the last.
//
// Writing is deliberately separated from producing. Output files are always
// gathered into the result, whether or not they will be written, so a host that
// asked not to write still gets the bytes. When writing is enabled, a file is
// only touched if its contents actually differ from what is already there, and
// a file that this pass no longer produces is deleted — that is what keeps a
// renamed entry point from leaving its old output behind.
//
// The name-mangling cache is returned only on success. A failed build may have
// renamed things inconsistently part-way through, and handing that back would
// make the next build reuse renamings that no longer describe its own output.
//
// Finally the end-of-build callbacks run, in registration order, against a
// result they may add messages to. A callback that reports an error stops the
// chain: the remaining callbacks are not run, and the build is a failure.
//
// Input:  validated options, two entry points, write = true, and the hashes of
//         the previous pass
// Output: a RebuildState whose result holds the emitted files, the metafile when
//         one was requested, and every message; plus the watch data for the
//         files that were read
static RebuildState rebuild_impl(const RebuildArgs& args,
                                  const std::unordered_map<std::string, std::string>& old_hashes) {
    logger::Log log = logger::NewStderrLog(args.log_options);

    for (const logger::Msg& msg : args.log_warnings) {
        log.add_msg(msg);
    }

    std::string fs_error;
    std::unique_ptr<filesystem::Fs> real_fs = filesystem::MakeRealFS({
        .abs_working_dir = args.abs_working_dir,
        .want_watch_data = args.options.WatchMode,
    }, fs_error);

    api::BuildResult result;
    filesystem::WatchData watch_data;
    std::unordered_map<std::string, std::string> new_hashes = old_hashes;

    helpers::Timer timer{"build"};

    auto bundle = bundler::ScanBundle(
        config::APICall::kBuildCall, log, *real_fs, *args.caches,
        args.entry_points, args.options, &timer);
    watch_data = real_fs->GetWatchData();

    if (!log.has_errors()) {
        auto [results, metafile] = bundle.Compile(log, timer, const_cast<std::unordered_map<std::string, bool>&>(args.mangle_cache));

        if (args.options.CancelFlagData && args.options.CancelFlagData->DidCancel()) {
            log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_BuildCanceled));
        }

        if (!log.has_errors()) {
            result.metafile = metafile;
            new_hashes.clear();
            result.output_files.reserve(results.size());
            for (auto& item : results) {
                if (args.options.WriteToStdout) item.abs_path = "<stdout>";
                std::string hash = hash_for_build_mangling_hash(item.contents);
                new_hashes[item.abs_path] = hash;
                result.output_files.push_back(api::OutputFile{
                    .path = item.abs_path,
                    .contents = std::vector<uint8_t>(item.contents.begin(), item.contents.end()),
                    .hash = std::move(hash),
                });
            }
        }
    }

    if (args.write) {
        timer.Begin("Write output files");
        if (args.options.WriteToStdout) {
            if (!log.has_errors()) {
                if (result.output_files.size() != 1) {
                    log.AddError(nullptr, logger::Range{},
                        logger::FormatMsg(logger::MsgCat::kAPI_UnexpectedFileCountWhenWritingToStdout, std::to_string(result.output_files.size())));
                }
            }
        } else {
            auto real_fs2 = filesystem::MakeRealFS({
                .abs_working_dir = args.abs_working_dir,
            }, fs_error);
            if (!real_fs2) {
                log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_FailedToCreateRealFS));
            } else {
                std::vector<std::string> to_delete;
                for (auto& [abs_path, _] : old_hashes) {
                    if (new_hashes.find(abs_path) == new_hashes.end()) {
                        to_delete.push_back(abs_path);
                    }
                }

                for (const auto& output : result.output_files) {
                    filesystem::BeforeFileOpen();
                    std::string dir_error;
                    std::string dir = real_fs2->Dir(output.path);
                    if (dir != output.path) {
                        filesystem::MkdirAll(*real_fs2, dir, dir_error);
                        if (!dir_error.empty()) {
                            log.AddError(nullptr, logger::Range{},
                                logger::FormatMsg(logger::MsgCat::kAPI_FailedToCreateOutputDirectory, dir_error));
                        } else {
                            auto existing = real_fs2->ReadFile(output.path);
                            if (!existing.Ok() || existing.value.size() != output.contents.size() ||
                                std::memcmp(existing.value.data(), output.contents.data(), output.contents.size()) != 0) {
                                std::ofstream ofs(filesystem::PathFromUTF8(output.path), std::ios::binary);
                                if (ofs.is_open()) {
                                    ofs.write(reinterpret_cast<const char*>(output.contents.data()),
                                              static_cast<std::streamsize>(output.contents.size()));
                                } else {
                                    log.AddError(nullptr, logger::Range{},
                                        logger::FormatMsg(logger::MsgCat::kAPI_FailedToWriteOutputFile, output.path));
                                }
                            }
                        }
                    }
                    filesystem::AfterFileClose();
                }

                for (const std::string& abs_path : to_delete) {
                    filesystem::BeforeFileOpen();
                    std::error_code ec;
                    std::filesystem::remove(filesystem::PathFromUTF8(abs_path), ec);
                    filesystem::AfterFileClose();
                }
            }
        }
        timer.End("Write output files");
    }

    if (log.has_errors()) {
        result.mangle_cache.clear();
    } else {
        result.mangle_cache = args.mangle_cache;
    }

    auto msgs = log.peek();
    result.errors = convert_messages_to_public(logger::MsgKind::kError, msgs, args.options.LogPathStyle);
    result.warnings = convert_messages_to_public(logger::MsgKind::kWarning, msgs, args.options.LogPathStyle);

    timer.Begin("On-end callbacks");
    DetailArena on_end_arena;
    for (const auto& on_end : args.on_end_callbacks) {
        api::OnEndResult from_plugin;
        try {
            from_plugin = on_end.fn(result);
        } catch (const std::exception& e) {
            from_plugin.errors.push_back(api::Message{
                .id = {},
                .plugin_name = on_end.plugin_name,
                .text = e.what(),
                .location = std::nullopt,
                .notes = {},
                .detail = {},
            });
        }

        for (auto& m : from_plugin.errors) {
            if (m.plugin_name.empty()) m.plugin_name = on_end.plugin_name;
        }
        for (auto& m : from_plugin.warnings) {
            if (m.plugin_name.empty()) m.plugin_name = on_end.plugin_name;
        }

        for (auto& msg : convert_errors_and_warnings_to_internal(
                 from_plugin.errors, from_plugin.warnings, on_end_arena)) {
            log.add_msg(msg);
        }

        result.errors.insert(result.errors.end(), from_plugin.errors.begin(), from_plugin.errors.end());
        result.warnings.insert(result.warnings.end(), from_plugin.warnings.begin(), from_plugin.warnings.end());

        if (!from_plugin.errors.empty()) break;
    }
    timer.End("On-end callbacks");

    if (args.options.CancelFlagData && args.options.CancelFlagData->DidCancel() && result.errors.empty()) {
        result.errors.push_back(api::Message{
            .id = {},
            .plugin_name = {},
            .text = "The build was canceled",
            .location = std::nullopt,
            .notes = {},
            .detail = {},
        });
    }

    return RebuildState{
        std::move(result),
        std::move(watch_data),
        args.options,
    };
}

// Prints the size table a build ends with: one row per output file, showing the
// path relative to the working directory, the size, and the byte count.
//
// Paths are made relative to the current directory here rather than at build
// time, because the summary is printed after the build and the caller's current
// directory is whatever it is at that moment. A negative elapsed time means the
// caller has no timing to report, in which case the duration column is omitted
// rather than shown as zero.
//
// Input:  two output files of 2048 and 512 bytes, elapsed_ms = 12.5
// Output: two rows ending in "2.0kb 2048" and "512b 512", and a duration
//         column reading "12.5ms"
void print_summary(logger::UseColor color,
                           const std::vector<api::OutputFile>& output_files,
                           double elapsed_ms) {
    if (output_files.empty()) return;

    std::vector<logger::SummaryTableEntry> table;
    table.reserve(output_files.size());

    auto cwd_fs = filesystem::MakeRealFS({}, *(new std::string()));
    for (size_t i = 0; i < output_files.size(); i++) {
        const auto& f = output_files[i];
        std::string base;
        std::string dir;
        if (cwd_fs) {
            auto rel = cwd_fs->Rel(cwd_fs->Cwd(), f.path);
            std::string display = rel.value_or(f.path);
            base = cwd_fs->Base(display);
            dir = display.substr(0, display.size() - base.size());
        } else {
            base = f.path;
        }
        table.push_back(logger::SummaryTableEntry{
            dir,
            base,
            pretty_print_byte_count(f.contents.size()),
            static_cast<int>(f.contents.size()),
            base.size() >= 4 && base.substr(base.size() - 4) == ".map",
        });
    }

    const double* elapsed_ptr = nullptr;
    if (elapsed_ms >= 0) elapsed_ptr = &elapsed_ms;
    logger::PrintSummary(color, table, elapsed_ptr);
}

// The build session. One of these exists per Context() call and survives as long
// as the host keeps it.
//
// Concurrency is the main thing this class has to get right, because a watch
// session is driven from a background thread while the host is free to call any
// method from its own. The rules are:
//
//   * "mu" guards the session's own fields, and is never held while a build runs.
//   * At most one build runs at a time. A caller arriving during one joins it,
//     so two simultaneous rebuild requests produce one build and two copies of
//     its result.
//   * A dispose waits for any running build to finish before running the
//     plugins' dispose callbacks, so nothing is torn down underneath a pass.
//
// Watch mode and serving are mutually exclusive, and both are refused once the
// session has been disposed.
struct InternalContext : public api::BuildContext {
    mutable std::mutex mu;
    RebuildArgs args;
    std::unique_ptr<filesystem::Fs> real_fs;
    std::string abs_working_dir;
    std::shared_ptr<BuildInProgress> active_build;
    std::unique_ptr<api::BuildResult> recent_build;
    std::unique_ptr<api::Watcher> watcher;
    bool did_dispose = false;
    std::unordered_map<std::string, std::string> latest_hashes;

    // Runs a pass, or joins one that is already running. Returns an empty state
    // once the session has been disposed, which is how a late caller from another
    // thread is told to stop rather than being handed a build.
    //
    // Input:  two callers calling rebuild() at once
    // Output: one pass, and the same RebuildState handed to both callers
    RebuildState rebuild() {
        std::unique_lock<std::mutex> lock(mu);
        if (did_dispose) return RebuildState{};

        if (active_build) {
            auto build = active_build;
            lock.unlock();
            std::unique_lock ul(build->mu);
            build->cv.wait(ul, [&]{ return build->done; });
            lock.lock();
            return build->state;
        }

        auto build = std::make_shared<BuildInProgress>();
        active_build = build;
        RebuildArgs local_args = args;
        auto wh = watcher.get();
        RebuildState state;
        lock.unlock();

        auto cancel = std::make_shared<config::CancelFlag>();
        local_args.options.CancelFlagData = cancel.get();
        build->cancel = cancel;

        state = rebuild_impl(local_args, latest_hashes);

        // The result of a finished pass is kept alive separately from the state
        // it came in, because a later pass may start before a caller has read
        // the earlier one. It is a copy rather than a move, because the state
        // that is about to be handed back still has to own its own result: a
        // caller that asked a build for its output files is asking for them from
        // the value it gets back, not from a copy kept alive on the side. The
        // hash map is not kept at all: it is rebuilt from the next pass's output,
        // so between passes there is nothing to report.
        lock.lock();
        recent_build = std::make_unique<api::BuildResult>(state.result);
        latest_hashes.clear();
        active_build = nullptr;
        lock.unlock();

        // Hand the fresh watch data to the watcher with the session lock
        // released. The watcher holds its own lock while it scans, and taking
        // that lock here would block every future rebuild behind a scan that may
        // be waiting on a file the build itself is producing.
        if (wh && state.watch_data.paths.size() > 0) {
            watcher->SetWatchData(std::move(state.watch_data));
        }

        build->mu.lock();
        build->state = std::move(state);
        build->done = true;
        build->cv.notify_all();
        build->mu.unlock();

        return build->state;
    }

    // Runs one pass and hands back only the public result, discarding the watch
    // data. This is the call a one-shot build and an editor make; a watch session
    // goes through rebuild() directly so that it can see the watch data.
    api::BuildResult Rebuild() override {
        return rebuild().result;
    }

    // Turns the session into a watch session. The watcher is given a function
    // that runs a pass and returns the files it touched, so every change on disk
    // produces a fresh set of watched paths — that is how a build that follows
    // imports into new directories starts watching them.
    //
    // The delay is the quiet period before a change triggers a rebuild, and the
    // logging flag is passed on rather than re-derived, since the caller has
    // already decided how chatty this session should be.
    //
    // Input:  WatchOptions{delay = 100ms} on a session that has not been
    //         disposed
    // Output: a running watcher thread rebuilding on change; throws if the
    //         session is already disposed or already watching
    void Watch(const api::WatchOptions& options) override {
        std::lock_guard lock(mu);
        if (did_dispose) throw std::runtime_error("Cannot watch a disposed context");
        if (watcher) throw std::runtime_error("Watch mode has already been enabled");

        bool should_log = (args.log_options.log_level == logger::LogLevel::kInfo ||
                           args.log_options.log_level == logger::LogLevel::kDebug ||
                           args.log_options.log_level == logger::LogLevel::kVerbose);

        auto rebuild_fn = [this]() -> filesystem::WatchData {
            return rebuild().watch_data;
        };

        watcher = std::make_unique<api::Watcher>(
            *real_fs, std::move(rebuild_fn),
            std::chrono::milliseconds(options.delay), should_log,
            args.log_options.color, args.log_options.path_style);

        args.options.WatchMode = true;
        watcher->Start();
    }

    // Serves the build over HTTP. Like watching, this takes over the session:
    // each request runs a pass and answers from its result. A session cannot both
    // watch and serve, since both want to own the rebuild loop.
    //
    // Input:  ServeOptions{port = 8080} on a live, idle session
    // Output: the host and port that were bound; throws if the session is
    //         disposed or is already watching
    api::ServeResult Serve(const api::ServeOptions& options) override {
        std::lock_guard lock(mu);
        if (did_dispose) throw std::runtime_error("Cannot serve a disposed context");
        if (watcher) throw std::runtime_error("Cannot serve a context that's actively watching");

        auto rebuild_fn = [this]() -> api::BuildResult {
            return rebuild().result;
        };

        std::string error;
        auto result = api::Serve(*real_fs, rebuild_fn, options,
                                 args.log_options.log_level, args.log_options.color, error);
        if (!error.empty()) {
            throw std::runtime_error(error);
        }
        return result;
    }

    // Asks the running pass to stop, then waits for it. The pass notices the flag
    // at its next checkpoint — after the scan, and again after compiling — and
    // turns the cancellation into an error message. Waiting here is what makes
    // the call synchronous: when it returns, no build is running, so the caller
    // can safely dispose or start something else.
    //
    // Calling this when nothing is running is a no-op, and calling it on a
    // disposed session does nothing at all.
    //
    // Input:  Cancel() during a pass over a large graph
    // Output: the pass has stopped; the result it produced reports that the
    //         build was canceled
    void Cancel() override {
        std::lock_guard lock(mu);
        if (did_dispose) return;
        auto build = active_build;
        mu.unlock();
        if (build) {
            build->cancel->Cancel();
            std::unique_lock ul(build->mu);
            build->cv.wait(ul, [&]{ return build->done; });
        }
        mu.lock();
    }

    // Shuts the session down: stops the watcher, waits for any running pass, then
    // runs the plugins' dispose callbacks so they can release whatever they
    // allocated in setup. Disposing twice is harmless, which lets a host dispose
    // in a destructor without tracking whether it already did.
    //
    // After this returns, the session is inert — a later rebuild produces
    // nothing rather than a build, and a later watch or serve throws.
    void Dispose() override {
        mu.lock();
        if (did_dispose) {
            mu.unlock();
            return;
        }
        did_dispose = true;
        auto build = active_build;
        mu.unlock();

        if (watcher) watcher->Stop();

        if (build) {
            std::unique_lock ul(build->mu);
            build->cv.wait(ul, [&]{ return build->done; });
        }

        mu.lock();
        for (auto& fn : args.on_dispose_callbacks) fn();
        mu.unlock();
    }
};

// Assembles a build session from public options. This is where a one-shot build
// and a long-lived session meet: both are assembled here, and both pay for the same
// one-time setup.
//
// The order of the steps matters. Plugins are set up first, before options are
// validated, because the callbacks a plugin registers are part of what validation
// has to check. The finalized options are then handed back to the plugin list, so
// that a plugin calling the build's own resolve entry point — which is how a
// plugin asks Guchho to resolve something on its behalf — sees the fully
// validated options rather than the raw public ones.
//
// Everything is collected into a deferred log first, so a caller gets every
// problem with its configuration in one list instead of discovering them one
// build at a time. The log is drained into the context as warnings: a session is
// still created when the log holds only warnings, and those warnings are
// replayed at the start of every pass.
//
// A null context with a filled error list is the failure mode. There is no
// partial session: either the options were good enough to build a session from,
// or there is nothing to hand back.
//
// Input:  BuildOptions with two entry points, one plugin, and one invalid
//         out_extension key
// Output: a live session with the plugin registered, plus one error naming the
//         bad key; or a null session and that error
std::pair<std::unique_ptr<InternalContext>, std::vector<api::Message>>
context_impl(api::BuildOptions build_options) {
    PendingErrors pending;
    logger::OutputOptions log_options{
        .message_limit = build_options.log_limit,
        .include_source = true,
        .color = validate_color(build_options.color),
        .log_level = validate_log_level(build_options.log_level, pending),
        .path_style = extract_path_style(build_options.abs_paths, api::AbsPathsFlags::kLogAbsPath),
        .overrides = validate_log_overrides(build_options.log_override, pending),
    };
    // The log is the first thing that exists which can be told about a setting
    // that was not a setting at all, so everything the validators held back is
    // reported here. It joins the rest of the validation rather than ending it:
    // a caller who set a bad level and a contradictory output path has made two
    // mistakes, and finding the second one costs nothing now.
    logger::Log pending_log = logger::NewDeferLog(logger::DeferLogKind::kDeferLogNoVerboseOrDebug,
                                                  log_options.overrides);
    pending.ReportTo(pending_log);

    std::string abs_working_dir = build_options.abs_working_dir;
    std::string fs_error;
    auto real_fs = filesystem::MakeRealFS({
        .abs_working_dir = abs_working_dir,
        .do_not_cache = true,
    }, fs_error);
    if (!real_fs) {
        logger::Log log = logger::NewStderrLog(log_options);
        log.AddError(nullptr, logger::Range{}, fs_error);
        return {nullptr, convert_messages_to_public(logger::MsgKind::kError, log.done(), log_options.path_style)};
    }

    // The cache set is created here and moved into the context, because the
    // context has to own it: an empty cache lives for as long as the session,
    // and a watch session is exactly the case where reusing it pays off.
    auto caches = std::make_shared<cache::CacheSet>();
    auto caches_ptr = cache::MakeCacheSet();
    auto log = logger::NewDeferLog(logger::DeferLogKind::kDeferLogNoVerboseOrDebug, log_options.overrides);
    for (const logger::Msg& msg : pending_log.done()) {
        log.add_msg(msg);
    }

    std::vector<OnEndCallback> on_end_callbacks;
    std::vector<std::function<void()>> on_dispose_callbacks;
    std::vector<std::shared_ptr<PluginImpl>> plugin_impls;
    std::function<void(config::Options&)> finalize_build_options;

    load_plugins(&build_options, *real_fs, log, *caches_ptr,
                 on_end_callbacks, on_dispose_callbacks, plugin_impls, finalize_build_options);

    auto [options_pair, extras_pair] =
        validate_build_options(build_options, log, *real_fs);
    auto& [options, entry_points] = options_pair;
    auto& [defines_owner, stdin_owner] = extras_pair;
    finalize_build_options(options);

    auto msgs = log.done();
    if (log.has_errors()) {
        if (log_options.log_level > logger::LogLevel::kSilent) {
            auto stderr_log = logger::NewStderrLog(log_options);
            for (const auto& msg : msgs) stderr_log.add_msg(msg);
            stderr_log.done();
        }
        return {nullptr, convert_messages_to_public(logger::MsgKind::kError, msgs, options.LogPathStyle)};
    }

    auto ctx = std::make_unique<InternalContext>();
    ctx->real_fs = std::move(real_fs);
    ctx->abs_working_dir = abs_working_dir;
    ctx->args.caches = std::move(caches_ptr);
    ctx->args.on_end_callbacks = std::move(on_end_callbacks);
    ctx->args.on_dispose_callbacks = std::move(on_dispose_callbacks);
    ctx->args.log_options = log_options;
    ctx->args.log_warnings = std::move(msgs);
    ctx->args.entry_points = std::move(entry_points);
    ctx->args.options = std::move(options);
    ctx->args.mangle_cache = clone_mangle_cache(build_options.mangle_cache);
    ctx->args.abs_working_dir = abs_working_dir;
    ctx->args.write = build_options.write;
    ctx->args.defines_owner = std::move(defines_owner);
    ctx->args.stdin_owner = std::move(stdin_owner);
    return {std::move(ctx), std::vector<api::Message>{}};
}

// The public Context() entry point, which differs from context_impl only in how
// it reports failure: a null context with the error list filled in, rather than a
// pair.
//
// Input:  BuildOptions with an outfile and an outdir both set
// Output: nullptr, with one error saying the two cannot be combined
std::unique_ptr<api::BuildContext> create_build_context(
    api::BuildOptions build_options, std::vector<api::Message>& errors) {
    auto [ctx, errs] = context_impl(std::move(build_options));
    if (!ctx) {
        errors = std::move(errs);
        return nullptr;
    }
    return std::move(ctx);
}

// The public Build() entry point: a session is created, one pass is run, a
// summary is printed, and the session is disposed — all in one call.
//
// Options that fail validation short-circuit before any pass runs, and come back
// as a result whose only content is the errors. A host therefore does not have
// to distinguish "the build failed" from "the build could not be configured";
// both arrive as a BuildResult with errors in it.
//
// The summary is printed only when the log level asks for it and the output is
// not going to standard output, because in that case the terminal is carrying the
// program's own bytes and a table would corrupt them.
//
// Input:  entry_points = {"src/index.js"}, bundle = true, outdir = "dist",
//         write = true
// Output: a BuildResult whose output_files hold the bytes that were written, a
//         printed size table on stderr, and a disposed session
api::BuildResult build_impl(const api::BuildOptions& options) {
    auto start = std::chrono::steady_clock::now();
    auto build_opts = options;
    auto [ctx, errors] = context_impl(build_opts);
    if (!ctx) {
        return api::BuildResult{
            .errors = std::move(errors),
            .warnings = {},
            .output_files = {},
            .metafile = {},
            .mangle_cache = {},
        };
    }
    auto result = ctx->Rebuild();

    // The size table goes to stderr, not stdout: in the one case where stdout is
    // carrying the program's own bytes, a table printed there would corrupt them.
    if (ctx->args.log_options.log_level >= logger::LogLevel::kInfo &&
        !ctx->args.options.WriteToStdout) {
        auto end = std::chrono::steady_clock::now();
        double elapsed_ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        print_summary(ctx->args.log_options.color, result.output_files,
                      elapsed_ms);
    }

    ctx->Dispose();
    return result;
}

// Turns public build options into the internal option set a pass runs on. This is
// the single place where the public shape of a build is interpreted, and it is
// where most configuration errors are caught.
//
// Two things make it more than a field-by-field copy.
//
// First, the option set is a *derived* view. Minification is only enabled when all
// three of its parts are, defines are filtered by the platform and the drop flags,
// the target engine list becomes a set of unsupported features, and the output
// format is inferred from the platform when the caller left it alone. A caller
// therefore sets a handful of intent-level fields and gets a coherent
// configuration out, rather than having to keep a set of derived fields
// consistent by hand.
//
// Second, options are checked against each other, not just individually. A build
// that both bundles and asks to preserve its input format is a contradiction, so
// the platform's usual format is substituted; a build that is not bundling cannot
// have externals or aliases, because there is no graph for them to apply to; code
// splitting needs a module format to split into. These are reported rather than
// silently ignored, because each one means the caller asked for something that
// cannot happen.
//
// The result is a pair of pairs: the options and the entry points, plus the owners
// those options point at. The processed define table and the in-memory entry point
// are returned alongside because the options only hold raw pointers into them,
// and the caller has to keep them alive for as long as the options are used.
//
// Input:  target = kES2020, platform = kBrowser, minify_whitespace only,
//         bundle = false, and one external
// Output: options with minification off and the browser's usual output format,
//         one error about the external needing a bundle, and the owners
//         returned so the pointers stay valid
static std::pair<std::pair<config::Options, std::vector<config::EntryPoint>>,
                  std::pair<std::shared_ptr<config::ProcessedDefines>, std::optional<config::StdinInfo>>>
validate_build_options(api::BuildOptions& build_opts, logger::Log& log, filesystem::Fs& real_fs) {
    auto features = validate_features(log, build_opts.target, build_opts.engines);
    auto supported = validate_supported(log, build_opts.supported);
    auto [out_js, out_css] = validate_output_extensions(log, build_opts.out_extension);
    auto [banner_js, banner_css] = validate_banner_or_footer(log, "banner", build_opts.banner);
    auto [footer_js, footer_css] = validate_banner_or_footer(log, "footer", build_opts.footer);
    bool minify = build_opts.minify_whitespace && build_opts.minify_identifiers && build_opts.minify_syntax;
    config::Platform platform = validate_platform(build_opts.platform);
    auto [defines, injected_defines] = validate_defines(
        log, build_opts.define, build_opts.pure, platform, true, minify, build_opts.drop);

    config::Options options;
    options.CSSPrefixData = features.css_prefix_data;
    options.UnsupportedJSFeatures = compat::ApplyOverrides(features.unsupported_js, supported.unsupported_js, supported.js_mask);
    options.UnsupportedCSSFeatures = compat::ApplyOverrides(features.unsupported_css, supported.unsupported_css, supported.css_mask);
    options.UnsupportedJSFeatureOverrides = supported.unsupported_js;
    options.UnsupportedJSFeatureOverridesMask = supported.js_mask;
    options.UnsupportedCSSFeatureOverrides = supported.unsupported_css;
    options.UnsupportedCSSFeatureOverridesMask = supported.css_mask;
    options.OriginalTargetEnv = std::move(features.target_env);
    options.JSX = config::JSXOptions{
        .Factory = validate_jsx_expr(log, build_opts.jsx_factory, "factory"),
        .Fragment = validate_jsx_expr(log, build_opts.jsx_fragment, "fragment"),
        .Preserve = (build_opts.jsx == api::JSX::kPreserve),
        .AutomaticRuntime = (build_opts.jsx == api::JSX::kAutomatic),
        .ImportSource = build_opts.jsx_import_source,
        .Development = build_opts.jsx_dev,
        .SideEffects = build_opts.jsx_side_effects,
    };
    options.InjectedDefines = std::move(injected_defines);
    options.OutputPlatform = platform;
    options.SourceMapData = validate_source_map(build_opts.sourcemap);
    options.LegalCommentsData = validate_legal_comments(build_opts.legal_comments, build_opts.bundle);
    options.SourceRoot = build_opts.source_root;
    options.ExcludeSourcesContent = (build_opts.sources_content == api::SourcesContent::kExclude);
    options.MinifySyntax = build_opts.minify_syntax;
    options.MinifyWhitespace = build_opts.minify_whitespace;
    options.MinifyIdentifiers = build_opts.minify_identifiers;
    options.LineLimit = build_opts.line_limit;
    if (auto mp = validate_regex(log, "mangle props", build_opts.mangle_props)) {
        options.MangleProps = std::make_shared<std::regex>(*mp, std::regex::ECMAScript);
    }
    if (auto rp = validate_regex(log, "reserve props", build_opts.reserve_props)) {
        options.ReserveProps = std::make_shared<std::regex>(*rp, std::regex::ECMAScript);
    }
    options.MangleQuoted = (build_opts.mangle_quoted == api::MangleQuoted::kTrue);
    options.DropLabels = build_opts.drop_labels;
    options.DropDebugger = Has(build_opts.drop, api::DropFlags::kDropDebugger);
    options.AllowOverwrite = build_opts.allow_overwrite;
    options.ASCIIOnly = validate_ascii_only(build_opts.charset);
    options.IgnoreDCEAnnotations = build_opts.ignore_annotations;
    options.TreeShaking = validate_tree_shaking(build_opts.tree_shaking, build_opts.bundle, build_opts.format);
    auto global_name = validate_global_name(log, build_opts.global_name, "(global name)");
    options.GlobalName = std::move(global_name);
    options.Amd.auto_id = build_opts.amd_auto_id;
    options.Amd.base_path = build_opts.amd_base_path;
    options.Amd.id = build_opts.amd_id;
    options.Amd.define = build_opts.amd_define;
    options.Amd.force_js_extension_for_imports = build_opts.amd_force_js_extension_for_imports;
    options.Extend = build_opts.extend;
    options.NoConflict = build_opts.no_conflict;
    options.Strict = build_opts.strict;
    options.Globals = build_opts.globals;
    options.SystemNullSetters = build_opts.system_null_setters;
    options.CodeSplitting = build_opts.splitting;
    options.OutputFormat = validate_format(build_opts.format);
    options.AbsOutputFile = validate_path(log, real_fs, build_opts.outfile, "outfile path");
    options.AbsOutputDir = validate_path(log, real_fs, build_opts.outdir, "outdir path");
    options.AbsOutputBase = validate_path(log, real_fs, build_opts.outbase, "outbase path");
    options.NeedsMetafile = build_opts.metafile;
    options.EntryPathTemplate = validate_path_template(build_opts.entry_names);
    options.ChunkPathTemplate = validate_path_template(build_opts.chunk_names);
    options.AssetPathTemplate = validate_path_template(build_opts.asset_names);
    options.OutputExtensionJS = out_js;
    options.OutputExtensionCSS = out_css;
    options.ExtensionToLoader = validate_loaders(log, build_opts.loader);
    options.ExtensionOrder = validate_resolve_extensions(log, build_opts.resolve_extensions);
    options.ExternalSettingsData = validate_externals(log, real_fs, build_opts.external);
    options.ExternalPackages = validate_external_packages(build_opts.packages);
    options.PackageAliases = validate_alias(log, real_fs, build_opts.alias);
    options.TSConfigPath = validate_path(log, real_fs, build_opts.tsconfig, "tsconfig path");
    options.TSConfigRaw = build_opts.tsconfig_raw;
    options.MainFields = build_opts.main_fields;
    options.PublicPath = build_opts.public_path;
    options.KeepNames = build_opts.keep_names;
    options.CodePathStyle = extract_path_style(build_opts.abs_paths, api::AbsPathsFlags::kCodeAbsPath);
    options.LogPathStyle = extract_path_style(build_opts.abs_paths, api::AbsPathsFlags::kLogAbsPath);
    options.MetafilePathStyle = extract_path_style(build_opts.abs_paths, api::AbsPathsFlags::kMetafileAbsPath);
    options.InjectPaths = build_opts.inject;
    options.AbsNodePaths.resize(build_opts.node_paths.size());
    for (size_t i = 0; i < build_opts.node_paths.size(); i++) {
        options.AbsNodePaths[i] = validate_path(log, real_fs, build_opts.node_paths[i], "node path");
    }
    options.JSBanner = banner_js;
    options.JSFooter = footer_js;
    options.CSSBanner = banner_css;
    options.CSSFooter = footer_css;
    options.PreserveSymlinks = build_opts.preserve_symlinks;

    validate_keep_names(log, options);

    if (!build_opts.conditions.empty()) {
        options.Conditions = build_opts.conditions;
    }

    std::vector<config::EntryPoint> entry_points;
    bool has_wildcard = false;
    for (const auto& ep : build_opts.entry_points) {
        entry_points.push_back(config::EntryPoint{ep});
        if (ep.find('*') != std::string::npos) has_wildcard = true;
    }
    for (const auto& ep : build_opts.entry_points_advanced) {
        entry_points.push_back(config::EntryPoint{ep.input_path, ep.output_path, false});
        if (ep.input_path.find('*') != std::string::npos) has_wildcard = true;
    }

    int entry_count = static_cast<int>(entry_points.size());
    std::optional<config::StdinInfo> stdin_owner;
    if (build_opts.stdin_data) {
        entry_count++;
        config::StdinInfo si;
        si.Ldr = validate_loader(build_opts.stdin_data->loader);
        si.Contents = build_opts.stdin_data->contents;
        si.SourceFile = build_opts.stdin_data->sourcefile;
        si.AbsResolveDir = validate_path(log, real_fs, build_opts.stdin_data->resolve_dir, "resolve directory path");
        stdin_owner = std::move(si);
        options.Stdin = &*stdin_owner;
    }

    if (options.AbsOutputDir.empty() && (entry_count > 1 || has_wildcard)) {
        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_MustUseOutdirMultipleInputFiles));
    } else if (options.AbsOutputDir.empty() && options.CodeSplitting) {
        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_MustUseOutdirCodeSplitting));
    } else if (!options.AbsOutputFile.empty() && !options.AbsOutputDir.empty()) {
        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_CannotUseBothOutfileAndOutdir));
    } else if (!options.AbsOutputFile.empty()) {
        options.AbsOutputDir = real_fs.Dir(options.AbsOutputFile);
    } else if (options.AbsOutputDir.empty()) {
        options.WriteToStdout = true;
        if (options.SourceMapData != config::SourceMap::kNone && options.SourceMapData != config::SourceMap::kInline) {
            log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_CannotUseExternalSourceMap));
        }
        if (options.LegalCommentsData == config::LegalComments::kLinkedWithComment ||
            options.LegalCommentsData == config::LegalComments::kExternalWithoutComment) {
            log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_CannotUseLinkedOrExternalLegalComments));
        }
        for (auto& [ext, loader] : build_opts.loader) {
            // Only a loader the caller asked for by name can rule out an output
            // path. The default extension table carries a file loader for every
            // binary asset format, so testing that table would reject every build
            // that writes to standard output, including the ones that only ever
            // touch JavaScript.
            if (loader == api::Loader::kFile || loader == api::Loader::kCopy) {
                log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_CannotUseFileLoader));
                break;
            }
        }
        options.AbsOutputDir = real_fs.Cwd();
    }

    if (!build_opts.bundle) {
        if (options.ExternalSettingsData.PreResolve.HasMatchers() || options.ExternalSettingsData.PostResolve.HasMatchers()) {
            log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_CannotUseExternalWithoutBundle));
        }
        if (!options.PackageAliases.empty()) {
            log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_CannotUseAliasWithoutBundle));
        }
    } else if (options.OutputFormat == config::Format::kPreserve) {
        switch (options.OutputPlatform) {
            case config::Platform::kBrowser: options.OutputFormat = config::Format::kIIFE; break;
            case config::Platform::kNode: options.OutputFormat = config::Format::kCommonJS; break;
            case config::Platform::kNeutral: options.OutputFormat = config::Format::kESModule; break;
            default: break;
        }
    }

    if (build_opts.bundle) {
        options.BuildMode = config::Mode::kBundle;
    } else if (options.OutputFormat != config::Format::kPreserve) {
        options.BuildMode = config::Mode::kConvertFormat;
    }

    if (options.Conditions.empty() && options.OutputPlatform != config::Platform::kNeutral) {
        options.Conditions = {"module"};
    }

    if (options.CodeSplitting && options.OutputFormat != config::Format::kESModule) {
        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_SplittingOnlyESM));
    }

    if (!options.TSConfigPath.empty() && !options.TSConfigRaw.empty()) {
        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_CannotProvideTsconfigBoth));
    }

    if (!build_opts.write) {
        options.AllowOverwrite = true;
    }

    auto defines_owner = std::make_shared<config::ProcessedDefines>(std::move(defines));
    options.Defines = defines_owner.get();
    return {{std::move(options), std::move(entry_points)}, {std::move(defines_owner), std::move(stdin_owner)}};
}

// ===========================================================================
// The single-string half
// ===========================================================================
//
// A transform is a build with exactly one input, held in memory, with the result
// returned rather than written. It is not a shortcut around the build machinery —
// the same scan, compile and minify path runs — so a transform and a build of the
// same source with the same options produce the same bytes. What differs is only
// where the bytes come from and where they end up.

// Runs the transform described above.
//
// A transform can only reach so far on its own. It has no graph to resolve imports
// against, so the only loaders it makes sense to use are the ones that parse a
// single file: asking for a loader that reads and copies a file is reported
// instead of attempted, because there is nothing on disk to read.
//
// The source is presented as an in-memory entry point, which is what makes
// diagnostics come back with a source name: the caller can set one, and if it
// does not, the source is named as coming from standard input rather than from
// the empty string, so error messages point somewhere.
//
// Three options are impossible here and are reported rather than ignored. A
// source map or legal comment text that has to be linked with a comment needs a
// file to be linked from, and a source map with no source file name has nothing
// to name the map after. A transform is a pipe, so what goes in has to be able to
// stand on its own.
//
// The outputs are told apart by name rather than by position. The shortest of the
// generated paths is the primary output, and the source map and legal comment text
// are the paths that extend it, which is why the input's own name is used to
// generate all three.
//
// The mangle cache comes back on success and is dropped on failure, matching the
// rule the build path uses: a cache produced by a build that failed may describe
// renamings that the failed build never fully applied.
//
// Input:  input = "const x = 1", loader = kJS, target = kES2020,
//         sourcemap = kNone
// Output: result.code holding the compiled text, empty errors and warnings, and
//         an empty map
api::TransformResult transform_impl(const std::string& input,
                                            const api::TransformOptions& transform_opts) {
    PendingErrors pending;
    logger::OutputOptions log_options{
        .message_limit = transform_opts.log_limit,
        .include_source = true,
        .color = validate_color(transform_opts.color),
        .log_level = validate_log_level(transform_opts.log_level, pending),
        .path_style = extract_path_style(transform_opts.abs_paths, api::AbsPathsFlags::kLogAbsPath),
        .overrides = validate_log_overrides(transform_opts.log_override, pending),
    };
    auto log = logger::NewStderrLog(log_options);
    // The log exists now, so a level that was not a level can be named instead of
    // thrown, and the transform still reports every other error it found too.
    pending.ReportTo(log);

    api::TransformOptions opts = transform_opts;
    if (opts.sourcefile.empty()) opts.sourcefile = "<stdin>";
    if (opts.loader == api::Loader::kNone) opts.loader = api::Loader::kJS;

    auto features = validate_features(log, opts.target, opts.engines);
    auto supported = validate_supported(log, opts.supported);
    config::Platform platform = validate_platform(opts.platform);
    auto [defines, injected_defines] = validate_defines(
        log, opts.define, opts.pure, platform, false, false, opts.drop);

    auto mangle_cache = clone_mangle_cache(opts.mangle_cache);

    config::Options options;
    options.CSSPrefixData = features.css_prefix_data;
    options.UnsupportedJSFeatures = compat::ApplyOverrides(features.unsupported_js, supported.unsupported_js, supported.js_mask);
    options.UnsupportedCSSFeatures = compat::ApplyOverrides(features.unsupported_css, supported.unsupported_css, supported.css_mask);
    options.UnsupportedJSFeatureOverrides = supported.unsupported_js;
    options.UnsupportedJSFeatureOverridesMask = supported.js_mask;
    options.UnsupportedCSSFeatureOverrides = supported.unsupported_css;
    options.UnsupportedCSSFeatureOverridesMask = supported.css_mask;
    options.OriginalTargetEnv = std::move(features.target_env);
    options.TSConfigRaw = opts.tsconfig_raw;
    options.JSX = config::JSXOptions{
        .Factory = validate_jsx_expr(log, opts.jsx_factory, "factory"),
        .Fragment = validate_jsx_expr(log, opts.jsx_fragment, "fragment"),
        .Preserve = (opts.jsx == api::JSX::kPreserve),
        .AutomaticRuntime = (opts.jsx == api::JSX::kAutomatic),
        .ImportSource = opts.jsx_import_source,
        .Development = opts.jsx_dev,
        .SideEffects = opts.jsx_side_effects,
    };
    auto defines_owner = std::make_shared<config::ProcessedDefines>(std::move(defines));
    options.Defines = defines_owner.get();
    options.InjectedDefines = std::move(injected_defines);
    options.OutputPlatform = platform;
    options.SourceMapData = validate_source_map(opts.sourcemap);
    options.LegalCommentsData = validate_legal_comments(opts.legal_comments, false);
    options.SourceRoot = opts.source_root;
    options.ExcludeSourcesContent = (opts.sources_content == api::SourcesContent::kExclude);
    options.OutputFormat = validate_format(opts.format);
    auto global_name = validate_global_name(log, opts.global_name, "(global name)");
    options.GlobalName = std::move(global_name);
    options.MinifySyntax = opts.minify_syntax;
    options.MinifyWhitespace = opts.minify_whitespace;
    options.MinifyIdentifiers = opts.minify_identifiers;
    options.LineLimit = opts.line_limit;
    if (auto mp = validate_regex(log, "mangle props", opts.mangle_props)) {
        options.MangleProps = std::make_shared<std::regex>(*mp, std::regex::ECMAScript);
    }
    if (auto rp = validate_regex(log, "reserve props", opts.reserve_props)) {
        options.ReserveProps = std::make_shared<std::regex>(*rp, std::regex::ECMAScript);
    }
    options.MangleQuoted = (opts.mangle_quoted == api::MangleQuoted::kTrue);
    options.DropLabels = opts.drop_labels;
    options.DropDebugger = Has(opts.drop, api::DropFlags::kDropDebugger);
    options.ASCIIOnly = validate_ascii_only(opts.charset);
    options.IgnoreDCEAnnotations = opts.ignore_annotations;
    options.TreeShaking = validate_tree_shaking(opts.tree_shaking, false, opts.format);
    options.AbsOutputFile = opts.sourcefile + "-out";
    options.KeepNames = opts.keep_names;
    options.CodePathStyle = extract_path_style(opts.abs_paths, api::AbsPathsFlags::kCodeAbsPath);
    options.LogPathStyle = extract_path_style(opts.abs_paths, api::AbsPathsFlags::kLogAbsPath);
    options.MetafilePathStyle = extract_path_style(opts.abs_paths, api::AbsPathsFlags::kMetafileAbsPath);

    config::StdinInfo stdin_info;
    stdin_info.Ldr = validate_loader(opts.loader);
    stdin_info.Contents = input;
    stdin_info.SourceFile = opts.sourcefile;
    options.Stdin = &stdin_info;

    validate_keep_names(log, options);

    if (stdin_info.Ldr == config::Loader::kCSS || stdin_info.Ldr == config::Loader::kLocalCSS || stdin_info.Ldr == config::Loader::kGlobalCSS) {
        options.CSSBanner = opts.banner;
        options.CSSFooter = opts.footer;
    } else {
        options.JSBanner = opts.banner;
        options.JSFooter = opts.footer;
    }

    if (options.SourceMapData == config::SourceMap::kLinkedWithComment) {
        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_CannotTransformWithLinkedSourceMaps));
    }
    if (options.SourceMapData != config::SourceMap::kNone && stdin_info.SourceFile.empty()) {
        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_MustUseSourcefileWithSourcemap));
    }
    if (options.LegalCommentsData == config::LegalComments::kLinkedWithComment) {
        log.AddError(nullptr, logger::Range{}, logger::FormatMsg(logger::MsgCat::kAPI_CannotTransformWithLinkedLegalComments));
    }

    if (options.OutputFormat != config::Format::kPreserve) {
        options.BuildMode = config::Mode::kConvertFormat;
    }

    std::vector<graph::OutputFile> results;

    if (!log.has_errors()) {
        helpers::Timer timer{"transform"};
        // A transform has no file system of its own: the source arrives through
        // "options.Stdin" and the entry point list is empty, so the file system
        // is only used for path resolution and falls back to the working
        // directory of the process.
        std::string fs_error;
        auto fs = filesystem::MakeRealFS({}, fs_error);
        if (!fs) {
            log.AddError(nullptr, logger::Range{}, fs_error);
        } else {
            auto caches = cache::MakeCacheSet();
            auto bundle = bundler::ScanBundle(
                config::APICall::kTransformCall, log, *fs, *caches,
                {}, options, &timer);

            if (!log.has_errors()) {
                results = bundle.Compile(log, timer, mangle_cache).first;
            }
        }
        timer.Log("done");
    }

    api::TransformResult result;
    std::string shortest_abs_path;
    for (const auto& r : results) {
        if (shortest_abs_path.empty() || r.abs_path.size() < shortest_abs_path.size()) {
            shortest_abs_path = r.abs_path;
        }
    }
    for (const auto& r : results) {
        if (r.abs_path == shortest_abs_path) {
            result.code = std::vector<uint8_t>(r.contents.begin(), r.contents.end());
        } else if (r.abs_path == shortest_abs_path + ".map") {
            result.map = std::vector<uint8_t>(r.contents.begin(), r.contents.end());
        } else if (r.abs_path == shortest_abs_path + ".LEGAL.txt") {
            result.legal_comments = std::vector<uint8_t>(r.contents.begin(), r.contents.end());
        }
    }

    auto msgs = log.done();
    result.errors = convert_messages_to_public(logger::MsgKind::kError, msgs, options.LogPathStyle);
    result.warnings = convert_messages_to_public(logger::MsgKind::kWarning, msgs, options.LogPathStyle);
    if (log.has_errors()) {
        result.mangle_cache.clear();
    } else {
        result.mangle_cache = std::move(mangle_cache);
    }
    return result;
}

// ===========================================================================
// Reporting on a build that already happened
// ===========================================================================
//
// A build can be asked to hand back its messages as structured data, which is
// what an editor or a language server wants. Those messages are usually kept
// around rather than shown, and when the time comes to show them the host is out
// of process, has no access to the build, and knows nothing about Guchho's
// message types.
//
// FormatMessages closes that gap: it takes Message values back in and returns
// finished text, one string per message. The conversion runs through the same
// internal message type the build itself produces and the same printer, so the
// text is identical to what a build would have written to a terminal — including
// the source line, the code frame, and any colour the caller asks for.
//
// The caller's kind choice is what tells the printer which of the two message
// streams it is formatting, since a Message does not say on its own whether it was
// an error or a warning.
//
// Input:  one Message of kind kError at "src/index.js:3:1" with one note, and
//         FormatMessagesOptions{color = kTrue, terminal_width = 80}
// Output: one string holding the rendered diagnostic, with the code frame and
//         colour escapes
std::vector<std::string> format_msgs_impl(const std::vector<api::Message>& msgs,
                                              const api::FormatMessagesOptions& options) {
    logger::MsgKind kind = (options.kind == api::MessageKind::kWarning) ? logger::MsgKind::kWarning : logger::MsgKind::kError;
    DetailArena arena;
    auto log_msgs = convert_messages_to_internal({}, kind, msgs, arena);
    std::vector<std::string> strings;
    strings.reserve(log_msgs.size());
    for (const auto& msg : log_msgs) {
        strings.push_back(msg.String(
            logger::OutputOptions{.include_source = true},
            logger::TerminalInfo{
                .use_color_escapes = options.color,
                .width = options.terminal_width,
            }));
    }
    return strings;
}

// ===========================================================================
// Turning a metafile back into a report
// ===========================================================================
//
// A metafile is a description of a build, not a rendering of one: a tree of
// outputs, each listing the inputs that ended up inside it and how many bytes of
// each survived minification. AnalyzeMetafile turns that into the same size table
// a build prints, for a build that already happened and whose metafile is all
// that is left.

namespace {

// One row of the report, and the row beneath it. An output holds its own name and
// size plus the list of inputs it contains, so a report entry is a small tree
// rather than a flat list. The "entry_point" field is the output's own entry point
// and is not printed, but keeping it on the row means the size and the name always
// travel together.
struct MetafileEntry {
    std::string name{};
    std::string entry_point{};
    std::vector<MetafileEntry> entries{};
    uint64_t size = 0;
};

// Property lookup over a parsed JSON value. The metafile is walked as a syntax
// tree rather than turned into a generic value, so these four accessors are the
// whole of the "read a field" layer: a missing property and a property of the
// wrong type are both reported the same way, as an absent one, and the caller
// decides what that means.
//
// The typed accessors return a pointer into the tree, so a caller holds a
// reference into the parsed metafile and must not outlive it.
static javascript::Expr get_object_property(javascript::Expr expr, const std::string& key) {
    if (auto* obj = std::get_if<std::shared_ptr<javascript::EObject>>(&expr.data)) {
        for (const auto& prop : (*obj)->properties) {
            if (auto* str = std::get_if<std::shared_ptr<javascript::EString>>(&prop.key.data)) {
                if (helpers::UTF16EqualsString((*str)->value, key)) {
                    return prop.value_or_nil;
                }
            }
        }
    }
    return javascript::Expr{};
}

static javascript::ENumber* get_object_property_number(javascript::Expr expr, const std::string& key) {
    auto val = get_object_property(std::move(expr), key);
    if (auto* num = std::get_if<std::shared_ptr<javascript::ENumber>>(&val.data)) return (*num).get();
    return nullptr;
}

static javascript::EString* get_object_property_string(javascript::Expr expr, const std::string& key) {
    auto val = get_object_property(std::move(expr), key);
    if (auto* str = std::get_if<std::shared_ptr<javascript::EString>>(&val.data)) return (*str).get();
    return nullptr;
}

static javascript::EObject* get_object_property_object(javascript::Expr expr, const std::string& key) {
    auto val = get_object_property(std::move(expr), key);
    if (auto* obj = std::get_if<std::shared_ptr<javascript::EObject>>(&val.data)) return (*obj).get();
    return nullptr;
}

[[maybe_unused]] static javascript::EArray* get_object_property_array(javascript::Expr expr, const std::string& key) {
    auto val = get_object_property(std::move(expr), key);
    if (auto* arr = std::get_if<std::shared_ptr<javascript::EArray>>(&val.data)) return (*arr).get();
    return nullptr;
}

// Counts characters rather than bytes, because the report aligns its columns and
// a name containing anything outside plain ASCII is one character but several
// bytes. Counting bytes would push every column after a non-ASCII name out by the
// difference.
//
// A malformed encoding ends the count rather than being skipped, so the returned
// count is the number of characters decoded before the problem.
//
// Input:  "dist/index.js"  => 13
// Input:  "\xf0\x9f\x8e\x89" => 1
static int rune_count(std::string_view s) {
    int count = 0;
    size_t i = 0;
    while (i < s.size()) {
        auto [rune, len] = helpers::DecodeWTF8Rune(s.substr(i));
        if (len == 0) break;
        i += static_cast<size_t>(len);
        count++;
    }
    return count;
}

} // namespace

// Renders a metafile as the size report a build would have printed: one block
// per output, largest first, each with its inputs indented beneath it in the same
// order.
//
// A metafile that cannot be parsed, or that has no outputs, produces an empty
// string. There is nothing useful to say about a metafile this function cannot
// read, and the caller is the one that knows whether that is worth reporting.
//
// Source maps are not outputs in the sense that matters here. A ".map" entry is
// skipped rather than reported, because its size is a function of the code it
// describes and listing it would make every build look twice as large as it is.
//
// An output is only reported if it has a size and an input list. The metafile
// format allows an output to omit either, and one without them has nothing to
// break down.
//
// Each output's inputs are filtered to those that contributed bytes. An input
// that was bundled in and then dropped entirely contributes nothing to the output
// and is not part of what made it big.
//
// The nesting is drawn with box-drawing characters, with the last child of each
// output closing the branch and the others branching off it. The verbose option
// replaces the horizontal rules with a line character; the report has to be
// readable in a log file, a terminal, and an editor's output pane, and those three
// render the same characters differently.
//
// Input:  a metafile with one 2048-byte output containing a 1536-byte input,
//         verbose = false
// Output: a block with "dist/index.js" at 2.0kb 100.0%, and one indented child
//         reading "src/index.js" at 1.5kb 75.0%
std::string analyze_metafile_impl(const std::string& metafile, const api::AnalyzeMetafileOptions& options) {
    auto log = logger::NewDeferLog(logger::DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    logger::Source source;
    source.contents = metafile;

    auto [result, ok] = javascript::Parser::ParseJSON(log, source, javascript::JSONOptions{});
    if (!ok) return "";

    // The lookup is handed the tree rather than a reference to it, and it
    // answers with a raw pointer into that tree. Handing it a moved-from tree
    // would therefore drop the last reference to the metafile the pointer aims
    // into: the value read back would be whatever the freed memory happens to
    // look like, which for a report is an empty string and for a caller that
    // keeps reading is a crash. Copying the tree is one reference, and the copy
    // outlives every pointer taken out of it.
    auto* outputs = get_object_property_object(result, "outputs");
    if (!outputs) return "";

    std::vector<MetafileEntry> entries;
    std::vector<std::string> entry_points;

    for (const auto& output : outputs->properties) {
        std::string key = helpers::UTF16ToString(
            [&]() -> std::u16string {
                if (auto* str = std::get_if<std::shared_ptr<javascript::EString>>(&output.key.data))
                    return (*str)->value;
                return {};
            }());
        if (key.size() >= 4 && key.substr(key.size() - 4) == ".map") continue;

        std::string entry_point_path;
        if (auto* ep = get_object_property_string(output.value_or_nil, "entryPoint")) {
            entry_point_path = helpers::UTF16ToString(ep->value);
            entry_points.push_back(entry_point_path);
        }

        auto* bytes = get_object_property_number(output.value_or_nil, "bytes");
        if (!bytes) continue;

        auto* inputs = get_object_property_object(output.value_or_nil, "inputs");
        if (!inputs) continue;

        std::vector<MetafileEntry> children;
        for (const auto& input : inputs->properties) {
            auto* bytes_in_output = get_object_property_number(input.value_or_nil, "bytesInOutput");
            if (bytes_in_output && bytes_in_output->value > 0) {
                children.push_back(MetafileEntry{
                    .name = helpers::UTF16ToString(
                        [&]() -> std::u16string {
                            if (auto* str = std::get_if<std::shared_ptr<javascript::EString>>(&input.key.data))
                                return (*str)->value;
                            return {};
                        }()),
                    .size = static_cast<uint64_t>(bytes_in_output->value),
                });
            }
        }
        std::sort(children.begin(), children.end(), [](const MetafileEntry& a, const MetafileEntry& b) {
            if (a.size != b.size) return a.size > b.size;
            return a.name < b.name;
        });

        entries.push_back(MetafileEntry{
            .name = key,
            .entry_point = entry_point_path,
            .entries = std::move(children),
            .size = static_cast<uint64_t>(bytes->value),
        });
    }

    std::sort(entries.begin(), entries.end(), [](const MetafileEntry& a, const MetafileEntry& b) {
        if (a.size != b.size) return a.size > b.size;
        return a.name < b.name;
    });

    struct TableEntry {
        std::string first;
        std::string second;
        std::string third;
        int first_len = 0;
        int second_len = 0;
        int third_len = 0;
        bool is_top_level = false;
    };

    std::vector<TableEntry> table;
    for (const auto& entry : entries) {
        std::string second = pretty_print_byte_count(entry.size);
        std::string third = "100.0%";
        table.push_back(TableEntry{
            entry.name, second, third,
            rune_count(entry.name), static_cast<int>(second.size()), static_cast<int>(third.size()),
            true,
        });

        for (size_t j = 0; j < entry.entries.size(); j++) {
            const auto& child = entry.entries[j];
            std::string indent = (j + 1 == entry.entries.size()) ? " \xe2\x94\x94 " : " \xe2\x94\x9c ";
            double percent = 100.0 * static_cast<double>(child.size) /
                             static_cast<double>(entry.size);
            std::string first = indent + child.name;
            std::string second_c = pretty_print_byte_count(child.size);
            std::string third_c = std::format("{:.1f}%", percent);
            table.push_back(TableEntry{
                first, second_c, third_c,
                rune_count(first), static_cast<int>(second_c.size()), static_cast<int>(third_c.size()),
                false,
            });
        }
    }

    int max_first = 0, max_second = 0, max_third = 0;
    for (const auto& e : table) {
        if (e.first_len > max_first) max_first = e.first_len;
        if (e.second_len > max_second) max_second = e.second_len;
        if (e.third_len > max_third) max_third = e.third_len;
    }

    std::string sb;
    for (const auto& entry : table) {
        std::string prefix = entry.is_top_level ? "\n" : "";
        if (entry.second.empty() && entry.third.empty()) {
            sb += prefix + "  " + entry.first + "\n";
            continue;
        }
        std::string line_char = options.verbose ? "\xe2\x94\x80" : " ";
        int extra = options.verbose ? 1 : 0;
        std::string first_colored = entry.first;
        std::string second_trimmed = entry.second;
        size_t last_nonspace = second_trimmed.find_last_not_of(' ');
        if (last_nonspace != std::string::npos) second_trimmed.resize(last_nonspace + 1);
        int filler1 = extra + max_first - entry.first_len + max_second - entry.second_len;
        int filler2 = extra + max_third - entry.third_len + static_cast<int>(entry.second.size()) - static_cast<int>(second_trimmed.size());

        sb += prefix + "  " + first_colored;
        sb += " " + repeat_unit(filler1, line_char);
        sb += " " + second_trimmed;
        sb += " " + repeat_unit(filler2, line_char);
        sb += " " + entry.third + "\n";
    }

    return sb;
}

} // namespace guchho::api: public entry points above, machinery below