// Ranking a build request against the config file and the built-in defaults.
//
// Everything that decides what a build does ultimately lives in BuildOptions,
// and Build() will do exactly what that struct says. The question this file
// answers is the one in front of that: given what somebody asked for, a
// project's guchho config, and the settings Guchho ships with, what should the
// struct say?
//
// The order is fixed and it is the order a person would expect — explicit
// options, then the config file, then the built-in defaults, then whatever can
// only be worked out once those three have spoken. Explicit beats config beats
// default because each is more specific than the next: a flag is an instruction
// about this build, a config file is an instruction about this project, and a
// default is a statement about Guchho rather than about anything here.
//
// There is exactly one implementation of that ranking, and every caller shares
// it. That is the point of the file: a project driven by "guchho build" and the
// same project driven through the C++ API must not be able to disagree about
// what their own config file means.

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "guchho/api.hpp"
#include "guchho/cache.hpp"
#include "guchho/config.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/resolver.hpp"

namespace guchho::api {

namespace {

    // Whether "key" was named explicitly.
    //
    // With no ExplicitlySet, the answer is no — nothing was named, because
    // nothing could have been: the record is written by the command line as it
    // reads each flag, and a struct that never passed through it has no flag
    // anywhere in its history. Every field below also asks whether its own value
    // is the struct's sentinel, so the two questions together decide each one:
    // "the set does not have this key" and "this field still holds what a fresh
    // struct holds" both mean the same thing, and the field is then free to be
    // answered by the config file or by the built-in defaults.
    //
    // Reading a null set as "everything was named" was the other option, and it
    // is wrong in a way that only shows up later. A library caller who wrote
    // "BuildOptions opts; ResolveEffectiveBuildConfigs(opts, dir);" to ask what
    // a project builds with would get back the struct it already had — no
    // config, no "dist", nothing — and a helper that cannot answer a question
    // about a project it has already found is not an aid to anybody. A caller
    // who really has decided everything attaches a set and says so, which is
    // what the type is for and the only way to assert a value against a config
    // that would otherwise overrule it.
    bool WasSet(const BuildOptions& opts, std::string_view key)
    {
        if (!opts.explicit_set) {
            return false;
        }
        return opts.explicit_set->Has(key);
    }

    // The public option struct names an output location relative to the working
    // directory, while the config stores it as an absolute path resolved against
    // the config file's own directory. Converting on the way out keeps a config
    // file in a parent directory meaning what it means on disk, and keeps the
    // value handed to Build() in the shape Build() documents.
    std::string Relativize(filesystem::Fs& fs, const std::string& abs_path)
    {
        if (abs_path.empty()) {
            return "";
        }
        std::optional<std::string> rel = fs.Rel(fs.Cwd(), abs_path);
        if (rel && !rel->empty()) {
            return *rel;
        }
        // No relative spelling exists — a different drive, for instance. The
        // absolute path is still correct, and Abs() passes it through.
        return abs_path;
    }

    api::Format ToApiFormat(config::Format format)
    {
        switch (format) {
            case config::Format::kESModule: return api::Format::kESModule;
            case config::Format::kIIFE:     return api::Format::kIIFE;
            case config::Format::kCommonJS: return api::Format::kCommonJS;
            case config::Format::kUMD:      return api::Format::kUMD;
            case config::Format::kAMD:      return api::Format::kAMD;
            case config::Format::kSystem:   return api::Format::kSystem;
            default:                        return api::Format::kDefault;
        }
    }

    api::Platform ToApiPlatform(config::Platform platform)
    {
        switch (platform) {
            case config::Platform::kBrowser: return api::Platform::kBrowser;
            case config::Platform::kNode:    return api::Platform::kNode;
            case config::Platform::kNeutral: return api::Platform::kNeutral;
            default:                         return api::Platform::kDefault;
        }
    }

    api::SourceMap ToApiSourceMap(config::SourceMap source_map)
    {
        switch (source_map) {
            case config::SourceMap::kLinkedWithComment:      return api::SourceMap::kLinked;
            case config::SourceMap::kInline:                 return api::SourceMap::kInline;
            case config::SourceMap::kExternalWithoutComment: return api::SourceMap::kExternal;
            case config::SourceMap::kInlineAndExternal:      return api::SourceMap::kInlineAndExternal;
            default:                                         return api::SourceMap::kNone;
        }
    }

    // The tables a target string is read out of. A language level is a single
    // edition, and two names may mean the same one: "es6" and "es2015" are the
    // same edition, and both are here because both are what people type. An
    // engine is a name and a version, so it is matched by prefix and the
    // version is whatever follows.
    const std::unordered_map<std::string, api::Target>& ValidTargets()
    {
        static const std::unordered_map<std::string, api::Target> targets = {
            {"esnext", api::Target::kESNext}, {"es5",    api::Target::kES5},
            {"es6",    api::Target::kES2015}, {"es2015", api::Target::kES2015},
            {"es2016", api::Target::kES2016}, {"es2017", api::Target::kES2017},
            {"es2018", api::Target::kES2018}, {"es2019", api::Target::kES2019},
            {"es2020", api::Target::kES2020}, {"es2021", api::Target::kES2021},
            {"es2022", api::Target::kES2022}, {"es2023", api::Target::kES2023},
            {"es2024", api::Target::kES2024}, {"es2025", api::Target::kES2025},
        };
        return targets;
    }

} // namespace

const std::unordered_map<std::string, api::EngineName>& ValidEngineNames()
{
    static const std::unordered_map<std::string, api::EngineName> engines = {
        {"chrome",  api::EngineName::kChrome},   {"deno",    api::EngineName::kDeno},
        {"edge",    api::EngineName::kEdge},     {"firefox", api::EngineName::kFirefox},
        {"hermes",  api::EngineName::kHermes},   {"ie",      api::EngineName::kIE},
        {"ios",     api::EngineName::kIOS},      {"node",    api::EngineName::kNode},
        {"opera",   api::EngineName::kOpera},    {"rhino",   api::EngineName::kRhino},
        {"safari",  api::EngineName::kSafari},
    };
    return engines;
}

TargetParse ParseTargetSpec(std::string_view text, TargetSpec& out, std::string_view* bad_value)
{
    out = TargetSpec{};

    size_t start = 0;
    while (start <= text.size()) {
        size_t comma = text.find(',', start);
        std::string_view piece = (comma == std::string_view::npos)
                                     ? text.substr(start)
                                     : text.substr(start, comma - start);
        start = (comma == std::string_view::npos) ? text.size() + 1 : comma + 1;

        if (piece.empty()) {
            // A trailing comma or an empty element. "es2020," names the same
            // target as "es2020", and refusing it would be refusing a typo in a
            // way that helps nobody.
            continue;
        }

        // ASCII only, so a name typed in any ASCII case is found, and no
        // assumption is made about the case rules of a script the version might
        // be written in.
        std::string lower = helpers::ToLowerASCII(piece);

        const auto& targets = ValidTargets();
        auto             t_it = targets.find(lower);
        if (t_it != targets.end()) {
            out.target = t_it->second;
            continue;
        }

        // Not a language level, so it has to be an engine followed by a
        // version. The loop ends at the first name that matches rather than
        // continuing, because a longer name is a prefix of a shorter one and
        // "ios" would otherwise never be reached from the list above.
        bool found_engine = false;
        for (const auto& [engine_name, engine_id] : ValidEngineNames()) {
            if (!lower.starts_with(engine_name)) {
                continue;
            }
            std::string version = lower.substr(engine_name.size());
            if (version.empty()) {
                if (bad_value) {
                    *bad_value = piece;
                }
                return TargetParse::kMissingVersion;
            }
            out.engines.push_back(api::Engine{.name = engine_id, .version = version});
            found_engine = true;
            break;
        }
        if (found_engine) {
            continue;
        }

        if (bad_value) {
            *bad_value = piece;
        }
        return TargetParse::kUnknownName;
    }

    return TargetParse::kOk;
}

namespace {

// Applies one configuration's fields, then the built-in defaults, to "out".
//
// "out" already holds the explicit options, because it starts as a copy of the
// request. Nothing below is allowed to take any of those back. The function is
// called once per configuration a config file resolved to, and never sees any
// other configuration, which is what keeps one element's fields from leaking
// into another.
void ResolveOneBuild(BuildOptions& out,
                     const config::Options& cfg,
                     const std::vector<config::EntryPoint>& config_entries,
                     bool entry_from_config,
                     filesystem::Fs& fs)
{
    // The one place the parse state survives into the answer. The resolved
    // options are no longer a description of what somebody typed — they are a
    // description of what the build will do — so keeping the set on them would
    // claim that defaults were requested. Resolving the result a second time
    // therefore behaves like resolving the request, rather than treating its own
    // output as a fresh set of instructions.
    //
    // It is cleared on the way out rather than here, and that is not a detail:
    // every "WasSet" below asks "out", so an "out" whose set has already been
    // dropped reports every option as explicitly requested and the config file
    // and the built-in defaults are both skipped. A resolution that quietly
    // answers with the request it was given is the one failure this whole
    // function exists to prevent.

    // ---- 1. Explicit options ----------------------------------------------
    // Already in "out", because "out" is a copy of what was asked for. Nothing
    // below is allowed to take any of these back.

    // ---- 2. The config file ------------------------------------------------
    // Applied only where nobody spoke first. The guard on every field is the
    // whole of the precedence rule and it is spelled out on each one: a helper
    // that took the value and the key together would read as a mechanism worth
    // extending, and the answer to "is this the same kind of thing" is sometimes
    // no.
    if (!WasSet(out, kOptOutdir) && !WasSet(out, kOptOutfile) && out.outdir.empty() &&
        out.outfile.empty()) {
        if (!cfg.AbsOutputFile.empty()) {
            // A config that names a single output file names its directory as
            // a consequence. Handing Build() an outfile it can derive the
            // directory from would be correct, but handing it both is an error
            // there, so only the file is carried across.
            out.outfile = Relativize(fs, cfg.AbsOutputFile);
        } else if (!cfg.AbsOutputDir.empty()) {
            out.outdir = Relativize(fs, cfg.AbsOutputDir);
        }
    }
    if (!WasSet(out, kOptOutbase) && out.outbase.empty() && !cfg.AbsOutputBase.empty()) {
        out.outbase = Relativize(fs, cfg.AbsOutputBase);
    }
    // A format is only a question where something needs one. An unbundled entry
    // is converted or copied and the answer "leave it as it was" is the only
    // honest one, so the config's format is carried across only when the build
    // is bundling — which is also the only case in which a config's "iife" and
    // a command line's "--format iife" are statements about the same thing.
    if (!WasSet(out, kOptFormat) && out.format == api::Format::kDefault && out.bundle &&
        cfg.OutputFormat != config::Format::kPreserve) {
        out.format = ToApiFormat(cfg.OutputFormat);
    }
    // Whether "format" was somebody's decision rather than the built-in
    // default. The HTML-entry warnings need to tell an explicit format apart
    // from the "esm" installed below, so the record is taken here, before the
    // built-in default has a chance to look like a request. The config merge
    // above only decides the value; the fact that a config file named
    // "build.format" is a request even when the merge never ran (an HTML entry
    // bundles only after this function has returned), so the two are read
    // separately.
    out.format_was_explicit = WasSet(out, kOptFormat) || cfg.FormatFromConfig;
    if (!WasSet(out, kOptPlatform) && out.platform == api::Platform::kDefault) {
        out.platform = ToApiPlatform(cfg.OutputPlatform);
    }
    if (!WasSet(out, kOptTarget) && out.target == api::Target::kDefault &&
        out.engines.empty() && !cfg.OriginalTargetEnv.empty()) {
        // Read the way a command line would read it, so that a target named in a
        // config and the same target named on the command line are known to be
        // the same request rather than merely spelled the same. A value neither
        // table recognises is left alone: the build reports it, and a config
        // that names something this resolver has never heard of is not a reason
        // to refuse the whole build.
        TargetSpec spec;
        if (ParseTargetSpec(cfg.OriginalTargetEnv, spec) == TargetParse::kOk) {
            out.target  = spec.target;
            out.engines = spec.engines;
        }
    }
    if (!WasSet(out, kOptMinify)) {
        // Taken from the config as a whole, including when the config turns
        // minification off, which is the half of this that used to be missing.
        // The old guard here was "unless all three are already on", on the
        // reasoning that a request for no minification should not be undone —
        // but those three are the structure's own initializers, so a caller who
        // simply left the struct alone was indistinguishable from one who asked
        // for minification, and a project saying "minify: false" was ignored
        // unless it had also been contradicted on the command line. The
        // explicit set is the only record of somebody having said something, and
        // when there is none the config's answer is the answer.
        out.minify_whitespace  = cfg.MinifyWhitespace;
        out.minify_identifiers = cfg.MinifyIdentifiers;
        out.minify_syntax      = cfg.MinifySyntax;
    }
    if (!WasSet(out, kOptSourcemap) && out.sourcemap == api::SourceMap::kNone &&
        cfg.SourceMapData != config::SourceMap::kNone) {
        out.sourcemap = ToApiSourceMap(cfg.SourceMapData);
    }
    if (!WasSet(out, kOptSplitting) && !out.splitting) {
        out.splitting = cfg.CodeSplitting;
    }
    if (!WasSet(out, kOptTreeShaking) && out.tree_shaking == api::TreeShaking::kDefault) {
        out.tree_shaking = cfg.TreeShaking ? api::TreeShaking::kTrue
                                            : api::TreeShaking::kFalse;
    }
    if (!WasSet(out, kOptPretty) && out.pretty) {
        out.pretty = cfg.PrettyPrint;
    }
    if (!WasSet(out, kOptMinifyHtml) && !out.minify_html) {
        out.minify_html = cfg.MinifyHtml;
    }
    // The two HTML switches say opposite things about the same bytes, and the
    // printer settles it in favour of indentation when both are on. Left alone
    // that makes "--minify-html" a flag that does nothing at all on a default
    // build, because pretty printing is on by default — a request for the
    // compact form answered with the indented one, and no diagnostic to say so.
    //
    // So the request wins, and only when the request is a request: somebody who
    // named both on a command line meant what they typed, and the named
    // pretty-print flag is the one that is answered. Turning pretty off
    // automatically instead would mean "guchho build --minify-html --pretty"
    // printed indented markup, which is the confusing half of the same problem.
    if (out.minify_html && out.pretty && !WasSet(out, kOptPretty)) {
        out.pretty = false;
    }
    if (!WasSet(out, kOptEntryPoints) && out.entry_points.empty() &&
        out.entry_points_advanced.empty() && entry_from_config) {
        // Only the entries the project actually asked for. The built-in
        // "index.html" is a default, and a default does not become a build's
        // entry point behind the back of a command that named none: a build
        // with no entry point is a usage error, and that is still true.
        for (const config::EntryPoint& ep : config_entries) {
            if (!ep.InputPath.empty()) {
                out.entry_points.push_back(ep.InputPath);
            }
        }
    }
    if (!WasSet(out, kOptResolveExtensions) && out.resolve_extensions.empty()) {
        out.resolve_extensions = cfg.ExtensionOrder;
    }
    if (!WasSet(out, kOptMainFields) && out.main_fields.empty() && cfg.MainFieldsSet) {
        out.main_fields = cfg.MainFields;
    }
    if (!WasSet(out, kOptConditions) && out.conditions.empty()) {
        out.conditions = cfg.Conditions;
    }
    if (!WasSet(out, kOptExternal) && out.external.empty() &&
        out.packages == api::Packages::kDefault) {
        for (const auto& [name, _] : cfg.ExternalSettingsData.PreResolve.Exact) {
            out.external.push_back(name);
        }
    }
    if (!WasSet(out, kOptAlias) && out.alias.empty()) {
        for (const auto& [from, to] : cfg.PackageAliases) {
            out.alias.emplace(from, Relativize(fs, to));
        }
    }
    if (!WasSet(out, kOptSourceRoot) && out.source_root.empty() && !cfg.SourceRoot.empty()) {
        out.source_root = cfg.SourceRoot;
    }
    if (!WasSet(out, kOptPublicPath) && out.public_path.empty() && !cfg.PublicPath.empty()) {
        out.public_path = cfg.PublicPath;
    }

    // ---- 3. The built-in defaults -----------------------------------------
    // Reached only by whatever neither the caller nor the config mentioned.
    //
    // The output directory is the one default that changes a build's shape, so
    // it is worth saying plainly what it does and does not do. A caller that
    // never resolves keeps the behaviour Build() has always had: a build with
    // no output location is a build whose bytes come back to the caller. A
    // resolved build has a project behind it, and a project that named no
    // destination almost always means the conventional one.
    if (!WasSet(out, kOptOutdir) && !WasSet(out, kOptOutfile) && out.outdir.empty() &&
        out.outfile.empty()) {
        out.outdir = "dist";
    }
    // A format only becomes a requirement once something needs one. An
    // unbundled entry is converted or copied, and "leave it as it was" is the
    // only honest answer for that; a bundle has to pick a module system, and
    // ES module is the one that needs nothing rewritten around it.
    if (out.format == api::Format::kDefault && out.bundle) {
        out.format = api::Format::kESModule;
    }
    // Bundling is the one thing the built-in defaults do not decide: it is the
    // caller's to make, and "no" is a real answer. Kept out of this file so
    // that there is nowhere here a bundle could be started by accident.

    // ---- 4. Derived values ------------------------------------------------
    // There is nothing left to derive. The one derivation that matters — an
    // output file needs a directory to be written beside — belongs to Build(),
    // because Build() is the one place that knows which of the two was named and
    // which was not. Deriving it here as well is how "both were set" starts
    // happening, and that combination is an error on purpose.
    //
    // The parse state is dropped here, where every "WasSet" above is finished
    // with it, and not one line earlier.
    out.explicit_set = nullptr;
}

} // namespace

EffectiveBuildConfigs ResolveEffectiveBuildConfigs(const BuildOptions& explicit_options,
                                                   const std::string& start_dir)
{
    EffectiveBuildConfigs result;

    // ---- A file system to look for the config with -------------------------
    std::string fs_error;
    std::unique_ptr<filesystem::Fs> fs =
        filesystem::MakeRealFS({.abs_working_dir = start_dir}, fs_error);
    if (!fs) {
        // Without a file system there is nothing to discover and nothing to
        // derive, so the request is answered as best it can be from the
        // explicit options alone. A build that still needs an output directory
        // says so through the ordinary "must use outdir" message, which names
        // the problem rather than the failure to look for it.
        BuildOptions out = explicit_options;
        out.explicit_set = nullptr;
        result.builds.push_back(std::move(out));
        return result;
    }

    result.root = fs->Cwd();

    // ---- The config file ---------------------------------------------------
    config::Options base;
    std::unique_ptr<cache::CacheSet> caches = cache::MakeCacheSet();
    logger::Log log = logger::NewStderrLog({});
    resolver::GuchhoConfig config = resolver::LoadGuchhoConfig(
        log, caches->json_cache, *fs, base, result.root);

    result.config_path    = config.config_path;
    result.config_dir     = config.config_dir;
    result.config_found   = config.found;
    result.config_invalid = config.parse_error;

    // A config file that could not be turned into configurations has nothing to
    // resolve. The caller reads "config_invalid" and decides what to do; there
    // is no single build to answer with.
    if (config.parse_error) {
        return result;
    }

    result.builds.reserve(config.builds.size());
    for (const resolver::GuchhoBuildConfig& item : config.builds) {
        BuildOptions out = explicit_options;
        ResolveOneBuild(out, item.opts, item.entry_points, item.entry_from_config, *fs);
        result.builds.push_back(std::move(out));
    }

    // ---- Output collisions -------------------------------------------------
    // Configurations are independent builds, but they share a disk. Two of them
    // that resolve to the same destination would have the later one silently
    // overwrite the earlier one, so the collision is named here rather than
    // discovered as a missing file afterwards. Only destinations that can be
    // compared exactly are considered: a named output file, or the same output
    // directory fed the same entry points (distinct entry points get distinct
    // output names).
    if (result.builds.size() > 1) {
        auto target_key = [&](const BuildOptions& b) -> std::optional<std::string> {
            if (!b.outfile.empty()) {
                auto abs = fs->Abs(b.outfile);
                return std::string("file:") + (abs ? *abs : b.outfile);
            }
            if (b.outdir.empty()) {
                return std::nullopt;
            }
            if (b.entry_points.empty() && b.entry_points_advanced.empty()) {
                return std::nullopt;
            }
            auto abs = fs->Abs(b.outdir);
            std::string key = std::string("dir:") + (abs ? *abs : b.outdir);
            if (!b.outbase.empty()) {
                key += "|outbase:" + b.outbase;
            }
            for (const std::string& ep : b.entry_points) {
                key += "|" + ep;
            }
            for (const EntryPoint& ep : b.entry_points_advanced) {
                key += "|" + ep.input_path + ">" + ep.output_path;
            }
            return key;
        };

        std::unordered_map<std::string, size_t> seen;
        for (size_t i = 0; i < result.builds.size(); ++i) {
            std::optional<std::string> key = target_key(result.builds[i]);
            if (!key) {
                continue;
            }
            auto [it, inserted] = seen.emplace(*key, i);
            if (!inserted) {
                log.AddID(logger::MsgID::kGuchhoConfig_ConflictingOutput,
                          logger::MsgKind::kError, nullptr, logger::Range{},
                          logger::FormatMsg(logger::MsgCat::kGuchhoConfig_ConflictingOutputFile,
                                            "configuration " + std::to_string(it->second + 1),
                                            "configuration " + std::to_string(i + 1)));
                result.config_invalid = true;
                break;
            }
        }
    }

    return result;
}

} // namespace guchho::api
