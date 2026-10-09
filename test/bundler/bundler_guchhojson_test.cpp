// Bundler-level tests for the "guchho.json" project configuration format.
//
// The resolver-level behaviour (defaults, file-name priority, parent
// discovery, invalid configs, partial overrides) is covered by
// "test/resolver/guchho_config_test.cpp". The tests here take the other half:
// a config file is mapped onto "config::Options" and those options drive a
// real bundle, so every field is checked through the output it produces (or,
// for the fields that have no output effect, through the diagnostic the
// config loader logs).
//
// Each scenario supplies the config as text via "Bundled::guchho_config",
// which the bundler harness maps with "resolver::LoadGuchhoConfigFromText":
// the JSON overlays the scenario options and "build.entry" adds entry points.
// Relative paths inside a config resolve against the directory of
// "Bundled::guchho_config_path", which defaults to the working directory.

#include "test/helpers/bundler_test.hpp"
#include "test/helpers/filesystem_test.hpp"
#include "test/guchho_test.hpp"

#include "guchho/cache.hpp"
#include "guchho/config.hpp"
#include "guchho/logger.hpp"
#include "guchho/resolver.hpp"

#include <string>
#include <unordered_map>
#include <utility>

namespace bundler::test {

Suite guchhojson_suite{"guchhojson"};

// ---------------------------------------------------------------------------
// build
// ---------------------------------------------------------------------------

TEST(BundlerGuchhoJSON, BuildFormatIIFE) {
    // "build.format" selects the output format, so the same input wraps
    // itself in an IIFE when the config asks for one.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"format":"iife"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatCommonJS) {
    // The same input in CommonJS keeps its imports as "require" calls.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "import {helper} from './helper.js'\nconsole.log(helper());\n"},
            {"/helper.js", "export function helper() { return 'ok' }\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"format":"cjs"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMD) {
    // "build.format" also accepts "umd". The name comes from the caller here,
    // because an entry that exports something has to be told where its exports
    // go -- the browser branch of a UMD wrapper invents no global of its own.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
        },
        .guchho_config = R"({"build":{"format":"umd"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDWithConfigName) {
    // "build.name" is the config spelling of the same value the caller passes
    // as a build-command option. It is parsed as a name and validated by the
    // same code path, so the wrapper publishes on the namespace the file asked
    // for rather than on one the config had to be told about twice.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"format":"umd","name":"ConfigLib"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDWithGlobalNameAlias) {
    // "build.globalName" is accepted as an alias for "build.name". The name is
    // the same either way, so the wrapper is byte-identical to the test above
    // and no diagnostic is expected for using the alias.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"format":"umd","globalName":"ConfigLib"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDNameWinsOverGlobalNameAlias) {
    // Both spellings at once. "name" is canonical and wins; the alias is
    // reported as ignored rather than silently dropped, and the wrapper
    // publishes on the name the canonical field carried.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"format":"umd","name":"Canonical","globalName":"Ignored"}})",
        .expected_scan_log =
            "guchho.json: WARNING: Both \"build.globalName\" and \"build.name\" are set; "
            "\"build.name\" is the canonical field and the other will be ignored\n",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDWithoutNameReportsError) {
    // Neither spelling present and the entry exports something: the browser
    // branch has nowhere to put the exports, so the build stops with a
    // diagnostic naming the option rather than emitting a bundle whose global
    // silently never appears.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"format":"umd"}})",
        .expected_compile_log =
            "ERROR: UMD output with exports requires a global name (--name=<name>)\n",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDWithoutExportsNeedsNoName) {
    // The same format with an entry that publishes nothing. A side-effect
    // script has nothing to put on a global, so asking for a name here would
    // invent a requirement instead of reporting a missing one, and the wrapper
    // still runs under all three module systems.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "console.log(42);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"format":"umd"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildExportsDefault) {
    // "build.exports" selects what the UMD wrapper publishes as its public
    // value. "default" hands every module system the entry's default export
    // directly: the factory returns it, the CommonJS arm assigns that to
    // "module.exports", and the named export alongside it is exposed nowhere.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export default 42;\nexport const named = 1;\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"format":"umd","name":"ConfigLib","exports":"default"}})",
    });
}

TEST(BundlerGuchhoJSON, InvalidExportsValueWarns) {
    // A value that names no mode is reported and then ignored, so the build
    // keeps the namespace-object behaviour it had before the config was read
    // -- the same contract the invalid "format" value above honours.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export default 42;\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
        },
        .guchho_config = R"({"build":{"format":"umd","exports":"banana"}})",
        .expected_scan_log =
            "guchho.json: WARNING: Invalid exports \"banana\" (expected \"default\", \"named\", "
            "\"none\", or \"auto\")\n",
    });
}

TEST(BundlerGuchhoJSON, BuildExportsNamed) {
    // "named" is the namespace-object behaviour and what the build did before
    // the field existed; naming it explicitly must land on that same answer
    // with no diagnostic, which the empty scan log is.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export default 42;\nexport const named = 1;\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
        },
        .guchho_config = R"({"build":{"format":"umd","exports":"named"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildExportsAutoWithASoleDefault) {
    // "auto" reads the entry and finds a lone default export, so this build
    // resolves to default mode and publishes through the wrapper's return.
    // The empty scan log matters twice over: the value was accepted, and the
    // mixed-exports warning that fires when "auto" lands on named did not.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export default 42;\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
        },
        .guchho_config = R"({"build":{"format":"umd","exports":"auto"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildExportsAutoMixedExportsWarns) {
    // "auto" on an entry that mixes a default export in with named ones
    // resolves to named -- the default stops being special -- and Rollup's
    // contract, which Guchho shares, is to say so rather than dropping the
    // default silently.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export default 42;\nexport const named = 1;\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
        },
        .guchho_config = R"({"build":{"format":"umd","exports":"auto"}})",
        .expected_compile_log =
            "WARNING: --exports=auto resolved to \"named\": the entry point mixes default and "
            "named exports\n",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDWithGlobalName) {
    // The same config with a global name supplied by the caller. It is a
    // build-command option like "BuildMode" rather than a config field, which
    // is why it lives in the options and not in the "guchho.json" text.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
        },
        .guchho_config = R"({"build":{"format":"umd"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDWithExternal) {
    // "external" and "format" together: the marked package stays out of the
    // bundle and the UMD wrapper passes it to all three of its branches. The
    // package exists in the file system, so the import being left alone is
    // what shows that the config took effect.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "import {create} from 'vue'\nexport const app = create();\n"},
            {"/node_modules/vue/index.js", "export const create = () => 1;\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
        },
        .guchho_config = R"({"build":{"format":"umd"},"external":["vue"]})",
    });
}

// The three tests above leave "MinifyWhitespace" at its false default, so
// their snapshots show the wrapper pretty-printed. With the switch on, the
// wrapper's template text goes back through the parser and printer before
// the body is spliced in, so these tests set the switch and pin what comes
// back: every cosmetic space gone, the token-fusing spaces kept. They set
// only "MinifyWhitespace"; the expression and identifier passes that a full
// "--minify" turns on get their own coverage in BuildFormatUMDFullMinify
// below.

TEST(BundlerGuchhoJSON, BuildFormatUMDMinified) {
    // The bare wrapper: three branch arms, no global namespace. The entry
    // exports nothing, which is the one case where a name is not required.
    // Every space that is only there for legibility is gone; the ones after
    // "typeof" and inside "void 0" stay, because dropping them would fuse two
    // tokens.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "console.log(42);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .MinifyWhitespace = true,
        },
        .guchho_config = R"({"build":{"format":"umd"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDMinifiedDirectExports) {
    // A minified entry that does export: the same wrapper as the test above
    // plus a namespace, and the "use strict" directive still sitting inside the
    // factory where it is inherited by the body. The wrapper now passes through
    // the printer like any other code, and a printer never drops a directive,
    // but this snapshot is what pins that: a UMD bundle without it would
    // silently lose strictness the same input still has in every other format.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
            .MinifyWhitespace = true,
        },
        .guchho_config = R"({"build":{"format":"umd"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDKeepsExplicitUseStrict) {
    // An entry that already asks for strict mode keeps its own "use strict",
    // once, inside the factory. Treating it as "already handled" and emitting
    // nothing would leave a UMD bundle without strictness while the same input
    // still has it in every other format. Minified here so the directive also
    // has to survive whitespace removal.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "\"use strict\";\nexport const answer = 42;\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
            .MinifyWhitespace = true,
        },
        .guchho_config = R"({"build":{"format":"umd"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDMinifiedWithGlobalName) {
    // The same wrapper with a namespace to create. The "|| {}" extend form is
    // generated with the switch set, so the assignments and the separators
    // around them both collapse.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
            .MinifyWhitespace = true,
        },
        .guchho_config = R"({"build":{"format":"umd"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDMinifiedWithExternal) {
    // The external case is the one with the most wrapper text, because the
    // dependency name is written three times: into "require(...)" for the
    // CommonJS arm, into the "define([...])" list for the AMD arm, and onto
    // the global for the browser arm. All three are minified, which is what
    // makes the missed spacing easy to see.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "import {create} from 'vue'\nexport const app = create();\n"},
            {"/node_modules/vue/index.js", "export const create = () => 1;\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
            .MinifyWhitespace = true,
        },
        .guchho_config = R"({"build":{"format":"umd"},"external":["vue"]})",
    });
}

TEST(BundlerGuchhoJSON, BuildFormatUMDFullMinify) {
    // The full "--minify" case: all three passes on at once, which is what
    // a real minified UMD build takes. Two things happen that the
    // whitespace-only tests above cannot show. The dispatcher's parameters
    // are identifiers like any other, so they are drawn from the pool that
    // named this chunk's symbols and come out short ("global,factory" ->
    // names like "e,f"). And the comparisons go through the binder's normal
    // expression minification, so the spacing around them and the strict
    // equality itself are rewritten the same way the body's are. The
    // factory's own parameters keep their names: the body spliced between
    // the wrapper's halves refers to them by exactly those names.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .GlobalName = {"MyLib"},
            .MinifyWhitespace = true,
            .MinifyIdentifiers = true,
            .MinifySyntax = true,
        },
        .guchho_config = R"({"build":{"format":"umd"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildPlatformNodeKeepsBuiltin) {
    // Under "platform": "node" a Node built-in is external, so the import is
    // preserved instead of being resolved against the file system.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "import path from 'path'\nconsole.log(path.sep);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"platform":"node"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildMinifyObjectForm) {
    // The object form of "minify" turns individual passes off, so
    // "whitespace": false keeps the line breaks of the original source while
    // identifiers and syntax are still minified.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "const    greeting   =   'hello';\n\nconsole.log(   greeting   );\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"minify":{"whitespace":false}}})",
    });
}

TEST(BundlerGuchhoJSON, BuildSourcemapLinked) {
    // "sourcemap": "linked" writes a ".map" next to the bundle and adds the
    // comment that points at it.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"sourcemap":"linked"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildTargetLowersNewerSyntax) {
    // "target": "es2015" records the engines that lack optional chaining and
    // nullish coalescing, so both are lowered away.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js",
                "const config = {mode: 'x'};\n"
                "export const mode = config?.mode ?? 'default';\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"target":"es2015"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildSplittingEmitsSharedChunk) {
    // "splitting": true moves the module shared by two entries out into its
    // own chunk, and the config is what turns splitting on.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/a.js", "import {shared} from './shared.js'\nconsole.log(shared)\n"},
            {"/b.js", "import {shared} from './shared.js'\nconsole.log(shared)\n"},
            {"/shared.js", "export const shared = 123\n"},
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputDir = "/out",
        },
        .guchho_config = R"({"build":{"entry":["a.js","b.js"],"splitting":true}})",
    });
}

TEST(BundlerGuchhoJSON, BuildEntryAndOutdir) {
    // "build.entry" and "build.outdir" are resolved against the directory
    // holding the config file, and together decide where the output lands.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/project/src/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
        },
        .guchho_config = R"({"build":{"entry":"src/app.js","outdir":"out"}})",
        .guchho_config_path = "/project/guchho.json",
    });
}

TEST(BundlerGuchhoJSON, BuildHtmlEntryPage) {
    // "build.entry" may name a page instead of a module, and the page is what
    // the build emits: the script and the stylesheet it references become
    // chunks, and the URLs in the markup are rewritten to point at them. Only
    // the entry and the output directory come from the config here, so what
    // the snapshot shows is the page pipeline driven by "build.entry".
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/index.html",
                R"(<!DOCTYPE html>
<html>
  <head><link rel="stylesheet" href="style.css"></head>
  <body><script src="app.js"></script><img src="logo.png"></body>
</html>)"},
            {"/app.js", "import {x} from './dep.js'\nconsole.log(x);\n"},
            {"/dep.js", "export const x = 42;\n"},
            {"/style.css", "p { color: red }\n"},
            {"/logo.png", "fake-png-data"},
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
        },
        .guchho_config = R"({"build":{"entry":"index.html","outdir":"out"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildOutfile) {
    // "build.outfile" names the single output file, and clears any output
    // directory, so the bundle is written exactly where the config says.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
        },
        .guchho_config = R"({"build":{"outfile":"out/bundle.js"}})",
    });
}

TEST(BundlerGuchhoJSON, OutputEntryFileNamesTemplate) {
    // "output.entryFileNames" is a path template, so the entry is written
    // under the directory the template spells out. The extension is still
    // taken from the source file, which is why the template omits it.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
        },
        .guchho_config = R"({"build":{"outdir":"out"},"output":{"entryFileNames":"chunks/[name]"}})",
    });
}

TEST(BundlerGuchhoJSON, BuildTreeShaking) {
    // "build.treeShaking": false keeps an unused export in the bundle, which
    // is what a bundle without tree shaking looks like.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "import {used, unused} from './lib.js'\nconsole.log(used);\n"},
            {"/lib.js", "export const used = 1;\nexport const unused = 2;\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"treeShaking":false}})",
    });
}

// ---------------------------------------------------------------------------
// resolve
// ---------------------------------------------------------------------------

TEST(BundlerGuchhoJSON, ResolveAliasAndExtensions) {
    // "resolve.alias" rewrites the bare specifier, and "resolve.extensions"
    // decides which file the rewritten path picks. Both the ".ts" and the
    // ".js" file exist, so the extension order is observable in the output.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "import {which} from '@app/lib'\nconsole.log(which);\n"},
            {"/src/lib.ts", "export const which = 'from ts';\n"},
            {"/src/lib.js", "export const which = 'from js';\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config =
            R"({"resolve":{"alias":{"@app":"/src"},"extensions":[".ts",".js"]}})",
    });
}

// ---------------------------------------------------------------------------
// define and external
// ---------------------------------------------------------------------------

TEST(BundlerGuchhoJSON, DefineInlinesValue) {
    // A "define" value is a JSON expression in string form, so the quoted
    // text below becomes the string literal that is inlined for the key.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "console.log(process.env.NODE_ENV);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"define":{"process.env.NODE_ENV":"\"production\""}})",
    });
}

TEST(BundlerGuchhoJSON, ExternalPreservesImport) {
    // "external" marks a specifier as not-to-be-bundled, so the import is
    // left in the output for the runtime to load.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "import {create} from 'vue'\nconsole.log(create);\n"},
            {"/node_modules/vue/index.js", "export const create = () => 1;\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"external":["vue"]})",
    });
}

// ---------------------------------------------------------------------------
// Diagnostics
//
// These fields either have no effect on the output or are rejected, so what
// the config loader reports is the observable behaviour. The messages land
// in the scan log because that is the log the config is loaded into.
// ---------------------------------------------------------------------------

TEST(BundlerGuchhoJSON, UnknownFieldsWarn) {
    // Unknown properties are reported per object, prefixed with the path of
    // the object they were found in, and are otherwise ignored.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "console.log(1);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"nope":1},"bogus":2})",
        .expected_scan_log = R"scan(
guchho.json: WARNING: Unknown field "build.nope" in guchho config will be ignored
guchho.json: WARNING: Unknown field "bogus" in guchho config will be ignored
)scan",
    });
}

TEST(BundlerGuchhoJSON, InvalidValuesWarn) {
    // A value the loader cannot map is reported and then ignored, so every
    // field keeps the value it had before the config was read.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "console.log(1);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config =
            R"({"build":{"format":"nope","platform":"nope","sourcemap":"nope"},"logLevel":"nope"})",
        .expected_scan_log = R"scan(
guchho.json: WARNING: Invalid format "nope" (expected "esm", "cjs", "iife", "umd", "amd", or "system")
guchho.json: WARNING: Invalid platform "nope" (expected "browser", "node", or "neutral")
guchho.json: WARNING: Invalid sourcemap "nope" (expected "linked", "external", or "inline")
guchho.json: WARNING: Invalid log level "nope"
)scan",
    });
}

TEST(BundlerGuchhoJSON, UnsupportedFeaturesWarn) {
    // A define value that is not a JSON expression, and a non-empty
    // "plugins" array, are both reported rather than quietly dropped.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "console.log(1);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"define":{"FLAG":{}},"plugins":["my-plugin"]})",
        .expected_scan_log = R"scan(
guchho.json: WARNING: Unsupported define value for "FLAG"
guchho.json: WARNING: Guchho plugins are not implemented yet; the "plugins" field will be ignored
)scan",
    });
}

TEST(BundlerGuchhoJSON, MalformedConfigIsAnError) {
    // A config that exists but cannot be parsed is never treated as absent:
    // the parse failure is reported and no bundle is produced.
    guchhojson_suite.ExpectBundled(Bundled{
        .files = {
            {"/app.js", "console.log(1);\n"},
        },
        .entry_paths = {"/app.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
        .guchho_config = R"({"build":{"outdir":"out",}})",
        .expected_scan_log = R"scan(
guchho.json: ERROR: JSON does not support trailing commas
)scan",
    });
}

namespace {

// A fake "guchho.config.js" loader: it reads the file from the mock file
// system and parses the contents as JSON, which is exactly what the real
// loader receives from Node after evaluating the module. It never spawns a
// child process, so the tests below do not need Node installed.
guchho::resolver::GuchhoConfig FakeJSLoader(
    guchho::logger::Log&       log,
    guchho::cache::JSONCache&  json_cache,
    guchho::filesystem::Fs&     fs,
    guchho::config::Options&   opts,
    const std::string&         file_path)
{
    auto contents = fs.ReadFile(file_path);
    if (!contents.Ok()) {
        return guchho::resolver::GuchhoConfig{};
    }
    return guchho::resolver::LoadGuchhoConfigFromText(
        log, json_cache, fs, opts, contents.value, file_path);
}

// Installs a fake JS-config loader and restores the previous one.
class JSLoaderGuard {
public:
    explicit JSLoaderGuard(guchho::resolver::GuchhoConfigJSLoader loader)
        : prev_(guchho::resolver::SetGuchhoConfigJSLoader(loader))
    {
    }

    ~JSLoaderGuard()
    {
        guchho::resolver::SetGuchhoConfigJSLoader(prev_);
    }

private:
    guchho::resolver::GuchhoConfigJSLoader prev_;
};

// The result of running config discovery over a mock file system. The
// GuchhoConfig is held by value because the options it produces point into
// the processed defines it owns, so it has to outlive the bundle run.
struct DiscoveredProject {
    guchho::resolver::GuchhoConfig config;
    std::string                     log;
};

// Discovers the project config exactly as a build would: the same walk, the
// same file-name priority, and the same field mapping. Only the Node
// evaluation of a "guchho.config.js" is faked out.
DiscoveredProject DiscoverProject(
    const std::unordered_map<std::string, std::string>& files,
    const std::string&                                 start_dir)
{
    static const std::unordered_map<
        guchho::logger::MsgID, guchho::logger::LogLevel> empty_overrides;
    auto log  = guchho::logger::NewDeferLog(
        guchho::logger::DeferLogKind::kDeferLogNoVerboseOrDebug,
        empty_overrides);
    auto caches = guchho::cache::MakeCacheSet();
    auto fs     = guchho::test::MakeMockFS(
        files, guchho::filesystem::MockKind::kUnix, start_dir);

    guchho::config::Options opts;
    DiscoveredProject       result;
    result.config = guchho::resolver::LoadGuchhoConfig(
        log, caches->json_cache, *fs, opts, start_dir);
    result.log = FormatLog(log.done());
    return result;
}

// Turns a discovered project config into a bundle scenario. "BuildMode" is
// the one setting the config schema does not describe: it is a build-command
// choice, so the caller states it and everything else comes from the config.
Bundled ScenarioForProject(
    std::unordered_map<std::string, std::string> files,
    const guchho::resolver::GuchhoConfig&         config)
{
    guchho::config::Options options = config.builds[0].opts;
    options.BuildMode              = guchho::config::Mode::kBundle;

    Bundled scenario;
    scenario.files = std::move(files);
    for (const auto& entry_point : config.builds[0].entry_points) {
        scenario.entry_paths.push_back(entry_point.InputPath);
    }
    scenario.options = std::move(options);
    return scenario;
}

} // namespace

// ---------------------------------------------------------------------------
// The default entry
//
// A config that says nothing about "build.entry" still builds a page, because
// the schema's default entry is "index.html" beside the config. That default
// belongs to the discovery walk rather than to the field mapping, so the test
// below finds a real "guchho.json" in the mock file system instead of passing
// config text to the harness.
// ---------------------------------------------------------------------------

TEST(BundlerGuchhoJSON, DefaultEntryIsThePage) {
    // The config asks for an output directory and nothing else, so the entry
    // that gets built is the one nobody wrote down.
    std::unordered_map<std::string, std::string> files = {
        {"/project/guchho.json", R"({"build":{"outdir":"out"}})"},
        {"/project/index.html",
            R"(<!DOCTYPE html>
<html>
  <head><link rel="stylesheet" href="style.css"></head>
  <body><script src="app.js"></script><img src="logo.png"></body>
</html>)"},
        {"/project/app.js", "import {x} from './dep.js'\nconsole.log(x);\n"},
        {"/project/dep.js", "export const x = 42;\n"},
        {"/project/style.css", "p { color: red }\n"},
        {"/project/logo.png", "fake-png-data"},
    };

    DiscoveredProject project = DiscoverProject(files, "/project");
    EXPECT_TRUE(project.config.found);
    EXPECT_FALSE(project.config.parse_error);
    EXPECT_EQ(project.config.config_path, std::string("/project/guchho.json"));
    EXPECT_TRUE(project.log.empty());

    // One entry, and it is the page next to the config rather than a module.
    ASSERT_EQ(project.config.builds[0].entry_points.size(), size_t{1});
    EXPECT_EQ(project.config.builds[0].entry_points[0].InputPath,
              std::string("/project/index.html"));

    guchhojson_suite.ExpectBundledUnix(
        ScenarioForProject(std::move(files), project.config));
}

// ---------------------------------------------------------------------------
// guchho.config.js
//
// A JS config supports the same fields as a JSON config because the object it
// exports travels back as JSON. These tests drive the real discovery walk
// with the Node evaluation faked, so a JS config is shown to configure an
// actual bundle. They run against the Unix mock file system only, because
// discovery happens here rather than inside the harness, which is what
// converts the scenario paths for the Windows mock.
// ---------------------------------------------------------------------------

TEST(BundlerGuchhoJSON, ConfigJSDrivesBundle) {
    std::unordered_map<std::string, std::string> files = {
        {"/project/guchho.config.js",
            R"({"build":{"entry":"src/app.js","format":"iife","outdir":"out"}})"},
        {"/project/src/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
    };

    JSLoaderGuard      guard(&FakeJSLoader);
    DiscoveredProject project = DiscoverProject(files, "/project");
    EXPECT_TRUE(project.config.found);
    EXPECT_FALSE(project.config.parse_error);
    EXPECT_EQ(project.config.config_path, std::string("/project/guchho.config.js"));
    EXPECT_TRUE(project.log.empty());

    guchhojson_suite.ExpectBundledUnix(
        ScenarioForProject(std::move(files), project.config));
}

TEST(BundlerGuchhoJSON, ConfigJSFileNameWins) {
    // Within one directory "guchho.config.js" outranks the JSON spellings,
    // so the IIFE wrapping below comes from the JS file even though a
    // "guchho.json" asking for CommonJS sits next to it.
    std::unordered_map<std::string, std::string> files = {
        {"/project/guchho.config.js",
            R"({"build":{"entry":"app.js","format":"iife"}})"},
        {"/project/guchho.config.json",
            R"({"build":{"entry":"app.js","format":"cjs"}})"},
        {"/project/guchho.json",
            R"({"build":{"entry":"app.js","format":"umd"}})"},
        {"/project/app.js", "export const answer = 42;\nconsole.log(answer);\n"},
    };

    JSLoaderGuard      guard(&FakeJSLoader);
    DiscoveredProject project = DiscoverProject(files, "/project");
    EXPECT_TRUE(project.config.found);
    EXPECT_FALSE(project.config.parse_error);
    EXPECT_EQ(project.config.config_path, std::string("/project/guchho.config.js"));
    EXPECT_EQ(project.config.builds[0].opts.OutputFormat, guchho::config::Format::kIIFE);
    EXPECT_TRUE(project.log.empty());

    guchhojson_suite.ExpectBundledUnix(
        ScenarioForProject(std::move(files), project.config));
}

} // namespace bundler::test
