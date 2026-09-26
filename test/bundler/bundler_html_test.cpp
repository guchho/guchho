#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

Suite html_suite{"html"};

TEST(BundlerHTML, HtmlEntryWithAllResourceKinds) {
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html>
<html>
  <head><link rel="stylesheet" href="style.css"></head>
  <body><script src="app.js"></script><img src="logo.png"></body>
</html>)"},
			{"/app.js", R"(import { x } from "./dep.js"; console.log(x);)"},
			{"/dep.js", R"(export const x = 42;)"},
			{"/style.css", R"(p { color: red })"},
			{"/logo.png", "fake-png-data"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlInlineModuleScriptIsSkipped) {
	// <script type="module"> is deferred by browsers, so it cannot be merged
	// into the concatenated classic-script bundle; it is left untouched while
	// classic inline scripts are still bundled.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><script type="module">console.log("m")</script></head><body><script>alert(1)</script></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlInlineBundleEscapesClosingTag) {
	// A bundled inline script must not be able to terminate the <script> element
	// early, so literal "</script" and "<!--" sequences coming from imported
	// dependencies are escaped in the re-inlined output.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><script>import { s, c } from "./dep.js"; console.log(s, c)</script></body></html>)"},
			{"/dep.js", R"(export const s = "</script>"; export const c = "<!--";)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlTwoPagesEachInlineSharedDep) {
	// Each HTML page's inline bundle is independent, so an external dep imported
	// by both pages' inline scripts is inlined into each page separately.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><script>import { x } from "./dep.js"; console.log(x)</script></body></html>)"},
			{"/other.html",
			 R"(<!DOCTYPE html><html><body><script>import { x } from "./dep.js"; alert(x)</script></body></html>)"},
			{"/dep.js", R"(export const x = 1;)"},
		},
		.entry_paths = {"/index.html", "/other.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlInlineStyleImportsExternalCss) {
	// An @import inside an inline <style> resolves relative to the HTML file and
	// the imported CSS is bundled into the re-inlined style.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><style>@import "./theme.css"; body { margin: 0 }</style></head><body></body></html>)"},
			{"/theme.css", R"(h1 { color: green })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlTwoEntriesSharingResource) {
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="style.css"></head><body><script src="app.js"></script></body></html>)"},
			{"/other.html",
			 R"(<!DOCTYPE html><html><body><script src="app.js"></script><img src="logo.png"></body></html>)"},
			{"/app.js", R"(console.log("app"))"},
			{"/style.css", R"(body { margin: 0 })"},
			{"/logo.png", "PNGDATA"},
		},
		.entry_paths = {"/index.html", "/other.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlInlineScriptAndStyleAreBundled) {
	// Inline <script>/<style> contents are bundled and re-inlined into the HTML
	// output; no separate JS/CSS files are emitted for them.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><style>p{color:red}</style></head><body><script>console.log("hi")</script></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlInlineScriptCanImport) {
	// Imports inside an inline <script> resolve relative to the HTML file, so
	// the inline bundle includes the dependency.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><script>import { greeting } from "./greet.js"; console.log(greeting)</script></body></html>)"},
			{"/greet.js", R"(export const greeting = "hello";)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlTwoElementsShareOneInlineBundle) {
	// Multiple inline <script> elements from the same page are concatenated in
	// order into a single bundle that is re-inlined at both positions.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><style>p{color:red}</style><style>.b{color:blue}</style></head><body><script>var a = 1</script><script>console.log(a)</script></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlEmbeddedScopedAssets) {
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/pages/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="../styles/main.css"></head><body><img src="../img/hero.png"></body></html>)"},
			{"/styles/main.css", R"(a { color: blue })"},
			{"/img/hero.png", "HERO"},
		},
		.entry_paths = {"/pages/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlExternalAndDataUrlsArePreserved) {
	// External URLs (https/http/protocol-relative) and data URLs must never be
	// bundled, resolved, or rewritten. Non-stylesheet <link> elements are
	// likewise preserved verbatim instead of becoming CSS entries.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"html(<!DOCTYPE html><html><head><link rel="icon" href="./favicon.ico"><link rel="preload" href="./font.woff2"><link rel="stylesheet" href="https://cdn.example.com/site.css"></head><body><script src="https://example.com/app.js"></script><script src="//cdn.example.com/app.js"></script><script src="data:text/javascript,console.log(1)"></script><img src="data:image/png;base64,AAAA"><img src="//cdn.example.com/logo.png"><img src="./local-logo.png"></body></html>)html"},
			{"/favicon.ico", "ICO"},
			{"/font.woff2", "FONT"},
			{"/local-logo.png", "LOCAL"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlLinkStylesheetFiltering) {
	// Only <link rel="stylesheet"> becomes a CSS bundle entry. <link
	// rel="icon">/rel="preload"> keep their original href and produce no CSS
	// output file.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="icon" href="./favicon.ico"><link rel="stylesheet" href="./site.css"><link rel="preload" href="./font.woff2"></head><body></body></html>)"},
			{"/favicon.ico", "ICO"},
			{"/site.css", R"(body { margin: 0 })"},
			{"/font.woff2", "FONT"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlInlineStyleReferencesAsset) {
	// A url() inside an inline <style> resolves relative to the HTML file and
	// uses the existing CSS asset pipeline: the referenced file is copied and
	// the URL inside the re-inlined CSS points at the generated asset.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/pages/index.html",
			 R"(<!DOCTYPE html><html><head><style>.app { background: url("../img/bg.png") }</style></head><body>hi</body></html>)"},
			{"/img/bg.png", "BGPNG"},
		},
		.entry_paths = {"/pages/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlNestedOutputDirRelativePaths) {
	// HTML inside a nested output directory must reference its resources with
	// paths relative to that directory.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/admin/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="style.css"></head><body><script src="app.js"></script><img src="logo.png"></body></html>)"},
			{"/admin/app.js", R"(console.log("admin"))"},
			{"/admin/style.css", R"(p { color: orange })"},
			{"/admin/logo.png", "ADMINLOGO"},
		},
		.entry_paths = {"/admin/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlInlineRunsSplitAtExternalResources) {
	// Inline <script>/<style> runs are split by intervening external resources:
	// a <script src> splits the script run and a <link rel="stylesheet"> splits
	// the style run. Each run is bundled independently and re-inlined at its own
	// first element; the external resources stay as separate entries.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><style>a{color:red}</style><link rel="stylesheet" href="theme.css"><style>b{color:blue}</style></head><body><script>var a=1</script><script src="ext.js"></script><script>console.log(a)</script></body></html>)"},
			{"/theme.css", R"(h1 { font-size: 1em })"},
			{"/ext.js", R"(console.log("ext"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlFinalAcceptanceFixture) {
	// PLAN.md §24 end-to-end fixture: external JS entry, external CSS entry,
	// inline style referencing an asset, <img> asset, and an inline classic
	// script all in one page.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!doctype html><html><head><link rel="stylesheet" href="./src/style.css"><style>body { background: url("./assets/logo.png"); }</style></head><body><img src="./assets/logo.png"><script type="module" src="./src/app.js"></script><script>console.log("inline");</script></body></html>)"},
			{"/src/app.js", R"(import { foo } from "./foo.js"; console.log(foo);)"},
			{"/src/foo.js", R"(export const foo = "foo";)"},
			{"/src/style.css", R"(.card { padding: 8px })"},
			{"/assets/logo.png", "FAKEPNG"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlModulePreloadAndCssInjection) {
	// A <script type="module"> entry that imports a JS dependency chunk and a
	// stylesheet from JS must, in the HTML output, receive a `crossorigin`
	// attribute plus <head> module-preload links for its dependency chunks and a
	// <link rel="stylesheet"> for the CSS the JS-imported stylesheet produced.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>T</title></head><body><script type="module" src="app.js"></script></body></html>)"},
			{"/app.js",
			 R"(import "./style.css"; import("./dep.js").then(m => console.log(m.x));)"},
			{"/dep.js", R"(export const x = 42;)"},
			{"/style.css", R"(p { color: red })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,

			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlClassicScriptUntouched) {
	// Classic (non-module) <script src> elements must not receive a
	// `crossorigin` attribute nor any module-preload/CSS link injection, since
	// only ES modules require same-origin credential preload semantics.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script src="app.js"></script></body></html>)"},
			{"/app.js", R"(console.log("hi"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlPrettyPrintOutput) {
	// When PrettyPrint is enabled the HTML serializer emits an indented,
	// multi-line form of the document. Inline <script>/<style> content that was
	// bundled from JS/CSS is still re-inlined verbatim (script/style are
	// preformatted elements and are not re-indented).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="style.css"></head><body><script>import "./style.css"; console.log("hi");</script></body></html>)"},
			{"/style.css", R"(p { color: red })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.PrettyPrint = true,

			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlStyleAttributeUrlRebasing) {
	// url() references inside a "style" attribute are resolved relative to the
	// HTML file, copied to the output as content-hashed assets, and the URL
	// within the attribute is rewritten in place (other declarations are left
	// untouched). Multiple url() occurrences and quoted/unquoted forms are
	// handled, applied right-to-left so offsets stay correct.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/pages/index.html",
			 R"html(<!DOCTYPE html><html><body><div style="background: url('../img/bg.png');border:0;background-image:url('./img/logo2.png')"></div></body></html>)html"},
			{"/img/bg.png", "BGPNG"},
			{"/pages/img/logo2.png", "LOGO2"},
		},
		.entry_paths = {"/pages/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlImportOnlyFacadeInlined) {
	// A <script type="module"> whose source is a pure re-export facade (no
	// runtime statements) must not be emitted as a redundant module load. When
	// the facade's single dependency is a shared chunk, the <script> should
	// point directly at that real chunk instead of the import-only facade.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="app.js"></script></body></html>)"},
			{"/app.js", R"(export * from "./shared.js";)"},
			{"/shared.js", R"(import "./util.js"; export const value = 1;)"},
			{"/util.js", R"(console.log("util-initialized");)"},
			{"/other.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="shared.js"></script></body></html>)"},
		},
		.entry_paths = {"/index.html", "/other.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlProductionModuleEntryWithSharedChunkAndPreloads) {
	// Production ES-module app: the entry imports a shared JS chunk statically,
	// a stylesheet, and a chunk via dynamic import. The HTML output must stay
	// readable (non-minified) and include module-preload links for every
	// dependent chunk, a <link rel="stylesheet"> for the JS-imported CSS, and a
	// `crossorigin` attribute on the module <script>.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>My App</title></head><body><script type="module" src="src/main.js"></script></body></html>)"},
			{"/src/main.js",
			 R"(import { Button } from "./ui/button.js"; import "./app.css"; const lazy = () => import("./lazy/widget.js"); console.log(Button, lazy);)"},
			{"/src/ui/button.js",
			 R"(export const Button = "button-class";)"},
			{"/src/lazy/widget.js",
			 R"(export function widget() { return "widget"; })"},
			{"/src/app.css",
			 R"(.app { display: grid })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlMultiPageAppSharedModuleChunk) {
	// Vite-style multi-page app: two HTML pages each load their own entry, but
	// both import the same shared UI module. With code splitting the shared
	// module becomes one content-hashed chunk that both pages reference, and
	// each page module-preloads it (non-minified output, readable paths).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="home.js"></script></body></html>)"},
			{"/about.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="about.js"></script></body></html>)"},
			{"/home.js",
			 R"(import { Header } from "./ui/header.js"; console.log("home", Header);)"},
			{"/about.js",
			 R"(import { Header } from "./ui/header.js"; console.log("about", Header);)"},
			{"/ui/header.js",
			 R"(export const Header = "site-header";)"},
		},
		.entry_paths = {"/index.html", "/about.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlNonMinifiedClassicBundleKeepsSourceComments) {
	// A classic (non-module) inline bundle spanning several imported files
	// stays readable: with whitespace/minify disabled, each bundled file keeps
	// its source-path comment and the content is not compressed. This is the
	// "not minify" production style for a single-page script.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script>import { add } from "./utils/math.js"; import { greet } from "./utils/greet.js"; console.log(add(1, 2), greet("hi"));</script></body></html>)"},
			{"/utils/math.js", R"(export function add(a, b) { return a + b; })"},
			{"/utils/greet.js", R"(export function greet(name) { return `hello ${name}`; })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",

			.MinifyWhitespace = false,
			.MinifyIdentifiers = false,
			.MinifySyntax = false,
		},
	});
}

TEST(BundlerHTML, HtmlMinifiedClassicBundleStripsSourceComments) {
	// The mirror image of HtmlNonMinifiedClassicBundleKeepsSourceComments: the
	// same classic (non-module) inline bundle spanning several imported files
	// is compressed to a single line when minification is enabled, and each
	// bundled file's source-path comment is stripped in the process.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script>import { add } from "./utils/math.js"; import { greet } from "./utils/greet.js"; console.log(add(1, 2), greet("hi"));</script></body></html>)"},
			{"/utils/math.js", R"(export function add(a, b) { return a + b; })"},
			{"/utils/greet.js", R"(export function greet(name) { return `hello ${name}`; })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",

			.MinifyWhitespace = true,
			.MinifyIdentifiers = true,
			.MinifySyntax = true,
		},
	});
}

TEST(BundlerHTML, HtmlPrettyPrintModuleAppPage) {
	// Pretty-printed production page: a module entry with a real dependency and
	// an inline classic script. The document is emitted indented and readable,
	// while the inline classic bundle and the module <script src> are preserved
	// (module scripts keep their src and are not merged into classic runs).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>Docs</title></head><body><header>Top</header><script type="module" src="main.js"></script><script>console.log("boot");</script><footer>Bottom</footer></body></html>)"},
			{"/main.js", R"(import { v } from "./value.js"; console.log(v);)"},
			{"/value.js", R"(export const v = 7;)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.PrettyPrint = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlPrettyPrintSimpleTextElements) {
	// Pretty-print should keep simple text-only elements on a single line
	// (e.g. <title>, <header>, <footer>) rather than breaking the opening and
	// closing tags across lines, while still indenting nested structure.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>Docs</title></head><body><header>Top</header><main>Body</main><footer>Bottom</footer></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.PrettyPrint = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlPrettyPrintInlineCodeIndented) {
	// Inline bundled <script>/<style> content should be indented one level
	// deeper than the element itself, keeping the closing tag aligned and the
	// generated source comments readable, without altering the code or the
	// injected inline-run grouping.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><style>p { color: red }</style></head><body><script>import "./a.css"; console.log("boot");</script></body></html>)"},
			{"/a.css", R"(body { margin: 0 })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.PrettyPrint = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlPrettyPrintPreservesSimpleAttributes) {
	// Pretty-printing must preserve all existing attributes exactly, including
	// empty-valued attributes as crossorigin="", plus async/defer markers.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="main.js" crossorigin="" defer></script><script async src="other.js"></script></body></html>)"},
			{"/main.js", R"(console.log("main"))"},
			{"/other.js", R"(console.log("other"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.PrettyPrint = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlPrettyPrintWhitespaceSensitiveElements) {
	// Whitespace-sensitive elements (<pre>, <textarea>) must keep their
	// rendered whitespace; they must not be re-indented by the pretty printer.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><pre>  keep
  this</pre><textarea>  lines
  intact</textarea></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.PrettyPrint = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlPrettyPrintBlankLineFreeSiblings) {
	// Sibling block elements sit on consecutive indented lines in pretty mode
	// (no blank lines inserted between them). A module entry script and an
	// inline classic script are both preserved and indented.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>Docs</title></head><body><header>Top</header><script type="module" src="main.js"></script><script>console.log("boot");</script><footer>Bottom</footer></body></html>)"},
			{"/main.js", R"(console.log("main"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.PrettyPrint = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlEnvReplacement) {
	// `%KEY%` placeholders in text nodes are replaced with the configured
	// define values (mirrors Vite's `%VITE_*%` env replacement in HTML).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>%APP%</title></head><body><p>Version %VERSION%</p></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.DefineMap = {
				{"APP", "MyApp"},
				{"VERSION", "1.2.3"},
			},
		},
	});
}

TEST(BundlerHTML, HtmlEnvReplacementInAttributes) {
	// `%KEY%` placeholders in attribute values are replaced. Non-resource
	// attributes (meta content, anchor href, img alt) are the safe surface for
	// a purely textual replacement; resource URLs are still resolved from their
	// raw pre-replacement values.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><meta name="description" content="%DESC%"></head><body><a href="%HOME_URL%">Home</a><img src="logo.png" alt="%IMG_ALT%"></body></html>)"},
			{"/logo.png", "LOGO"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.DefineMap = {
				{"DESC", "A product catalog"},
				{"HOME_URL", "https://example.com/"},
				{"IMG_ALT", "Logo"},
			},
		},
	});
}

TEST(BundlerHTML, HtmlEnvReplacementUnknownKeyUntouched) {
	// Unrecognized `%...%` patterns are never touched (no error, no warning).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>%UNKNOWN%</title></head><body></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.DefineMap = {
				{"APP", "MyApp"},
			},
		},
	});
}

TEST(BundlerHTML, HtmlEnvReplacementMixed) {
	// A single string can mix known and unknown placeholders; only the keys
	// present in the map are replaced, the rest stay verbatim.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>%A% and %B%</title></head><body></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.DefineMap = {
				{"A", "Hello"},
			},
		},
	});
}

TEST(BundlerHTML, HtmlEnvReplacementEmptyValue) {
	// An empty define value replaces the placeholder with an empty string.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>%A%</title></head><body></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.DefineMap = {
				{"A", ""},
			},
		},
	});
}

TEST(BundlerHTML, HtmlEnvReplacementNoScriptContent) {
	// <script>/<style> contents travel through the JS/CSS pipelines and must
	// never have `%KEY%` placeholders replaced inside them. A module inline
	// script is left untouched by the bundler, so its literal "%A%" string
	// stays intact.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><style>p { content: "%A%" }</style></head><body><script type="module">console.log("%A%")</script></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.DefineMap = {
				{"A", "Hello"},
			},
		},
	});
}

TEST(BundlerHTML, HtmlGuchhoIgnoreSkipsRewriting) {
	// An element marked "guchho-ignore" is not bundled: its resource URL is
	// preserved verbatim, no asset output is produced for it, and the marker
	// attribute itself is removed from the output.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><img src="./a.png" guchho-ignore></body></html>)"},
			{"/a.png", "PNG"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlGuchhoIgnoreScript) {
	// An ignored module <script> keeps its src and receives neither the
	// `crossorigin` attribute nor module-preload/CSS link injection.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="./ext.js" guchho-ignore defer></script></body></html>)"},
			{"/ext.js", R"(console.log("external"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlGuchhoIgnoreMultiple) {
	// guchho-ignore only suppresses the elements that carry it; a sibling
	// resource on the same page is still bundled and rewritten.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><img src="logo.png"><img src="skip.png" guchho-ignore></body></html>)"},
			{"/logo.png", "LOGO"},
			{"/skip.png", "SKIP"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlGuchhoIgnoreInlineScript) {
	// An ignored inline classic <script> is left verbatim: it joins no inline
	// bundle run and keeps its exact content, while neighboring inline
	// scripts are still bundled together.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><script>alert("a")</script><script guchho-ignore>var x = 1;</script><script>alert("b")</script></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlGuchhoIgnoreInlineStyle) {
	// An ignored inline <style> is left verbatim: it joins no CSS run.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><style>.a{color:red}</style><style guchho-ignore>.b { color: blue }</style></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlCspNonceInjection) {
	// A configured CSP nonce is added to every <script>/<style>/<link> element,
	// including the modulepreload/CSS links generated for JS dependencies, and
	// a <meta property="csp-nonce"> tag is injected into <head>. Non-stylesheet
	// <link> elements keep their href and still receive the nonce.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="modulepreload" href="./x.js"><link rel="preload" as="font" type="font/woff2" href="./f.woff2"></head><body><link rel="stylesheet" href="./a.css"><script type="module" src="./a.js"></script><style>body{color:red}</style></body></html>)"},
			{"/a.css", "body{color:red}"},
			{"/a.js", R"(import "./b.js"; console.log("a"))"},
			{"/b.js", R"(console.log("b"))"},
			{"/x.js", R"(console.log("x"))"},
			{"/f.woff2", "\x4F\x54\x54\x4F"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.CspNonce = "abc123",
		},
	});
}

TEST(BundlerHTML, HtmlCspNonceMetaInjected) {
	// Even for an otherwise empty document, the meta tag is injected as the
	// first child of <head>.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html", R"(<!DOCTYPE html><html><head></head><body></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.CspNonce = "abc123",
		},
	});
}

TEST(BundlerHTML, HtmlCspNoncePreservesExisting) {
	// A user-provided nonce attribute is preserved, never overwritten.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script nonce="existing" type="module" src="./a.js"></script></body></html>)"},
			{"/a.js", R"(console.log("a"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.CspNonce = "abc123",
		},
	});
}

TEST(BundlerHTML, HtmlCspNonceCreatesHead) {
	// When the document lacks a <head>, one is created to carry the meta tag.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><style>.x{color:red}</style></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.CspNonce = "abc123",
		},
	});
}

TEST(BundlerHTML, HtmlImportMapReordered) {
	// An import map that appears after a module <script> is moved before it.
	// The map's JSON body is preserved verbatim and is never bundled as JS.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="./app.js"></script><script type="importmap">{"imports":{"vue":"/vue.js"}}</script></body></html>)"},
			{"/app.js", R"(console.log("app"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
		.expected_compile_log = R"compile(
index.html: WARNING: import map in "index.html" was reordered to appear before module scripts
)compile",
	});
}

TEST(BundlerHTML, HtmlImportMapAlreadyCorrect) {
	// An import map already before every module script is left in place.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="importmap">{"imports":{"vue":"/vue.js"}}</script><script type="module" src="./app.js"></script></body></html>)"},
			{"/app.js", R"(console.log("app"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlImportMapMultiple) {
	// Two import maps after module scripts are both moved before them, keeping
	// their relative order.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="./app.js"></script><script type="importmap">{"imports":{"a":"/a.js"}}</script><script type="importmap">{"imports":{"b":"/b.js"}}</script></body></html>)"},
			{"/app.js", R"(console.log("app"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
		.expected_compile_log = R"compile(
index.html: WARNING: import map in "index.html" was reordered to appear before module scripts
)compile",
	});
}

TEST(BundlerHTML, HtmlImportMapNoModules) {
	// Without any module script or modulepreload link, the import map stays in
	// place (no reordering happens).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><div>hello</div><script type="importmap">{"imports":{"vue":"/vue.js"}}</script></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

namespace {

using guchho::config::HtmlTagDescriptor;
using guchho::config::HtmlTransformContext;
using guchho::config::Plugin;

// A transformIndexHtml hook that returns a replaced HTML string.
using TransformIndexHtmlResult = std::variant<
    std::string, std::vector<HtmlTagDescriptor>>;

// Builds a descriptor-injecting hook from an explicitly returned vector.
std::vector<HtmlTagDescriptor> MakeTags() {
	std::vector<HtmlTagDescriptor> tags;

	HtmlTagDescriptor meta;
	meta.tag = "meta";
	meta.attrs = {{"name", "plugin-a"}, {"content", "b"}};
	meta.inject_to = HtmlTagDescriptor::kHeadPrepend;
	tags.push_back(std::move(meta));

	HtmlTagDescriptor link;
	link.tag = "link";
	link.attrs = {{"rel", "stylesheet"}, {"href", "/x.css"}};
	link.inject_to = HtmlTagDescriptor::kHead;
	tags.push_back(std::move(link));

	HtmlTagDescriptor div;
	div.tag = "div";
	div.children = "body-start";
	div.inject_to = HtmlTagDescriptor::kBodyPrepend;
	tags.push_back(std::move(div));

	HtmlTagDescriptor late;
	late.tag = "script";
	late.attrs = {{"src", "/late.js"}};
	late.inject_to = HtmlTagDescriptor::kBody;
	tags.push_back(std::move(late));

	return tags;
}

// Inserts "marker" right before the closing </body> of "html".
std::string InsertBeforeBody(std::string html, const std::string& marker) {
	const std::string needle = "</body>";
	const size_t pos = html.find(needle);
	if (pos != std::string::npos) {
		html.insert(pos, marker);
	}
	return html;
}

} // namespace

TEST(BundlerHTML, HtmlPluginTransformString) {
	// A plugin whose transformIndexHtml hook returns a new string replaces the
	// entire document. The hook runs after all other HTML passes.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>T</title></head><body><p>hi</p></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.Plugins = {Plugin{
				.Name = "replace",
				.TransformIndexHtml = [](const std::string& html,
					const HtmlTransformContext&) {
					return std::variant<std::string, std::vector<HtmlTagDescriptor>>{
						InsertBeforeBody(html, "<!--transform-->")};
				},
			}},
		},
	});
}

TEST(BundlerHTML, HtmlPluginTransformTags) {
	// A plugin returning tag descriptors injects each element at its requested
	// <head>/<body> boundary.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>T</title></head><body><p>hi</p></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.Plugins = {Plugin{
				.Name = "tags",
				.TransformIndexHtml = [](const std::string&,
					const HtmlTransformContext&) {
					return std::variant<std::string, std::vector<HtmlTagDescriptor>>{
						MakeTags()};
				},
			}},
		},
	});
}

TEST(BundlerHTML, HtmlPluginTransformBoth) {
	// One plugin replaces the document string; the next injects tags: both
	// effects are present in the final output, applied in registration order.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>T</title></head><body><p>hi</p></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.Plugins = {Plugin{
					.Name = "replace",
					.TransformIndexHtml = [](const std::string& html,
						const HtmlTransformContext&) {
						return std::variant<std::string,
						                   std::vector<HtmlTagDescriptor>>{
							InsertBeforeBody(html, "<!--transform-->")};
					},
				},
				Plugin{
					.Name = "tags",
					.TransformIndexHtml = [](const std::string&,
						const HtmlTransformContext&) {
						return std::variant<std::string,
						                   std::vector<HtmlTagDescriptor>>{
							MakeTags()};
					},
				}},
		},
	});
}

TEST(BundlerHTML, HtmlPluginTransformOrdering) {
	// Two plugins returning replacement strings are invoked in registration
	// order, and each receives the previous plugin's output.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><title>T</title></head><body><p>hi</p></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.Plugins = {Plugin{
					.Name = "first",
					.TransformIndexHtml = [](const std::string& html,
						const HtmlTransformContext&) {
						return std::variant<std::string,
						                   std::vector<HtmlTagDescriptor>>{
							InsertBeforeBody(html, "<!--first-->")};
					},
				},
				Plugin{
					.Name = "second",
					.TransformIndexHtml = [](const std::string& html,
						const HtmlTransformContext&) {
						return std::variant<std::string,
						                   std::vector<HtmlTagDescriptor>>{
							InsertBeforeBody(html, "<!--second-->")};
					},
				}},
		},
	});
}

TEST(BundlerHTML, HtmlPluginTransformHeadBoundaryWarning) {
	// Injecting a non-<head> element into <head> produces a warning.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.Plugins = {Plugin{
				.Name = "tags",
				.TransformIndexHtml = [](const std::string&,
					const HtmlTransformContext&) {
					std::vector<HtmlTagDescriptor> tags;
					HtmlTagDescriptor div;
					div.tag = "div";
					div.children = "oops";
					div.inject_to = HtmlTagDescriptor::kHead;
					tags.push_back(std::move(div));
					return std::variant<std::string,
					                   std::vector<HtmlTagDescriptor>>{
						std::move(tags)};
				},
			}},
		},
		.expected_compile_log = R"compile(
WARNING: "div" element injected into <head> by plugin "tags"
)compile",
	});
}

TEST(BundlerHTML, HtmlMinificationBasic) {
	// With MinifyHtml set, whitespace in text nodes collapses to a single
	// space, comments are stripped, and safe attribute quotes are removed.
	// The inline <script> (still preformatted) is preserved verbatim via
	// guchho-ignore so no JS bundling interferes.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html>
<html lang="en">
  <head>
    <title>  Hello   World  </title>
    <meta name="viewport" content="width=device-width">
    <!-- page title -->
  </head>
  <body>
    <p>Hello   World</p>
    <script guchho-ignore>
      var x = "  preserved  ";
    </script>
  </body>
</html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyHtml = true,
		},
	});
}

TEST(BundlerHTML, HtmlMinificationPreservesPre) {
	// With MinifyHtml, whitespace inside <pre> is never collapsed: the browser
	// renders it verbatim, so preserving the bytes is what minification must do.
	// The whitespace-only text nodes around the block elements are dropped.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html>
<html>
  <body>
    <pre>   hello
    world  </pre>
    <p>a   b</p>
  </body>
</html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyHtml = true,
		},
	});
}

TEST(BundlerHTML, HtmlMinificationPreservesScript) {
	// Raw-text element content stays byte-for-byte identical; only surrounding
	// document whitespace is collapsed. guchho-ignore keeps the <script> out of
	// the JS pipeline so no bundling changes its text.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html>
<html>
  <body>
    <p>x   y</p>
    <script guchho-ignore>
      var a = "  preserved  ";
      var b = "1  2";
    </script>
    <p>m   n</p>
  </body>
</html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyHtml = true,
		},
	});
}

TEST(BundlerHTML, HtmlMinificationPreservesStyle) {
	// Same for <style>: the CSS text must not be reformatted even in minify
	// mode. guchho-ignore routes it around the CSS pipeline.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html>
<html>
  <head>
    <style guchho-ignore>
      p {
        margin: 0  0  0  0;
      }
    </style>
  </head>
  <body>
    <p>a   b</p>
  </body>
</html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyHtml = true,
		},
	});
}

TEST(BundlerHTML, HtmlMinificationPreservesTextarea) {
	// <textarea> is a RCDATA element whose value (including leading/trailing
	// whitespace) is submitted by the form; it must be preserved verbatim.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html>
<html>
  <body>
    <textarea>   a
    b  </textarea>
    <p>c   d</p>
  </body>
</html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyHtml = true,
		},
	});
}

TEST(BundlerHTML, HtmlMinificationCommentRemoval) {
	// Ordinary comments are dropped in minify mode; conditional comments
	// (<!--[if ...]> -->) survive because they are active code for old IE.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html>
<html>
  <head>
    <!-- remove me -->
    <!--[if IE]><script>document.title = "IE";</script><![endif]-->
    <title>  T   T  </title>
  </head>
  <body>
    <p>Hi</p>
  </body>
</html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyHtml = true,
		},
	});
}

TEST(BundlerHTML, HtmlMinificationAttributeQuotes) {
	// Attribute values with no HTML-forbidden characters lose their quotes;
	// empty values collapse to a bare attribute name. Values containing
	// whitespace or quotes must keep their quotes.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html>
<html lang="en">
  <body>
    <div class="a-b_c" data-id="42">one</div>
    <div class="a b">two</div>
    <input disabled="" checked="">
  </body>
</html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyHtml = true,
		},
	});
}

TEST(BundlerHTML, HtmlMinificationUnsafeQuotes) {
	// Unquoted attribute values can never contain whitespace, quotes, or the
	// characters =, <, >, `. Even with MinifyHtml these values keep their
	// quotes so the document remains valid and unambiguous.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html>
<html>
  <body>
    <div class="a b">spaces</div>
    <div data-x="a=b">equals</div>
    <div data-y="on`off">backtick</div>
    <div data-z="a'b">single-quote</div>
  </body>
</html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyHtml = true,
		},
	});
}

TEST(BundlerHTML, HtmlBasePathAbsolute) {
	// With an absolute PublicPath ("/app/"), every rewritten resource URL in
	// the HTML is joined onto it: the module script, the module-preload and
	// stylesheet links injected into <head>, and plain asset attributes (such
	// as the <img src>). The linked files are still emitted under /out.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="app.js"></script><img src="logo.png"></body></html>)"},
			{"/app.js",
			 R"(import "./style.css"; import("./dep.js").then(m => console.log(m.x));)"},
			{"/dep.js", R"(export const x = 42;)"},
			{"/style.css", R"(p { color: red })"},
			{"/logo.png", "fake-png-data"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
			.PublicPath = "/app/",
		},
	});
}

TEST(BundlerHTML, HtmlBasePathRelative) {
	// PublicPath values that resolve to "no base" (empty or "./") must leave
	// the current relative-URL behavior untouched.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script src="app.js"></script><img src="logo.png"></body></html>)"},
			{"/app.js", R"(console.log("hi"))"},
			{"/logo.png", "fake-png-data"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.PublicPath = "./",
		},
	});
}

TEST(BundlerHTML, HtmlBasePathURL) {
	// A full URL PublicPath is used as the base for all rewritten assets,
	// producing absolute URLs pointing at the CDN.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="app.js"></script></body></html>)"},
			{"/app.js", R"(import("./dep.js").then(m => console.log(m.x));)"},
			{"/dep.js", R"(export const x = 42;)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
			.PublicPath = "https://cdn.example.com/",
		},
	});
}

TEST(BundlerHTML, HtmlBasePathExternalUntouched) {
	// External / data URLs are never prefixed with PublicPath; only locally
	// bundled resources are.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="app.js"></script><img src="https://static.example.com/x.png"><img src="data:image/png;base64,AAAA"></body></html>)"},
			{"/app.js", R"(console.log("hi"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.PublicPath = "/app/",
		},
	});
}

TEST(BundlerHTML, HtmlSrcsetRewritingMultipleEntries) {
	// Each candidate URL in a <img srcset> is resolved and rewritten in place;
	// the width descriptor and the ", " separator chain are preserved exactly.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><img src="fallback.png" srcset="hero-480.png 480w, hero-800.png 800w, hero-1200.png 1200w"></body></html>)"},
			{"/hero-480.png", "P480"},
			{"/hero-800.png", "P800"},
			{"/hero-1200.png", "P1200"},
			{"/fallback.png", "PFALLBACK"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlSrcsetDensityDescriptor) {
	// A single-entry srcset with a pixel-density descriptor keeps its
	// descriptor after the URL is rewritten.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><img srcset="icon-2x.png 2x"></body></html>)"},
			{"/icon-2x.png", "ICON2X"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlSrcsetSourceElement) {
	// <source> elements inside <picture> use srcset; each candidate URL is a
	// separate import record and is rewritten independently.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><picture><source type="image/avif" srcset="a.avif 1x, b.avif 2x"><source type="image/webp" srcset="c.webp 1x, d.webp 2x"><img src="fallback.png"></picture></body></html>)"},
			{"/a.avif", "AVIF1"},
			{"/b.avif", "AVIF2"},
			{"/c.webp", "WEBP1"},
			{"/d.webp", "WEBP2"},
			{"/fallback.png", "PFALLBACK"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlSrcsetImagesrcsetLink) {
	// <link rel="preload" as="image" imagesrcset="..."> is rewritten but is
	// still never treated as a stylesheet (no CSS bundle, href untouched).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="preload" as="image" imagesrcset="card-1x.png 1x, card-2x.png 2x"></head><body></body></html>)"},
			{"/card-1x.png", "CARD1"},
			{"/card-2x.png", "CARD2"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlSrcsetExternalAndDataUntouched) {
	// External (https) and protocol-relative (//cdn) URLs inside a srcset are
	// never bundled or rewritten; the local candidate next to them is.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><img srcset="local.png 1x, https://cdn.example.com/remote.png 2x, //cdn.example.com/proto.png 3x"></body></html>)"},
			{"/local.png", "LOCAL"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlSrcsetMalformedInputPreserved) {
	// Forgiveness: empty candidates and stray commas produce no records, and
	// the surviving candidate URLs are still rewritten without disturbing the
	// surrounding punctuation.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><img srcset="  good.png 1x , ,bad.png 2x,  "></body></html>)"},
			{"/good.png", "GOOD"},
			{"/bad.png", "BAD"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlSrcsetRespectsPublicPath) {
	// PublicPath applies to srcset candidates exactly like it does to other
	// resource URLs: local URLs are joined onto the base, external stay as-is.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><body><img srcset="hero-480.png 480w, https://cdn.example.com/hero-800.png 800w"></body></html>)"},
			{"/hero-480.png", "P480"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.PublicPath = "/app/",
		},
	});
}

TEST(BundlerHTML, HtmlResourceHintsDisabled) {
	// Phase 9 is a Guchho extension that defaults to disabled; no resource-hint
	// links are injected into an otherwise normal bundle.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><img src="logo.png"><script src="app.js"></script></body></html>)"},
			{"/app.js", R"(console.log("hi"))"},
			{"/logo.png", "fake-png-data"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlResourceHintsFontPreload) {
	// A local @font-face source becomes a `<link rel="preload" as="font">`
	// hint pointing at the emitted asset; the hint is the first child of
	// <head>. Only the font's output location matters: the stylesheet's folder
	// is used to resolve the url().
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body></body></html>)"},
			{"/style.css",
			 R"(@font-face { font-family: "Inter"; src: url("./fonts/Inter.woff2") format("woff2"); } body { font-family: "Inter"; })"},
			{"/fonts/Inter.woff2", "FAKE-FONT-WOFF2"},
		},
		.entry_paths = {"/index.html", "/style.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ResourceHints = guchho::config::ResourceHintsConfig{
				.enabled = true,
				.preload = true,
				.fonts = true,
			},
		},
	});
}

TEST(BundlerHTML, HtmlResourceHintsExternalPreconnect) {
	// External resource origins get preconnect + dns-prefetch hints; the first
	// hint block is <head>'s first child. <script> origins and relative/data
	// URLs are not hinted.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><img src="https://images.example.com/logo.png"><link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter"><script src="https://scripts.example.com/app.js"></script></body></html>)"},
			{"/app.js", R"(console.log("hi"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ResourceHints = guchho::config::ResourceHintsConfig{
				.enabled = true,
				.preconnect = true,
				.dns_prefetch = true,
			},
		},
	});
}

TEST(BundlerHTML, HtmlResourceHintsDeduplication) {
	// Multiple resources from the same origin produce a single preconnect and
	// a single dns-prefetch hint.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><img src="https://cdn.example.com/a.png"><img src="https://cdn.example.com/b.png"><link rel="stylesheet" href="https://cdn.example.com/c.css"></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ResourceHints = guchho::config::ResourceHintsConfig{
				.enabled = true,
				.preconnect = true,
				.dns_prefetch = true,
			},
		},
	});
}

TEST(BundlerHTML, HtmlResourceHintsSkipsDataUrl) {
	// A data: URL inside @font-face is not a local asset and must not produce
	// a preload hint.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body></body></html>)"},
			{"/style.css",
			 R"(@font-face { font-family: "Icons"; src: url("data:font/woff2;base64,AAAA") format("woff2"); })"},
		},
		.entry_paths = {"/index.html", "/style.css"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ResourceHints = guchho::config::ResourceHintsConfig{
				.enabled = true,
				.preload = true,
				.fonts = true,
			},
		},
	});
}

TEST(BundlerHTML, HtmlResourceHintsOrderingAndScriptSkip) {
	// Hint links land in "preconnect, dns-prefetch" order as the first
	// children of <head>, and origins referenced only by <script> (browsers
	// handle script connection setup themselves) are skipped.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="app.js"></script><img src="https://cdn.example.com/x.png"></body></html>)"},
			{"/app.js", R"(import { x } from "./dep.js"; console.log(x))"},
			{"/dep.js", R"(export const x = 1;)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
			.ResourceHints = guchho::config::ResourceHintsConfig{
				.enabled = true,
				.preload = true,
				.preconnect = true,
				.dns_prefetch = true,
				.fonts = false,
			},
		},
	});
}

TEST(BundlerHTML, HtmlSRIEnabled) {
	// With SRI enabled, the module <script> and the external stylesheet <link>
	// both get an integrity attribute over their final emitted bytes plus
	// `crossorigin` (added, and not duplicated when already present).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="style.css"></head><body><script type="module" src="app.js"></script></body></html>)"},
			{"/app.js", R"(console.log("script body");)"},
			{"/style.css", R"(body { color: red; })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.SRI = true,
			.SRIAlgorithm = "sha384",
		},
	});
}

TEST(BundlerHTML, HtmlSRIDefaultAlgorithm) {
	// Omitting "SRIAlgorithm" falls back to sha384 (the config default).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="style.css"></head><body><script type="module" src="app.js"></script></body></html>)"},
			{"/app.js", R"(console.log("script body");)"},
			{"/style.css", R"(body { color: red; })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.SRI = true,
		},
	});
}

TEST(BundlerHTML, HtmlSRIAlgorithmSHA256) {
	// A non-default algorithm is honored, and inline <script> runs are NOT
	// hashed (their content is bundled into the page, never fetched).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script>document.write("inline");</script><script src="app.js"></script></body></html>)"},
			{"/app.js", R"(console.log("hi");)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.SRI = true,
			.SRIAlgorithm = "sha256",
		},
	});
}

TEST(BundlerHTML, HtmlSRISkipsExternalURL) {
	// External resources are left untouched: no integrity, no crossorigin.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="https://cdn.example.com/app.js"></script></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.SRI = true,
		},
	});
}

TEST(BundlerHTML, HtmlSRIDisabled) {
	// Default (SRI off): output identical to the non-SRI pipeline.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="style.css"></head><body><script type="module" src="app.js"></script></body></html>)"},
			{"/app.js", R"(console.log("script body");)"},
			{"/style.css", R"(body { color: red; })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlCSSLoadingNonBlocking) {
	// "non-blocking" rewrites every local stylesheet link (both the original
	// one and the JS-imported stylesheet link) into a preload with an onload
	// swap plus a <noscript> fallback, while modulepreload links and external
	// stylesheets (like the CDN link below) stay untouched.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="https://cdn.example.com/theme.css"><link rel="stylesheet" href="site.css"></head><body><script type="module" src="app.js"></script></body></html>)"},
			{"/app.js", R"(import "./style.css"; console.log("hi");)"},
			{"/site.css", R"(body { background: blue; })"},
			{"/style.css", R"(p { color: green; })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
			.CSSLoadingStrategyData =
				guchho::config::CSSLoadingStrategy::kNonBlocking,
		},
	});
}

TEST(BundlerHTML, HtmlCSSLoadingBlocking) {
	// Default strategy: a plain <link rel="stylesheet"> for both the original
	// and the JS-imported stylesheet (the modulepreload link is unaffected).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="site.css"></head><body><script type="module" src="app.js"></script></body></html>)"},
			{"/app.js", R"(import "./style.css"; console.log("hi");)"},
			{"/site.css", R"(body { background: blue; })"},
			{"/style.css", R"(p { color: green; })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlJSImportedStylesheetIsDeduplicatedWithLink) {
	// A stylesheet referenced both by an HTML <link rel="stylesheet"> and
	// imported from a JS module must be emitted exactly once. The CSS entry
	// point (from the <link>) is the canonical output; the JS entry must not
	// produce a second, identical CSS chunk (previously this generated both
	// "style.css" and a duplicate "app.css" with an injected duplicate <link>).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="style.css"></head><body><script type="module" src="app.js"></script></body></html>)"},
			{"/app.js", R"(import "./style.css"; console.log("hi");)"},
			{"/style.css", R"(p { color: green; })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlJSImportedStylesheetIsDeduplicatedWithLinkNoSplitting) {
	// Same deduplication requirement as
	// HtmlJSImportedStylesheetIsDeduplicatedWithLink, but with code splitting
	// disabled (the default). Each entry point is linked separately here, so the
	// duplicate-CSS suppression must hold in that path too.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head><link rel="stylesheet" href="style.css"></head><body><script type="module" src="app.js"></script></body></html>)"},
			{"/app.js", R"(import "./style.css"; console.log("hi");)"},
			{"/style.css", R"(p { color: green; })"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
	});
}

TEST(BundlerHTML, HtmlErrorDiagnosticCodeFrame) {
	// When a resource referenced from HTML fails to resolve, the error points
	// at the referencing element with a source code frame (file:line:col).
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="./missing.js"></script></body></html>)"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
		
		.expected_scan_log = R"scan(
X [ERROR] Could not resolve "./missing.js"

    index.html:1:40:
      1 │ .../head><body><script type="module" src="./missing.js"></script></...
        ╵                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

)scan",
.source_logs = true,
	});
}

TEST(BundlerHTML, HtmlWarningDiagnosticCodeFrame) {
	// The import-map reorder warning carries the location of the import map
	// element that was moved, also rendered as a code frame.
	html_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.html",
			 R"(<!DOCTYPE html><html><head></head><body><script type="module" src="./app.js"></script><script type="importmap">{"imports":{}}</script></body></html>)"},
			{"/app.js", R"(console.log("app"))"},
		},
		.entry_paths = {"/index.html"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
		},
		
		.expected_compile_log = R"compile(
▲ [WARNING] import map in "index.html" was reordered to appear before module scripts [import-map-reordered]

    index.html:1:86:
      1 │ ..."./app.js"></script><script type="importmap">{"imports":{}}</scr...
        ╵                        ~~~~~~~~~~~~~~~~~~~~~~~~~

)compile",
.source_logs = true,
	});
}

} // namespace bundler::test