// =============================================================================
// src/cli/cli_options.cpp — the flag grammar
// =============================================================================
//
// A command line arrives here as a vector of strings and leaves as two option
// structures, a handful of run settings, and either nothing or one complaint
// about the first thing it did not understand. This file is the whole of that
// translation: every flag Guchho accepts is decided in parseOptionsImpl below,
// and the four functions around it are the ways in.
//
// The grammar has one unusual property, and most of the shape of this file
// follows from it: there are two things a command line can be describing, a
// build of a graph of files or a transform of a single input, and their option
// structures overlap almost completely. Minifying, shortening property names,
// dropping statements, choosing a target — all of that means the same thing for
// both. Only a handful of settings are meaningful for one and not the other.
//
// So rather than write the grammar twice, parseOptionsImpl fills in whichever
// structure it was handed. Every branch asks which one it has and writes to
// that, and the branches that are meaningless for a transform are simply not
// taken. A build-only flag reaching a transform run is not caught by a check of
// its own; it falls past every branch that could claim it and arrives at the
// unknown-flag block at the bottom, which is where "this command does not take
// that" and "that is not a flag" are the same message.
//
// Four things are worth knowing before reading the code.
//
// One pass, in order, first complaint wins. The loop walks the arguments once,
// left to right, and returns the moment a value is rejected. Everything read
// before that point stays in the structures it was read into, because a caller
// that reports one problem at a time still benefits from knowing what was
// understood. Nothing is rolled back and nothing is reported twice.
//
// Order is the tiebreaker. A command line may say the same thing twice, and the
// last mention wins because each branch simply overwrites what it finds. The
// branches are ordered from most specific to least, and each one ends in
// "continue", so an argument is claimed by the first rule that recognises it
// and cannot be claimed twice.
//
// Three settings are not part of either structure, because they describe the
// run rather than the build: watching, the delay before a watch fires, and the
// paths of the two files the run is asked to write beside its output. They are
// collected in ParseOptionsExtras, which is filled whether the run turns out to
// be a build or a transform.
//
// And "kind" decides who is asking. The command line itself wants a full
// complaint with a suggestion attached; a host program calling
// ParseBuildOptions() has asked what a command line would mean, and a note
// about a command line spelling is not an answer to that question. Both are
// answered by the same code and the same errors, and the kind selects the few
// places where the two differ.
// =============================================================================
#include "guchho/api.hpp"
#include "guchho/cli.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace guchho::cli {
// =============================================================================
// The grammar
// =============================================================================
//
// Reads every flag in "os_args" into whichever of the two option structures was
// handed in, stopping at the first complaint and returning it.
//
// Exactly one of "build_opts" and "transform_opts" is expected to be non-null,
// and that is the only thing that decides where a value goes. A caller that
// passed neither would find every branch quietly doing nothing, and a caller
// that passed both would find the build structure winning throughout, since
// every branch tests it first. Neither is checked for, because every caller in
// the project is one of the four functions further down this file and each of
// those passes exactly one.
//
// "extras" is filled in either way. A transform has no watching of its own, but
// a caller that has asked for it is entitled to find out, and keeping one
// out-parameter for all three run settings is simpler than making it optional.
//
// The result is an empty optional on success. On failure it holds the complaint
// and the note that says how to fix it, and both structures keep whatever was
// read before the argument that failed.
//
// Input:  { "--bundle", "--minify", "--outdir=dist", "src/index.js" } with a
//         build structure and ParseOptionsKind::kInternal
// Output: an empty optional, and a build with bundling on, all three minify
//         switches on, outdir "dist", and one entry point "src/index.js".
//
// Input:  { "--minify" } with a transform structure
// Output: an empty optional, and a transform with minify_syntax,
//         minify_whitespace and minify_identifiers all set.
//
// Input:  { "--outdir=dist" } with a transform structure
// Output: a complaint reading "Invalid transform flag: \"--outdir=dist\"",
//         because no branch above claims a flag that describes a graph, and a
//         transform writes one file to standard output.
//
// Input:  { "--platform=browser", "--nope" } with a build structure
// Output: a complaint reading "Invalid build flag: \"--nope\"", and a build
//         whose platform is already browser — the flag before the failure is
//         still in place.
std::optional<ErrorWithNote> parseOptionsImpl(
    const std::vector<std::string>& os_args,
    api::BuildOptions* build_opts,
    api::TransformOptions* transform_opts,
    ParseOptionsKind kind,
    ParseOptionsExtras& extras)
{
    // Whether the last thing said about source maps was a bare "--sourcemap"
    // with no value attached. It is needed after the loop rather than at the
    // branch, because whether a map can be linked depends on flags that may not
    // have been seen yet: the answer needs the output location, and the output
    // location can be named after the map.
    bool has_bare_sourcemap_flag = false;

    for (const auto& arg : os_args) {
        if (isBoolFlag(arg, "--bundle") && build_opts) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            build_opts->bundle = value;
            continue;
        }
        if (isBoolFlag(arg, "--preserve-symlinks") && build_opts) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            build_opts->preserve_symlinks = value;
            continue;
        }
        if (isBoolFlag(arg, "--splitting") && build_opts) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            build_opts->splitting = value;
            continue;
        }
        if (isBoolFlag(arg, "--allow-overwrite") && build_opts) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            build_opts->allow_overwrite = value;
            continue;
        }
        // -------------------------------------------------------------------------
        // Watching
        // -------------------------------------------------------------------------
        //
        // The two flags that belong to the run rather than to either option
        // structure, and the only two that are accepted for a transform as well
        // as a build. A transform has nothing to watch, but refusing the flag
        // here would mean a command line could not be forwarded to either kind
        // of run without knowing which one it was going to be — and the caller
        // that forwards one is the dispatcher, before it has decided.
        if (isBoolFlag(arg, "--watch")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            extras.watch = value;
            continue;
        }
        if (arg.starts_with("--watch-delay=")) {
            auto value = arg.substr(std::string_view("--watch-delay=").size());
            // The hint is passed in rather than written here because only the
            // caller knows what the number is for, and "The watch delay must be
            // an integer." says something a person can act on where a generic
            // complaint about a value would not.
            std::optional<ErrorWithNote> err;
            extras.watch_delay = parseInt(value, arg, "The watch delay must be an integer.", err);
            if (err) return err;
            continue;
        }
        // -------------------------------------------------------------------------
        // Minification
        // -------------------------------------------------------------------------
        //
        // One flag for all of it and three for the parts, and the reason there
        // are three parts is that they are genuinely separate decisions: removing
        // whitespace, shortening names and rewriting the syntax are three
        // different amounts of work and three different kinds of damage from
        // getting one of them wrong. A person who wants output they can still
        // debug switches one off and leaves the others.
        if (isBoolFlag(arg, "--minify")) {
            // The shorthand, and the reason the three fields below are written
            // out three times rather than looped: the two structures spell them
            // differently and there is no common base to loop over, and a loop
            // with a cast per iteration would be longer and less obvious about
            // which field is which.
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            if (build_opts) {
                build_opts->minify_syntax = value;
                build_opts->minify_whitespace = value;
                build_opts->minify_identifiers = value;
            } else {
                transform_opts->minify_syntax = value;
                transform_opts->minify_whitespace = value;
                transform_opts->minify_identifiers = value;
            }
            continue;
        }
        if (isBoolFlag(arg, "--minify-syntax")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            if (build_opts) build_opts->minify_syntax = value;
            else transform_opts->minify_syntax = value;
            continue;
        }
        if (isBoolFlag(arg, "--minify-whitespace")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            if (build_opts) build_opts->minify_whitespace = value;
            else transform_opts->minify_whitespace = value;
            continue;
        }
        if (isBoolFlag(arg, "--minify-identifiers")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            if (build_opts) build_opts->minify_identifiers = value;
            else transform_opts->minify_identifiers = value;
            continue;
        }
        // -------------------------------------------------------------------------
        // Shortening property names
        // -------------------------------------------------------------------------
        //
        // Four flags with four different shapes, and the shapes are the point.
        // Two take a list of names as one string, one takes a switch that
        // becomes an enum, and one takes a switch that stays a bool — because
        // whether "not asked" is different from "asked and off" depends on
        // whether the engine has an opinion of its own.
        if (isBoolFlag(arg, "--mangle-quoted")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            // An enum rather than a bool, so that the three states are
            // distinguishable: quoted properties are shortened, are left alone,
            // or are left alone because nobody said. A bool could not say the
            // third thing, and the default for this option is a decision the
            // engine makes rather than one a command line defaults into.
            auto mq = value ? api::MangleQuoted::kTrue : api::MangleQuoted::kFalse;
            if (build_opts) build_opts->mangle_quoted = mq;
            else transform_opts->mangle_quoted = mq;
            continue;
        }
        if (arg.starts_with("--mangle-props=")) {
            auto value = arg.substr(std::string_view("--mangle-props=").size());
            if (build_opts) build_opts->mangle_props = value;
            else transform_opts->mangle_props = value;
            continue;
        }
        if (arg.starts_with("--reserve-props=")) {
            auto value = arg.substr(std::string_view("--reserve-props=").size());
            if (build_opts) build_opts->reserve_props = value;
            else transform_opts->reserve_props = value;
            continue;
        }
        if (arg.starts_with("--mangle-cache=") && build_opts && kind == ParseOptionsKind::kInternal) {
            // The one flag gated on the kind as well as on the structure, and
            // the reason is that the path is only meaningful to a process that
            // is about to run a build and write the file back. A host asking
            // what a command line means has no file to write, so the external
            // grammar reports this as a flag it does not know.
            extras.mangle_cache = arg.substr(std::string_view("--mangle-cache=").size());
            continue;
        }
        // -------------------------------------------------------------------------
        // Removing code
        // -------------------------------------------------------------------------
        //
        // Statements that are useful while writing and dead weight in a
        // shipped file. The colon form is repeatable and accumulates, which is
        // the one place in the grammar where saying a flag twice does not mean
        // the last one wins.
        if (arg.starts_with("--drop:")) {
            auto value = arg.substr(std::string_view("--drop:").size());
            // Or-ed into the existing set rather than assigned, so that
            // "--drop:console --drop:debugger" asks for both. Assigning would
            // make the order of a command line decide the result, and a flag
            // that means something different depending on where it appears is a
            // flag nobody can put in a script.
            if (value == "console") {
                if (build_opts) build_opts->drop = build_opts->drop | api::DropFlags::kDropConsole;
                else transform_opts->drop = transform_opts->drop | api::DropFlags::kDropConsole;
            } else if (value == "debugger") {
                if (build_opts) build_opts->drop = build_opts->drop | api::DropFlags::kDropDebugger;
                else transform_opts->drop = transform_opts->drop | api::DropFlags::kDropDebugger;
            } else {
                return MakeErrorWithNote(
                    "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
                    "Valid values are \"console\" or \"debugger\".");
            }
            continue;
        }
        if (arg.starts_with("--drop-labels=")) {
            auto value = arg.substr(std::string_view("--drop-labels=").size());
            // Split through the shared splitter rather than by hand, because an
            // empty label in the middle of the list is something a person meant
            // to write and this is the one list where dropping it quietly would
            // remove a statement they asked to keep.
            auto labels = splitWithEmptyCheck(value, ',');
            if (build_opts) build_opts->drop_labels = labels;
            else transform_opts->drop_labels = labels;
            continue;
        }
        if (arg.starts_with("--legal-comments=")) {
            auto value = arg.substr(std::string_view("--legal-comments=").size());
            api::LegalComments lc;
            if (value == "none")     lc = api::LegalComments::kNone;
            else if (value == "inline")   lc = api::LegalComments::kInline;
            else if (value == "eof")      lc = api::LegalComments::kEndOfFile;
            else if (value == "linked")   lc = api::LegalComments::kLinked;
            else if (value == "external") lc = api::LegalComments::kExternal;
            else {
                return MakeErrorWithNote(
                    "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
                    "Valid values are \"none\", \"inline\", \"eof\", \"linked\", or \"external\".");
            }
            if (build_opts) build_opts->legal_comments = lc;
            else transform_opts->legal_comments = lc;
            continue;
        }
        if (arg.starts_with("--charset=")) {
            auto name = arg.substr(std::string_view("--charset=").size());
            api::Charset cs;
            if (name == "ascii")      cs = api::Charset::kASCII;
            else if (name == "utf8") cs = api::Charset::kUTF8;
            else {
                return MakeErrorWithNote(
                    "Invalid value " + helpers::QuoteSingle(name, true) + " in " + helpers::QuoteSingle(arg, true),
                    "Valid values are \"ascii\" or \"utf8\".");
            }
            if (build_opts) build_opts->charset = cs;
            else transform_opts->charset = cs;
            continue;
        }
        // -------------------------------------------------------------------------
        // What the output is allowed to contain
        // -------------------------------------------------------------------------
        if (isBoolFlag(arg, "--tree-shaking")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            // The same three-state enum as above, and for the same reason: the
            // engine decides what to do about unused exports when nobody has
            // said, so "off" and "not asked" cannot be the same value.
            auto ts = value ? api::TreeShaking::kTrue : api::TreeShaking::kFalse;
            if (build_opts) build_opts->tree_shaking = ts;
            else transform_opts->tree_shaking = ts;
            continue;
        }
        if (isBoolFlag(arg, "--ignore-annotations")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            if (build_opts) build_opts->ignore_annotations = value;
            else transform_opts->ignore_annotations = value;
            continue;
        }
        if (isBoolFlag(arg, "--keep-names")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            if (build_opts) build_opts->keep_names = value;
            else transform_opts->keep_names = value;
            continue;
        }
        // -------------------------------------------------------------------------
        // Source maps
        // -------------------------------------------------------------------------
        //
        // The one flag in this file whose meaning depends on which of the two
        // structures is being filled, and the reason is the shape of the output
        // rather than the shape of the build. A build writes several files and
        // has somewhere to put a map beside them, so a bare request means a
        // linked map. A transform writes one file to a stream that is often a
        // pipe, where the only place a map fits is inside the file itself.
        if (arg == "--sourcemap") {
            if (build_opts) build_opts->sourcemap = api::SourceMap::kLinked;
            else transform_opts->sourcemap = api::SourceMap::kInline;
            // Remembered rather than decided, because whether linked is even
            // possible is not known yet — see the end of the function.
            has_bare_sourcemap_flag = true;
            continue;
        }
        if (arg.starts_with("--sourcemap=")) {
            auto value = arg.substr(std::string_view("--sourcemap=").size());
            api::SourceMap sm;
            if (value == "linked")           sm = api::SourceMap::kLinked;
            else if (value == "inline")     sm = api::SourceMap::kInline;
            else if (value == "external")   sm = api::SourceMap::kExternal;
            else if (value == "both")       sm = api::SourceMap::kInlineAndExternal;
            else {
                return MakeErrorWithNote(
                    "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
                    "Valid values are \"linked\", \"inline\", \"external\", or \"both\".");
            }
            if (build_opts) build_opts->sourcemap = sm;
            else transform_opts->sourcemap = sm;
            // Cleared, and this is the one place the tracker can be cancelled.
            // An explicit choice says what should happen, and the repair at the
            // end of the function exists only to rescue a request that was never
            // specific enough to have an opinion.
            has_bare_sourcemap_flag = false;
            continue;
        }
        if (arg.starts_with("--source-root=")) {
            auto value = arg.substr(std::string_view("--source-root=").size());
            if (build_opts) build_opts->source_root = value;
            else transform_opts->source_root = value;
            continue;
        }
        if (isBoolFlag(arg, "--sources-content")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            // Two states and no third, so a bool-shaped switch is enough here:
            // the engine's own default is to include the sources, and there is
            // no opinion for a command line to defer to.
            auto sc = value ? api::SourcesContent::kInclude : api::SourcesContent::kExclude;
            if (build_opts) build_opts->sources_content = sc;
            else transform_opts->sources_content = sc;
            continue;
        }
        if (arg.starts_with("--sourcefile=")) {
            auto value = arg.substr(std::string_view("--sourcefile=").size());
            // The name a piece of input should be reported under, which for a
            // build is only meaningful for input that arrived on the standard
            // input — so it goes into the stdin structure, creating that
            // structure if this is the first flag to need it.
            if (build_opts) {
                if (!build_opts->stdin_data.has_value()) {
                    build_opts->stdin_data = api::StdinOptions{};
                }
                build_opts->stdin_data->sourcefile = value;
            } else {
                transform_opts->sourcefile = value;
            }
            continue;
        }
        // -------------------------------------------------------------------------
        // Finding the files
        // -------------------------------------------------------------------------
        //
        // How an import of a name with no extension is turned into a path, and
        // which of a package's fields are read to find its entry. All build-only,
        // because a transform is given the file it is reading and has nothing to
        // resolve.
        if (arg.starts_with("--resolve-extensions=") && build_opts) {
            auto value = arg.substr(std::string_view("--resolve-extensions=").size());
            build_opts->resolve_extensions = splitWithEmptyCheck(value, ',');
            continue;
        }
        if (arg.starts_with("--main-fields=") && build_opts) {
            auto value = arg.substr(std::string_view("--main-fields=").size());
            build_opts->main_fields = splitWithEmptyCheck(value, ',');
            continue;
        }
        if (arg.starts_with("--conditions=") && build_opts) {
            auto value = arg.substr(std::string_view("--conditions=").size());
            build_opts->conditions = splitWithEmptyCheck(value, ',');
            continue;
        }
        if (arg.starts_with("--public-path=") && build_opts) {
            build_opts->public_path = arg.substr(std::string_view("--public-path=").size());
            continue;
        }
        if (arg.starts_with("--global-name=")) {
            auto value = arg.substr(std::string_view("--global-name=").size());
            if (build_opts) build_opts->global_name = value;
            else transform_opts->global_name = value;
            continue;
        }
        // -------------------------------------------------------------------------
        // Files written beside the output
        // -------------------------------------------------------------------------
        //
        // The metafile, which is the description of what a build read and
        // produced, and which the size summary is made from. The two spellings
        // are for two different callers: the bare switch says "produce one" and
        // lets the command decide where it goes, while the valued form names
        // the path and is only understood by the command line itself.
        if (arg == "--metafile" && build_opts && kind == ParseOptionsKind::kExternal) {
            build_opts->metafile = true;
            continue;
        }
        if (arg.starts_with("--metafile=") && build_opts && kind == ParseOptionsKind::kInternal) {
            // Both halves on purpose. The boolean is what the engine reads when
            // it decides whether to produce the text, and the path in extras is
            // what the command reads when it decides where to put it — a
            // decision that belongs to the run rather than to the build.
            build_opts->metafile = true;
            extras.metafile = arg.substr(std::string_view("--metafile=").size());
            continue;
        }
        // -------------------------------------------------------------------------
        // Where the output goes
        // -------------------------------------------------------------------------
        //
        // Three ways of saying it, and they are alternatives rather than
        // settings: one file at a known name, a directory to fill, and a base
        // directory that the output paths are taken relative to. All build-only,
        // since a transform's output is a stream.
        if (arg.starts_with("--outfile=") && build_opts) {
            build_opts->outfile = arg.substr(std::string_view("--outfile=").size());
            continue;
        }
        if (arg.starts_with("--outdir=") && build_opts) {
            build_opts->outdir = arg.substr(std::string_view("--outdir=").size());
            continue;
        }
        if (arg.starts_with("--outbase=") && build_opts) {
            build_opts->outbase = arg.substr(std::string_view("--outbase=").size());
            continue;
        }
        if (arg.starts_with("--tsconfig=") && build_opts) {
            build_opts->tsconfig = arg.substr(std::string_view("--tsconfig=").size());
            continue;
        }
        if (arg.starts_with("--tsconfig-raw=")) {
            auto value = arg.substr(std::string_view("--tsconfig-raw=").size());
            // The inline counterpart of the flag above, and unlike it accepted
            // for a transform, because a transform has the same question to ask
            // about a compiler's settings and none of the graph that would make
            // reading them from a file worthwhile.
            if (build_opts) build_opts->tsconfig_raw = value;
            else transform_opts->tsconfig_raw = value;
            continue;
        }
        if (arg.starts_with("--entry-names=") && build_opts) {
            build_opts->entry_names = arg.substr(std::string_view("--entry-names=").size());
            continue;
        }
        if (arg.starts_with("--chunk-names=") && build_opts) {
            build_opts->chunk_names = arg.substr(std::string_view("--chunk-names=").size());
            continue;
        }
        if (arg.starts_with("--asset-names=") && build_opts) {
            build_opts->asset_names = arg.substr(std::string_view("--asset-names=").size());
            continue;
        }
        // -------------------------------------------------------------------------
        // Substitutions
        // -------------------------------------------------------------------------
        //
        // The colon flags from here on. The colon is the grammar's way of
        // saying "this flag may be given more than once", which an equals sign
        // cannot say, and the part after it is split on the first "=" so that a
        // value is free to contain one — an injected path with a query string in
        // it, a definition whose replacement is itself a pair.
        if (arg.starts_with("--define:")) {
            auto value = arg.substr(std::string_view("--define:").size());
            auto eq = value.find('=');
            if (eq == std::string::npos) {
                return MakeErrorWithNote(
                    "Missing \"=\" in " + helpers::QuoteSingle(arg, true),
                    "You need to use \"=\" to specify both the original value and the replacement value. "
                    "For example, \"--define:DEBUG=true\" replaces \"DEBUG\" with \"true\".");
            }
            if (build_opts) build_opts->define[value.substr(0, eq)] = value.substr(eq + 1);
            else transform_opts->define[value.substr(0, eq)] = value.substr(eq + 1);
            continue;
        }
        if (arg.starts_with("--log-override:")) {
            auto value = arg.substr(std::string_view("--log-override:").size());
            auto eq = value.find('=');
            if (eq == std::string::npos) {
                return MakeErrorWithNote(
                    "Missing \"=\" in " + helpers::QuoteSingle(arg, true),
                    "You need to use \"=\" to specify both the message name and the log level.");
            }
            api::LogLevel ll;
            if (auto err = parseLogLevel(value.substr(eq + 1), arg, ll)) return err;
            if (build_opts) build_opts->log_override[value.substr(0, eq)] = ll;
            else transform_opts->log_override[value.substr(0, eq)] = ll;
            continue;
        }
        if (arg.starts_with("--abs-paths=")) {
            auto value = arg.substr(std::string_view("--abs-paths=").size());
            auto values = splitWithEmptyCheck(value, ',');
            // Accumulated into a small integer rather than stored as a list,
            // because the engine reads a set of bits rather than a list of
            // words, and because the three names are independent: asking for
            // absolute paths in the metafile has nothing to do with asking for
            // them in the printed messages.
            uint8_t flags = 0;
            for (const auto& v : values) {
                if (v == "code")      flags |= static_cast<uint8_t>(api::AbsPathsFlags::kCodeAbsPath);
                else if (v == "log")  flags |= static_cast<uint8_t>(api::AbsPathsFlags::kLogAbsPath);
                else if (v == "metafile") flags |= static_cast<uint8_t>(api::AbsPathsFlags::kMetafileAbsPath);
                else {
                    return MakeErrorWithNote(
                        "Invalid value " + helpers::QuoteSingle(v, true) + " in " + helpers::QuoteSingle(arg, true),
                        "Valid values are \"code\", \"log\", or \"metafile\".");
                }
            }
            auto ap = static_cast<api::AbsPathsFlags>(flags);
            if (build_opts) build_opts->abs_paths = ap;
            else transform_opts->abs_paths = ap;
            continue;
        }
        if (arg.starts_with("--supported:")) {
            auto value = arg.substr(std::string_view("--supported:").size());
            auto eq = value.find('=');
            if (eq == std::string::npos) {
                return MakeErrorWithNote(
                    "Missing \"=\" in " + helpers::QuoteSingle(arg, true),
                    "You need to use \"=\" to specify both the feature name and whether it is supported.");
            }
            // The whole argument is handed to the switch reader rather than the
            // part after the first "=", which works because the reader takes
            // everything after the first "=" itself — and works only while the
            // remainder is exactly "true" or "false". A feature name
            // containing an "=" therefore cannot be recorded, and the
            // complaint quotes the whole flag so the mismatch is visible.
            auto [is_supported, bool_err] = parseBoolFlag(arg, true);
            if (bool_err) return bool_err;
            if (build_opts) build_opts->supported[value.substr(0, eq)] = is_supported;
            else transform_opts->supported[value.substr(0, eq)] = is_supported;
            continue;
        }
        if (arg.starts_with("--pure:")) {
            auto value = arg.substr(std::string_view("--pure:").size());
            // Pushed rather than stored in a map: the same annotation may
            // legitimately be declared more than once by two different files,
            // and a map would quietly keep only one of them.
            if (build_opts) build_opts->pure.push_back(value);
            else transform_opts->pure.push_back(value);
            continue;
        }
        // -------------------------------------------------------------------------
        // How a file is read
        // -------------------------------------------------------------------------
        //
        // Two flags, one shape each. The colon form names a single extension and
        // goes into the build's table of per-extension choices; the equals form
        // sets the choice for every file at once, which for a build means the
        // standard input and for a transform means the one file being read.
        if (arg.starts_with("--loader:") && build_opts) {
            auto value = arg.substr(std::string_view("--loader:").size());
            auto eq = value.find('=');
            if (eq == std::string::npos) {
                return MakeErrorWithNote(
                    "Missing \"=\" in " + helpers::QuoteSingle(arg, true),
                    "You need to specify the file extension that the loader applies to.");
            }
            auto ext = value.substr(0, eq);
            auto text = value.substr(eq + 1);
            api::Loader loader;
            // The list of accepted names comes from the same table that accepts
            // them, so a name that is offered as a suggestion cannot stop being
            // accepted without the message changing with it.
            if (auto err = ParseLoader(text, loader)) {
                return MakeErrorWithNote(*err, "Valid loaders are \"js\", \"jsx\", \"ts\", \"tsx\", \"css\", \"json\", \"text\", \"base64\", \"dataurl\", \"file\", \"copy\", \"binary\", \"empty\", \"default\", \"local-css\", \"global-css\", and \"html\".");
            }
            build_opts->loader[ext] = loader;
            continue;
        }
        if (arg.starts_with("--loader=")) {
            auto value = arg.substr(std::string_view("--loader=").size());
            api::Loader loader;
            if (auto err = ParseLoader(value, loader)) {
                return MakeErrorWithNote(*err, "Valid loaders are \"js\", \"jsx\", \"ts\", \"tsx\", \"css\", \"json\", \"text\", \"base64\", \"dataurl\", \"file\", \"copy\", \"binary\", \"empty\", \"default\", \"local-css\", \"global-css\", and \"html\".");
            }
            // Two of the loaders need somewhere to put a second output file, and
            // a transform has no second file to put it in — it has a stream.
            // Saying so here, rather than letting the build fail later with a
            // complaint about an output path, is the difference between a
            // message about the flag and a message about a consequence.
            if (loader == api::Loader::kFile || loader == api::Loader::kCopy) {
                return MakeErrorWithNote(
                    helpers::QuoteSingle(value, true) + " is not supported when transforming stdin",
                    "Using guchho to transform stdin only generates one output file, so you cannot use the " +
                    helpers::QuoteSingle(value, true) + " loader since that needs to generate two output files.");
            }
            if (build_opts) {
                if (!build_opts->stdin_data.has_value()) {
                    build_opts->stdin_data = api::StdinOptions{};
                }
                build_opts->stdin_data->loader = loader;
            } else {
                transform_opts->loader = loader;
            }
            continue;
        }
        // -------------------------------------------------------------------------
        // What the output is allowed to assume
        // -------------------------------------------------------------------------
        //
        // The target, the platform and the module format: three settings that
        // describe the machine the output will run on rather than what it
        // contains, and all three accepted for a transform because a transform
        // has exactly as much opinion about the destination as a build does.
        if (arg.starts_with("--target=")) {
            auto value = arg.substr(std::string_view("--target=").size());
            auto targets = splitWithEmptyCheck(value, ',');
            api::Target target = api::Target::kDefault;
            std::vector<api::Engine> engines;
            // A list, and one call, because a target can name a language level
            // or several engines with versions and the two accumulate: a build
            // for a recent level and an old engine is a normal request.
            if (auto err = parseTargets(targets, arg, target, engines)) return err;
            if (build_opts) {
                build_opts->target = target;
                build_opts->engines = engines;
            } else {
                transform_opts->target = target;
                transform_opts->engines = engines;
            }
            continue;
        }
        if (arg.starts_with("--out-extension:") && build_opts) {
            auto value = arg.substr(std::string_view("--out-extension:").size());
            auto eq = value.find('=');
            if (eq == std::string::npos) {
                return MakeErrorWithNote(
                    "Missing \"=\" in " + helpers::QuoteSingle(arg, true),
                    "You need to use either \"--out-extension:.js=...\" or \"--out-extension:.css=...\"");
            }
            build_opts->out_extension[value.substr(0, eq)] = value.substr(eq + 1);
            continue;
        }
        if (arg.starts_with("--platform=")) {
            auto value = arg.substr(std::string_view("--platform=").size());
            api::Platform platform;
            if (value == "browser")      platform = api::Platform::kBrowser;
            else if (value == "node")    platform = api::Platform::kNode;
            else if (value == "neutral") platform = api::Platform::kNeutral;
            else {
                return MakeErrorWithNote(
                    "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
                    "Valid values are \"browser\", \"node\", or \"neutral\".");
            }
            if (build_opts) build_opts->platform = platform;
            else transform_opts->platform = platform;
            continue;
        }
        if (arg.starts_with("--format=")) {
            auto value = arg.substr(std::string_view("--format=").size());
            api::Format format;
            if (value == "iife")          format = api::Format::kIIFE;
            else if (value == "cjs")      format = api::Format::kCommonJS;
            else if (value == "esm")      format = api::Format::kESModule;
            else if (value == "umd")      format = api::Format::kUMD;
            else if (value == "amd")      format = api::Format::kAMD;
            else if (value == "system")   format = api::Format::kSystem;
            else {
                return MakeErrorWithNote(
                    "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
                    "Valid values are \"iife\", \"cjs\", \"esm\", \"umd\", \"amd\", or \"system\".");
            }
            if (build_opts) build_opts->format = format;
            else transform_opts->format = format;
            continue;
        }
        // -------------------------------------------------------------------------
        // What is in the graph
        // -------------------------------------------------------------------------
        //
        // The last build-only group, and the one where the colon form is most
        // load-bearing: a build usually has many packages to treat the same
        // way, and a list spelled as one flag can be appended to across
        // several of them.
        if (arg.starts_with("--packages=") && build_opts) {
            auto value = arg.substr(std::string_view("--packages=").size());
            api::Packages packages;
            if (value == "bundle")        packages = api::Packages::kBundle;
            else if (value == "external") packages = api::Packages::kExternal;
            else {
                return MakeErrorWithNote(
                    "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
                    "Valid values are \"bundle\" or \"external\".");
            }
            build_opts->packages = packages;
            continue;
        }
        if (arg.starts_with("--external:") && build_opts) {
            build_opts->external.push_back(arg.substr(std::string_view("--external:").size()));
            continue;
        }
        if (arg.starts_with("--inject:") && build_opts) {
            build_opts->inject.push_back(arg.substr(std::string_view("--inject:").size()));
            continue;
        }
        if (arg.starts_with("--alias:") && build_opts) {
            auto value = arg.substr(std::string_view("--alias:").size());
            auto eq = value.find('=');
            if (eq == std::string::npos) {
                return MakeErrorWithNote(
                    "Missing \"=\" in " + helpers::QuoteSingle(arg, true),
                    "You need to use \"=\" to specify both the original package name and the replacement package name.");
            }
            build_opts->alias[value.substr(0, eq)] = value.substr(eq + 1);
            continue;
        }
        // -------------------------------------------------------------------------
        // Markup in the source
        // -------------------------------------------------------------------------
        if (arg.starts_with("--jsx=")) {
            auto value = arg.substr(std::string_view("--jsx=").size());
            api::JSX jsx;
            if (value == "transform")    jsx = api::JSX::kTransform;
            else if (value == "preserve")   jsx = api::JSX::kPreserve;
            else if (value == "automatic")  jsx = api::JSX::kAutomatic;
            else {
                return MakeErrorWithNote(
                    "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
                    "Valid values are \"transform\", \"automatic\", or \"preserve\".");
            }
            if (build_opts) build_opts->jsx = jsx;
            else transform_opts->jsx = jsx;
            continue;
        }
        if (arg.starts_with("--jsx-factory=")) {
            auto value = arg.substr(std::string_view("--jsx-factory=").size());
            if (build_opts) build_opts->jsx_factory = value;
            else transform_opts->jsx_factory = value;
            continue;
        }
        if (arg.starts_with("--jsx-fragment=")) {
            auto value = arg.substr(std::string_view("--jsx-fragment=").size());
            if (build_opts) build_opts->jsx_fragment = value;
            else transform_opts->jsx_fragment = value;
            continue;
        }
        if (arg.starts_with("--jsx-import-source=")) {
            auto value = arg.substr(std::string_view("--jsx-import-source=").size());
            if (build_opts) build_opts->jsx_import_source = value;
            else transform_opts->jsx_import_source = value;
            continue;
        }
        if (isBoolFlag(arg, "--jsx-dev")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            if (build_opts) build_opts->jsx_dev = value;
            else transform_opts->jsx_dev = value;
            continue;
        }
        if (isBoolFlag(arg, "--jsx-side-effects")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            if (build_opts) build_opts->jsx_side_effects = value;
            else transform_opts->jsx_side_effects = value;
            continue;
        }
        // -------------------------------------------------------------------------
        // Text around the output
        // -------------------------------------------------------------------------
        //
        // Two pairs of flags doing the same thing at two different scopes, and
        // the difference between them is which structure can hold the answer.
        // A build produces both scripts and styles and keys its banner text by
        // which is which, so its form is a colon flag with a key. A transform
        // produces one kind of file and has a single field for the text, so its
        // form is a plain one.
        if (arg.starts_with("--banner=") && transform_opts) {
            transform_opts->banner = arg.substr(std::string_view("--banner=").size());
            continue;
        }
        if (arg.starts_with("--footer=") && transform_opts) {
            transform_opts->footer = arg.substr(std::string_view("--footer=").size());
            continue;
        }
        if (arg.starts_with("--banner:") && build_opts) {
            auto value = arg.substr(std::string_view("--banner:").size());
            auto eq = value.find('=');
            if (eq == std::string::npos) {
                return MakeErrorWithNote(
                    "Missing \"=\" in " + helpers::QuoteSingle(arg, true),
                    "You need to use either \"--banner:js=...\" or \"--banner:css=...\"");
            }
            build_opts->banner[value.substr(0, eq)] = value.substr(eq + 1);
            continue;
        }
        if (arg.starts_with("--footer:") && build_opts) {
            auto value = arg.substr(std::string_view("--footer:").size());
            auto eq = value.find('=');
            if (eq == std::string::npos) {
                return MakeErrorWithNote(
                    "Missing \"=\" in " + helpers::QuoteSingle(arg, true),
                    "You need to use either \"--footer:js=...\" or \"--footer:css=...\"");
            }
            build_opts->footer[value.substr(0, eq)] = value.substr(eq + 1);
            continue;
        }
        // -------------------------------------------------------------------------
        // How much the run says, and how it says it
        // -------------------------------------------------------------------------
        if (arg.starts_with("--log-limit=")) {
            auto value = arg.substr(std::string_view("--log-limit=").size());
            std::optional<ErrorWithNote> err;
            int limit = parseInt(value, arg, "The log limit must be a non-negative integer.", err);
            if (err) return err;
            // A second check, after the conversion, because the reader above
            // only knows what a whole number is. A limit is a count, and a
            // negative count has no meaning, so this is the first place the
            // value is compared with anything rather than merely parsed.
            if (limit < 0) {
                return MakeErrorWithNote(
                    "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
                    "The log limit must be a non-negative integer.");
            }
            if (build_opts) build_opts->log_limit = limit;
            else transform_opts->log_limit = limit;
            continue;
        }
        if (arg.starts_with("--line-limit=")) {
            auto value = arg.substr(std::string_view("--line-limit=").size());
            std::optional<ErrorWithNote> err;
            int limit = parseInt(value, arg, "The line limit must be a non-negative integer.", err);
            if (err) return err;
            // The same two steps as above, and kept as two steps rather than
            // folded into one: the number is read by shared code that cannot
            // know what the number is for, so the question of whether zero or a
            // negative value makes sense is necessarily asked here, by the only
            // caller that knows.
            if (limit < 0) {
                return MakeErrorWithNote(
                    "Invalid value " + helpers::QuoteSingle(value, true) + " in " + helpers::QuoteSingle(arg, true),
                    "The line limit must be a non-negative integer.");
            }
            if (build_opts) build_opts->line_limit = limit;
            else transform_opts->line_limit = limit;
            continue;
        }
        if (isBoolFlag(arg, "--color")) {
            auto [value, err] = parseBoolFlag(arg, true);
            if (err) return err;
            // Two states rather than three, because the third is what the
            // engine already defaults to: decide from whether the output is a
            // terminal. A switch that means "on" is therefore "always", and
            // the one that means "off" is "never" — the automatic case belongs
            // to the absence of a switch, not to one of its values.
            auto color = value ? api::StderrColor::kColorAlways : api::StderrColor::kColorNever;
            if (build_opts) build_opts->color = color;
            else transform_opts->color = color;
            continue;
        }
        if (arg.starts_with("--log-level=")) {
            auto value = arg.substr(std::string_view("--log-level=").size());
            api::LogLevel ll;
            if (auto err = parseLogLevel(value, arg, ll)) return err;
            if (build_opts) build_opts->log_level = ll;
            else transform_opts->log_level = ll;
            continue;
        }
        // -------------------------------------------------------------------------
        // Two complaints that are not about a flag's value
        // -------------------------------------------------------------------------
        if (arg.starts_with("'--")) {
            return MakeErrorWithNote(
                "Unexpected single quote character before flag: " + arg,
                "Try using double quote characters to quote arguments instead.");
        }
        if (!arg.starts_with("-") && build_opts) {
            // A bare path, and the one place in the grammar where a value can
            // be on either side of an "=". The output comes first, which is the
            // opposite of the order the two things are usually written in and
            // is worth stating here because nothing else in the file is like
            // it: "out/app.js=src/index.js" says where the result goes and what
            // it is made from, in that order.
            auto eq = arg.find('=');
            if (eq != std::string::npos) {
                build_opts->entry_points_advanced.push_back(api::EntryPoint{
                    .input_path = arg.substr(eq + 1),
                    .output_path = arg.substr(0, eq),
                });
            } else {
                build_opts->entry_points.push_back(arg);
            }
            continue;
        }
        // -------------------------------------------------------------------------
        // Everything else
        // -------------------------------------------------------------------------
        //
        // Reached by an argument no rule above claimed, which covers a flag that
        // does not exist, a flag that belongs to another command, and a
        // build-only flag given to a transform. All three produce the same
        // complaint, and the three sets below exist only to make the note say
        // something useful — they are a dictionary of spellings, not part of the
        // grammar, and nothing here is accepted because of them.

        {
            static const std::unordered_set<std::string> kBareFlags = {
                "allow-overwrite", "bundle", "ignore-annotations", "jsx-dev",
                "jsx-side-effects", "keep-names", "minify-identifiers",
                "minify-syntax", "minify-whitespace", "minify", "preserve-symlinks",
                "sourcemap", "splitting", "watch",
            };
            static const std::unordered_set<std::string> kEqualsFlags = {
                "abs-paths", "allow-overwrite", "asset-names", "banner", "bundle",
                "certfile", "charset", "chunk-names", "color", "conditions",
                "cors-origin", "drop-labels", "entry-names", "footer", "format",
                "global-name", "ignore-annotations", "jsx-factory", "jsx-fragment",
                "jsx-import-source", "jsx", "keep-names", "keyfile", "legal-comments",
                "loader", "log-level", "log-limit", "main-fields", "mangle-cache",
                "mangle-props", "mangle-quoted", "metafile", "minify-identifiers",
                "minify-syntax", "minify-whitespace", "minify", "outbase", "outdir",
                "outfile", "packages", "platform", "preserve-symlinks", "public-path",
                "reserve-props", "resolve-extensions", "serve-fallback", "serve",
                "servedir", "source-root", "sourcefile", "sourcemap", "sources-content",
                "splitting", "target", "tree-shaking", "tsconfig-raw", "tsconfig",
                "watch", "watch-delay",
            };
            static const std::unordered_set<std::string> kColonFlags = {
                "alias", "banner", "define", "drop", "external", "footer", "inject",
                "loader", "log-override", "out-extension", "pure", "supported",
            };

            // Left empty unless one of the cases below recognises the spelling,
            // which is why a complaint about a flag with no obvious correction
            // carries no note at all rather than an unhelpful one.
            std::string note;
            if (arg == "-o") {
                note = "Use \"--outfile=\" instead of \"-o\".";
            } else if (arg == "-v") {
                note = "Use \"--log-level=verbose\" instead of \"-v\".";
            } else if (arg.starts_with("--")) {
                // A colon written where an equals sign belongs, or the reverse.
                // Two separate tests rather than one chain, and they are
                // mutually exclusive: the first asks whether the colon comes
                // first, the second whether the equals sign does, so at most one
                // of them can fire and at most one suggestion is made.
                auto eq = arg.find('=');
                auto colon = arg.find(':');
                if (eq != std::string::npos && colon != std::string::npos && colon < eq) {
                    auto flag_name = arg.substr(2, colon - 2);
                    if (kColonFlags.count(flag_name)) {
                        auto fixed = arg.substr(0, colon) + ":" + arg.substr(colon + 1);
                        note = "Use " + helpers::QuoteSingle(fixed, true) + " instead of " + helpers::QuoteSingle(arg, true);
                    }
                }
                if (colon != std::string::npos && eq != std::string::npos && eq < colon) {
                    auto flag_name = arg.substr(2, eq - 2);
                    if (kEqualsFlags.count(flag_name)) {
                        auto fixed = arg.substr(0, eq) + "=" + arg.substr(eq + 1);
                        note = "Use " + helpers::QuoteSingle(fixed, true) + " instead of " + helpers::QuoteSingle(arg, true);
                    }
                }
            } else if (arg.starts_with("-")) {
                // A single dash, which is the mistake people make most often and
                // the one with the simplest explanation: the grammar has one
                // spelling for every flag, and it is two dashes.
                auto flag_no_dash = arg.substr(1);
                bool is_valid = kBareFlags.count(flag_no_dash) > 0;
                std::string fix = "-" + arg;
                auto eq = flag_no_dash.find('=');
                auto colon = flag_no_dash.find(':');
                // As above, a flag with a value attached is recognised as well as
                // a bare one, which is why the same three sets are consulted
                // here as in the two-dash cases.
                if (eq != std::string::npos && kEqualsFlags.count(flag_no_dash.substr(0, eq))) {
                    is_valid = true;
                } else if (colon != std::string::npos && kColonFlags.count(flag_no_dash.substr(0, colon))) {
                    is_valid = true;
                    fix = "-" + flag_no_dash.substr(0, colon) + ":" + flag_no_dash.substr(colon + 1);
                }
                if (is_valid) {
                    note = "Use " + helpers::QuoteSingle(fix, true) + " instead of " + helpers::QuoteSingle(arg, true) +
                           ". Flags are always specified with two dashes.";
                }
            }

            // Two messages rather than one, because "this is not a flag" is
            // useless to somebody who typed a flag and "this command does not
            // take that" is useless to somebody who did not. Which of the two
            // describes the situation is decided by which structure was
            // handed in, since that is what says what was being described.
            if (build_opts) {
                return MakeErrorWithNote("Invalid build flag: " + helpers::QuoteSingle(arg, true), note);
            } else {
                return MakeErrorWithNote("Invalid transform flag: " + helpers::QuoteSingle(arg, true), note);
            }
        }
    }

    // The bare source map, repaired once everything has been read.
    //
    // A map that is linked needs a file to be linked to, and the only place one
    // could be named is the output location — which is why this cannot be
    // decided where the flag was seen. A bare request with no output location
    // anywhere on the command line has nothing to point at, so it becomes an
    // inline map: the one form that needs no second file. The alternative would
    // be to fail a request that clearly meant something, on the grounds that it
    // did not quite mean it.
    if (build_opts && has_bare_sourcemap_flag && build_opts->outfile.empty() && build_opts->outdir.empty()) {
        build_opts->sourcemap = api::SourceMap::kInline;
    }

    return std::nullopt;
}
// =============================================================================
// The size summary, taken out of the argument list before the grammar sees it
// =============================================================================

// Removes the two analyze flags from "os_args" and reports how much was asked
// for, in one pass and in the caller's own vector.
//
// They are taken out rather than parsed because the grammar has no use for
// them. What a size summary needs is a build that kept its metafile, and that
// is an option like any other; asking the grammar to learn a flag whose only
// effect is to set another flag would have put a sentence about bundle sizes
// in the middle of a list of output settings.
//
// The argument list is modified in place rather than copied, because the caller
// has a vector it is about to hand to the grammar anyway and a second copy of
// every argument on a command line is not worth the allocation. The compaction
// is the standard one — a write index that trails the read index, and a resize
// at the end — and it is safe while iterating because the write index never
// passes the read index, so no element that has not been looked at yet is
// overwritten.
//
// Which of the three answers comes back is decided by the list as a whole, not
// by the flags: the analyze flags are only removed if something else on the
// line says the run is a build. On a line with nothing but flags, the list is
// left exactly as it was, and the grammar rejects the flag it did not expect —
// which is the same answer it would have given, arrived at without a special
// case here.
//
// If both spellings appear, the last one wins, because the loop does not stop
// at the first: a person who writes the flag twice has said it twice, and the
// second mention is the one they meant.
//
// Input:  { "src/index.js", "--analyze" }
// Output: AnalyzeMode::kEnabled, and { "src/index.js" } left in the vector.
//
// Input:  { "--analyze=verbose", "src/index.js", "--analyze" }
// Output: AnalyzeMode::kEnabled, and { "src/index.js" } left in the vector —
//         the bare flag came last.
//
// Input:  { "--analyze=verbose", "src/index.js" }
// Output: AnalyzeMode::kVerbose, and { "src/index.js" } left in the vector.
//
// Input:  { "--analyze" }
// Output: AnalyzeMode::kDisabled, and the vector unchanged, since a line of
//         nothing but flags is not yet known to be a build.
AnalyzeMode filterAnalyzeFlags(std::vector<std::string>& os_args) {
    for (const auto& arg : os_args) {
        if (isArgForBuild(arg)) {
            AnalyzeMode analyze = AnalyzeMode::kDisabled;
            size_t end = 0;
            for (const auto& a : os_args) {
                if (a == "--analyze") {
                    analyze = AnalyzeMode::kEnabled;
                } else if (a == "--analyze=verbose") {
                    analyze = AnalyzeMode::kVerbose;
                } else {
                    os_args[end++] = a;
                }
            }
            os_args.resize(end);
            return analyze;
        }
    }
    return AnalyzeMode::kDisabled;
}

// Switches on the one thing a build has to do differently when a size summary
// was asked for, which is to keep the metafile.
//
// The metafile is the description of what the build read and what it produced,
// and it is what any summary of sizes is made from, so a run that asked for
// sizes and did not keep the metafile would have nothing to measure. One
// assignment is the whole of it.
//
// The name says plugin and nothing is attached: what prints the sizes is a
// plugin the caller has already been given, and this only makes sure the data
// it needs will be there. Keeping the two apart is the point — the flag decides
// what is measured, and the plugin decides how it is shown, and a person who
// supplied their own plugin to draw it differently did not ask for the
// measurement to stop working.
//
// Input:  a build with metafile off
// Output: the same build with metafile on, and nothing else changed.
void addAnalyzePlugin(api::BuildOptions& build_options) {
    build_options.metafile = true;
}

// Attaches the plugins a host program handed to RunWithPlugins(), so that every
// build this process makes sees them.
//
// They are attached here, where the options are assembled, rather than where a
// command is dispatched. A command that assembled its own options on its own
// path would quietly produce a build with no plugins on it, and nothing in that
// code would look wrong — which is the failure mode this placement exists to
// prevent. A plugin is part of the build's configuration, so it belongs in the
// build's options from the beginning.
//
// Appended rather than assigned, because the engine's own plugins and anything
// the command attached have to survive. An empty list is not worth a branch
// beyond this one, though: the loop below is already the whole operation, and a
// guard that returns early only avoids an empty range.
//
// Input:  a build with no plugins and a list of one plugin
// Output: the same build with that plugin at the end of its plugin list.
void attachPlugins(api::BuildOptions& build_options,
                          const std::vector<api::Plugin>& plugins) {
    if (plugins.empty()) return;
    build_options.plugins.insert(build_options.plugins.end(), plugins.begin(), plugins.end());
}

// =============================================================================
// The options for a run
// =============================================================================
//
// Reads a command line for the dispatcher and applies everything that a run
// implies but a bare parse does not.
//
// Which of the two structures gets filled is not decided by a flag. It is
// decided by whether anything on the line says a build is being described: a
// bare path, or the bundling switch on its own. A line of nothing but flags is
// a transform, because a build needs something to build and the person who
// typed it has not said what. That decision is made here, once, by the same
// helper the analyze filter uses, so the two agree about what a build is.
//
// The defaults set here are the ones that belong to the command line rather
// than to the engine. The engine's own defaults describe what each option means
// and are the same for every caller; how chatty a run should be and how many
// messages it should print are questions about running the tool, and the answer
// is different for a build and for a transform.
//
// The result is a tuple rather than a structure because the two option types
// have no common base, and a caller that received both by value cannot be
// left holding a pointer into a frame that has gone. At most one of the two is
// filled; the other is a default-constructed structure. The first two elements
// are pointers to say which of them was filled, and no caller in the project
// reads them — the caller destructures the whole tuple and works from the two
// structures, comparing their contents. In the transform case both are null;
// in the build case the first points at the local that was just moved from and
// has left scope, which is another way of saying the same thing the two
// structures already say.
//
// A transform gets one check the grammar cannot make for it. A map other than
// an inline one needs a second file, and a transform has one output, so a
// request for anything else is refused here — where the message can say why,
// rather than deeper in where it could only say what went wrong.
//
// Input:  { "src/index.js", "--minify" }
// Output: a build with minify on, the log at info with a cap of six, write on,
//         no complaint.
//
// Input:  { "--minify" }
// Output: a transform with minify on, the log at info with a cap of six, write
//         left off, and no complaint.
//
// Input:  { "--sourcemap=linked" }
// Output: a complaint reading "Use \"--sourcemap\" instead of
//         \"--sourcemap=linked\" when transforming stdin", and the default
//         transform options.
//
// Input:  { "src/index.js", "--nope" }
// Output: a complaint reading "Invalid build flag: \"--nope\"", and a build
//         that already has its entry point.
ParseOptionsForRunResult parseOptionsForRun(const std::vector<std::string>& os_args,
                      const std::vector<api::Plugin>& plugins) {
    for (const auto& arg : os_args) {
        if (isArgForBuild(arg)) {
            auto build_opts = newBuildOptions();
            build_opts.log_limit = 6;
            build_opts.log_level = api::LogLevel::kInfo;
            build_opts.write = true;

            ParseOptionsExtras extras;
            auto err = parseOptionsImpl(os_args, &build_opts, nullptr, ParseOptionsKind::kInternal, extras);
            attachPlugins(build_opts, plugins);
            // Written on with plugins, and the internal kind: this is the
            // command line itself, so it is entitled to the two flags that name
            // files the run is about to write.
            return {nullptr, nullptr, std::move(build_opts), {}, std::move(extras), std::move(err)};
        }
    }

    auto transform_opts = newTransformOptions();
    transform_opts.log_limit = 6;
    transform_opts.log_level = api::LogLevel::kInfo;

    ParseOptionsExtras extras;
    auto err = parseOptionsImpl(os_args, nullptr, &transform_opts, ParseOptionsKind::kInternal, extras);

    if (!err) {
        // Checked only on the way to success, so that a command line which was
        // already rejected is reported for what was wrong with it rather than
        // for this as well — one complaint at a time, which is the same rule
        // the grammar follows.
        if (transform_opts.sourcemap != api::SourceMap::kNone &&
            transform_opts.sourcemap != api::SourceMap::kInline) {
            // The name is taken from the value so the message quotes what was
            // actually written. The default arm is unreachable while the outer
            // condition stands, and leaves the name empty rather than guessing.
            std::string mode;
            switch (transform_opts.sourcemap) {
                case api::SourceMap::kExternal:        mode = "external"; break;
                case api::SourceMap::kInlineAndExternal: mode = "both"; break;
                case api::SourceMap::kLinked:           mode = "linked"; break;
                default: break;
            }
            err = MakeErrorWithNote(
                "Use \"--sourcemap\" instead of \"--sourcemap=" + mode + "\" when transforming stdin",
                "Using guchho to transform stdin only generates one output file.");
        }
    }

    return {nullptr, nullptr, {}, std::move(transform_opts), std::move(extras), std::move(err)};
}

// =============================================================================
// Guessing the entry point when nobody named one
// =============================================================================
//
// The two functions below are the reason a bare "guchho dev" in a directory
// with an index.html does something useful. They are also the only place in the
// command line that touches the file system while reading options, and they are
// reached from the two commands that stay running long enough for a missing
// entry point to be worth guessing at.

// Looks for an index document in the places an HTML-first project puts one, and
// returns the first it finds as a path relative to the working directory.
//
// The relative path is what is returned rather than the absolute one, because
// that is what the option it fills is a list of, and because a relative path is
// what a person would have typed. The absolute form is only needed to open the
// file, and the working directory is joined to the candidate for that.
//
// The order is the order of likelihood: a document at the top of the project is
// the plainest case, the layout that "guchho init" creates puts it under src,
// and a document under public is what a project that keeps its static files
// separate looks like. Only the first three are tried, because a search that
// went looking through the tree would be guessing about a layout it cannot know.
//
// Only openability is tested. Nothing here reads the file or checks what is in
// it, so a directory called index.html would be accepted as readily as a
// document — the cost of finding out otherwise is a read of every candidate in
// three places, and the build that follows reports a bad file perfectly well.
//
// Going through the filesystem interface rather than reading the paths directly
// is what lets the same code run against an in-memory tree, and it is why a
// failure to create the filesystem is answered by declining rather than by an
// exception: the caller has a message of its own to give, and a second one
// about a working directory it cannot use would only be noise.
//
// Input:  a working directory containing src/index.html
// Output: an optional holding "src/index.html".
//
// Input:  a working directory with none of the three
// Output: an empty optional.
std::optional<std::string> findDefaultHtmlEntry() {
    filesystem::RealFsOptions fs_opts;
    std::string fs_err;
    auto fs = filesystem::MakeRealFS(fs_opts, fs_err);
    if (!fs) {
        return std::nullopt;
    }

    // A fixed list rather than something read from a configuration file: this
    // is a guess, and a guess that can be configured stops being a default and
    // becomes a setting nobody asked for. The braces are the list of candidates
    // as one argument, and the loop is short enough that the three of them read
    // better written out.
    for (const char* rel : {"index.html", "src/index.html", "public/index.html"}) {
        std::string path = fs->Join({fs->Cwd(), rel});
        auto opened = fs->OpenFile(path);
        if (opened.Ok()) {
            return std::string(rel);
        }
    }
    return std::nullopt;
}

// Puts a guessed entry point into "build_opts" when the caller named none, and
// reports whether the build can proceed.
//
// An explicit choice always wins, and there are three kinds of it. A named
// entry point is the ordinary one, either form of it. Input arriving on the
// standard input is the second, and it is noticed by the stdin structure being
// present — which is worth spelling out, because that structure is also created
// by a flag that says nothing about entry points at all, so a build that named
// a source file and a loader is treated here as having said what it is building
// and is not given a document it did not ask for. The quiet flag is the third,
// in the sense that it is the one thing about the run that this function is
// allowed to observe.
//
// When nothing was named and something is found, the guess is announced rather
// than made silently. A build that used a document nobody mentioned is
// surprising if it goes wrong, and one line saying which document was used is
// the difference between a build that can be explained and one that cannot. The
// two-space indent puts the line in the same place as the rest of a run's
// output.
//
// When nothing is found there is nothing to fall back on, and the run is
// stopped. The message lists the three ways out — write a document, scaffold a
// project, or name an entry point — because the most common cause by far is
// having run the command in a directory that has none of them yet. It goes to
// the error stream through the logger with the arguments attached, so it is
// coloured and formatted like everything else the run says, and the return
// value lets the caller stop rather than starting a build that cannot work.
//
// Input:  a build with no entry points, and an index.html in the working
//         directory, quiet off
// Output: true, one entry point "index.html", and "  Entry: index.html" on the
//         output stream.
//
// Input:  the same build with quiet on
// Output: true, one entry point, and nothing printed.
//
// Input:  the same build with the entry point already named
// Output: true, immediately, with the named entry points untouched and nothing
//         read from the file system.
//
// Input:  a build with no entry points and no index.html anywhere
// Output: false, and a complaint on the error stream beginning "No entry points
//         specified."
bool applyDefaultHtmlEntry(api::BuildOptions& build_opts,
                                  const std::vector<std::string>& os_args,
                                  bool quiet) {
    if (!build_opts.entry_points.empty() ||
        !build_opts.entry_points_advanced.empty() ||
        build_opts.stdin_data.has_value()) {
        return true;
    }

    if (auto entry = findDefaultHtmlEntry()) {
        build_opts.entry_points.push_back(*entry);
        if (!quiet) {
            std::cout << "  Entry: " << *entry << "\n";
        }
        return true;
    }

    logger::PrintErrorToStderr(os_args,
        "No entry points specified. Create an index.html, run \"guchho init\", or pass an entry point.");
    return false;
}

// =============================================================================
// Reading arguments without running anything
// =============================================================================
//
// The two public readers. Both do less than the run-level reader above, and the
// difference is the point of them: they answer a question, and they answer it
// without touching the file system, without the defaults a run applies, and
// without a command word.
//
// Neither looks for a default entry point, neither sets the log level or the
// message cap, and neither turns writing on. Everything they return came from
// the arguments and from the engine's own defaults, so a host that asks what a
// command line means gets an answer it can compare against another answer.
// Parses build flags, for a host that wants to know what a command line means
// rather than run it.
//
// The external kind, so the suggestion about the correct spelling is not
// produced: a host asking whether a set of flags makes sense has not mistyped
// anything, and a note about how to write the command line is not an answer to
// that question. The complaint itself is identical to the one a person would
// see.
//
// The options are returned even when there is a complaint, filled in as far as
// the arguments got. That is what makes a partial answer useful: a host can
// show which of the settings it did understand next to the one that was
// rejected, and a caller that only wants to know whether the command line is
// valid can ignore the first half of the pair entirely.
//
// The command word is not part of the arguments here, because a host that is
// configuring a build has already decided which build it is configuring.
//
// Input:  { "--bundle", "--outdir=dist", "src/index.js" }
// Output: a build with bundling on, outdir "dist" and one entry point, plus an
//         empty optional.
//
// Input:  { "dist/app.js=src/index.js" }
// Output: one advanced entry point whose output is "dist/app.js" and whose
//         input is "src/index.js", plus an empty optional.
//
// Input:  { "--outfil=out.js" }
// Output: the default options, and "Invalid build flag: \"--outfil=out.js\""
//         as the second half of the pair. The corrected spelling is offered to
//         the command line and withheld here.
std::pair<api::BuildOptions, std::optional<std::string>>
ParseBuildOptions(const std::vector<std::string>& os_args) {
    auto options = newBuildOptions();
    ParseOptionsExtras extras;
    auto err = parseOptionsImpl(os_args, &options, nullptr, ParseOptionsKind::kExternal, extras);
    if (err) {
        return {std::move(options), err->text};
    }
    return {std::move(options), std::nullopt};
}

// Parses transform flags, for the same reason as the function above and with the
// same rules.
//
// The difference is entirely in the grammar: the flags that describe a graph
// are not in it, so one of them reaching here produces "Invalid transform flag"
// rather than being quietly ignored. There is no guess at a default entry point
// either, because a transform is handed the file it is reading and a guessed
// document would be a different transformation altogether.
//
// Input:  { "--minify" }
// Output: a transform with all three minify switches on, plus an empty
//         optional.
//
// Input:  { "--minify-identifiers", "src/index.js" }
// Output: the default transform options, and "Invalid transform flag:
//         \"src/index.js\"" as the second half of the pair — a bare path is an
//         entry point to a build and a mistake to a transform.
std::pair<api::TransformOptions, std::optional<std::string>>
ParseTransformOptions(const std::vector<std::string>& os_args) {
    auto options = newTransformOptions();
    ParseOptionsExtras extras;
    auto err = parseOptionsImpl(os_args, nullptr, &options, ParseOptionsKind::kExternal, extras);
    if (err) {
        return {std::move(options), err->text};
    }
    return {std::move(options), std::nullopt};
}

} // namespace guchho::cli
