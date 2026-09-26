#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

Suite default_suite{"default"};


TEST(BundlerDefault, TestEntryNamesNoSlashAfterDir) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/app1/main.ts", "console.log(1)"},
			{"/src/app2/main.ts", "console.log(2)"},
			{"/src/app3/main.ts", "console.log(3)"},
		},
		.entry_paths_advanced = {
			{.InputPath = "/src/app1/main.ts"},
			{.InputPath = "/src/app2/main.ts"},
			{.InputPath = "/src/app3/main.ts", .OutputPath = "customPath"},
		},
		
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.EntryPathTemplate = {
				// "[dir]-[name]"
				{.Data = "./", .Placeholder = guchho::config::PathPlaceholder::kDir},
				{.Data = "-", .Placeholder = guchho::config::PathPlaceholder::kName},
			},
			
		},
	});
}


TEST(BundlerDefault, TestEntryNamesNonPortableCharacter) {
    default_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry1-:.ts", "console.log(1)"},
            {"/entry2-:.ts", "console.log(2)"},
        },
        .entry_paths_advanced = {
            // The ":" should turn into "_" for cross-platform Windows portability
            {.InputPath = "/entry1-:.ts"},

            // The ":" should be preserved since the user really wants it
            {.InputPath = "/entry2-:.ts", .OutputPath = "entry2-*"},
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerDefault, TestEntryNamesChunkNamesExtPlaceholder) {
    default_suite.ExpectBundled(Bundled{
        .files = {
            {"/src/entries/entry1.js",
             R"test(import "../lib/shared.js"; import "./entry1.css"; console.log('entry1'))test"},
            {"/src/entries/entry2.js",
             R"test(import "../lib/shared.js"; import "./entry2.css"; console.log('entry2'))test"},
            {"/src/entries/entry1.css",
             R"test(a:after { content: "entry1" })test"},
            {"/src/entries/entry2.css",
             R"test(a:after { content: "entry2" })test"},
            {"/src/lib/shared.js", "console.log('shared')"},
        },
        .entry_paths = {
            "/src/entries/entry1.js",
            "/src/entries/entry2.js",
        },
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .CodeSplitting = true,
            
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/src",

            .EntryPathTemplate = {
                {.Data = "main/", .Placeholder = guchho::config::PathPlaceholder::kExt},
                {.Data = "/", .Placeholder = guchho::config::PathPlaceholder::kName},
                {.Data = "-", .Placeholder = guchho::config::PathPlaceholder::kHash},
            },

            .ChunkPathTemplate = {
                {.Data = "common/", .Placeholder = guchho::config::PathPlaceholder::kExt},
                {.Data = "/", .Placeholder = guchho::config::PathPlaceholder::kName},
                {.Data = "-", .Placeholder = guchho::config::PathPlaceholder::kHash},
            },
        },
    });
}


TEST(BundlerDefault, TestManglePropsMinify) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry1.js", R"test(

				export function shouldMangle_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX() {
					let foo = {
						bar_: 0,
						baz_() {},
					};
					let { bar_ } = foo;
					({ bar_ } = foo);
					class foo_ {
						bar_ = 0
						baz_() {}
						static bar_ = 0
						static baz_() {}
					}
					return { bar_, foo_ }
				}

				export function shouldNotMangle_YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY() {
					let foo = {
						'bar_': 0,
						'baz_'() {},
					};
					let { 'bar_': bar_ } = foo;
					({ 'bar_': bar_ } = foo);
					class foo_ {
						'bar_' = 0
						'baz_'() {}
						static 'bar_' = 0
						static 'baz_'() {}
					}
					return { 'bar_': bar_, 'foo_': foo_ }
				}
			)test"},
			{"/entry2.js", R"test(

				export default {
					bar_: 0,
					'baz_': 1,
				}
			)test"},
		},
		.entry_paths = {
			"/entry1.js",
			"/entry2.js",
		},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MinifyIdentifiers = true,
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestSimpleES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import {fn} from './foo'
				console.log(fn())
			)test"},
			{"/foo.js", R"test(

				export function fn() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestSimpleCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const fn = require('./foo')
				console.log(fn())
			)test"},
			{"/foo.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestNestedCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function nestedScope() {
					const fn = require('./foo')
					console.log(fn())
				}
				nestedScope()
			)test"},
			{"/foo.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestNewExpressionCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				new (require("./foo.js")).Foo();
			)test"},
			{"/foo.js", R"test(

				class Foo {}
				module.exports = {Foo};
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestCommonJSFromES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const {foo} = require('./foo')
				console.log(foo(), bar())
				const {bar} = require('./bar') // This should not be hoisted
			)test"},
			{"/foo.js", R"test(

				export function foo() {
					return 'foo'
				}
			)test"},
			{"/bar.js", R"test(

				export function bar() {
					return 'bar'
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestES6FromCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import {foo} from './foo'
				console.log(foo(), bar())
				import {bar} from './bar' // This should be hoisted
			)test"},
			{"/foo.js", R"test(

				exports.foo = function() {
					return 'foo'
				}
			)test"},
			{"/bar.js", R"test(

				exports.bar = function() {
					return 'bar'
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestNestedES6FromCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import {fn} from './foo'
				(() => {
					console.log(fn())
				})()
			)test"},
			{"/foo.js", R"test(

				exports.fn = function() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestExportFormsES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export default 123
				export var v = 234
				export let l = 234
				export const c = 234
				export {Class as C}
				export function Fn() {}
				export class Class {}
				export * from './a'
				export * as b from './b'
			)test"},
			{"/a.js", "export const abc = undefined"},
			{"/b.js", "export const xyz = null"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestExportFormsIIFE) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export default 123
				export var v = 234
				export let l = 234
				export const c = 234
				export {Class as C}
				export function Fn() {}
				export class Class {}
				export * from './a'
				export * as b from './b'
			)test"},
			{"/a.js", "export const abc = undefined"},
			{"/b.js", "export const xyz = null"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			.GlobalName = {"globalName"},

			
		},
		
	});
}

TEST(BundlerDefault, TestExportFormsAMD) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				export default 123
				export var v = 234
				export let l = 234
				export const c = 234
				export {Class as C}
				export function Fn() {}
				export class Class {}
				export * from './a'
				export * as b from './b'
			)test"},
			{"/a.js", "export const abc = undefined"},
			{"/b.js", "export const xyz = null"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kAMD,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDefault, TestExportFormsUMD) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				export default 123
				export var v = 234
				export let l = 234
				export const c = 234
				export {Class as C}
				export function Fn() {}
				export class Class {}
				export * from './a'
				export * as b from './b'
			)test"},
			{"/a.js", "export const abc = undefined"},
			{"/b.js", "export const xyz = null"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kUMD,
			.AbsOutputFile = "/out.js",
			.GlobalName = {"globalName"},
		},
	});
}

TEST(BundlerDefault, TestExportFormsSystem) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				export default 123
				export var v = 234
				export let l = 234
				export const c = 234
				export {Class as C}
				export function Fn() {}
				export class Class {}
				export * from './a'
				export * as b from './b'
			)test"},
			{"/a.js", "export const abc = undefined"},
			{"/b.js", "export const xyz = null"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kSystem,
			.AbsOutputFile = "/out.js",
		},
	});
}

TEST(BundlerDefault, TestSystemJSExternalImportSetters) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				import * as ns from "pkg-a"
				import defaults from "pkg-b"
				import { named } from "pkg-b"
				import "pkg-c"
				import { val } from "./internal"
				console.log(ns.foo, defaults, named, val)
			)test"},
			{"/internal.js", "export const val = 42"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kSystem,
			.AbsOutputFile = "/out.js",
			.ExternalPackages = true,
		},
	});
}

TEST(BundlerDefault, TestExportFormsWithMinifyIdentifiersAndNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				export default 123
				export var varName = 234
				export let letName = 234
				export const constName = 234
				function Func2() {}
				class Class2 {}
				export {Class as Cls, Func2 as Fn2, Class2 as Cls2}
				export function Func() {}
				export class Class {}
				export * from './a'
				export * as fromB from './b'
			)test"},
			{"/b.js", "export default function() {}"},
			{"/c.js", "export default function foo() {}"},
			{"/d.js", "export default class {}"},
			{"/e.js", "export default class Foo {}"},
		},
		.entry_paths = {
			"/a.js",
			"/b.js",
			"/c.js",
			"/d.js",
			"/e.js",
		},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestImportFormsWithNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import 'foo'
				import {} from 'foo'
				import * as ns from 'foo'
				import {a, b as c} from 'foo'
				import def from 'foo'
				import def2, * as ns2 from 'foo'
				import def3, {a2, b as c3} from 'foo'
				const imp = [
					import('foo'),
					function nested() { return import('foo') },
				]
				console.log(ns, a, c, def, def2, ns2, def3, a2, c3, imp)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportFormsWithMinifyIdentifiersAndNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import 'foo'
				import {} from 'foo'
				import * as ns from 'foo'
				import {a, b as c} from 'foo'
				import def from 'foo'
				import def2, * as ns2 from 'foo'
				import def3, {a2, b as c3} from 'foo'
				const imp = [
					import('foo'),
					function() { return import('foo') },
				]
				console.log(ns, a, c, def, def2, ns2, def3, a2, c3, imp)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestExportFormsCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				require('./commonjs')
				require('./c')
				require('./d')
				require('./e')
				require('./f')
				require('./g')
				require('./h')
			)test"},
			{"/commonjs.js", R"test(

				export default 123
				export var v = 234
				export let l = 234
				export const c = 234
				export {Class as C}
				export function Fn() {}
				export class Class {}
				export * from './a'
				export * as b from './b'
			)test"},
			{"/a.js", "export const abc = undefined"},
			{"/b.js", "export const xyz = null"},
			{"/c.js", "export default class {}"},
			{"/d.js", "export default class Foo {} Foo.prop = 123"},
			{"/e.js", "export default function() {}"},
			{"/f.js", "export default function foo() {} foo.prop = 123"},
			{"/g.js", "export default async function() {}"},
			{"/h.js", "export default async function foo() {} foo.prop = 123"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestExportChain) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {b as a} from './foo'
			)test"},
			{"/foo.js", R"test(

				export {c as b} from './bar'
			)test"},
			{"/bar.js", R"test(

				export const c = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestExportInfiniteCycle1) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {a as b} from './entry'
				export {b as c} from './entry'
				export {c as d} from './entry'
				export {d as a} from './entry'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_compile_log = R"compile(
entry.js: ERROR: Detected cycle while resolving import "a"
entry.js: ERROR: Detected cycle while resolving import "b"
entry.js: ERROR: Detected cycle while resolving import "c"
entry.js: ERROR: Detected cycle while resolving import "d"
)compile",
		
	});
}

TEST(BundlerDefault, TestExportInfiniteCycle2) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {a as b} from './foo'
				export {c as d} from './foo'
			)test"},
			{"/foo.js", R"test(

				export {b as c} from './entry'
				export {d as a} from './entry'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_compile_log = R"compile(
entry.js: ERROR: Detected cycle while resolving import "a"
entry.js: ERROR: Detected cycle while resolving import "c"
foo.js: ERROR: Detected cycle while resolving import "b"
foo.js: ERROR: Detected cycle while resolving import "d"
)compile",
		
	});
}

TEST(BundlerDefault, TestJSXImportsCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				import {elem, frag} from './custom-react'
				console.log(<div/>, <>fragment</>)
			)test"},
			{"/custom-react.js", R"test(

				module.exports = {}
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.JSX = guchho::config::JSXOptions{
				.Factory  = guchho::config::DefineExpr{.Parts = {"elem"}},
				.Fragment = guchho::config::DefineExpr{.Parts = {"frag"}},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestJSXImportsES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				import {elem, frag} from './custom-react'
				console.log(<div/>, <>fragment</>)
			)test"},
			{"/custom-react.js", R"test(

				export function elem() {}
				export function frag() {}
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.JSX = guchho::config::JSXOptions{
				.Factory  = guchho::config::DefineExpr{.Parts = {"elem"}},
				.Fragment = guchho::config::DefineExpr{.Parts = {"frag"}},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestJSXSyntaxInJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(<div/>)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: The JSX syntax extension is not currently enabled
NOTE: The guchho loader for this file is currently set to "js" but it must be set to "jsx" to be able to parse JSX syntax. You can use '--loader:.js=jsx' to do that.
)scan",
		
	});
}

TEST(BundlerDefault, TestJSXConstantFragments) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import './default'
				import './null'
				import './boolean'
				import './number'
				import './string-single-empty'
				import './string-double-empty'
				import './string-single-punctuation'
				import './string-double-punctuation'
				import './string-template'
			)test"},
			{"/default.jsx", "console.log(<></>)"},
			{"/null.jsx", "console.log(<></>) // @jsxFrag null"},
			{"/boolean.jsx", "console.log(<></>) // @jsxFrag true"},
			{"/number.jsx", "console.log(<></>) // @jsxFrag 123"},
			{"/string-single-empty.jsx", "console.log(<></>) // @jsxFrag ''"},
			{"/string-double-empty.jsx", R"test(console.log(<></>) // @jsxFrag "")test"},
			{"/string-single-punctuation.jsx", "console.log(<></>) // @jsxFrag '['"},
			{"/string-double-punctuation.jsx", R"test(console.log(<></>) // @jsxFrag "[")test"},
			{"/string-template.jsx", R"test(
console.log(<></>) // @jsxFrag ``)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.JSX = guchho::config::JSXOptions{
				.Fragment = guchho::config::DefineExpr{
					.Constant = std::make_shared<guchho::javascript::EString>(guchho::javascript::EString{u"]"}),
				},
			},
			
		},
		.expected_scan_log = R"scan(
string-template.jsx: WARNING: Invalid JSX fragment: ``
)scan",
		
	});
}

TEST(BundlerDefault, TestJSXAutomaticImportsCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				import {jsx, Fragment} from './custom-react'
				console.log(<div jsx={jsx}/>, <><Fragment/></>)
			)test"},
			{"/custom-react.js", R"test(

				module.exports = {}
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"react/jsx-runtime", true},
					},
				},
			},
			.JSX = guchho::config::JSXOptions{
				.AutomaticRuntime = true,
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestJSXAutomaticImportsES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				import {jsx, Fragment} from './custom-react'
				console.log(<div jsx={jsx}/>, <><Fragment/></>)
			)test"},
			{"/custom-react.js", R"test(

				export function jsx() {}
				export function Fragment() {}
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"react/jsx-runtime", true},
					},
				},
			},
			.JSX = guchho::config::JSXOptions{
				.AutomaticRuntime = true,
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestJSXAutomaticSyntaxInJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(<div/>)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"react/jsx-runtime", true},
					},
				},
			},
			.JSX = guchho::config::JSXOptions{
				.AutomaticRuntime = true,
			},
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: The JSX syntax extension is not currently enabled
NOTE: The guchho loader for this file is currently set to "js" but it must be set to "jsx" to be able to parse JSX syntax. You can use '--loader:.js=jsx' to do that.
)scan",
		
	});
}

TEST(BundlerDefault, TestNodeModules) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import fn from 'demo-pkg'
				console.log(fn())
			)test"},
			{"/Users/user/project/node_modules/demo-pkg/index.js", R"test(

				module.exports = function() {
					return 123
				}
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireChildDirCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				console.log(require('./dir'))
			)test"},
			{"/Users/user/project/src/dir/index.js", R"test(

				module.exports = 123
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireChildDirES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import value from './dir'
				console.log(value)
			)test"},
			{"/Users/user/project/src/dir/index.js", R"test(

				export default 123
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireParentDirCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/dir/entry.js", R"test(

				console.log(require('..'))
			)test"},
			{"/Users/user/project/src/index.js", R"test(

				module.exports = 123
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/dir/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireParentDirES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/dir/entry.js", R"test(

				import value from '..'
				console.log(value)
			)test"},
			{"/Users/user/project/src/index.js", R"test(

				export default 123
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/dir/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportMissingES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import fn, {x as a, y as b} from './foo'
				console.log(fn(a, b))
			)test"},
			{"/foo.js", R"test(

				export const x = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_compile_log = R"compile(
entry.js: ERROR: No matching export in "foo.js" for import "default"
entry.js: ERROR: No matching export in "foo.js" for import "y"
)compile",
		
	});
}

TEST(BundlerDefault, TestImportMissingUnusedES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import fn, {x as a, y as b} from './foo'
			)test"},
			{"/foo.js", R"test(

				export const x = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_compile_log = R"compile(
entry.js: ERROR: No matching export in "foo.js" for import "default"
entry.js: ERROR: No matching export in "foo.js" for import "y"
)compile",
		
	});
}

TEST(BundlerDefault, TestImportMissingCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import fn, {x as a, y as b} from './foo'
				console.log(fn(a, b))
			)test"},
			{"/foo.js", R"test(

				exports.x = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportMissingNeitherES6NorCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/named.js", R"test(

				import fn, {x as a, y as b} from './foo'
				console.log(fn(a, b))
			)test"},
			{"/star.js", R"test(

				import * as ns from './foo'
				console.log(ns.default(ns.x, ns.y))
			)test"},
			{"/star-capture.js", R"test(

				import * as ns from './foo'
				console.log(ns)
			)test"},
			{"/bare.js", R"test(

				import './foo'
			)test"},
			{"/require.js", R"test(

				console.log(require('./foo'))
			)test"},
			{"/import.js", R"test(

				console.log(import('./foo'))
			)test"},
			{"/foo.js", R"test(

				console.log('no exports here')
			)test"},
		},
		.entry_paths = {
			"/named.js",
			"/star.js",
			"/star-capture.js",
			"/bare.js",
			"/require.js",
			"/import.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		.expected_compile_log = R"compile(
named.js: WARNING: Import "x" will always be undefined because the file "foo.js" has no exports
named.js: WARNING: Import "y" will always be undefined because the file "foo.js" has no exports
star.js: WARNING: Import "x" will always be undefined because the file "foo.js" has no exports
star.js: WARNING: Import "y" will always be undefined because the file "foo.js" has no exports
)compile",
		
	});
}

TEST(BundlerDefault, TestExportMissingES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import * as ns from './foo'
				console.log(ns)
			)test"},
			{"/foo.js", R"test(

				export {buton} from './bar'
			)test"},
			{"/bar.js", R"test(

				export const button = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_compile_log = R"compile(
foo.js: ERROR: No matching export in "bar.js" for import "buton"
bar.js: NOTE: Did you mean to import "button" instead?
)compile",
		
	});
}

TEST(BundlerDefault, TestDotImport) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import {x} from '.'
				console.log(x)
			)test"},
			{"/index.js", R"test(

				exports.x = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireWithTemplate) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				console.log(require('./b'))
				console.log(require(`./b`))
			)test"},
			{"/b.js", R"test(

				exports.x = 123
			)test"},
		},
		.entry_paths = {"/a.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestDynamicImportWithTemplateIIFE) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				import('./b').then(ns => console.log(ns))
				import(`./b`).then(ns => console.log(ns))
			)test"},
			{"/b.js", R"test(

				exports.x = 123
			)test"},
		},
		.entry_paths = {"/a.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireAndDynamicImportInvalidTemplate) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				require(tag`./b`)
				require(`./${b}`)

				try {
					require(tag`./b`)
					require(`./${b}`)
				} catch {
				}

				(async () => {
					import(tag`./b`)
					import(`./${b}`)
					await import(tag`./b`)
					await import(`./${b}`)

					try {
						import(tag`./b`)
						import(`./${b}`)
						await import(tag`./b`)
						await import(`./${b}`)
					} catch {
					}
				})()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestDynamicImportWithExpressionCJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				import('foo')
				import(foo())
			)test"},
		},
		.entry_paths = {"/a.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestMinifiedDynamicImportWithExpressionCJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				import('foo')
				import(foo())
			)test"},
		},
		.entry_paths = {"/a.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			.MinifyWhitespace = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestConditionalRequireResolve) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				require.resolve(x ? 'a' : y ? 'b' : 'c')
				require.resolve(x ? y ? 'a' : 'b' : c)
			)test"},
		},
		.entry_paths = {"/a.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"a", true},
						{"b", true},
						{"c", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestConditionalRequire) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				require(x ? 'a' : y ? './b' : 'c')
				require(x ? y ? 'a' : './b' : c)
			)test"},
			{"/b.js", R"test(

				exports.foo = 213
			)test"},
		},
		.entry_paths = {"/a.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"a", true},
						{"c", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestConditionalImport) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				import(x ? 'a' : y ? './import' : 'c')
			)test"},
			{"/b.js", R"test(

				import(x ? y ? 'a' : './import' : c)
			)test"},
			{"/import.js", R"test(

				exports.foo = 213
			)test"},
		},
		.entry_paths = {
			"/a.js",
			"/b.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"a", true},
						{"c", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireBadArgumentCount) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				require()
				require("a", "b")

				try {
					require()
					require("a", "b")
				} catch {
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireJson) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(require('./test.json'))
			)test"},
			{"/test.json", R"test(

				{
					"a": true,
					"b": 123,
					"c": [null]
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireTxt) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(require('./test.txt'))
			)test"},
			{"/test.txt", "This is a test."},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireBadExtension) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(require('./test.bad'))
			)test"},
			{"/test.bad", "This is a test."},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: No loader is configured for ".bad" files: test.bad
)scan",
		
	});
}

TEST(BundlerDefault, TestFalseRequire) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				(require => require('/test.txt'))()
			)test"},
			{"/test.txt", "This is a test."},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireWithoutCall) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const req = require
				req('./entry')
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestNestedRequireWithoutCall) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				(() => {
					const req = require
					req('./entry')
				})()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireWithCallInsideTry) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				try {
					const supportsColor = require('supports-color');
					if (supportsColor && (supportsColor.stderr || supportsColor).level >= 2) {
						exports.colors = [];
					}
				} catch (error) {
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireWithoutCallInsideTry) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				try {
					oldLocale = globalLocale._abbr;
					var aliasedRequire = require;
					aliasedRequire('./locale/' + name);
					getSetGlobalLocale(oldLocale);
				} catch (e) {}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequirePropertyAccessCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// These shouldn't warn since the format is CommonJS
				console.log(Object.keys(require.cache))
				console.log(Object.keys(require.extensions))
				delete require.cache['fs']
				delete require.extensions['.json']
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestAwaitImportInsideTry) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				async function main(name) {
					try {
						return await import(name)
					} catch {
					}
				}
				main('fs')
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportInsideTry) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				let x
				try {
					x = import('nope1')
					x = await import('nope2')
				} catch {
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Could not resolve "nope1"
NOTE: You can mark the path "nope1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle. You can also add ".catch()" here to handle this failure at run-time instead of bundle-time.
)scan",
		
	});
}

TEST(BundlerDefault, TestImportThenCatch) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import(name).then(pass, fail)
				import(name).then(pass).catch(fail)
				import(name).catch(fail)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestSourceMap) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(
				import {bar} from './bar'
				import data from './data.txt'
				function foo() { bar() }
				foo()
				console.log(data)
			)test"},
			{"/Users/user/project/src/bar.js", R"test(
				export function bar() { throw new Error('test') }
			)test"},
			{"/Users/user/project/src/data.txt", "#2041"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.SourceMapData = guchho::config::SourceMap::kLinkedWithComment,
			.AbsOutputFile = "/Users/user/project/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestNestedScopeBug) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				(() => {
					function a() {
						b()
					}
					{
						var b = () => {}
					}
					a()
				})()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestHashbangBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(#!/usr/bin/env a
				import {code} from './code'
				process.exit(code)
			)test"},
			{"/code.js", R"test(#!/usr/bin/env b
				export const code = 0
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestHashbangNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(#!/usr/bin/env node
				process.exit(0);
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestHashbangBannerUseStrictOrder) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(#! in file
				'use strict'
				foo()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			.JSBanner = "#! from banner",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireFSBrowser) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(require('fs'))
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kBrowser,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Could not resolve "fs"
NOTE: The package "fs" wasn't found on the file system but is built into node. Are you trying to bundle for node? You can use "Platform: api.PlatformNode" to do that, which will remove this error.
)scan",
		
	});
}

TEST(BundlerDefault, TestRequireFSNode) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				return require('fs')
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireFSNodeMinify) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				return require('fs')
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			.MinifyWhitespace = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestImportFSBrowser) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import 'fs'
				import * as fs from 'fs'
				import defaultValue from 'fs'
				import {readFileSync} from 'fs'
				console.log(fs, readFileSync, defaultValue)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kBrowser,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Could not resolve "fs"
NOTE: The package "fs" wasn't found on the file system but is built into node. Are you trying to bundle for node? You can use "Platform: api.PlatformNode" to do that, which will remove this error.
)scan",
		
	});
}

TEST(BundlerDefault, TestImportFSNodeCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import 'fs'
				import * as fs from 'fs'
				import defaultValue from 'fs'
				import {readFileSync} from 'fs'
				console.log(fs, readFileSync, defaultValue)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportFSNodeES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import 'fs'
				import * as fs from 'fs'
				import defaultValue from 'fs'
				import {readFileSync} from 'fs'
				console.log(fs, readFileSync, defaultValue)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestExportFSBrowser) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export * as fs from 'fs'
				export {readFileSync} from 'fs'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kBrowser,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Could not resolve "fs"
NOTE: The package "fs" wasn't found on the file system but is built into node. Are you trying to bundle for node? You can use "Platform: api.PlatformNode" to do that, which will remove this error.
)scan",
		
	});
}

TEST(BundlerDefault, TestExportFSNode) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export * as fs from 'fs'
				export {readFileSync} from 'fs'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestReExportFSNode) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {fs as f} from './foo'
				export {readFileSync as rfs} from './foo'
			)test"},
			{"/foo.js", R"test(

				export * as fs from 'fs'
				export {readFileSync} from 'fs'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestExportFSNodeInCommonJSModule) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import * as fs from 'fs'
				import {readFileSync} from 'fs'
				exports.fs = fs
				exports.readFileSync = readFileSync
				exports.foo = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestExportWildcardFSNodeES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export * from 'fs'
				export * from './internal'
				export * from './external'
			)test"},
			{"/internal.js", R"test(

				export let foo = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"./external", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestExportWildcardFSNodeCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export * from 'fs'
				export * from './internal'
				export * from './external'
			)test"},
			{"/internal.js", R"test(

				export let foo = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"./external", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestExportSpecialName) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.mjs", R"test(

			export const __proto__ = 123;
			)test"},
		},
		.entry_paths = {"/entry.mjs"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestExportSpecialNameBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const lib = require('./lib.mjs');
				console.log(lib.__proto__);
			)test"},
			{"/lib.mjs", R"test(

				export const __proto__ = 123;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestNodeAnnotationFalsePositiveIssue3544) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.mjs", R"test(

				export function confuseNode(exports) {
					// If this local is called "exports", node incorrectly
					// thinks this file has an export called "notAnExport".
					// We must make sure that it doesn't have that name
					// when targeting Node with CommonJS.
					exports.notAnExport = function() {
					};
				}
			)test"},
		},
		.entry_paths = {"/entry.mjs"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestNodeAnnotationInvalidIdentifierIssue4100) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.mjs", R"test(

				let foo, bar, baz
				export {
					foo, bar as if, baz as "..."
				}
			)test"},
		},
		.entry_paths = {"/entry.mjs"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestMinifiedBundleES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import {foo} from './a'
				console.log(foo())
			)test"},
			{"/a.js", R"test(

				export function foo() {
					return 123
				}
				foo()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifyWhitespace = true,
			.MinifyIdentifiers = true,
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestMinifiedBundleCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const {foo} = require('./a')
				console.log(foo(), require('./j.json'))
			)test"},
			{"/a.js", R"test(

				exports.foo = function() {
					return 123
				}
			)test"},
			{"/j.json", R"test(

				{"test": true}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifyWhitespace = true,
			.MinifyIdentifiers = true,
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestMinifiedBundleEndingWithImportantSemicolon) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				while(foo()); // This semicolon must not be stripped
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			.MinifyWhitespace = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestRuntimeNameCollisionNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function __require() { return 123 }
				console.log(__require())
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelReturnForbiddenImport) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				return
				import 'foo'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Top-level return cannot be used inside an ECMAScript module
entry.js: NOTE: This file is considered to be an ECMAScript module because of the "import" keyword here:
)scan",
		
	});
}

TEST(BundlerDefault, TestTopLevelReturnForbiddenExport) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				return
				export var foo
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Top-level return cannot be used inside an ECMAScript module
entry.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
)scan",
		
	});
}

TEST(BundlerDefault, TestTopLevelReturnForbiddenTLA) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				return await foo
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Top-level return cannot be used inside an ECMAScript module
entry.js: NOTE: This file is considered to be an ECMAScript module because of the top-level "await" keyword here:
)scan",
		
	});
}

TEST(BundlerDefault, TestThisOutsideFunction) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				if (shouldBeExportsNotThis) {
					console.log(this)
					console.log((x = this) => this)
					console.log({x: this})
					console.log(class extends this.foo {})
					console.log(class { [this.foo] })
					console.log(class { [this.foo]() {} })
					console.log(class { static [this.foo] })
					console.log(class { static [this.foo]() {} })
				}
				if (shouldBeThisNotExports) {
					console.log(class { foo = this })
					console.log(class { foo() { this } })
					console.log(class { static foo = this })
					console.log(class { static foo() { this } })
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestThisInsideFunction) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function foo(x = this) { console.log(this) }
				const objFoo = {
					foo(x = this) { console.log(this) }
				}
				class Foo {
					x = this
					static y = this.z
					foo(x = this) { console.log(this) }
					static bar(x = this) { console.log(this) }
				}
				new Foo(foo(objFoo))
				if (nested) {
					function bar(x = this) { console.log(this) }
					const objBar = {
						foo(x = this) { console.log(this) }
					}
					class Bar {
						x = this
						static y = this.z
						foo(x = this) { console.log(this) }
						static bar(x = this) { console.log(this) }
					}
					new Bar(bar(objBar))
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestThisWithES6Syntax) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import './cjs'

				import './es6-import-stmt'
				import './es6-import-assign'
				import './es6-import-dynamic'
				import './es6-import-meta'
				import './es6-expr-import-dynamic'
				import './es6-expr-import-meta'

				import './es6-export-variable'
				import './es6-export-function'
				import './es6-export-async-function'
				import './es6-export-enum'
				import './es6-export-const-enum'
				import './es6-export-module'
				import './es6-export-namespace'
				import './es6-export-class'
				import './es6-export-abstract-class'
				import './es6-export-default'
				import './es6-export-clause'
				import './es6-export-clause-from'
				import './es6-export-star'
				import './es6-export-star-as'
				import './es6-export-assign'
				import './es6-export-import-assign'

				import './es6-ns-export-variable'
				import './es6-ns-export-function'
				import './es6-ns-export-async-function'
				import './es6-ns-export-enum'
				import './es6-ns-export-const-enum'
				import './es6-ns-export-module'
				import './es6-ns-export-namespace'
				import './es6-ns-export-class'
				import './es6-ns-export-abstract-class'
			)test"},
			{"/dummy.js", "export const dummy = 123"},
			{"/cjs.js", "console.log(this)"},
			{"/es6-import-stmt.js", "import './dummy'; console.log(this)"},
			{"/es6-import-assign.ts", "import x = require('./dummy'); console.log(this)"},
			{"/es6-import-dynamic.js", "import('./dummy'); console.log(this)"},
			{"/es6-import-meta.js", "import.meta; console.log(this)"},
			{"/es6-expr-import-dynamic.js", "(import('./dummy')); console.log(this)"},
			{"/es6-expr-import-meta.js", "(import.meta); console.log(this)"},
			{"/es6-export-variable.js", "export const foo = 123; console.log(this)"},
			{"/es6-export-function.js", "export function foo() {} console.log(this)"},
			{"/es6-export-async-function.js", "export async function foo() {} console.log(this)"},
			{"/es6-export-enum.ts", "export enum Foo {} console.log(this)"},
			{"/es6-export-const-enum.ts", "export const enum Foo {} console.log(this)"},
			{"/es6-export-module.ts", "export module Foo {} console.log(this)"},
			{"/es6-export-namespace.ts", "export namespace Foo {} console.log(this)"},
			{"/es6-export-class.js", "export class Foo {} console.log(this)"},
			{"/es6-export-abstract-class.ts", "export abstract class Foo {} console.log(this)"},
			{"/es6-export-default.js", "export default 123; console.log(this)"},
			{"/es6-export-clause.js", "export {}; console.log(this)"},
			{"/es6-export-clause-from.js", "export {} from './dummy'; console.log(this)"},
			{"/es6-export-star.js", "export * from './dummy'; console.log(this)"},
			{"/es6-export-star-as.js", "export * as ns from './dummy'; console.log(this)"},
			{"/es6-export-assign.ts", "export = 123; console.log(this)"},
			{"/es6-export-import-assign.ts", "export import x = require('./dummy'); console.log(this)"},
			{"/es6-ns-export-variable.ts", "namespace ns { export const foo = 123; } console.log(this)"},
			{"/es6-ns-export-function.ts", "namespace ns { export function foo() {} } console.log(this)"},
			{"/es6-ns-export-async-function.ts", "namespace ns { export async function foo() {} } console.log(this)"},
			{"/es6-ns-export-enum.ts", "namespace ns { export enum Foo {} } console.log(this)"},
			{"/es6-ns-export-const-enum.ts", "namespace ns { export const enum Foo {} } console.log(this)"},
			{"/es6-ns-export-module.ts", "namespace ns { export module Foo {} } console.log(this)"},
			{"/es6-ns-export-namespace.ts", "namespace ns { export namespace Foo {} } console.log(this)"},
			{"/es6-ns-export-class.ts", "namespace ns { export class Foo {} } console.log(this)"},
			{"/es6-ns-export-abstract-class.ts", "namespace ns { export abstract class Foo {} } console.log(this)"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
es6-export-abstract-class.ts: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-abstract-class.ts: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-async-function.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-async-function.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-class.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-class.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-clause-from.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-clause-from.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-clause.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-clause.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-const-enum.ts: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-const-enum.ts: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-default.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-default.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-enum.ts: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-enum.ts: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-function.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-function.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-import-assign.ts: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-import-assign.ts: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-module.ts: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-module.ts: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-namespace.ts: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-namespace.ts: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-star-as.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-star-as.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-star.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-star.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-export-variable.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-export-variable.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
es6-expr-import-meta.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-expr-import-meta.js: NOTE: This file is considered to be an ECMAScript module because of the use of "import.meta" here:
es6-import-meta.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
es6-import-meta.js: NOTE: This file is considered to be an ECMAScript module because of the use of "import.meta" here:
)scan",
		.debug_logs = true,
		
	});
}

TEST(BundlerDefault, TestArrowFnScope) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				tests = {
					0: ((x = y => x + y, y) => x + y),
					1: ((y, x = y => x + y) => x + y),
					2: ((x = (y = z => x + y + z, z) => x + y + z, y, z) => x + y + z),
					3: ((y, z, x = (z, y = z => x + y + z) => x + y + z) => x + y + z),
					4: ((x = y => x + y, y), x + y),
					5: ((y, x = y => x + y), x + y),
					6: ((x = (y = z => x + y + z, z) => x + y + z, y, z), x + y + z),
					7: ((y, z, x = (z, y = z => x + y + z) => x + y + z), x + y + z),
				};
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestSwitchScopeNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				switch (foo) { default: var foo }
				switch (bar) { default: let bar }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestArgumentDefaultValueScopeNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export function a(x = foo) { var foo; return x }
				export class b { fn(x = foo) { var foo; return x } }
				export let c = [
					function(x = foo) { var foo; return x },
					(x = foo) => { var foo; return x },
					{ fn(x = foo) { var foo; return x }},
					class { fn(x = foo) { var foo; return x }},
				]
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestArgumentsSpecialCaseNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				(() => {
					var arguments;

					function foo(x = arguments) { return arguments }
					(function(x = arguments) { return arguments });
					({foo(x = arguments) { return arguments }});
					class Foo { foo(x = arguments) { return arguments } }
					(class { foo(x = arguments) { return arguments } });

					function foo(x = arguments) { var arguments; return arguments }
					(function(x = arguments) { var arguments; return arguments });
					({foo(x = arguments) { var arguments; return arguments }});

					(x => arguments);
					(() => arguments);
					(async () => arguments);
					((x = arguments) => arguments);
					(async (x = arguments) => arguments);

					x => arguments;
					() => arguments;
					async () => arguments;
					(x = arguments) => arguments;
					async (x = arguments) => arguments;

					(x => { return arguments });
					(() => { return arguments });
					(async () => { return arguments });
					((x = arguments) => { return arguments });
					(async (x = arguments) => { return arguments });

					x => { return arguments };
					() => { return arguments };
					async () => { return arguments };
					(x = arguments) => { return arguments };
					async (x = arguments) => { return arguments };
				})()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestWithStatementTaintingNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				(() => {
					let local = 1
					let outer = 2
					let outerDead = 3
					with ({}) {
						var hoisted = 4
						let local = 5
						hoisted++
						local++
						if (1) outer++
						if (0) outerDead++
					}
					if (1) {
						hoisted++
						local++
						outer++
						outerDead++
					}
				})()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestDirectEvalTaintingNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function test1() {
					function add(first, second) {
						return first + second
					}
					eval('add(1, 2)')
				}

				function test2() {
					function add(first, second) {
						return first + second
					}
					(0, eval)('add(1, 2)')
				}

				function test3() {
					function add(first, second) {
						return first + second
					}
				}

				function test4(eval) {
					function add(first, second) {
						return first + second
					}
					eval('add(1, 2)')
				}

				function test5() {
					function containsDirectEval() { eval() }
					if (true) { var shouldNotBeRenamed }
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestImportReExportES6Issue149) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/app.jsx", R"test(

				import { p as Part, h, render } from './import';
				import { Internal } from './in2';
				const App = () => <Part> <Internal /> T </Part>;
				render(<App />, document.getElementById('app'));
			)test"},
			{"/in2.jsx", R"test(

				import { p as Part, h } from './import';
				export const Internal = () => <Part> Test 2 </Part>;
			)test"},
			{"/import.js", R"test(

				import { h, render } from 'preact';
				export const p = "p";
				export { h, render }
			)test"},
		},
		.entry_paths = {"/app.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"preact", true},
					},
				},
			},
			.JSX = guchho::config::JSXOptions{
				.Factory = guchho::config::DefineExpr{.Parts = {"h"}},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestExternalModuleExclusionPackage) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.js", R"test(

				import { S3 } from 'aws-sdk';
				import { DocumentClient } from 'aws-sdk/clients/dynamodb';
				export const s3 = new S3();
				export const dynamodb = new DocumentClient();
			)test"},
		},
		.entry_paths = {"/index.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"aws-sdk", true},
					},
					.Patterns = {
						guchho::config::WildcardPattern{.Prefix = "aws-sdk/"},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestExternalModuleExclusionScopedPackage) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.js", R"test(

				import '@a1'
				import '@a1/a2'
				import '@a1-a2'

				import '@b1'
				import '@b1/b2'
				import '@b1/b2/b3'
				import '@b1/b2-b3'

				import '@c1'
				import '@c1/c2'
				import '@c1/c2/c3'
				import '@c1/c2/c3/c4'
				import '@c1/c2/c3-c4'
			)test"},
		},
		.entry_paths = {"/index.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"@a1", true},
						{"@b1/b2", true},
						{"@c1/c2/c3", true},
					},
					.Patterns = {
						guchho::config::WildcardPattern{.Prefix = "@a1/"},
						guchho::config::WildcardPattern{.Prefix = "@b1/b2/"},
						guchho::config::WildcardPattern{.Prefix = "@c1/c2/c3/"},
					},
				},
			},
			
		},
		.expected_scan_log = R"scan(
index.js: ERROR: Could not resolve "@a1-a2"
NOTE: You can mark the path "@a1-a2" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
index.js: ERROR: Could not resolve "@b1"
NOTE: You can mark the path "@b1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
index.js: ERROR: Could not resolve "@b1/b2-b3"
NOTE: You can mark the path "@b1/b2-b3" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
index.js: ERROR: Could not resolve "@c1"
NOTE: You can mark the path "@c1" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
index.js: ERROR: Could not resolve "@c1/c2"
NOTE: You can mark the path "@c1/c2" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
index.js: ERROR: Could not resolve "@c1/c2/c3-c4"
NOTE: You can mark the path "@c1/c2/c3-c4" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",
		
	});
}

TEST(BundlerDefault, TestScopedExternalModuleExclusion) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/index.js", R"test(

				import { Foo } from '@scope/foo';
				import { Bar } from '@scope/foo/bar';
				export const foo = new Foo();
				export const bar = new Bar();
			)test"},
		},
		.entry_paths = {"/index.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"@scope/foo", true},
					},
					.Patterns = {
						guchho::config::WildcardPattern{.Prefix = "@scope/foo/"},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestExternalModuleExclusionRelativePath) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/index.js", R"test(

				import './nested/folder/test'
			)test"},
			{"/Users/user/project/src/nested/folder/test.js", R"test(

				import foo from './foo.js'
				import out from '../../../out/in-out-dir.js'
				import sha256 from '../../sha256.min.js'
				import config from '/api/config?a=1&b=2'
				console.log(foo, out, sha256, config)
			)test"},
		},
		.entry_paths = {"/Users/user/project/src/index.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/Users/user/project/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"/api/config?a=1&b=2", true},
					},
				},
				.PostResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"/Users/user/project/out/in-out-dir.js", true},
						{"/Users/user/project/src/nested/folder/foo.js", true},
						{"/Users/user/project/src/sha256.min.js", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestImportWithHashInPath) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import foo from './file#foo.txt'
				import bar from './file#bar.txt'
				console.log(foo, bar)
			)test"},
			{"/file#foo.txt", "foo"},
			{"/file#bar.txt", "bar"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportWithHashParameter) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// Each of these should have a separate identity (i.e. end up in the output file twice)
				import foo from './file.txt#foo'
				import bar from './file.txt#bar'
				console.log(foo, bar)
			)test"},
			{"/file.txt", "This is some text"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportWithQueryParameter) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// Each of these should have a separate identity (i.e. end up in the output file twice)
				import foo from './file.txt?foo'
				import bar from './file.txt?bar'
				console.log(foo, bar)
			)test"},
			{"/file.txt", "This is some text"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportAbsPathWithQueryParameter) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/entry.js", R"test(

				// Each of these should have a separate identity (i.e. end up in the output file twice)
				import foo from '/Users/user/project/file.txt?foo'
				import bar from '/Users/user/project/file.txt#bar'
				console.log(foo, bar)
			)test"},
			{"/Users/user/project/file.txt", "This is some text"},
		},
		.entry_paths = {"/Users/user/project/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportAbsPathAsFile) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/entry.js", R"test(

				import pkg from '/Users/user/project/node_modules/pkg/index'
				console.log(pkg)
			)test"},
			{"/Users/user/project/node_modules/pkg/index.js", R"test(

				export default 123
			)test"},
		},
		.entry_paths = {"/Users/user/project/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportAbsPathAsDir) {
	default_suite.ExpectBundledUnix(Bundled{
		.files = {
			{"/Users/user/project/entry.js", R"test(

				import pkg from '/Users/user/project/node_modules/pkg'
				console.log(pkg)
			)test"},
			{"/Users/user/project/node_modules/pkg/index.js", R"test(

				export default 123
			)test"},
		},
		.entry_paths = {"/Users/user/project/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});

	default_suite.ExpectBundledWindows(Bundled{
		.files = {
			{"C:\\Users\\user\\project\\entry.js", R"test(

				import pkg from 'C:\\Users\\user\\project\\node_modules\\pkg'
				console.log(pkg)
			)test"},
			{"C:\\Users\\user\\project\\node_modules\\pkg\\index.js", R"test(

				export default 123
			)test"},
		},
		.entry_paths = {"C:\\Users\\user\\project\\entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "C:\\out",
			
		},
		
	});
}

TEST(BundlerDefault, TestAutoExternal) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// These URLs should be external automatically
				import "http://example.com/code.js";
				import "https://example.com/code.js";
				import "//example.com/code.js";
				import "data:application/javascript;base64,ZXhwb3J0IGRlZmF1bHQgMTIz";
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestAutoExternalNode) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// These URLs should be external automatically
				import fs from "node:fs/promises";
				fs.readFile();

				// This should be external and should be tree-shaken because it's side-effect free
				import "node:path";

				// This should be external too, but shouldn't be tree-shaken because it could be a run-time error
				import "node:what-is-this";
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestExternalWithWildcard) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// Should match
				import "/assets/images/test.jpg";
				import "/dir/x/file.gif";
				import "/dir//file.gif";
				import "./file.png";

				// Should not match
				import "/sassets/images/test.jpg";
				import "/dir/file.gif";
				import "./file.ping";
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Patterns = {
						guchho::config::WildcardPattern{.Prefix = "/assets/"},
						guchho::config::WildcardPattern{.Suffix = ".png"},
						guchho::config::WildcardPattern{.Prefix = "/dir/", .Suffix = "/file.gif"},
					},
				},
			},
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Could not resolve "/sassets/images/test.jpg"
entry.js: ERROR: Could not resolve "/dir/file.gif"
entry.js: ERROR: Could not resolve "./file.ping"
)scan",
		
	});
}

TEST(BundlerDefault, TestExternalWildcardDoesNotMatchEntryPoint) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import "foo"
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Patterns = {
						guchho::config::WildcardPattern{},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestManyEntryPoints) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/shared.js", "export default 123"},
			{"/e00.js", "import x from './shared'; console.log(x)"},
			{"/e01.js", "import x from './shared'; console.log(x)"},
			{"/e02.js", "import x from './shared'; console.log(x)"},
			{"/e03.js", "import x from './shared'; console.log(x)"},
			{"/e04.js", "import x from './shared'; console.log(x)"},
			{"/e05.js", "import x from './shared'; console.log(x)"},
			{"/e06.js", "import x from './shared'; console.log(x)"},
			{"/e07.js", "import x from './shared'; console.log(x)"},
			{"/e08.js", "import x from './shared'; console.log(x)"},
			{"/e09.js", "import x from './shared'; console.log(x)"},
			{"/e10.js", "import x from './shared'; console.log(x)"},
			{"/e11.js", "import x from './shared'; console.log(x)"},
			{"/e12.js", "import x from './shared'; console.log(x)"},
			{"/e13.js", "import x from './shared'; console.log(x)"},
			{"/e14.js", "import x from './shared'; console.log(x)"},
			{"/e15.js", "import x from './shared'; console.log(x)"},
			{"/e16.js", "import x from './shared'; console.log(x)"},
			{"/e17.js", "import x from './shared'; console.log(x)"},
			{"/e18.js", "import x from './shared'; console.log(x)"},
			{"/e19.js", "import x from './shared'; console.log(x)"},
			{"/e20.js", "import x from './shared'; console.log(x)"},
			{"/e21.js", "import x from './shared'; console.log(x)"},
			{"/e22.js", "import x from './shared'; console.log(x)"},
			{"/e23.js", "import x from './shared'; console.log(x)"},
			{"/e24.js", "import x from './shared'; console.log(x)"},
			{"/e25.js", "import x from './shared'; console.log(x)"},
			{"/e26.js", "import x from './shared'; console.log(x)"},
			{"/e27.js", "import x from './shared'; console.log(x)"},
			{"/e28.js", "import x from './shared'; console.log(x)"},
			{"/e29.js", "import x from './shared'; console.log(x)"},
			{"/e30.js", "import x from './shared'; console.log(x)"},
			{"/e31.js", "import x from './shared'; console.log(x)"},
			{"/e32.js", "import x from './shared'; console.log(x)"},
			{"/e33.js", "import x from './shared'; console.log(x)"},
			{"/e34.js", "import x from './shared'; console.log(x)"},
			{"/e35.js", "import x from './shared'; console.log(x)"},
			{"/e36.js", "import x from './shared'; console.log(x)"},
			{"/e37.js", "import x from './shared'; console.log(x)"},
			{"/e38.js", "import x from './shared'; console.log(x)"},
			{"/e39.js", "import x from './shared'; console.log(x)"},
		},
		.entry_paths = {
			"/e00.js",
			"/e01.js",
			"/e02.js",
			"/e03.js",
			"/e04.js",
			"/e05.js",
			"/e06.js",
			"/e07.js",
			"/e08.js",
			"/e09.js",
			"/e10.js",
			"/e11.js",
			"/e12.js",
			"/e13.js",
			"/e14.js",
			"/e15.js",
			"/e16.js",
			"/e17.js",
			"/e18.js",
			"/e19.js",
			"/e20.js",
			"/e21.js",
			"/e22.js",
			"/e23.js",
			"/e24.js",
			"/e25.js",
			"/e26.js",
			"/e27.js",
			"/e28.js",
			"/e29.js",
			"/e30.js",
			"/e31.js",
			"/e32.js",
			"/e33.js",
			"/e34.js",
			"/e35.js",
			"/e36.js",
			"/e37.js",
			"/e38.js",
			"/e39.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestRenameNestedVar) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import { foo } from './foo'
				var topLevel = 0
				{ var nested = 1 }
				function fn() { var inner = 2 }
				foo(topLevel, nested, fn)
			)test"},
			{"/foo.js", R"test(

				export function foo(a, b) {}
				var topLevel = 0
				{ var nested = 1 }
				function fn() { var inner = 2 }
				foo(topLevel, nested, fn)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRenamePrivateIdentifiersNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class Foo {
					#foo
					foo = class {
						#foo
						#foo2
						#bar
					}
					get #bar() {}
					set #bar(x) {}
				}
				class Bar {
					#foo
					foo = class {
						#foo2
						#foo
						#bar
					}
					get #bar() {}
					set #bar(x) {}
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestMinifyPrivateIdentifiersNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class Foo {
					#foo
					foo = class {
						#foo
						#foo2
						#bar
					}
					get #bar() {}
					set #bar(x) {}
				}
				class Bar {
					#foo
					foo = class {
						#foo2
						#foo
						#bar
					}
					get #bar() {}
					set #bar(x) {}
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestRenameLabelsNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				foo: {
					bar: {
						if (x) break bar
						break foo
					}
				}
				foo2: {
					bar2: {
						if (x) break bar2
						break foo2
					}
				}
				foo: {
					bar: {
						if (x) break bar
						break foo
					}
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestMinifySiblingLabelsNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				foo: {
					bar: {
						if (x) break bar
						break foo
					}
				}
				foo2: {
					bar2: {
						if (x) break bar2
						break foo2
					}
				}
				foo: {
					bar: {
						if (x) break bar
						break foo
					}
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

										TEST(BundlerDefault, TestMinifyNestedLabelsNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				L001:L002:L003:L004:L005:L006:L007:L008:L009:L010:L011:L012:L013:L014:L015:L016:{nl(`
`)
				L017:L018:L019:L020:L021:L022:L023:L024:L025:L026:L027:L028:L029:L030:L031:L032:{nl(`
`)
				L033:L034:L035:L036:L037:L038:L039:L040:L041:L042:L043:L044:L045:L046:L047:L048:{nl(`
`)
				L049:L050:L051:L052:L053:L054:L055:L056:L057:L058:L059:L060:L061:L062:L063:L064:{nl(`
`)
				L065:L066:L067:L068:L069:L070:L071:L072:L073:L074:L075:L076:L077:L078:L079:L080:{nl(`
`)
				L081:L082:L083:L084:L085:L086:L087:L088:L089:L090:L091:L092:L093:L094:L095:L096:{nl(`
`)
				L097:L098:L099:L100:L101:L102:L103:L104:L105:L106:L107:L108:L109:L110:L111:L112:{nl(`
`)
				L113:L114:L115:L116:L117:L118:L119:L120:L121:L122:L123:L124:L125:L126:L127:L128:{nl(`
`)
				L129:L130:L131:L132:L133:L134:L135:L136:L137:L138:L139:L140:L141:L142:L143:L144:{nl(`
`)
				L145:L146:L147:L148:L149:L150:L151:L152:L153:L154:L155:L156:L157:L158:L159:L160:{nl(`
`)
				L161:L162:L163:L164:L165:L166:L167:L168:L169:L170:L171:L172:L173:L174:L175:L176:{nl(`
`)
				L177:L178:L179:L180:L181:L182:L183:L184:L185:L186:L187:L188:L189:L190:L191:L192:{nl(`
`)
				L193:L194:L195:L196:L197:L198:L199:L200:L201:L202:L203:L204:L205:L206:L207:L208:{nl(`
`)
				L209:L210:L211:L212:L213:L214:L215:L216:L217:L218:L219:L220:L221:L222:L223:L224:{nl(`
`)
				L225:L226:L227:L228:L229:L230:L231:L232:L233:L234:L235:L236:L237:L238:L239:L240:{nl(`
`)
				L241:L242:L243:L244:L245:L246:L247:L248:L249:L250:L251:L252:L253:L254:L255:L256:{nl(`
`)
				L257:L258:L259:L260:L261:L262:L263:L264:L265:L266:L267:L268:L269:L270:L271:L272:{nl(`
`)
				L273:L274:L275:L276:L277:L278:L279:L280:L281:L282:L283:L284:L285:L286:L287:L288:{nl(`
`)
				L289:L290:L291:L292:L293:L294:L295:L296:L297:L298:L299:L300:L301:L302:L303:L304:{nl(`
`)
				L305:L306:L307:L308:L309:L310:L311:L312:L313:L314:L315:L316:L317:L318:L319:L320:{nl(`
`)
L321:L322:L323:L324:L325:L326:L327:L328:L329:L330:L331:L332:L333:{}}}}}}}}}}}}}}}}}}nl(`
`)}}}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifyWhitespace = true,
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestExportsAndModuleFormatCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import * as foo from './foo/test'
				import * as bar from './bar/test'
				console.log(exports, module.exports, foo, bar)
			)test"},
			{"/foo/test.js", R"test(

				export let foo = 123
			)test"},
			{"/bar/test.js", R"test(

				export let bar = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
	}
TEST(BundlerDefault, TestMinifiedExportsAndModuleFormatCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import * as foo from './foo/test'
				import * as bar from './bar/test'
				console.log(exports, module.exports, foo, bar)
			)test"},
			{"/foo/test.js", R"test(

				export let foo = 123
			)test"},
			{"/bar/test.js", R"test(

				export let bar = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestEmptyExportClauseBundleAsCommonJSIssue910) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(require('./types.mjs'))
			)test"},
			{"/types.mjs", R"test(

				export {}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestUseStrictDirectiveMinifyNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				'use strict'
				'use loose'
				a
				b
			)test"},
		},
.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.MinifyWhitespace = true,
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestUseStrictDirectiveBundleIssue1837) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(require('./cjs'))
			)test"},
			{"/cjs.js", R"test(

				'use strict'
				exports.foo = process
			)test"},
			{"/shims.js", R"test(

				import process from 'process'
				export { process }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			.InjectPaths = {"/shims.js"},
			
		},
		
	});
}

TEST(BundlerDefault, TestUseStrictDirectiveBundleIIFEIssue2264) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				'use strict'
				export let a = 1
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestUseStrictDirectiveBundleCJSIssue2264) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				'use strict'
				export let a = 1
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestUseStrictDirectiveBundleESMIssue2264) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				'use strict'
				export let a = 1
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestNoOverwriteInputFileError) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(123)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/",
			
		},
		.expected_compile_log = R"compile(
ERROR: Refusing to overwrite input file "entry.js" (use "--allow-overwrite" to allow this)
)compile",
		
	});
}

TEST(BundlerDefault, TestDuplicateEntryPoint) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(123)
			)test"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRelativeEntryPointError) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(123)
			)test"},
		},
		.entry_paths = {"entry"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out.js",
			
		},
		.expected_scan_log = R"scan(
ERROR: Could not resolve "entry"
NOTE: Use the relative path "./entry" to reference the file "entry.js". Without the leading "./", the path "entry" is being interpreted as a package path instead.
)scan",
		
	});
}

TEST(BundlerDefault, TestMultipleEntryPointsSameNameCollision) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a/entry.js", "import {foo} from '../common.js'; console.log(foo)"},
			{"/b/entry.js", "import {foo} from '../common.js'; console.log(foo)"},
			{"/common.js", "export let foo = 123"},
		},
		.entry_paths = {
			"/a/entry.js",
			"/b/entry.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out/",
			
		},
		
	});
}

TEST(BundlerDefault, TestReExportCommonJSAsES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {bar} from './foo'
			)test"},
			{"/foo.js", R"test(

				exports.bar = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestReExportDefaultInternal) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {default as foo} from './foo'
				export {default as bar} from './bar'
			)test"},
			{"/foo.js", R"test(

				export default 'foo'
			)test"},
			{"/bar.js", R"test(

				export default 'bar'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestReExportDefaultExternalES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {default as foo} from 'foo'
				export {bar} from './bar'
			)test"},
			{"/bar.js", R"test(

				export {default as bar} from 'bar'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"foo", true},
						{"bar", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestReExportDefaultExternalCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {default as foo} from 'foo'
				export {bar} from './bar'
			)test"},
			{"/bar.js", R"test(

				export {default as bar} from 'bar'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"foo", true},
						{"bar", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestReExportDefaultNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {default as foo} from './foo'
				export {default as bar} from './bar'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestReExportDefaultNoBundleES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {default as foo} from './foo'
				export {default as bar} from './bar'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestReExportDefaultNoBundleCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export {default as foo} from './foo'
				export {default as bar} from './bar'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportMetaCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(import.meta.url, import.meta.path)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: WARNING: "import.meta" is not available with the "cjs" output format and will be empty
NOTE: You need to set the output format to "esm" for "import.meta" to work correctly.
entry.js: WARNING: "import.meta" is not available with the "cjs" output format and will be empty
NOTE: You need to set the output format to "esm" for "import.meta" to work correctly.
)scan",
		
	});
}

TEST(BundlerDefault, TestImportMetaES6) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(import.meta.url, import.meta.path)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportMetaNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(import.meta.url, import.meta.path)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsNone) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/empty.js", ""},
			{"/entry.js", R"test(

				import './a'
				import './b'
				import './c'
			)test"},
			{"/a.js", "console.log('in a') //! Copyright notice 1"},
			{"/b.js", "console.log('in b') //! Copyright notice 1"},
			{"/c.js", "console.log('in c') //! Copyright notice 2"},
			{"/empty.css", ""},
			{"/entry.css", R"test(

				@import "./a.css";
				@import "./b.css";
				@import "./c.css";
			)test"},
			{"/a.css", "a { zoom: 2 } /*! Copyright notice 1 */"},
			{"/b.css", "b { zoom: 2 } /*! Copyright notice 1 */"},
			{"/c.css", "c { zoom: 2 } /*! Copyright notice 2 */"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry.css",
			"/empty.js",
			"/empty.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.LegalCommentsData = guchho::config::LegalComments::kNone,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsInline) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/empty.js", ""},
			{"/entry.js", R"test(

				import './a'
				import './b'
				import './c'
			)test"},
			{"/a.js", "console.log('in a') //! Copyright notice 1"},
			{"/b.js", "console.log('in b') //! Copyright notice 1"},
			{"/c.js", "console.log('in c') //! Copyright notice 2"},
			{"/empty.css", ""},
			{"/entry.css", R"test(

				@import "./a.css";
				@import "./b.css";
				@import "./c.css";
			)test"},
			{"/a.css", "a { zoom: 2 } /*! Copyright notice 1 */"},
			{"/b.css", "b { zoom: 2 } /*! Copyright notice 1 */"},
			{"/c.css", "c { zoom: 2 } /*! Copyright notice 2 */"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry.css",
			"/empty.js",
			"/empty.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsEndOfFile) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/empty.js", ""},
			{"/entry.js", R"test(

				import './a'
				import './b'
				import './c'
			)test"},
			{"/a.js", "console.log('in a') //! Copyright notice 1"},
			{"/b.js", "console.log('in b') //! Copyright notice 1"},
			{"/c.js", "console.log('in c') //! Copyright notice 2"},
			{"/empty.css", ""},
			{"/entry.css", R"test(

				@import "./a.css";
				@import "./b.css";
				@import "./c.css";
			)test"},
			{"/a.css", "a { zoom: 2 } /*! Copyright notice 1 */"},
			{"/b.css", "b { zoom: 2 } /*! Copyright notice 1 */"},
			{"/c.css", "c { zoom: 2 } /*! Copyright notice 2 */"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry.css",
			"/empty.js",
			"/empty.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.LegalCommentsData = guchho::config::LegalComments::kEndOfFile,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsLinked) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/empty.js", ""},
			{"/entry.js", R"test(

				import './a'
				import './b'
				import './c'
			)test"},
			{"/a.js", "console.log('in a') //! Copyright notice 1"},
			{"/b.js", "console.log('in b') //! Copyright notice 1"},
			{"/c.js", "console.log('in c') //! Copyright notice 2"},
			{"/empty.css", ""},
			{"/entry.css", R"test(

				@import "./a.css";
				@import "./b.css";
				@import "./c.css";
			)test"},
			{"/a.css", "a { zoom: 2 } /*! Copyright notice 1 */"},
			{"/b.css", "b { zoom: 2 } /*! Copyright notice 1 */"},
			{"/c.css", "c { zoom: 2 } /*! Copyright notice 2 */"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry.css",
			"/empty.js",
			"/empty.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.LegalCommentsData = guchho::config::LegalComments::kLinkedWithComment,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsExternal) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/empty.js", ""},
			{"/entry.js", R"test(

				import './a'
				import './b'
				import './c'
			)test"},
			{"/a.js", "console.log('in a') //! Copyright notice 1"},
			{"/b.js", "console.log('in b') //! Copyright notice 1"},
			{"/c.js", "console.log('in c') //! Copyright notice 2"},
			{"/empty.css", ""},
			{"/entry.css", R"test(

				@import "./a.css";
				@import "./b.css";
				@import "./c.css";
			)test"},
			{"/a.css", "a { zoom: 2 } /*! Copyright notice 1 */"},
			{"/b.css", "b { zoom: 2 } /*! Copyright notice 1 */"},
			{"/c.css", "c { zoom: 2 } /*! Copyright notice 2 */"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry.css",
			"/empty.js",
			"/empty.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.LegalCommentsData = guchho::config::LegalComments::kExternalWithoutComment,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsModifyIndent) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export default () => {
					/**
					 * @preserve
					 */
				}
			)test"},
			{"/entry.css", R"test(

				@media (x: y) {
					/**
					 * @preserve
					 */
					z { zoom: 2 }
				}
			)test"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsAvoidSlashTagInline) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				//! <script>foo</script>
				export let x
			)test"},
			{"/entry.css", R"test(

				/*! <style>foo</style> */
				x { y: z }
			)test"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsAvoidSlashTagEndOfFile) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				//! <script>foo</script>
				export let x
			)test"},
			{"/entry.css", R"test(

				/*! <style>foo</style> */
				x { y: z }
			)test"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.LegalCommentsData = guchho::config::LegalComments::kEndOfFile,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsAvoidSlashTagExternal) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				//! <script>foo</script>
				export let x
			)test"},
			{"/entry.css", R"test(

				/*! <style>foo</style> */
				x { y: z }
			)test"},
		},
		.entry_paths = {
			"/entry.js",
			"/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.LegalCommentsData = guchho::config::LegalComments::kExternalWithoutComment,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsManyEndOfFile) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(

				import './a'
				import './b'
				import './c'
				import 'some-pkg/js'
			)test"},
			{"/project/a.js", R"test(

				console.log('in a') //! Copyright notice 1
				//! Duplicate comment
				//! Duplicate comment
			)test"},
			{"/project/b.js", R"test(

				console.log('in b') //! Copyright notice 1
				//! Duplicate comment
				//! Duplicate comment
			)test"},
			{"/project/c.js", R"test(

				function foo() {
					/*
					 * @license
					 * Copyright notice 2
					 */
					console.log('in c')
					// @preserve This is another comment
				}
				foo()
			)test"},
			{"/project/node_modules/some-pkg/js/index.js", R"test(

				import "some-other-pkg/js" //! (c) Good Software Corp
				//! Duplicate third-party comment
				//! Duplicate third-party comment
			)test"},
			{"/project/node_modules/some-other-pkg/js/index.js", R"test(

				function bar() {
					/*
					 * @preserve
					 * (c) Evil Software Corp
					 */
					console.log('some-other-pkg')
				}
				//! Duplicate third-party comment
				//! Duplicate third-party comment
				bar()
			)test"},
			{"/project/entry.css", R"test(

				@import "./a.css";
				@import "./b.css";
				@import "./c.css";
				@import 'some-pkg/css';
			)test"},
			{"/project/a.css", R"test(

				a { zoom: 2 } /*! Copyright notice 1 */
				/*! Duplicate comment */
				/*! Duplicate comment */
			)test"},
			{"/project/b.css", R"test(

				b { zoom: 2 } /*! Copyright notice 1 */
				/*! Duplicate comment */
				/*! Duplicate comment */
			)test"},
			{"/project/c.css", R"test(

				/*
				 * @license
				 * Copyright notice 2
				 */
				c {
					zoom: 2
				}
				/* @preserve This is another comment */
			)test"},
			{"/project/node_modules/some-pkg/css/index.css", R"test(

				@import "some-other-pkg/css"; /*! (c) Good Software Corp */
				/*! Duplicate third-party comment */
				/*! Duplicate third-party comment */
			)test"},
			{"/project/node_modules/some-other-pkg/css/index.css", R"test(

				/*! Duplicate third-party comment */
				/*! Duplicate third-party comment */
				.some-other-pkg {
					zoom: 2
				}
				/** @preserve
				 * (c) Evil Software Corp
				 */
			)test"},
		},
		.entry_paths = {
			"/project/entry.js",
			"/project/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyWhitespace = true,
			.LegalCommentsData = guchho::config::LegalComments::kEndOfFile,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsEscapeSlashScriptAndStyleEndOfFile) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(import "js-pkg"; a /*! </script> */)test"},
			{"/project/node_modules/js-pkg/index.js", "x /*! </script> */"},
			{"/project/entry.css", R"test(@import "css-pkg"; a { b: c } /*! </style> */)test"},
			{"/project/node_modules/css-pkg/index.css", "x { y: z } /*! </style> */"},
		},
		.entry_paths = {
			"/project/entry.js",
			"/project/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyWhitespace = true,
			.LegalCommentsData = guchho::config::LegalComments::kEndOfFile,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsEscapeSlashScriptAndStyleExternal) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(import "js-pkg"; a /*! </script> */)test"},
			{"/project/node_modules/js-pkg/index.js", "x /*! </script> */"},
			{"/project/entry.css", R"test(@import "css-pkg"; a { b: c } /*! </style> */)test"},
			{"/project/node_modules/css-pkg/index.css", "x { y: z } /*! </style> */"},
		},
		.entry_paths = {
			"/project/entry.js",
			"/project/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyWhitespace = true,
			.LegalCommentsData = guchho::config::LegalComments::kExternalWithoutComment,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsNoEscapeSlashScriptEndOfFile) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(import "js-pkg"; a /*! </script> */)test"},
			{"/project/node_modules/js-pkg/index.js", "x /*! </script> */"},
			{"/project/entry.css", R"test(@import "css-pkg"; a { b: c } /*! </style> */)test"},
			{"/project/node_modules/css-pkg/index.css", "x { y: z } /*! </style> */"},
		},
		.entry_paths = {
			"/project/entry.js",
			"/project/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyWhitespace = true,
			.LegalCommentsData = guchho::config::LegalComments::kEndOfFile,
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kInlineScript,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsNoEscapeSlashStyleEndOfFile) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(import "js-pkg"; a /*! </script> */)test"},
			{"/project/node_modules/js-pkg/index.js", "x /*! </script> */"},
			{"/project/entry.css", R"test(@import "css-pkg"; a { b: c } /*! </style> */)test"},
			{"/project/node_modules/css-pkg/index.css", "x { y: z } /*! </style> */"},
		},
		.entry_paths = {
			"/project/entry.js",
			"/project/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyWhitespace = true,
			.LegalCommentsData = guchho::config::LegalComments::kEndOfFile,
			.UnsupportedCSSFeatures = guchho::compat::CSSFeature::kInlineStyle,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsManyLinked) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(

				import './a'
				import './b'
				import './c'
				import 'some-pkg/js'
			)test"},
			{"/project/a.js", "console.log('in a') //! Copyright notice 1"},
			{"/project/b.js", "console.log('in b') //! Copyright notice 1"},
			{"/project/c.js", R"test(

				function foo() {
					/*
					 * @license
					 * Copyright notice 2
					 */
					console.log('in c')
					// @preserve This is another comment
				}
				foo()
			)test"},
			{"/project/node_modules/some-pkg/js/index.js", R"test(import "some-other-pkg/js" //! (c) Good Software Corp)test"},
			{"/project/node_modules/some-other-pkg/js/index.js", R"test(

				function bar() {
					/*
					 * @preserve
					 * (c) Evil Software Corp
					 */
					console.log('some-other-pkg')
				}
				bar()
			)test"},
			{"/project/entry.css", R"test(

				@import "./a.css";
				@import "./b.css";
				@import "./c.css";
				@import 'some-pkg/css';
			)test"},
			{"/project/a.css", "a { zoom: 2 } /*! Copyright notice 1 */"},
			{"/project/b.css", "b { zoom: 2 } /*! Copyright notice 1 */"},
			{"/project/c.css", R"test(

				/*
				 * @license
				 * Copyright notice 2
				 */
				c {
					zoom: 2
				}
				/* @preserve This is another comment */
			)test"},
			{"/project/node_modules/some-pkg/css/index.css", R"test(@import "some-other-pkg/css"; /*! (c) Good Software Corp */)test"},
			{"/project/node_modules/some-other-pkg/css/index.css", R"test(

				.some-other-pkg {
					zoom: 2
				}
				/** @preserve
				 * (c) Evil Software Corp
				 */
			)test"},
		},
		.entry_paths = {
			"/project/entry.js",
			"/project/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyWhitespace = true,
			.LegalCommentsData = guchho::config::LegalComments::kLinkedWithComment,
			
		},
		
	});
}

TEST(BundlerDefault, TestLegalCommentsMergeDuplicatesIssue4139) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(

				import 'pkg/a'
				import 'pkg/b'
				import 'pkg/c'
				import 'pkg/d'
			)test"},
			{"/project/node_modules/pkg/a.js", R"test(

/*!-----------------------------------------------------------------------------
 * Copyright (c) Example Corporation. All rights reserved.
 * Version: 1.2.3
 * Released under the MIT license
 * https://example.com/LICENSE.txt
 *-----------------------------------------------------------------------------*/
			)test"},
			{"/project/node_modules/pkg/b.js", R"test(

/*!-----------------------------------------------------------------------------
 * Copyright (c) Example Corporation. All rights reserved.
 * Version: 1.2.3
 * Released under the MIT license
 * https://example.com/LICENSE.txt
 *-----------------------------------------------------------------------------*/
			)test"},
			{"/project/node_modules/pkg/c.js", R"test(

//! some other comment
/*!-----------------------------------------------------------------------------
 * Copyright (c) Example Corporation. All rights reserved.
 * Version: 1.2.3
 * Released under the MIT license
 * https://example.com/LICENSE.txt
 *-----------------------------------------------------------------------------*/
			)test"},
			{"/project/node_modules/pkg/d.js", R"test(

/*!-----------------------------------------------------------------------------
 * Copyright (c) Example Corporation. All rights reserved.
 * Version: 1.2.3
 * Released under the MIT license
 * https://example.com/LICENSE.txt
 *-----------------------------------------------------------------------------*/
			)test"},
		},
		.entry_paths = {"/project/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.LegalCommentsData = guchho::config::LegalComments::kEndOfFile,
			
		},
		
	});
}

TEST(BundlerDefault, TestIIFE_ES5) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log('test');
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures(
				{{
					guchho::compat::Engine::kES,
					guchho::compat::Semver{{5}},
				}}),
			
		},
		
	});
}

TEST(BundlerDefault, TestOutputExtensionRemappingFile) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log('test');
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestOutputExtensionRemappingDir) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log('test');
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.OutputExtensionJS = ".notjs",
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitIIFE) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				await foo;
				for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Top-level await is currently not supported with the "iife" output format
entry.js: ERROR: Top-level await is currently not supported with the "iife" output format
)scan",
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitIIFEDeadBranch) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				if (false) await foo;
				if (false) for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitCJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				await foo;
				for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Top-level await is currently not supported with the "cjs" output format
entry.js: ERROR: Top-level await is currently not supported with the "cjs" output format
)scan",
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitCJSDeadBranch) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				if (false) await foo;
				if (false) for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitESM) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				await foo;
				for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitESMDeadBranch) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				if (false) await foo;
				if (false) for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				await foo;
				for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitNoBundleDeadBranch) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				if (false) await foo;
				if (false) for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitNoBundleESM) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				await foo;
				for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitNoBundleESMDeadBranch) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				if (false) await foo;
				if (false) for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitNoBundleCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				await foo;
				for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Top-level await is currently not supported with the "cjs" output format
entry.js: ERROR: Top-level await is currently not supported with the "cjs" output format
)scan",
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitNoBundleCommonJSDeadBranch) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				if (false) await foo;
				if (false) for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitNoBundleIIFE) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				await foo;
				for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Top-level await is currently not supported with the "iife" output format
entry.js: ERROR: Top-level await is currently not supported with the "iife" output format
)scan",
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitNoBundleIIFEDeadBranch) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				if (false) await foo;
				if (false) for await (foo of bar) ;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitForbiddenRequire) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				require('./a')
				require('./b')
				require('./c')
				require('./entry')
				await 0
			)test"},
			{"/a.js", R"test(

				import './something' // Deliberately offset the import record index
				import './b'
			)test"},
			{"/b.js", R"test(

				import './c'
			)test"},
			{"/c.js", R"test(

				await 0
			)test"},
			{"/something.js", ""},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: This require call is not allowed because the transitive dependency "c.js" contains a top-level await
a.js: NOTE: The file "a.js" imports the file "b.js" here:
b.js: NOTE: The file "b.js" imports the file "c.js" here:
c.js: NOTE: The top-level await in "c.js" is here:
entry.js: ERROR: This require call is not allowed because the transitive dependency "c.js" contains a top-level await
b.js: NOTE: The file "b.js" imports the file "c.js" here:
c.js: NOTE: The top-level await in "c.js" is here:
entry.js: ERROR: This require call is not allowed because the imported file "c.js" contains a top-level await
c.js: NOTE: The top-level await in "c.js" is here:
entry.js: ERROR: This require call is not allowed because the imported file "entry.js" contains a top-level await
entry.js: NOTE: The top-level await in "entry.js" is here:
)scan",
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitForbiddenRequireDeadBranch) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				require('./a')
				require('./b')
				require('./c')
				require('./entry')
				if (false) for await (let x of y) await 0
			)test"},
			{"/a.js", R"test(

				import './b'
			)test"},
			{"/b.js", R"test(

				import './c'
			)test"},
			{"/c.js", R"test(

				if (false) for await (let x of y) await 0
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitAllowedImportWithoutSplitting) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import('./a')
				import('./b')
				import('./c')
				import('./entry')
				await 0
			)test"},
			{"/a.js", R"test(

				import './b'
			)test"},
			{"/b.js", R"test(

				import './c'
			)test"},
			{"/c.js", R"test(

				await 0
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestTopLevelAwaitAllowedImportWithSplitting) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import('./a')
				import('./b')
				import('./c')
				import('./entry')
				await 0
			)test"},
			{"/a.js", R"test(

				import './b'
			)test"},
			{"/b.js", R"test(

				import './c'
			)test"},
			{"/c.js", R"test(

				await 0
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestAssignToImport) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import "./bad0.js"
				import "./bad1.js"
				import "./bad2.js"
				import "./bad3.js"
				import "./bad4.js"
				import "./bad5.js"
				import "./bad6.js"
				import "./bad7.js"
				import "./bad8.js"
				import "./bad9.js"
				import "./bad10.js"
				import "./bad11.js"
				import "./bad12.js"
				import "./bad13.js"
				import "./bad14.js"
				import "./bad15.js"

				import "./good0.js"
				import "./good1.js"
				import "./good2.js"
				import "./good3.js"
				import "./good4.js"
			)test"},
			{"/node_modules/foo/index.js", ""},
			{"/bad0.js", R"test(import x from "foo"; x = 1)test"},
			{"/bad1.js", R"test(import x from "foo"; x++)test"},
			{"/bad2.js", R"test(import x from "foo"; ([x] = 1))test"},
			{"/bad3.js", R"test(import x from "foo"; ({x} = 1))test"},
			{"/bad4.js", R"test(import x from "foo"; ({y: x} = 1))test"},
			{"/bad5.js", R"test(import {x} from "foo"; x++)test"},
			{"/bad6.js", R"test(import * as x from "foo"; x++)test"},
			{"/bad7.js", R"test(import * as x from "foo"; x.y = 1)test"},
			{"/bad8.js", R"test(import * as x from "foo"; x[y] = 1)test"},
			{"/bad9.js", R"test(import * as x from "foo"; x['y'] = 1)test"},
			{"/bad10.js", R"test(import * as x from "foo"; x['y z'] = 1)test"},
			{"/bad11.js", R"test(import x from "foo"; delete x)test"},
			{"/bad12.js", R"test(import {x} from "foo"; delete x)test"},
			{"/bad13.js", R"test(import * as x from "foo"; delete x.y)test"},
			{"/bad14.js", R"test(import * as x from "foo"; delete x['y'])test"},
			{"/bad15.js", R"test(import * as x from "foo"; delete x[y])test"},
			{"/good0.js", R"test(import x from "foo"; ({y = x} = 1))test"},
			{"/good1.js", R"test(import x from "foo"; ({[x]: y} = 1))test"},
			{"/good2.js", R"test(import x from "foo"; x.y = 1)test"},
			{"/good3.js", R"test(import x from "foo"; x[y] = 1)test"},
			{"/good4.js", R"test(import x from "foo"; x['y'] = 1)test"},
			{"/good5.js", R"test(import x from "foo"; x['y z'] = 1)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
bad0.js: ERROR: Cannot assign to import "x"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad1.js: ERROR: Cannot assign to import "x"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad10.js: ERROR: Cannot assign to import "y z"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file and then import and call that function here instead.
bad11.js: ERROR: Delete of a bare identifier cannot be used in an ECMAScript module
bad11.js: NOTE: This file is considered to be an ECMAScript module because of the "import" keyword here:
bad12.js: ERROR: Delete of a bare identifier cannot be used in an ECMAScript module
bad12.js: NOTE: This file is considered to be an ECMAScript module because of the "import" keyword here:
bad13.js: ERROR: Cannot assign to import "y"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setY") and then import and call that function here instead.
bad14.js: ERROR: Cannot assign to import "y"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setY") and then import and call that function here instead.
bad15.js: ERROR: Cannot assign to property on import "x"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file and then import and call that function here instead.
bad2.js: ERROR: Cannot assign to import "x"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad3.js: ERROR: Cannot assign to import "x"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad4.js: ERROR: Cannot assign to import "x"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad5.js: ERROR: Cannot assign to import "x"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad6.js: ERROR: Cannot assign to import "x"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad7.js: ERROR: Cannot assign to import "y"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setY") and then import and call that function here instead.
bad8.js: ERROR: Cannot assign to property on import "x"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file and then import and call that function here instead.
bad9.js: ERROR: Cannot assign to import "y"
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setY") and then import and call that function here instead.
)scan",
		
	});
}

TEST(BundlerDefault, TestAssignToImportNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/bad0.js", R"test(import x from "foo"; x = 1)test"},
			{"/bad1.js", R"test(import x from "foo"; x++)test"},
			{"/bad2.js", R"test(import x from "foo"; ([x] = 1))test"},
			{"/bad3.js", R"test(import x from "foo"; ({x} = 1))test"},
			{"/bad4.js", R"test(import x from "foo"; ({y: x} = 1))test"},
			{"/bad5.js", R"test(import {x} from "foo"; x++)test"},
			{"/bad6.js", R"test(import * as x from "foo"; x++)test"},
			{"/uncaught7.js", R"test(import * as x from "foo"; x.y = 1)test"},
			{"/uncaught8.js", R"test(import * as x from "foo"; x[y] = 1)test"},
			{"/uncaught9.js", R"test(import * as x from "foo"; x['y'] = 1)test"},
			{"/uncaught10.js", R"test(import * as x from "foo"; x['y z'] = 1)test"},
			{"/bad11.js", R"test(import x from "foo"; delete x)test"},
			{"/bad12.js", R"test(import {x} from "foo"; delete x)test"},
			{"/uncaught13.js", R"test(import * as x from "foo"; delete x.y)test"},
			{"/uncaught14.js", R"test(import * as x from "foo"; delete x['y'])test"},
			{"/uncaught15.js", R"test(import * as x from "foo"; delete x[y])test"},
			{"/good0.js", R"test(import x from "foo"; ({y = x} = 1))test"},
			{"/good1.js", R"test(import x from "foo"; ({[x]: y} = 1))test"},
			{"/good2.js", R"test(import x from "foo"; x.y = 1)test"},
			{"/good3.js", R"test(import x from "foo"; x[y] = 1)test"},
			{"/good4.js", R"test(import x from "foo"; x['y'] = 1)test"},
			{"/good5.js", R"test(import x from "foo"; x['y z'] = 1)test"},
		},
		.entry_paths = {
			"/bad0.js",
			"/bad1.js",
			"/bad2.js",
			"/bad3.js",
			"/bad4.js",
			"/bad5.js",
			"/bad6.js",
			"/uncaught7.js",
			"/uncaught8.js",
			"/uncaught9.js",
			"/uncaught10.js",
			"/bad11.js",
			"/bad12.js",
			"/uncaught13.js",
			"/uncaught14.js",
			"/uncaught15.js",
			"/good0.js",
			"/good1.js",
			"/good2.js",
			"/good3.js",
			"/good4.js",
			"/good5.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
bad0.js: WARNING: This assignment will throw because "x" is an import
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad1.js: WARNING: This assignment will throw because "x" is an import
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad11.js: ERROR: Delete of a bare identifier cannot be used in an ECMAScript module
bad11.js: NOTE: This file is considered to be an ECMAScript module because of the "import" keyword here:
bad12.js: ERROR: Delete of a bare identifier cannot be used in an ECMAScript module
bad12.js: NOTE: This file is considered to be an ECMAScript module because of the "import" keyword here:
bad2.js: WARNING: This assignment will throw because "x" is an import
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad3.js: WARNING: This assignment will throw because "x" is an import
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad4.js: WARNING: This assignment will throw because "x" is an import
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad5.js: WARNING: This assignment will throw because "x" is an import
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
bad6.js: WARNING: This assignment will throw because "x" is an import
NOTE: Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file (e.g. "setX") and then import and call that function here instead.
)scan",
		
	});
}

TEST(BundlerDefault, TestMinifyArguments) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function a(x = arguments) {
					let arguments
				}
				function b(x = arguments) {
					let arguments
				}
				function c(x = arguments) {
					let arguments
				}
				a()
				b()
				c()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestWarningsInsideNodeModules) {
	default_suite.ExpectBundledUnix(Bundled{
		.files = {
			{"/entry.js", R"test(

				import "./dup-case.js";        import "./node_modules/dup-case.js";        import "@plugin/dup-case.js"
				import "./not-in.js";          import "./node_modules/not-in.js";          import "@plugin/not-in.js"
				import "./not-instanceof.js";  import "./node_modules/not-instanceof.js";  import "@plugin/not-instanceof.js"
				import "./return-asi.js";      import "./node_modules/return-asi.js";      import "@plugin/return-asi.js"
				import "./bad-typeof.js";      import "./node_modules/bad-typeof.js";      import "@plugin/bad-typeof.js"
				import "./equals-neg-zero.js"; import "./node_modules/equals-neg-zero.js"; import "@plugin/equals-neg-zero.js"
				import "./equals-nan.js";      import "./node_modules/equals-nan.js";      import "@plugin/equals-nan.js"
				import "./equals-object.js";   import "./node_modules/equals-object.js";   import "@plugin/equals-object.js"
				import "./write-getter.js";    import "./node_modules/write-getter.js";    import "@plugin/write-getter.js"
				import "./read-setter.js";     import "./node_modules/read-setter.js";     import "@plugin/read-setter.js"
				import "./delete-super.js";    import "./node_modules/delete-super.js";    import "@plugin/delete-super.js"
			)test"},
			{"/dup-case.js", "switch (x) { case 0: case 0: }"},
			{"/node_modules/dup-case.js", "switch (x) { case 0: case 0: }"},
			{"/plugin-dir/node_modules/dup-case.js", "switch (x) { case 0: case 0: }"},
			{"/not-in.js", "!a in b"},
			{"/node_modules/not-in.js", "!a in b"},
			{"/plugin-dir/node_modules/not-in.js", "!a in b"},
			{"/not-instanceof.js", "!a instanceof b"},
			{"/node_modules/not-instanceof.js", "!a instanceof b"},
			{"/plugin-dir/node_modules/not-instanceof.js", "!a instanceof b"},
			{"/return-asi.js", R"test(
return
123)test"},
			{"/node_modules/return-asi.js", R"test(
return
123)test"},
			{"/plugin-dir/node_modules/return-asi.js", R"test(
return
123)test"},
			{"/bad-typeof.js", "typeof x == 'null'"},
			{"/node_modules/bad-typeof.js", "typeof x == 'null'"},
			{"/plugin-dir/node_modules/bad-typeof.js", "typeof x == 'null'"},
			{"/equals-neg-zero.js", "x === -0"},
			{"/node_modules/equals-neg-zero.js", "x === -0"},
			{"/plugin-dir/node_modules/equals-neg-zero.js", "x === -0"},
			{"/equals-nan.js", "x === NaN"},
			{"/node_modules/equals-nan.js", "x === NaN"},
			{"/plugin-dir/node_modules/equals-nan.js", "x === NaN"},
			{"/equals-object.js", "x === []"},
			{"/node_modules/equals-object.js", "x === []"},
			{"/plugin-dir/node_modules/equals-object.js", "x === []"},
			{"/write-getter.js", "class Foo { get #foo() {} foo() { this.#foo = 123 } }"},
			{"/node_modules/write-getter.js", "class Foo { get #foo() {} foo() { this.#foo = 123 } }"},
			{"/plugin-dir/node_modules/write-getter.js", "class Foo { get #foo() {} foo() { this.#foo = 123 } }"},
			{"/read-setter.js", "class Foo { set #foo(x) {} foo() { return this.#foo } }"},
			{"/node_modules/read-setter.js", "class Foo { set #foo(x) {} foo() { return this.#foo } }"},
			{"/plugin-dir/node_modules/read-setter.js", "class Foo { set #foo(x) {} foo() { return this.#foo } }"},
			{"/delete-super.js", "class Foo extends Bar { foo() { delete super.foo } }"},
			{"/node_modules/delete-super.js", "class Foo extends Bar { foo() { delete super.foo } }"},
			{"/plugin-dir/node_modules/delete-super.js", "class Foo extends Bar { foo() { delete super.foo } }"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.Plugins = {
				guchho::config::Plugin{
					.OnResolveList = {
						guchho::config::OnResolve{
							.Filter = std::regex("^@plugin/"),
							.Callback =
								[](guchho::config::OnResolveArgs args)
									-> guchho::config::OnResolveResult {
									guchho::config::OnResolveResult result;
									result.ResultPath.text =
										"/plugin-dir/node_modules/" +
										args.PathData.substr(8);
									result.ResultPath.namespace_ = "file";
									return result;
								},
						},
					},
				},
			},
			
		},
		.expected_scan_log = R"scan(
bad-typeof.js: WARNING: The "typeof" operator will never evaluate to "null"
NOTE: The expression "typeof x" actually evaluates to "object" in JavaScript, not "null". You need to use "x === null" to test for null.
delete-super.js: WARNING: Attempting to delete a property of "super" will throw a ReferenceError
dup-case.js: WARNING: This case clause will never be evaluated because it duplicates an earlier case clause
dup-case.js: NOTE: The earlier case clause is here:
equals-nan.js: WARNING: Comparison with NaN using the "===" operator here is always false
NOTE: Floating-point equality is defined such that NaN is never equal to anything, so "x === NaN" always returns false. You need to use "Number.isNaN(x)" instead to test for NaN.
equals-neg-zero.js: WARNING: Comparison with -0 using the "===" operator will also match 0
NOTE: Floating-point equality is defined such that 0 and -0 are equal, so "x === -0" returns true for both 0 and -0. You need to use "Object.is(x, -0)" instead to test for -0.
equals-object.js: WARNING: Comparison using the "===" operator here is always false
NOTE: Equality with a new object is always false in JavaScript because the equality operator tests object identity. You need to write code to compare the contents of the object instead. For example, use "Array.isArray(x) && x.length === 0" instead of "x === []" to test for an empty array.
not-in.js: WARNING: Suspicious use of the "!" operator inside the "in" operator
NOTE: The code "!x in y" is parsed as "(!x) in y". You need to insert parentheses to get "!(x in y)" instead.
not-instanceof.js: WARNING: Suspicious use of the "!" operator inside the "instanceof" operator
NOTE: The code "!x instanceof y" is parsed as "(!x) instanceof y". You need to insert parentheses to get "!(x instanceof y)" instead.
read-setter.js: WARNING: Reading from setter-only property "#foo" will throw
return-asi.js: WARNING: The following expression is not returned because of an automatically-inserted semicolon
write-getter.js: WARNING: Writing to getter-only property "#foo" will throw
)scan",
		
	});
}

TEST(BundlerDefault, TestRequireResolve) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(require.resolve)
				console.log(require.resolve())
				console.log(require.resolve(foo))
				console.log(require.resolve('a', 'b'))
				console.log(require.resolve('./present-file'))
				console.log(require.resolve('./missing-file'))
				console.log(require.resolve('./external-file'))
				console.log(require.resolve('missing-pkg'))
				console.log(require.resolve('external-pkg'))
				console.log(require.resolve('@scope/missing-pkg'))
				console.log(require.resolve('@scope/external-pkg'))
				try {
					console.log(require.resolve('inside-try'))
				} catch (e) {
				}
				if (false) {
					console.log(require.resolve('dead-code'))
				}
				console.log(false ? require.resolve('dead-if') : 0)
				console.log(true ? 0 : require.resolve('dead-if'))
				console.log(false && require.resolve('dead-and'))
				console.log(true || require.resolve('dead-or'))
				console.log(true ?? require.resolve('dead-nullish'))
			)test"},
			{"/present-file.js", ""},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"external-pkg", true},
						{"@scope/external-pkg", true},
					},
				},
				.PostResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"/external-file", true},
					},
				},
			},
			
		},
		.expected_scan_log = R"scan(
entry.js: WARNING: "./present-file" should be marked as external for use with "require.resolve"
entry.js: WARNING: "./missing-file" should be marked as external for use with "require.resolve"
entry.js: WARNING: "missing-pkg" should be marked as external for use with "require.resolve"
entry.js: WARNING: "@scope/missing-pkg" should be marked as external for use with "require.resolve"
)scan",
		
	});
}

TEST(BundlerDefault, TestInjectMissing) {
	default_suite.ExpectBundledUnix(Bundled{
		.files = {
			{"/entry.js", ""},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.InjectPaths = {"/inject.js"},
		},
		.expected_scan_log = "ERROR: Could not resolve \"/inject.js\"\n",
	});

	default_suite.ExpectBundledWindows(Bundled{
		.files = {
			{"C:\\entry.js", ""},
		},
		.entry_paths = {"C:\\entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "C:\\out.js",
			.InjectPaths = {"C:\\inject.js"},
		},
		.expected_scan_log = "ERROR: Could not resolve \"C:\\inject.js\"\n",
	});
}

TEST(BundlerDefault, TestInjectDuplicate) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", ""},
			{"/inject.js", "console.log('injected')"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.InjectPaths = {"/inject.js", "/inject.js"},
		},
	});
}

TEST(BundlerDefault, TestInject) {
	guchho::config::ProcessedDefines defines;
	{
		guchho::config::DefineExpr expr;
		expr.Parts = {"replace"};
		defines.DotDefines["prop"].push_back(guchho::config::DefineData{
			{"chain", "prop"}, std::make_shared<guchho::config::DefineExpr>(expr), {}});
	}
	{
		auto expr = std::make_shared<guchho::config::DefineExpr>();
		auto str = std::make_shared<guchho::javascript::EString>();
		str->value = u"defined";
		expr->Constant = str;
		defines.DotDefines["defined"].push_back(guchho::config::DefineData{
			{"obj", "defined"}, expr, {}});
	}
	{
		auto expr = std::make_shared<guchho::config::DefineExpr>();
		auto str = std::make_shared<guchho::javascript::EString>();
		str->value = u"should be used";
		expr->Constant = str;
		defines.IdentifierDefines["injectedAndDefined"] = guchho::config::DefineData{
			{"injectedAndDefined"}, expr, {}};
	}
	{
		auto expr = std::make_shared<guchho::config::DefineExpr>();
		auto str = std::make_shared<guchho::javascript::EString>();
		str->value = u"should be used";
		expr->Constant = str;
		defines.DotDefines["defined"].push_back(guchho::config::DefineData{
			{"injected", "and", "defined"}, expr, {}});
	}

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				let sideEffects = console.log('this should be renamed')
				let collide = 123
				console.log(obj.prop)
				console.log(obj.defined)
				console.log(injectedAndDefined)
				console.log(injected.and.defined)
				console.log(chain.prop.test)
				console.log(chain2.prop2.test)
				console.log(collide)
				console.log(re_export)
				console.log(re.export)
			)test"},
			{"/inject.js", R"test(
				export let obj = {}
				export let sideEffects = console.log('side effects')
				export let noSideEffects = /* @__PURE__ */ console.log('side effects')
				export let injectedAndDefined = 'should not be used'
				let injected_and_defined = 'should not be used'
				export { injected_and_defined as 'injected.and.defined' }
			)test"},
			{"/node_modules/unused/index.js", R"test(
				console.log('This is unused but still has side effects')
			)test"},
			{"/node_modules/sideEffects-false/index.js", R"test(
				console.log('This is unused and has no side effects')
			)test"},
			{"/node_modules/sideEffects-false/package.json", R"test(
				{ "sideEffects": false }
			)test"},
			{"/replacement.js", R"test(
				export let replace = {
					test() {}
				}
				let replace2 = {
					test() {}
				}
				export { replace2 as 'chain2.prop2' }
			)test"},
			{"/collision.js", R"test(
				export let collide = 123
			)test"},
			{"/re-export.js", R"test(
				export {re_export} from 'external-pkg'
				export {'re.export'} from 'external-pkg2'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.Defines = &defines,

			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"external-pkg", true},
						{"external-pkg2", true},
					},
				},
			},
			.InjectPaths = {
				"/inject.js",
				"/node_modules/unused/index.js",
				"/node_modules/sideEffects-false/index.js",
				"/replacement.js",
				"/collision.js",
				"/re-export.js",
			}
		},
	});
}

TEST(BundlerDefault, TestInjectNoBundle) {
	guchho::config::ProcessedDefines defines;
	{
		guchho::config::DefineExpr expr;
		expr.Parts = {"replace"};
		defines.DotDefines["prop"].push_back(guchho::config::DefineData{
			{"chain", "prop"}, std::make_shared<guchho::config::DefineExpr>(expr), {}});
	}
	{
		auto expr = std::make_shared<guchho::config::DefineExpr>();
		auto str = std::make_shared<guchho::javascript::EString>();
		str->value = u"defined";
		expr->Constant = str;
		defines.DotDefines["defined"].push_back(guchho::config::DefineData{
			{"obj", "defined"}, expr, {}});
	}
	{
		auto expr = std::make_shared<guchho::config::DefineExpr>();
		auto str = std::make_shared<guchho::javascript::EString>();
		str->value = u"should be used";
		expr->Constant = str;
		defines.IdentifierDefines["injectedAndDefined"] = guchho::config::DefineData{
			{"injectedAndDefined"}, expr, {}};
	}
	{
		auto expr = std::make_shared<guchho::config::DefineExpr>();
		auto str = std::make_shared<guchho::javascript::EString>();
		str->value = u"should be used";
		expr->Constant = str;
		defines.DotDefines["defined"].push_back(guchho::config::DefineData{
			{"injected", "and", "defined"}, expr, {}});
	}

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				let sideEffects = console.log('side effects')
				let collide = 123
				console.log(obj.prop)
				console.log(obj.defined)
				console.log(injectedAndDefined)
				console.log(injected.and.defined)
				console.log(chain.prop.test)
				console.log(chain2.prop2.test)
				console.log(collide)
				console.log(re_export)
				console.log(reexpo.rt)
			)test"},
			{"/inject.js", R"test(
				export let obj = {}
				export let sideEffects = console.log('this should be renamed')
				export let noSideEffects = /* @__PURE__ */ console.log('side effects')
				export let injectedAndDefined = 'should not be used'
				let injected_and_defined = 'should not be used'
				export { injected_and_defined as 'injected.and.defined' }
			)test"},
			{"/node_modules/unused/index.js", R"test(
				console.log('This is unused but still has side effects')
			)test"},
			{"/node_modules/sideEffects-false/index.js", R"test(
				console.log('This is unused and has no side effects')
			)test"},
			{"/node_modules/sideEffects-false/package.json", R"test(
				{ "sideEffects": false }
			)test"},
			{"/replacement.js", R"test(
				export let replace = {
					test() {}
				}
				let replaceDot = {
					test() {}
				}
				export { replaceDot as 'chain2.prop2' }
			)test"},
			{"/collision.js", R"test(
				export let collide = 123
			)test"},
			{"/re-export.js", R"test(
				export {re_export} from 'external-pkg'
				export {'reexpo.rt'} from 'external-pkg2'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.InjectPaths = {
				"/inject.js",
				"/node_modules/unused/index.js",
				"/node_modules/sideEffects-false/index.js",
				"/replacement.js",
				"/collision.js",
				"/re-export.js",
			},
			.TreeShaking = true
		},
	});
}

TEST(BundlerDefault, TestInjectJSX) {
	guchho::config::ProcessedDefines defines;
	{
		guchho::config::DefineExpr expr;
		expr.Parts = {"el"};
		defines.DotDefines["createElement"].push_back(guchho::config::DefineData{
			{"React", "createElement"}, std::make_shared<guchho::config::DefineExpr>(expr), {}});
	}
	{
		guchho::config::DefineExpr expr;
		expr.Parts = {"frag"};
		defines.DotDefines["Fragment"].push_back(guchho::config::DefineData{
			{"React", "Fragment"}, std::make_shared<guchho::config::DefineExpr>(expr), {}});
	}

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(
				console.log(<><div/></>)
			)test"},
			{"/inject.js", R"test(
				export function el() {}
				export function frag() {}
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.InjectPaths = {"/inject.js"},
		},
	});
}

TEST(BundlerDefault, TestInjectJSXDotNames) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(
				console.log(<><div/></>)
			)test"},
			{"/inject.js", R"test(
				function el() {}
				function frag() {}
				export {
					el as 'React.createElement',
					frag as 'React.Fragment',
				}
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.InjectPaths = {"/inject.js"},
		},
	});
}

TEST(BundlerDefault, TestInjectImportTS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.ts", R"test(
				console.log('here')
			)test"},
			{"/inject.js", R"test(
				console.log('must be present')
			)test"},
		},
		.entry_paths = {"/entry.ts"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			.InjectPaths = {"/inject.js"},
		},
	});
}

TEST(BundlerDefault, TestInjectImportOrder) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.ts", R"test(
				import 'third'
				console.log('third')
			)test"},
			{"/inject-1.js", R"test(
				import 'first'
				console.log('first')
			)test"},
			{"/inject-2.js", R"test(
				import 'second'
				console.log('second')
			)test"},
		},
		.entry_paths = {"/entry.ts"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"first", true},
						{"second", true},
						{"third", true},
					},
				},
			},
			.InjectPaths = {"/inject-1.js", "/inject-2.js"}
		},
	});
}

TEST(BundlerDefault, TestInjectAssign) {
	guchho::config::ProcessedDefines defines;
	{
		guchho::config::DefineExpr expr;
		expr.Parts = {"some", "define"};
		defines.IdentifierDefines["defined"] = guchho::config::DefineData{
			{"defined"}, std::make_shared<guchho::config::DefineExpr>(expr), {}};
	}

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				test = true
				foo.bar = true
				defined = true
			)test"},
			{"/inject.js", R"test(
				export let test = 0
				let fooBar = 1
				let someDefine = 2
				export { fooBar as 'foo.bar' }
				export { someDefine as 'some.define' }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.InjectPaths = {"/inject.js"},
		},
		.expected_scan_log = R"scan(entry.js: ERROR: Cannot assign to "test" because it's an import from an injected file
inject.js: NOTE: The symbol "test" was exported from "inject.js" here:
entry.js: ERROR: Cannot assign to "foo.bar" because it's an import from an injected file
inject.js: NOTE: The symbol "foo.bar" was exported from "inject.js" here:
entry.js: ERROR: Cannot assign to "some.define" because it's an import from an injected file
inject.js: NOTE: The symbol "some.define" was exported from "inject.js" here:
)scan",
	});
}

TEST(BundlerDefault, TestInjectWithDefine) {
	guchho::config::ProcessedDefines defines;
	{
		auto expr = std::make_shared<guchho::config::DefineExpr>();
		auto str = std::make_shared<guchho::javascript::EString>();
		str->value = u"define";
		expr->Constant = str;
		defines.IdentifierDefines["both"] = guchho::config::DefineData{
			{"both"}, expr, {}};
	}
	{
		guchho::config::DefineExpr expr;
		expr.Parts = {"second"};
		defines.IdentifierDefines["first"] = guchho::config::DefineData{
			{"first"}, std::make_shared<guchho::config::DefineExpr>(expr), {}};
	}
	{
		auto expr = std::make_shared<guchho::config::DefineExpr>();
		auto str = std::make_shared<guchho::javascript::EString>();
		str->value = u"defi.ne";
		expr->Constant = str;
		defines.DotDefines["th"].push_back(guchho::config::DefineData{
			{"bo", "th"}, expr, {}});
	}
	{
		guchho::config::DefineExpr expr;
		expr.Parts = {"seco", "nd"};
		defines.DotDefines["st"].push_back(guchho::config::DefineData{
			{"fir", "st"}, std::make_shared<guchho::config::DefineExpr>(expr), {}});
	}

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				console.log(
					// define wins over inject
					both === 'define',
					bo.th === 'defi.ne',
					// define forwards to inject
					first === 'success (identifier)',
					fir.st === 'success (dot name)',
				)
			)test"},
			{"/inject.js", R"test(
				export let both = 'inject'
				export let first = 'TEST FAILED!'
				export let second = 'success (identifier)'

				let both2 = 'inject'
				let first2 = 'TEST FAILED!'
				let second2 = 'success (dot name)'
				export {
					both2 as 'bo.th',
					first2 as 'fir.st',
					second2 as 'seco.nd',
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.InjectPaths = {"/inject.js"},
		},
	});
}

TEST(BundlerDefault, TestOutbase) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a/b/c.js", R"test(

				console.log('c')
			)test"},
			{"/a/b/d.js", R"test(

				console.log('d')
			)test"},
		},
		.entry_paths = {
			"/a/b/c.js",
			"/a/b/d.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/",
			
		},
		
	});
}

TEST(BundlerDefault, TestAvoidTDZ) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class Foo {
					static foo = new Foo
				}
				let foo = Foo.foo
				console.log(foo)
				export class Bar {}
				export let bar = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestAvoidTDZNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class Foo {
					static foo = new Foo
				}
				let foo = Foo.foo
				console.log(foo)
				export class Bar {}
				export let bar = 123
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestDefineImportMeta) {
	guchho::config::ProcessedDefines defines = guchho::config::ProcessDefines({
		{
			{"import", "meta"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{1}),
			}),
			{},
		},
		{
			{"import", "meta", "foo"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{2}),
			}),
			{},
		},
		{
			{"import", "meta", "foo", "bar"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{3}),
			}),
			{},
		},
	});

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(
					// These should be fully substituted
					import.meta,
					import.meta.foo,
					import.meta.foo.bar,

					// Should just substitute "import.meta.foo"
					import.meta.foo.baz,

					// This should not be substituted
					import.meta.bar,
				)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestDefineImportMetaES5) {
	guchho::config::ProcessedDefines defines = guchho::config::ProcessDefines({
		{
			{"import", "meta", "x"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{1}),
			}),
			{},
		},
	});

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/replaced.js", R"test(

				console.log(import.meta.x)
			)test"},
			{"/kept.js", R"test(

				console.log(import.meta.y)
			)test"},
			{"/dead-code.js", R"test(

				var x = () => console.log(import.meta.z)
			)test"},
		},
		.entry_paths = {
			"/replaced.js",
			"/kept.js",
			"/dead-code.js",
		},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kImportMeta,
			
		},
		.expected_scan_log = R"scan(
dead-code.js: WARNING: "import.meta" is not available in the configured target environment and will be empty
kept.js: WARNING: "import.meta" is not available in the configured target environment and will be empty
)scan",
		
	});
}

TEST(BundlerDefault, TestInjectImportMeta) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				console.log(
					// These should be fully substituted
					import.meta,
					import.meta.foo,
					import.meta.foo.bar,

					// Should just substitute "import.meta.foo"
					import.meta.foo.baz,

					// This should not be substituted
					import.meta.bar,
				)
			)test"},
			{"/inject.js", R"test(
				let foo = 1
				let bar = 2
				let baz = 3
				export {
					foo as 'import.meta',
					bar as 'import.meta.foo',
					baz as 'import.meta.foo.bar',
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.InjectPaths = {"/inject.js"},
		},
	});
}

TEST(BundlerDefault, TestDefineThis) {
	guchho::config::ProcessedDefines defines = guchho::config::ProcessDefines({
		{
			{"this"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{1}),
			}),
			{},
		},
		{
			{"this", "foo"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{2}),
			}),
			{},
		},
		{
			{"this", "foo", "bar"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{3}),
			}),
			{},
		},
	});

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				ok(
					// These should be fully substituted
					this,
					this.foo,
					this.foo.bar,

					// Should just substitute "this.foo"
					this.foo.baz,

					// This should not be substituted
					this.bar,
				);

				// This code should be the same as above
				(() => {
					ok(
						this,
						this.foo,
						this.foo.bar,
						this.foo.baz,
						this.bar,
					);
				})();

				// Nothing should be substituted in this code
				(function() {
					doNotSubstitute(
						this,
						this.foo,
						this.foo.bar,
						this.foo.baz,
						this.bar,
					);
				})();
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestDefineOptionalChain) {
	guchho::config::ProcessedDefines defines = guchho::config::ProcessDefines({
		{
			{"a", "b", "c"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{1}),
			}),
			{},
		},
	});

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log([
					a.b.c,
					a?.b.c,
					a.b?.c,
				], [
					a['b']['c'],
					a?.['b']['c'],
					a['b']?.['c'],
				], [
					a[b][c],
					a?.[b][c],
					a[b]?.[c],
				])
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestDefineOptionalChainLowered) {
	guchho::config::ProcessedDefines defines = guchho::config::ProcessDefines({
		{
			{"a", "b", "c"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{1}),
			}),
			{},
		},
	});

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log([
					a.b.c,
					a?.b.c,
					a.b?.c,
				], [
					a['b']['c'],
					a?.['b']['c'],
					a['b']?.['c'],
				], [
					a[b][c],
					a?.[b][c],
					a[b]?.[c],
				])
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kOptionalChain,
			
		},
		
	});
}

TEST(BundlerDefault, TestDefineOptionalChainPanicIssue3551) {
	guchho::config::ProcessedDefines defines = guchho::config::ProcessDefines({
		{
			{"x"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{1}),
			}),
			{},
		},
		{
			{"a", "b"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENumber>(guchho::javascript::ENumber{1}),
			}),
			{},
		},
	});

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/id-define.js", R"test(

				x?.y.z;
				(x?.y).z;
				x?.y["z"];
				(x?.y)["z"];
				x?.y();
				(x?.y)();
				x?.y.z();
				(x?.y).z();
				x?.y["z"]();
				(x?.y)["z"]();
				delete x?.y.z;
				delete (x?.y).z;
				delete x?.y["z"];
				delete (x?.y)["z"];
			)test"},
			{"/dot-define.js", R"test(

				a?.b.c;
				(a?.b).c;
				a?.b["c"];
				(a?.b)["c"];
				a?.b();
				(a?.b)();
				a?.b.c();
				(a?.b).c();
				a?.b["c"]();
				(a?.b)["c"]();
				delete a?.b.c;
				delete (a?.b).c;
				delete a?.b["c"];
				delete (a?.b)["c"];
			)test"},
		},
		.entry_paths = {
			"/id-define.js",
			"/dot-define.js",
		},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestDefineInfiniteLoopIssue2407) {
	guchho::config::ProcessedDefines defines = guchho::config::ProcessDefines({
		{
			{"a", "b"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Parts = {"b", "c"},
			}),
			{},
		},
		{
			{"b", "c"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Parts = {"c", "a"},
			}),
			{},
		},
		{
			{"c", "a"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Parts = {"a", "b"},
			}),
			{},
		},
		{
			{"x", "y"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Parts = {"y"},
			}),
			{},
		},
	});

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				a.b()
				x.y()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestDefineAssignWarning) {
	guchho::config::ProcessedDefines defines = guchho::config::ProcessDefines({
		{
			{"a"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENull>(),
			}),
			{},
		},
		{
			{"b", "c"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Constant = std::make_shared<guchho::javascript::ENull>(),
			}),
			{},
		},
		{
			{"d"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Parts = {"ident"},
			}),
			{},
		},
		{
			{"e", "f"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Parts = {"ident"},
			}),
			{},
		},
		{
			{"g"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Parts = {"dot", "chain"},
			}),
			{},
		},
		{
			{"h", "i"},
			std::make_shared<guchho::config::DefineExpr>(guchho::config::DefineExpr{
				.Parts = {"dot", "chain"},
			}),
			{},
		},
	});

	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/read.js", R"test(

				console.log(
					[a, b.c, b['c']],
					[d, e.f, e['f']],
					[g, h.i, h['i']],
				)
			)test"},
			{"/write.js", R"test(

				console.log(
					[a = 0, b.c = 0, b['c'] = 0],
					[d = 0, e.f = 0, e['f'] = 0],
					[g = 0, h.i = 0, h['i'] = 0],
				)
			)test"},
		},
		.entry_paths = {
			"/read.js",
			"/write.js",
		},
		.options = guchho::config::Options{
			.Defines = &defines,
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		.expected_scan_log = R"scan(
write.js: WARNING: Suspicious assignment to defined constant "a"
NOTE: The expression "a" has been configured to be replaced with a constant using the "define" feature. If this expression is supposed to be a compile-time constant, then it doesn't make sense to assign to it here. Or if this expression is supposed to change at run-time, this "define" substitution should be removed.
write.js: WARNING: Suspicious assignment to defined constant "b.c"
NOTE: The expression "b.c" has been configured to be replaced with a constant using the "define" feature. If this expression is supposed to be a compile-time constant, then it doesn't make sense to assign to it here. Or if this expression is supposed to change at run-time, this "define" substitution should be removed.
write.js: WARNING: Suspicious assignment to defined constant "b['c']"
NOTE: The expression "b['c']" has been configured to be replaced with a constant using the "define" feature. If this expression is supposed to be a compile-time constant, then it doesn't make sense to assign to it here. Or if this expression is supposed to change at run-time, this "define" substitution should be removed.
)scan",
		
	});
}

TEST(BundlerDefault, TestKeepNamesAllForms) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/keep.js", R"test(

				// Initializers
				function fn() {}
				function foo(fn = function() {}) {}
				var fn = function() {};
				var obj = { "f n": function() {} };
				class Foo0 { "f n" = function() {} }
				class Foo1 { static "f n" = function() {} }
				class Foo2 { accessor "f n" = function() {} }
				class Foo3 { static accessor "f n" = function() {} }
				class Foo4 { #fn = function() {} }
				class Foo5 { static #fn = function() {} }
				class Foo6 { accessor #fn = function() {} }
				class Foo7 { static accessor #fn = function() {} }

				// Assignments
				fn = function() {};
				fn ||= function() {};
				fn &&= function() {};
				fn ??= function() {};

				// Destructuring
				var [fn = function() {}] = [];
				var { fn = function() {} } = {};
				for (var [fn = function() {}] = []; ; ) ;
				for (var { fn = function() {} } = {}; ; ) ;
				for (var [fn = function() {}] in obj) ;
				for (var { fn = function() {} } in obj) ;
				for (var [fn = function() {}] of obj) ;
				for (var { fn = function() {} } of obj) ;
				function foo([fn = function() {}]) {}
				function foo({ fn = function() {} }) {}
				[fn = function() {}] = [];
				({ fn = function() {} } = {});
			)test"},
			{"/do-not-keep.js", R"test(

				// Class methods
				class Foo0 { fn() {} }
				class Foo1 { *fn() {} }
				class Foo2 { get fn() {} }
				class Foo3 { set fn(_) {} }
				class Foo4 { async fn() {} }
				class Foo5 { static fn() {} }
				class Foo6 { static *fn() {} }
				class Foo7 { static get fn() {} }
				class Foo8 { static set fn(_) {} }
				class Foo9 { static async fn() {} }

				// Class private methods
				class Bar0 { #fn() {} }
				class Bar1 { *#fn() {} }
				class Bar2 { get #fn() {} }
				class Bar3 { set #fn(_) {} }
				class Bar4 { async #fn() {} }
				class Bar5 { static #fn() {} }
				class Bar6 { static *#fn() {} }
				class Bar7 { static get #fn() {} }
				class Bar8 { static set #fn(_) {} }
				class Bar9 { static async #fn(_) {} }

				// Object methods
				const Baz0 = { fn() {} }
				const Baz1 = { *fn() {} }
				const Baz2 = { get fn() {} }
				const Baz3 = { set fn(_) {} }
				const Baz4 = { async fn() {} }
			)test"},
		},
		.entry_paths = {
			"/keep.js",
			"/do-not-keep.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.KeepNames = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestKeepNamesTreeShaking) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				function fnStmtRemove() {}
				function fnStmtKeep() {}
				x = fnStmtKeep

				let fnExprRemove = function remove() {}
				let fnExprKeep = function keep() {}
				x = fnExprKeep

				class clsStmtRemove {}
				class clsStmtKeep {}
				new clsStmtKeep()

				let clsExprRemove = class remove {}
				let clsExprKeep = class keep {}
				new clsExprKeep()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
			.KeepNames = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestKeepNamesClassStaticName) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class A { static foo }
				class B { static name }
				class C { static name() {} }
				class D { static get name() {} }
				class E { static set name(x) {} }
				class F { static ['name'] = 0 }

				let a = class a { static foo }
				let b = class b { static name }
				let c = class c { static name() {} }
				let d = class d { static get name() {} }
				let e = class e { static set name(x) {} }
				let f = class f { static ['name'] = 0 }

				let a2 = class { static foo }
				let b2 = class { static name }
				let c2 = class { static name() {} }
				let d2 = class { static get name() {} }
				let e2 = class { static set name(x) {} }
				let f2 = class { static ['name'] = 0 }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.KeepNames = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestCharFreqIgnoreComments) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				export default function(one, two, three, four) {
					return 'the argument names must be the same'
				}
			)test"},
			{"/b.js", R"test(

				export default function(one, two, three, four) {
					return 'the argument names must be the same'
				}

				// Some comment text to change the character frequency histogram:
				// ________________________________________________________________________________
				// FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
				// AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
				// IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII
				// LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL
			)test"},
		},
		.entry_paths = {
			"/a.js",
			"/b.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestImportRelativeAsPackage) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/src/entry.js", R"test(

				import 'some/other/file'
			)test"},
			{"/Users/user/project/src/some/other/file.js", R"test(

			)test"},
		},
		.entry_paths = {"/Users/user/project/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/Users/user/project/out.js",
			
		},
		.expected_scan_log = R"scan(
Users/user/project/src/entry.js: ERROR: Could not resolve "some/other/file"
NOTE: Use the relative path "./some/other/file" to reference the file "Users/user/project/src/some/other/file.js". Without the leading "./", the path "some/other/file" is being interpreted as a package path instead.
)scan",
		
	});
}

TEST(BundlerDefault, TestForbidConstAssignWhenBundling) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const x = 1
				x = 2
				function foo() {
					const y = 1
					y = 2
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Cannot assign to "x" because it is a constant
entry.js: NOTE: The symbol "x" was declared a constant here:
entry.js: ERROR: Cannot assign to "y" because it is a constant
entry.js: NOTE: The symbol "y" was declared a constant here:
)scan",
		
	});
}

TEST(BundlerDefault, TestForbidConstAssignWhenLoweringUsing) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const x = 1
				using x2 = 2
				x = 3
				function foo() {
					const y = 1
					using y2 = 2
					y = 3
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kUsing,
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Cannot assign to "x" because it is a constant
entry.js: NOTE: The symbol "x" was declared a constant here:
entry.js: WARNING: This assignment will throw because "y" is a constant
entry.js: NOTE: The symbol "y" was declared a constant here:
)scan",
		
	});
}

TEST(BundlerDefault, TestConstWithLet) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const a = 1; console.log(a)
				if (true) { const b = 2; console.log(b) }
				if (true) { const b = 3; unknownFn(b) }
				for (const c = x;;) console.log(c)
				for (const d in x) console.log(d)
				for (const e of x) console.log(e)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestConstWithLetNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const a = 1; console.log(a)
				if (true) { const b = 2; console.log(b) }
				if (true) { const b = 3; unknownFn(b) }
				for (const c = x;;) console.log(c)
				for (const d in x) console.log(d)
				for (const e of x) console.log(e)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestConstWithLetNoMangle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				const a = 1; console.log(a)
				if (true) { const b = 2; console.log(b) }
				for (const c = x;;) console.log(c)
				for (const d in x) console.log(d)
				for (const e of x) console.log(e)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestRequireMainCacheCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log('is main:', require.main === module)
				console.log(require('./is-main'))
				console.log('cache:', require.cache);
			)test"},
			{"/is-main.js", R"test(

				module.exports = require.main === module
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestExternalES6ConvertedToCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				require('./a')
				require('./b')
				require('./c')
				require('./d')
				require('./e')
			)test"},
			{"/a.js", R"test(

				import * as ns from 'x'
				export {ns}
			)test"},
			{"/b.js", R"test(

				import * as ns from 'x' // "ns" must be renamed to avoid collisions with "a.js"
				export {ns}
			)test"},
			{"/c.js", R"test(

				export * as ns from 'x'
			)test"},
			{"/d.js", R"test(

				export {ns} from 'x'
			)test"},
			{"/e.js", R"test(

				export * from 'x'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"x", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestCallImportNamespaceWarning) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/js.js", R"test(

				import * as a from "a"
				import {b} from "b"
				import c from "c"
				a()
				b()
				c()
				new a()
				new b()
				new c()
			)test"},
			{"/ts.ts", R"test(

				import * as a from "a"
				import {b} from "b"
				import c from "c"
				a()
				b()
				c()
				new a()
				new b()
				new c()
			)test"},
			{"/jsx-components.jsx", R"test(

				import * as A from "a"
				import {B} from "b"
				import C from "c"
				<A/>;
				<B/>;
				<C/>;
			)test"},
			{"/jsx-a.jsx", R"test(

				// @jsx a
				import * as a from "a"
				<div/>
			)test"},
			{"/jsx-b.jsx", R"test(

				// @jsx b
				import {b} from "b"
				<div/>
			)test"},
			{"/jsx-c.jsx", R"test(

				// @jsx c
				import c from "c"
				<div/>
			)test"},
		},
		.entry_paths = {
			"/js.js",
			"/ts.ts",
			"/jsx-components.jsx",
			"/jsx-a.jsx",
			"/jsx-b.jsx",
			"/jsx-c.jsx",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputDir = "/out",
			
		},
		.expected_scan_log = R"scan(
js.js: WARNING: Calling "a" will crash at run-time because it's an import namespace object, not a function
js.js: NOTE: Consider changing "a" to a default import instead:
js.js: WARNING: Constructing "a" will crash at run-time because it's an import namespace object, not a constructor
js.js: NOTE: Consider changing "a" to a default import instead:
jsx-a.jsx: WARNING: Calling "a" will crash at run-time because it's an import namespace object, not a function
jsx-a.jsx: NOTE: Consider changing "a" to a default import instead:
jsx-components.jsx: WARNING: Using "A" in a JSX expression will crash at run-time because it's an import namespace object, not a component
jsx-components.jsx: NOTE: Consider changing "A" to a default import instead:
ts.ts: WARNING: Calling "a" will crash at run-time because it's an import namespace object, not a function
ts.ts: NOTE: Consider changing "a" to a default import instead:
NOTE: Make sure to enable TypeScript's "esModuleInterop" setting so that TypeScript's type checker generates an error when you try to do this. You can read more about this setting here: https://www.typescriptlang.org/tsconfig#esModuleInterop
ts.ts: WARNING: Constructing "a" will crash at run-time because it's an import namespace object, not a constructor
ts.ts: NOTE: Consider changing "a" to a default import instead:
NOTE: Make sure to enable TypeScript's "esModuleInterop" setting so that TypeScript's type checker generates an error when you try to do this. You can read more about this setting here: https://www.typescriptlang.org/tsconfig#esModuleInterop
)scan",
		
	});
}

TEST(BundlerDefault, TestJSXThisValueCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/factory.jsx", R"test(

				console.log([
					<x />,
					/* @__PURE__ */ this('x', null),
				])
				f = function() {
					console.log([
						<y />,
						/* @__PURE__ */ this('y', null),
					])
				}
			)test"},
			{"/fragment.jsx", R"test(

				console.log([
					<>x</>,
					/* @__PURE__ */ this(this, null, 'x'),
				]),
				f = function() {
					console.log([
						<>y</>,
						/* @__PURE__ */ this(this, null, 'y'),
					])
				}
			)test"},
		},
		.entry_paths = {
			"/factory.jsx",
			"/fragment.jsx",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.JSX = guchho::config::JSXOptions{
				.Factory  = guchho::config::DefineExpr{.Parts = {"this"}},
				.Fragment = guchho::config::DefineExpr{.Parts = {"this"}},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestJSXThisValueESM) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/factory.jsx", R"test(

				console.log([
					<x />,
					/* @__PURE__ */ this('x', null),
				])
				f = function() {
					console.log([
						<y />,
						/* @__PURE__ */ this('y', null),
					])
				}
				export {}
			)test"},
			{"/fragment.jsx", R"test(

				console.log([
					<>x</>,
					/* @__PURE__ */ this(this, null, 'x'),
				]),
				f = function() {
					console.log([
						<>y</>,
						/* @__PURE__ */ this(this, null, 'y'),
					])
				}
				export {}
			)test"},
		},
		.entry_paths = {
			"/factory.jsx",
			"/fragment.jsx",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.JSX = guchho::config::JSXOptions{
				.Factory  = guchho::config::DefineExpr{.Parts = {"this"}},
				.Fragment = guchho::config::DefineExpr{.Parts = {"this"}},
			},
			
		},
		.expected_scan_log = R"scan(
factory.jsx: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
factory.jsx: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
fragment.jsx: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
fragment.jsx: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
)scan",
		.debug_logs = true,
		
	});
}

TEST(BundlerDefault, TestJSXThisPropertyCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/factory.jsx", R"test(

				console.log([
					<x />,
					/* @__PURE__ */ this.factory('x', null),
				])
				f = function() {
					console.log([
						<y />,
						/* @__PURE__ */ this.factory('y', null),
					])
				}
			)test"},
			{"/fragment.jsx", R"test(

				console.log([
					<>x</>,
					/* @__PURE__ */ this.factory(this.fragment, null, 'x'),
				]),
				f = function() {
					console.log([
						<>y</>,
						/* @__PURE__ */ this.factory(this.fragment, null, 'y'),
					])
				}
			)test"},
		},
		.entry_paths = {
			"/factory.jsx",
			"/fragment.jsx",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.JSX = guchho::config::JSXOptions{
				.Factory  = guchho::config::DefineExpr{.Parts = {"this", "factory"}},
				.Fragment = guchho::config::DefineExpr{.Parts = {"this", "fragment"}},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestJSXThisPropertyESM) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/factory.jsx", R"test(

				console.log([
					<x />,
					/* @__PURE__ */ this.factory('x', null),
				])
				f = function() {
					console.log([
						<y />,
						/* @__PURE__ */ this.factory('y', null),
					])
				}
				export {}
			)test"},
			{"/fragment.jsx", R"test(

				console.log([
					<>x</>,
					/* @__PURE__ */ this.factory(this.fragment, null, 'x'),
				]),
				f = function() {
					console.log([
						<>y</>,
						/* @__PURE__ */ this.factory(this.fragment, null, 'y'),
					])
				}
				export {}
			)test"},
		},
		.entry_paths = {
			"/factory.jsx",
			"/fragment.jsx",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.JSX = guchho::config::JSXOptions{
				.Factory  = guchho::config::DefineExpr{.Parts = {"this", "factory"}},
				.Fragment = guchho::config::DefineExpr{.Parts = {"this", "fragment"}},
			},
			
		},
		.expected_scan_log = R"scan(
factory.jsx: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
factory.jsx: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
fragment.jsx: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
fragment.jsx: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
)scan",
		.debug_logs = true,
		
	});
}

TEST(BundlerDefault, TestJSXImportMetaValue) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/factory.jsx", R"test(

				console.log([
					<x />,
					/* @__PURE__ */ import.meta('x', null),
				])
				f = function() {
					console.log([
						<y />,
						/* @__PURE__ */ import.meta('y', null),
					])
				}
				export {}
			)test"},
			{"/fragment.jsx", R"test(

				console.log([
					<>x</>,
					/* @__PURE__ */ import.meta(import.meta, null, 'x'),
				]),
				f = function() {
					console.log([
						<>y</>,
						/* @__PURE__ */ import.meta(import.meta, null, 'y'),
					])
				}
				export {}
			)test"},
		},
		.entry_paths = {
			"/factory.jsx",
			"/fragment.jsx",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kImportMeta,
			.JSX = guchho::config::JSXOptions{
				.Factory  = guchho::config::DefineExpr{.Parts = {"import", "meta"}},
				.Fragment = guchho::config::DefineExpr{.Parts = {"import", "meta"}},
			},
			
		},
		.expected_scan_log = R"scan(
factory.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
factory.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
fragment.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
fragment.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
fragment.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
fragment.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
)scan",
		
	});
}

TEST(BundlerDefault, TestJSXImportMetaProperty) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/factory.jsx", R"test(

				console.log([
					<x />,
					/* @__PURE__ */ import.meta.factory('x', null),
				])
				f = function() {
					console.log([
						<y />,
						/* @__PURE__ */ import.meta.factory('y', null),
					])
				}
				export {}
			)test"},
			{"/fragment.jsx", R"test(

				console.log([
					<>x</>,
					/* @__PURE__ */ import.meta.factory(import.meta.fragment, null, 'x'),
				]),
				f = function() {
					console.log([
						<>y</>,
						/* @__PURE__ */ import.meta.factory(import.meta.fragment, null, 'y'),
					])
				}
				export {}
			)test"},
		},
		.entry_paths = {
			"/factory.jsx",
			"/fragment.jsx",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kImportMeta,
			.JSX = guchho::config::JSXOptions{
				.Factory  = guchho::config::DefineExpr{.Parts = {"import", "meta", "factory"}},
				.Fragment = guchho::config::DefineExpr{.Parts = {"import", "meta", "fragment"}},
			},
			
		},
		.expected_scan_log = R"scan(
factory.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
factory.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
fragment.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
fragment.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
fragment.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
fragment.jsx: WARNING: "import.meta" is not available in the configured target environment and will be empty
)scan",
		
	});
}

TEST(BundlerDefault, TestBundlingFilesOutsideOfOutbase) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/src/entry.js", R"test(

				console.log('test')
			)test"},
		},
		.entry_paths = {"/src/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.CodeSplitting = true,
			.AbsOutputDir = "/out",
			.AbsOutputBase = "/some/nested/directory",
			
		},
		
	});
}

TEST(BundlerDefault, TestVarRelocatingBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/top-level.js", R"test(

				var a;
				for (var b; 0;);
				for (var { c, x: [d] } = {}; 0;);
				for (var e of []);
				for (var { f, x: [g] } of []);
				for (var h in {});
				for (var i = 1 in {});
				for (var { j, x: [k] } in {});
				function l() {}
			)test"},
			{"/nested.js", R"test(

				if (true) {
					var a;
					for (var b; 0;);
					for (var { c, x: [d] } = {}; 0;);
					for (var e of []);
					for (var { f, x: [g] } of []);
					for (var h in {});
					for (var i = 1 in {});
					for (var { j, x: [k] } in {});
					function l() {}
				}
			)test"},
			{"/let.js", R"test(

				if (true) {
					let a;
					for (let b; 0;);
					for (let { c, x: [d] } = {}; 0;);
					for (let e of []);
					for (let { f, x: [g] } of []);
					for (let h in {});
					// for (let i = 1 in {});
					for (let { j, x: [k] } in {});
				}
			)test"},
			{"/function.js", R"test(

				function x() {
					var a;
					for (var b; 0;);
					for (var { c, x: [d] } = {}; 0;);
					for (var e of []);
					for (var { f, x: [g] } of []);
					for (var h in {});
					for (var i = 1 in {});
					for (var { j, x: [k] } in {});
					function l() {}
				}
				x()
			)test"},
			{"/function-nested.js", R"test(

				function x() {
					if (true) {
						var a;
						for (var b; 0;);
						for (var { c, x: [d] } = {}; 0;);
						for (var e of []);
						for (var { f, x: [g] } of []);
						for (var h in {});
						for (var i = 1 in {});
						for (var { j, x: [k] } in {});
						function l() {}
					}
				}
				x()
			)test"},
		},
		.entry_paths = {
			"/top-level.js",
			"/nested.js",
			"/let.js",
			"/function.js",
			"/function-nested.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestVarRelocatingNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/top-level.js", R"test(

				var a;
				for (var b; 0;);
				for (var { c, x: [d] } = {}; 0;);
				for (var e of []);
				for (var { f, x: [g] } of []);
				for (var h in {});
				for (var i = 1 in {});
				for (var { j, x: [k] } in {});
				function l() {}
			)test"},
			{"/nested.js", R"test(

				if (true) {
					var a;
					for (var b; 0;);
					for (var { c, x: [d] } = {}; 0;);
					for (var e of []);
					for (var { f, x: [g] } of []);
					for (var h in {});
					for (var i = 1 in {});
					for (var { j, x: [k] } in {});
					function l() {}
				}
			)test"},
			{"/let.js", R"test(

				if (true) {
					let a;
					for (let b; 0;);
					for (let { c, x: [d] } = {}; 0;);
					for (let e of []);
					for (let { f, x: [g] } of []);
					for (let h in {});
					// for (let i = 1 in {});
					for (let { j, x: [k] } in {});
				}
			)test"},
			{"/function.js", R"test(

				function x() {
					var a;
					for (var b; 0;);
					for (var { c, x: [d] } = {}; 0;);
					for (var e of []);
					for (var { f, x: [g] } of []);
					for (var h in {});
					for (var i = 1 in {});
					for (var { j, x: [k] } in {});
					function l() {}
				}
				x()
			)test"},
			{"/function-nested.js", R"test(

				function x() {
					if (true) {
						var a;
						for (var b; 0;);
						for (var { c, x: [d] } = {}; 0;);
						for (var e of []);
						for (var { f, x: [g] } of []);
						for (var h in {});
						for (var i = 1 in {});
						for (var { j, x: [k] } in {});
						function l() {}
					}
				}
				x()
			)test"},
		},
		.entry_paths = {
			"/top-level.js",
			"/nested.js",
			"/let.js",
			"/function.js",
			"/function-nested.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestImportNamespaceThisValue) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/a.js", R"test(

				import def, * as ns from 'external'
				console.log(ns[foo](), new ns[foo]())
			)test"},
			{"/b.js", R"test(

				import def, * as ns from 'external'
				console.log(ns.foo(), new ns.foo())
			)test"},
			{"/c.js", R"test(

				import def, {foo} from 'external'
				console.log(def(), foo())
				console.log(new def(), new foo())
			)test"},
		},
		.entry_paths = {
			"/a.js",
			"/b.js",
			"/c.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"external", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestThisUndefinedWarningESM) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import x from './file1.js'
				import y from 'pkg/file2.js'
				console.log(x, y)
			)test"},
			{"/file1.js", R"test(

				export default [this, this]
			)test"},
			{"/node_modules/pkg/file2.js", R"test(

				export default [this, this]
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		.expected_scan_log = R"scan(
file1.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
file1.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
node_modules/pkg/file2.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
node_modules/pkg/file2.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
)scan",
		.debug_logs = true,
		
	});
}

TEST(BundlerDefault, TestQuotedProperty) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import * as ns from 'ext'
				console.log(ns.mustBeUnquoted, ns['mustBeQuoted'])
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"ext", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestQuotedPropertyMangle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import * as ns from 'ext'
				console.log(ns.mustBeUnquoted, ns['mustBeUnquoted2'])
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"ext", true},
					},
				},
			},
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestDuplicatePropertyWarning) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import './outside-node-modules'
				import 'inside-node-modules'
			)test"},
			{"/outside-node-modules/index.jsx", R"test(

				console.log({ a: 1, a: 2 }, <div a2 a2={3}/>)
			)test"},
			{"/outside-node-modules/package.json", R"test(

				{ "b": 1, "b": 2 }
			)test"},
			{"/node_modules/inside-node-modules/index.jsx", R"test(

				console.log({ c: 1, c: 2 }, <div c2 c2={3}/>)
			)test"},
			{"/node_modules/inside-node-modules/package.json", R"test(

				{ "d": 1, "d": 2 }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		.expected_scan_log = R"scan(
outside-node-modules/index.jsx: WARNING: Duplicate key "a" in object literal
outside-node-modules/index.jsx: NOTE: The original key "a" is here:
outside-node-modules/index.jsx: WARNING: Duplicate "a2" attribute in JSX element
outside-node-modules/index.jsx: NOTE: The original "a2" attribute is here:
outside-node-modules/package.json: WARNING: Duplicate key "b" in object literal
outside-node-modules/package.json: NOTE: The original key "b" is here:
)scan",
		
	});
}

TEST(BundlerDefault, TestRequireShimSubstitution) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log([
					require,
					typeof require,
					require('./example.json'),
					require('./example.json', { type: 'json' }),
					require(window.SOME_PATH),
					module.require('./example.json'),
					module.require('./example.json', { type: 'json' }),
					module.require(window.SOME_PATH),
					require.resolve('some-path'),
					require.resolve(window.SOME_PATH),
					import('some-path'),
					import(window.SOME_PATH),
				])
			)test"},
			{"/example.json", R"test({ "works": true })test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"some-path", true},
					},
				},
			},
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kDynamicImport,
			
		},
		
	});
}

TEST(BundlerDefault, TestStrictModeNestedFnDeclKeepNamesVariableInliningIssue1552) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export function outer() {
					{
						function inner() {
							return Math.random();
						}
						const x = inner();
						console.log(x);
					}
				}
				outer();
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
			.KeepNames = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestBuiltInNodeModulePrecedence) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log([
					// These are node core modules
					require('fs'),
					require('fs/promises'),
					require('node:foo'),

					// These are not node core modules
					require('fs/abc'),
					require('fs/'),
				])
			)test"},
			{"/node_modules/fs/abc.js", R"test(

				console.log('include this')
			)test"},
			{"/node_modules/fs/index.js", R"test(

				console.log('include this too')
			)test"},
			{"/node_modules/fs/promises.js", R"test(

				throw 'DO NOT INCLUDE THIS'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.OutputPlatform = guchho::config::Platform::kNode,
			.AbsOutputDir = "/out",
			
		},
		
	});
}




TEST(BundlerDefault, TestMinifyIdentifiersImportPathFrequencyAnalysis) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/import.js", R"test(

				import foo from "./WWWWWWWWWWXXXXXXXXXXYYYYYYYYYYZZZZZZZZZZ"
				console.log(foo, 'no identifier in this file should be named W, X, Y, or Z')
			)test"},
			{"/WWWWWWWWWWXXXXXXXXXXYYYYYYYYYYZZZZZZZZZZ.js", "export default 123"},
			{"/require.js", R"test(

				const foo = require("./AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD")
				console.log(foo, 'no identifier in this file should be named A, B, C, or D')
			)test"},
			{"/AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD.js", "module.exports = 123"},
		},
		.entry_paths = {
			"/import.js",
			"/require.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.MinifyWhitespace = true,
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestToESMWrapperOmission) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import 'a_nowrap'

				import { b } from 'b_nowrap'
				b()

				export * from 'c_nowrap'

				import * as d from 'd_WRAP'
				x = d.x

				import e from 'e_WRAP'
				e()

				import { default as f } from 'f_WRAP'
				f()

				import { __esModule as g } from 'g_WRAP'
				g()

				import * as h from 'h_WRAP'
				x = h

				import * as i from 'i_WRAP'
				i.x()

				import * as j from 'j_WRAP'
				j.x``

				x = import("k_WRAP")
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputDir = "/out",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kDynamicImport,
			
		},
		
	});
}

TEST(BundlerDefault, TestNamedFunctionExpressionArgumentCollision) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				let x = function foo(foo) {
					var foo;
					return foo;
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestNoWarnCommonJSExportsInESMPassThrough) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/cjs-in-esm.js", R"test(

				export let foo = 1
				exports.foo = 2
				module.exports = 3
			)test"},
			{"/import-in-cjs.js", R"test(

				import { foo } from 'bar'
				exports.foo = foo
				module.exports = foo
			)test"},
			{"/no-warnings-here.js", R"test(

				console.log(module, exports)
			)test"},
		},
		.entry_paths = {
			"/cjs-in-esm.js",
			"/import-in-cjs.js",
			"/no-warnings-here.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestWarnCommonJSExportsInESMConvert) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/cjs-in-esm.js", R"test(

				export let foo = 1
				exports.foo = 2
				module.exports = 3
			)test"},
			{"/cjs-in-esm2.js", R"test(

				export let foo = 1
				module.exports.bar = 3
			)test"},
			{"/import-in-cjs.js", R"test(

				import { foo } from 'bar'
				exports.foo = foo
				module.exports = foo
				module.exports.bar = foo
			)test"},
			{"/no-warnings-here.js", R"test(

				console.log(module, exports)
			)test"},
		},
		.entry_paths = {
			"/cjs-in-esm.js",
			"/cjs-in-esm2.js",
			"/import-in-cjs.js",
			"/no-warnings-here.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputDir = "/out",
			
		},
		.expected_scan_log = R"scan(
cjs-in-esm.js: WARNING: The CommonJS "exports" variable is treated as a global variable in an ECMAScript module and may not work as expected
cjs-in-esm.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
cjs-in-esm.js: WARNING: The CommonJS "module" variable is treated as a global variable in an ECMAScript module and may not work as expected
cjs-in-esm.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
cjs-in-esm2.js: WARNING: The CommonJS "module" variable is treated as a global variable in an ECMAScript module and may not work as expected
cjs-in-esm2.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
)scan",
		
	});
}

TEST(BundlerDefault, TestWarnCommonJSExportsInESMBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/cjs-in-esm.js", R"test(

				export let foo = 1
				exports.foo = 2
				module.exports = 3
			)test"},
			{"/import-in-cjs.js", R"test(

				import { foo } from 'bar'
				exports.foo = foo
				module.exports = foo
			)test"},
			{"/no-warnings-here.js", R"test(

				console.log(module, exports)
			)test"},
		},
		.entry_paths = {
			"/cjs-in-esm.js",
			"/import-in-cjs.js",
			"/no-warnings-here.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"bar", true},
					},
				},
			},
			
		},
		.expected_scan_log = R"scan(
cjs-in-esm.js: WARNING: The CommonJS "exports" variable is treated as a global variable in an ECMAScript module and may not work as expected
cjs-in-esm.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
cjs-in-esm.js: WARNING: The CommonJS "module" variable is treated as a global variable in an ECMAScript module and may not work as expected
cjs-in-esm.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
)scan",
		
	});
}

TEST(BundlerDefault, TestMangleProps) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry1.js", R"test(

				export function shouldMangle() {
					let foo = {
						bar_: 0,
						baz_() {},
					};
					let { bar_ } = foo;
					({ bar_ } = foo);
					class foo_ {
						bar_ = 0
						baz_() {}
						static bar_ = 0
						static baz_() {}
					}
					return { bar_, foo_ }
				}

				export function shouldNotMangle() {
					let foo = {
						'bar_': 0,
						'baz_'() {},
					};
					let { 'bar_': bar_ } = foo;
					({ 'bar_': bar_ } = foo);
					class foo_ {
						'bar_' = 0
						'baz_'() {}
						static 'bar_' = 0
						static 'baz_'() {}
					}
					return { 'bar_': bar_, 'foo_': foo_ }
				}
			)test"},
			{"/entry2.js", R"test(

				export default {
					bar_: 0,
					'baz_': 1,
				}
			)test"},
		},
		.entry_paths = {
			"/entry1.js",
			"/entry2.js",
		},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			
		},
		
	});
}



TEST(BundlerDefault, TestManglePropsKeywordPropertyMinify) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class Foo {
					static bar = { get baz() { return 123 } }
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("."),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MinifyWhitespace = true,
			.MinifyIdentifiers = true,
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsOptionalChain) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export default function(x) {
					x.foo_;
					x.foo_?.();
					x?.foo_;
					x?.foo_();
					x?.foo_.bar_;
					x?.foo_.bar_();
					x?.['foo_'].bar_;
					x?.foo_['bar_'];
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsLoweredOptionalChain) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export default function(x) {
					x.foo_;
					x.foo_?.();
					x?.foo_;
					x?.foo_();
					x?.foo_.bar_;
					x?.foo_.bar_();
					x?.['foo_'].bar_;
					x?.foo_['bar_'];
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kOptionalChain,
			
		},
		
	});
}

TEST(BundlerDefault, TestReserveProps) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export default {
					foo_: 0,
					_bar_: 1,
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.ReserveProps = std::make_shared<std::regex>("^_.*_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsImportExport) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/esm.js", R"test(

				export let foo_ = 123
				import { bar_ } from 'xyz'
			)test"},
			{"/cjs.js", R"test(

				exports.foo_ = 123
				let bar_ = require('xyz').bar_
			)test"},
		},
		.entry_paths = {
			"/esm.js",
			"/cjs.js",
		},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			
		},
		
	});
}



TEST(BundlerDefault, TestManglePropsJSXTransform) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				let Foo = {
					Bar_(props) {
						return <>{props.text_}</>
					},
					hello_: 'hello, world',
					createElement_(...args) {
						console.log('createElement', ...args)
					},
					Fragment_(...args) {
						console.log('Fragment', ...args)
					},
				}
				export default <Foo.Bar_ text_={Foo.hello_}></Foo.Bar_>
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.JSX = guchho::config::JSXOptions{
				.Factory = {.Parts = {"Foo", "createElement_"}},
				.Fragment = {.Parts = {"Foo", "Fragment_"}},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsJSXPreserve) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				let Foo = {
					Bar_(props) {
						return <>{props.text_}</>
					},
					hello_: 'hello, world',
				}
				export default <Foo.Bar_ text_={Foo.hello_}></Foo.Bar_>
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.jsx",
			.JSX = guchho::config::JSXOptions{
				.Preserve = true,
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsJSXTransformNamespace) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				export default [
					<KEEP_THIS_ />,
					<KEEP:THIS_ />,
					<foo KEEP:THIS_ />,
				]
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsAvoidCollisions) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export default {
					foo_: 0, // Must not be named "a"
					bar_: 1, // Must not be named "b"
					a: 2,
					b: 3,
					__proto__: {}, // Always avoid mangling this
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsTypeScriptFeatures) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/parameter-properties.ts", R"test(

				class Foo {
					constructor(
						public KEEP_FIELD: number,
						public MANGLE_FIELD_: number,
					) {
					}
				}

				let foo = new Foo
				console.log(foo.KEEP_FIELD, foo.MANGLE_FIELD_)
			)test"},
			{"/namespace-exports.ts", R"test(

				namespace ns {
					export var MANGLE_VAR_ = 1
					export let MANGLE_LET_ = 2
					export const MANGLE_CONST_ = 3
					export let { NESTED_: { DESTRUCTURING_ } } = 4
					export function MANGLE_FUNCTION_() {}
					export class MANGLE_CLASS_ {}
					export namespace MANGLE_NAMESPACE_ { ; }
					export enum MANGLE_ENUM_ {}

					console.log({
						VAR: MANGLE_VAR_,
						LET: MANGLE_LET_,
						CONST: MANGLE_CONST_,
						DESTRUCTURING: DESTRUCTURING_,
						FUNCTION: MANGLE_FUNCTION_,
						CLASS: MANGLE_CLASS_,
						NAMESPACE: MANGLE_NAMESPACE_,
						ENUM: MANGLE_ENUM_,
					})
				}

				console.log({
					VAR: ns.MANGLE_VAR_,
					LET: ns.MANGLE_LET_,
					CONST: ns.MANGLE_CONST_,
					DESTRUCTURING: ns.DESTRUCTURING_,
					FUNCTION: ns.MANGLE_FUNCTION_,
					CLASS: ns.MANGLE_CLASS_,
					NAMESPACE: ns.MANGLE_NAMESPACE_,
					ENUM: ns.MANGLE_ENUM_,
				})

				namespace ns {
					console.log({
						VAR: MANGLE_VAR_,
						LET: MANGLE_LET_,
						CONST: MANGLE_CONST_,
						DESTRUCTURING: DESTRUCTURING_,
						FUNCTION: MANGLE_FUNCTION_,
						CLASS: MANGLE_CLASS_,
						NAMESPACE: MANGLE_NAMESPACE_,
						ENUM: MANGLE_ENUM_,
					})
				}
			)test"},
			{"/enum-values.ts", R"test(

				enum TopLevelNumber { foo_ = 0 }
				enum TopLevelString { bar_ = '' }
				console.log({
					foo: TopLevelNumber.foo_,
					bar: TopLevelString.bar_,
				})

				function fn() {
					enum NestedNumber { foo_ = 0 }
					enum NestedString { bar_ = '' }
					console.log({
						foo: TopLevelNumber.foo_,
						bar: TopLevelString.bar_,
					})
				}
			)test"},
		},
		.entry_paths = {
			"/parameter-properties.ts",
			"/namespace-exports.ts",
			"/enum-values.ts",
		},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsShorthand) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// This should print as "({ y }) => ({ y })" not "({ y: y }) => ({ y: y })"
				export let yyyyy = ({ xxxxx }) => ({ xxxxx })
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("x"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsNoShorthand) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				// This should print as "({ y }) => ({ y: y })" not "({ y: y }) => ({ y: y })"
				export let yyyyy = ({ xxxxx }) => ({ xxxxx })
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("x"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kObjectExtensions,
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsLoweredClassFields) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class Foo {
					foo_ = 123
					static bar_ = 234
				}
				Foo.bar_ = new Foo().foo_
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_$"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kClassField | guchho::compat::JSFeature::kClassStaticField,
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsSuperCall) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				class Foo {}
				class Bar extends Foo {
					constructor() {
						super();
					}
				}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("."),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

TEST(BundlerDefault, TestMangleNoQuotedProps) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				x['_doNotMangleThis'];
				x?.['_doNotMangleThis'];
				x[y ? '_doNotMangleThis' : z];
				x?.[y ? '_doNotMangleThis' : z];
				x[y ? z : '_doNotMangleThis'];
				x?.[y ? z : '_doNotMangleThis'];
				({ '_doNotMangleThis': x });
				(class { '_doNotMangleThis' = x });
				var { '_doNotMangleThis': x } = y;
				'_doNotMangleThis' in x;
				(y ? '_doNotMangleThis' : z) in x;
				(y ? z : '_doNotMangleThis') in x;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MangleQuoted = false,
			
		},
		
	});
}

TEST(BundlerDefault, TestMangleNoQuotedPropsMinifySyntax) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				x['_doNotMangleThis'];
				x?.['_doNotMangleThis'];
				x[y ? '_doNotMangleThis' : z];
				x?.[y ? '_doNotMangleThis' : z];
				x[y ? z : '_doNotMangleThis'];
				x?.[y ? z : '_doNotMangleThis'];
				({ '_doNotMangleThis': x });
				(class { '_doNotMangleThis' = x });
				var { '_doNotMangleThis': x } = y;
				'_doNotMangleThis' in x;
				(y ? '_doNotMangleThis' : z) in x;
				(y ? z : '_doNotMangleThis') in x;
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
			.MangleQuoted = false,
			
		},
		
	});
}

TEST(BundlerDefault, TestMangleQuotedProps) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/keep.js", R"test(

				foo("_keepThisProperty");
				foo((x, "_keepThisProperty"));
				foo(x ? "_keepThisProperty" : "_keepThisPropertyToo");
				x[foo("_keepThisProperty")];
				x?.[foo("_keepThisProperty")];
				({ [foo("_keepThisProperty")]: x });
				(class { [foo("_keepThisProperty")] = x });
				var { [foo("_keepThisProperty")]: x } = y;
				foo("_keepThisProperty") in x;
			)test"},
			{"/mangle.js", R"test(

				x['_mangleThis'];
				x?.['_mangleThis'];
				x[y ? '_mangleThis' : z];
				x?.[y ? '_mangleThis' : z];
				x[y ? z : '_mangleThis'];
				x?.[y ? z : '_mangleThis'];
				x[y, '_mangleThis'];
				x?.[y, '_mangleThis'];
				({ '_mangleThis': x });
				({ ['_mangleThis']: x });
				({ [(y, '_mangleThis')]: x });
				(class { '_mangleThis' = x });
				(class { ['_mangleThis'] = x });
				(class { [(y, '_mangleThis')] = x });
				var { '_mangleThis': x } = y;
				var { ['_mangleThis']: x } = y;
				var { [(z, '_mangleThis')]: x } = y;
				'_mangleThis' in x;
				(y ? '_mangleThis' : z) in x;
				(y ? z : '_mangleThis') in x;
				(y, '_mangleThis') in x;
			)test"},
		},
		.entry_paths = {
			"/keep.js",
			"/mangle.js",
		},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MangleQuoted = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestMangleQuotedPropsMinifySyntax) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/keep.js", R"test(

				foo("_keepThisProperty");
				foo((x, "_keepThisProperty"));
				foo(x ? "_keepThisProperty" : "_keepThisPropertyToo");
				x[foo("_keepThisProperty")];
				x?.[foo("_keepThisProperty")];
				({ [foo("_keepThisProperty")]: x });
				(class { [foo("_keepThisProperty")] = x });
				var { [foo("_keepThisProperty")]: x } = y;
				foo("_keepThisProperty") in x;
			)test"},
			{"/mangle.js", R"test(

				x['_mangleThis'];
				x?.['_mangleThis'];
				x[y ? '_mangleThis' : z];
				x?.[y ? '_mangleThis' : z];
				x[y ? z : '_mangleThis'];
				x?.[y ? z : '_mangleThis'];
				x[y, '_mangleThis'];
				x?.[y, '_mangleThis'];
				({ '_mangleThis': x });
				({ ['_mangleThis']: x });
				({ [(y, '_mangleThis')]: x });
				(class { '_mangleThis' = x });
				(class { ['_mangleThis'] = x });
				(class { [(y, '_mangleThis')] = x });
				var { '_mangleThis': x } = y;
				var { ['_mangleThis']: x } = y;
				var { [(z, '_mangleThis')]: x } = y;
				'_mangleThis' in x;
				(y ? '_mangleThis' : z) in x;
				(y ? z : '_mangleThis') in x;
				(y, '_mangleThis') in x;
			)test"},
		},
		.entry_paths = {
			"/keep.js",
			"/mangle.js",
		},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
			.MangleQuoted = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestPreserveKeyComment) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				x(/* __KEY__ */ 'notKey', /* __KEY__ */ `notKey`)
				x(/* @__KEY__ */ 'key', /* @__KEY__ */ `key`)
				x(/* #__KEY__ */ 'alsoKey', /* #__KEY__ */ `alsoKey`)
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsKeyComment) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				x(/* __KEY__ */ '_doNotMangleThis', /* __KEY__ */ `_doNotMangleThis`)
				x._mangleThis(/* @__KEY__ */ '_mangleThis', /* @__KEY__ */ `_mangleThis`)
				x._mangleThisToo(/* #__KEY__ */ '_mangleThisToo', /* #__KEY__ */ `_mangleThisToo`)
				x._someKey = /* #__KEY__ */ '_someKey' in y
				x([
					`foo.${/* @__KEY__ */ '_mangleThis'} = bar.${/* @__KEY__ */ '_mangleThisToo'}`,
					`foo.${/* @__KEY__ */ 'notMangled'} = bar.${/* @__KEY__ */ 'notMangledEither'}`,
				])
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestManglePropsKeyCommentMinify) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				x = class {
					_mangleThis = 1;
					[/* @__KEY__ */ '_mangleThisToo'] = 2;
					'_doNotMangleThis' = 3;
				}
				x = {
					_mangleThis: 1,
					[/* @__KEY__ */ '_mangleThisToo']: 2,
					'_doNotMangleThis': 3,
				}
				x._mangleThis = 1
				x[/* @__KEY__ */ '_mangleThisToo'] = 2
				x['_doNotMangleThis'] = 3
				x([
					`${foo}.${/* @__KEY__ */ '_mangleThis'} = bar.${/* @__KEY__ */ '_mangleThisToo'}`,
					`${foo}.${/* @__KEY__ */ 'notMangled'} = bar.${/* @__KEY__ */ 'notMangledEither'}`,
				])
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.MangleProps = std::make_shared<std::regex>("_"),
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestIndirectRequireMessage) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/array.js", "let x = [require]"},
			{"/assign.js", "require = x"},
			{"/ident.js", "let x = require"},
			{"/dot.js", "let x = require.cache"},
			{"/index.js", "let x = require[cache]"},
		},
		.entry_paths = {
			"/array.js",
			"/assign.js",
			"/dot.js",
			"/ident.js",
			"/index.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		.expected_scan_log = R"scan(
array.js: DEBUG: Indirect calls to "require" will not be bundled
assign.js: DEBUG: Indirect calls to "require" will not be bundled
ident.js: DEBUG: Indirect calls to "require" will not be bundled
)scan",
		.debug_logs = true,
		
	});
}

TEST(BundlerDefault, TestAmbiguousReexportMsg) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				export * from './a'
				export * from './b'
				export * from './c'
			)test"},
			{"/a.js", "export let a = 1, x = 2"},
			{"/b.js", "export let b = 3; export { b as x }"},
			{"/c.js", "export let c = 4, x = 5"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		.expected_compile_log = R"compile(
DEBUG: Re-export of "x" in "entry.js" is ambiguous and has been removed
a.js: NOTE: One definition of "x" comes from "a.js" here:
b.js: NOTE: Another definition of "x" comes from "b.js" here:
)compile",
		.debug_logs = true,
		
	});
}

TEST(BundlerDefault, TestNonDeterminismIssue2537) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.ts", R"test(

				export function aap(noot: boolean, wim: number) {
					let mies = "teun"
					if (noot) {
						function vuur(v: number) {
							return v * 2
						}
						function schaap(s: number) {
							return s / 2
						}
						mies = vuur(wim) + schaap(wim)
					}
					return mies
				}
			)test"},
			{"/tsconfig.json", R"test(

				{
					"compilerOptions": {
						"alwaysStrict": true
					}
				}
			)test"},
		},
		.entry_paths = {"/entry.ts"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifyIdentifiers = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestMinifiedJSXPreserveWithObjectSpread) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				const obj = {
					before,
					...{ [key]: value },
					...{ key: value },
					after,
				};
				<Foo
					before
					{...{ [key]: value }}
					{...{ key: value }}
					after
				/>;
				<Bar
					{...{
						a,
						[b]: c,
						...d,
						e,
					}}
				/>;
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.MinifySyntax = true,
			.JSX = guchho::config::JSXOptions{
				.Preserve = true,
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestPackageAlias) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import "pkg1"
				import "pkg2/foo"
				import "./nested3"
				import "@scope/pkg4"
				import "@scope/pkg5/foo"
				import "@abs-path/pkg6"
				import "@abs-path/pkg7/foo"
				import "@scope-only/pkg8"
				import "slash/"
				import "prefix-foo"
				import "@scope/prefix-foo"
			)test"},
			{"/nested3/index.js", R"test(import "pkg3")test"},
			{"/nested3/node_modules/alias3/index.js", "test failure"},
			{"/node_modules/alias1/index.js", "console.log(1)"},
			{"/node_modules/alias2/foo.js", "console.log(2)"},
			{"/node_modules/alias3/index.js", "console.log(3)"},
			{"/node_modules/alias4/index.js", "console.log(4)"},
			{"/node_modules/alias5/foo.js", "console.log(5)"},
			{"/alias6/dir/index.js", "console.log(6)"},
			{"/alias7/dir/foo/index.js", "console.log(7)"},
			{"/alias8/dir/pkg8/index.js", "console.log(8)"},
			{"/alias9/some/file.js", "console.log(9)"},
			{"/node_modules/prefix-foo/index.js", "console.log(10)"},
			{"/node_modules/@scope/prefix-foo/index.js", "console.log(11)"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.PackageAliases = {
				{"pkg1", "alias1"},
				{"pkg2", "alias2"},
				{"pkg3", "alias3"},
				{"@scope/pkg4", "alias4"},
				{"@scope/pkg5", "alias5"},
				{"@abs-path/pkg6", "/alias6/dir"},
				{"@abs-path/pkg7", "/alias7/dir"},
				{"@scope-only", "/alias8/dir"},
				{"slash", "/alias9/some/file.js"},
				{"prefix", "alias10"},
				{"@scope/prefix", "alias11"},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestPackageAliasMatchLongest) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import "pkg"
				import "pkg/foo"
				import "pkg/foo/bar"
				import "pkg/foo/bar/baz"
				import "pkg/bar/baz"
				import "pkg/baz"
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",

			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Patterns = {
						{"alias/", ""},
					},
				},
			},
			.PackageAliases = {
				{"pkg", "alias/pkg"},
				{"pkg/foo", "alias/pkg_foo"},
				{"pkg/foo/bar", "alias/pkg_foo_bar"},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestErrorsForAssertTypeJSON) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/js-entry.js", R"test(

				import all from './foo.json' assert { type: 'json' }
				import { default as def } from './foo.json' assert { type: 'json' }
				import { unused } from './foo.json' assert { type: 'json' }
				import { used } from './foo.json' assert { type: 'json' }
				import * as ns from './foo.json' assert { type: 'json' }
				use(used, ns.prop)
				export { exported } from './foo.json' assert { type: 'json' }
				export { default as def2 } from './foo.json' assert { type: 'json' }
				export { def3 as default } from './foo.json' assert { type: 'json' }
				import text from './foo.text' assert { type: 'json' }
				import file from './foo.file' assert { type: 'json' }
				import copy from './foo.copy' assert { type: 'json' }
			)test"},
			{"/ts-entry.ts", R"test(

				import all from './foo.json' assert { type: 'json' }
				import { default as def } from './foo.json' assert { type: 'json' }
				import { unused } from './foo.json' assert { type: 'json' }
				import { used } from './foo.json' assert { type: 'json' }
				import * as ns from './foo.json' assert { type: 'json' }
				use(used, ns.prop)
				export { exported } from './foo.json' assert { type: 'json' }
				export { default as def2 } from './foo.json' assert { type: 'json' }
				export { def3 as default } from './foo.json' assert { type: 'json' }
				import text from './foo.text' assert { type: 'json' }
				import file from './foo.file' assert { type: 'json' }
				import copy from './foo.copy' assert { type: 'json' }
			)test"},
			{"/foo.json", "{}"},
			{"/foo.text", "{}"},
			{"/foo.file", "{}"},
			{"/foo.copy", "{}"},
		},
		.entry_paths = {
			"/js-entry.js",
			"/ts-entry.ts",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".ts", guchho::config::Loader::kTS},
				{".json", guchho::config::Loader::kJSON},
				{".text", guchho::config::Loader::kText},
				{".file", guchho::config::Loader::kFile},
				{".copy", guchho::config::Loader::kCopy},
			},
			
		},
		.expected_scan_log = R"scan(
js-entry.js: ERROR: Cannot use non-default import "unused" with a JSON import assertion
js-entry.js: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "unused" import.
js-entry.js: ERROR: Cannot use non-default import "used" with a JSON import assertion
js-entry.js: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "used" import.
js-entry.js: WARNING: Non-default import "prop" is undefined with a JSON import assertion
js-entry.js: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "prop" import.
js-entry.js: ERROR: Cannot use non-default import "exported" with a JSON import assertion
js-entry.js: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "exported" import.
js-entry.js: ERROR: Cannot use non-default import "def3" with a JSON import assertion
js-entry.js: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "def3" import.
js-entry.js: ERROR: The file "foo.text" was loaded with the "text" loader
js-entry.js: NOTE: This import assertion requires the loader to be "json" instead:
NOTE: Either reconfigure Guchho to use the "json" loader for this file, or remove this import assertion.
js-entry.js: ERROR: The file "foo.file" was loaded with the "file" loader
js-entry.js: NOTE: This import assertion requires the loader to be "json" instead:
NOTE: Either reconfigure Guchho to use the "json" loader for this file, or remove this import assertion.
ts-entry.ts: ERROR: Cannot use non-default import "used" with a JSON import assertion
ts-entry.ts: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "used" import.
ts-entry.ts: WARNING: Non-default import "prop" is undefined with a JSON import assertion
ts-entry.ts: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "prop" import.
ts-entry.ts: ERROR: Cannot use non-default import "exported" with a JSON import assertion
ts-entry.ts: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "exported" import.
ts-entry.ts: ERROR: Cannot use non-default import "def3" with a JSON import assertion
ts-entry.ts: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "def3" import.
)scan",
		
	});
}

TEST(BundlerDefault, TestOutputForAssertTypeJSON) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/js-entry.js", R"test(

				import all from './foo.json' assert { type: 'json' }
				import copy from './foo.copy' assert { type: 'json' }
				import { default as def } from './foo.json' assert { type: 'json' }
				import * as ns from './foo.json' assert { type: 'json' }
				use(all, copy, def, ns.prop)
				export { default } from './foo.json' assert { type: 'json' }
			)test"},
			{"/ts-entry.ts", R"test(

				import all from './foo.json' assert { type: 'json' }
				import copy from './foo.copy' assert { type: 'json' }
				import { default as def } from './foo.json' assert { type: 'json' }
				import { unused } from './foo.json' assert { type: 'json' }
				import * as ns from './foo.json' assert { type: 'json' }
				use(all, copy, def, ns.prop)
				export { default } from './foo.json' assert { type: 'json' }
			)test"},
			{"/foo.json", "{}"},
			{"/foo.copy", "{}"},
		},
		.entry_paths = {
			"/js-entry.js",
			"/ts-entry.ts",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".ts", guchho::config::Loader::kTS},
				{".json", guchho::config::Loader::kJSON},
				{".copy", guchho::config::Loader::kCopy},
			},
			
		},
		.expected_scan_log = R"scan(
js-entry.js: WARNING: Non-default import "prop" is undefined with a JSON import assertion
js-entry.js: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "prop" import.
ts-entry.ts: WARNING: Non-default import "prop" is undefined with a JSON import assertion
ts-entry.ts: NOTE: The JSON import assertion is here:
NOTE: You can either keep the import assertion and only use the "default" import, or you can remove the import assertion and use the "prop" import.
)scan",
		
	});
}

TEST(BundlerDefault, TestExternalPackages) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(

				import 'pkg1'
				import './file'
				import './node_modules/pkg2/index.js'
				import '#pkg3'
			)test"},
			{"/project/package.json", R"test(
{
				"imports": {
					"#pkg3": "./libs/pkg3.js"
				}
			})test"},
			{"/project/file.js", R"test(

				console.log('file')
			)test"},
			{"/project/node_modules/pkg2/index.js", R"test(

				console.log('pkg2')
			)test"},
			{"/project/libs/pkg3.js", R"test(

				console.log('pkg3')
			)test"},
		},
		.entry_paths = {"/project/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalPackages = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestMetafileVariousCases) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(
				import a from 'extern-esm'
				import b from './esm'
				import c from 'data:application/json,2'
				import d from './file.file'
				import e from './copy.copy'
				console.log(
					a,
					b,
					c,
					d,
					e,
					require('extern-cjs'),
					require('./cjs'),
					import('./dynamic'),
				)
				export let exported
			)test"},
			{"/project/entry.css", R"test(
				@import "extern.css";
				a { background: url(inline.svg) }
				b { background: url(file.file) }
				c { background: url(copy.copy) }
				d { background: url(extern.png) }
			)test"},
			{"/project/esm.js", "export default 1"},
			{"/project/cjs.js", "module.exports = 4"},
			{"/project/dynamic.js", "export default 5"},
			{"/project/file.file", "file"},
			{"/project/copy.copy", "copy"},
			{"/project/inline.svg", "<svg/>"},
		},
		.entry_paths = {
			"/project/entry.js",
			"/project/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.NeedsMetafile = true,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"extern-esm", true},
						{"extern-cjs", true},
						{"extern.css", true},
						{"extern.png", true},
					},
				},
			},
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".css", guchho::config::Loader::kCSS},
				{".file", guchho::config::Loader::kFile},
				{".copy", guchho::config::Loader::kCopy},
				{".svg", guchho::config::Loader::kDataURL},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestMetafileNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(
				import a from 'pkg'
				import b from './file'
				console.log(
					a,
					b,
					require('pkg2'),
					require('./file2'),
					import('./dynamic'),
				)
				export let exported
			)test"},
			{"/project/entry.css", R"test(
				@import "pkg";
				@import "./file";
				a { background: url(pkg2) }
				a { background: url(./file2) }
			)test"},
		},
		.entry_paths = {
			"/project/entry.js",
			"/project/entry.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.NeedsMetafile = true,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestMetafileVeryLongExternalPaths) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/bytesInOutput should be at least 99 (1).js", R"test(
				import a from './111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111.file'
				console.log(a)
			)test"},
			{"/project/bytesInOutput should be at least 99 (2).js", R"test(
				import a from './222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222.copy'
				console.log(a)
			)test"},
			{"/project/bytesInOutput should be at least 99 (3).js", R"test(
				import('./333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333.js').then(console.log)
			)test"},
			{"/project/bytesInOutput should be at least 99.css", R"test(
				a { background: url(444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444.file) }
			)test"},
			{"/project/111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111.file", ""},
			{"/project/222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222.copy", ""},
			{"/project/333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333.js", ""},
			{"/project/444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444.file", ""},
		},
		.entry_paths = {
			"/project/bytesInOutput should be at least 99 (1).js",
			"/project/bytesInOutput should be at least 99 (2).js",
			"/project/bytesInOutput should be at least 99 (3).js",
			"/project/bytesInOutput should be at least 99.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.CodeSplitting = true,
			.NeedsMetafile = true,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".js", guchho::config::Loader::kJS},
				{".css", guchho::config::Loader::kCSS},
				{".file", guchho::config::Loader::kFile},
				{".copy", guchho::config::Loader::kCopy},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestMetafileImportWithTypeJSON) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/entry.js", R"test(
				import a from './data.json'
				import b from './data.json' assert { type: 'json' }
				import c from './data.json' with { type: 'json' }
				x = [a, b, c]
			)test"},
			{"/project/data.json", R"test({"some": "data"})test"},
		},
		.entry_paths = {"/project/entry.js"},
		.options = guchho::config::Options{
			
			.BuildMode = guchho::config::Mode::kBundle,
			.NeedsMetafile = true,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestCommentPreservation) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				console.log(
					import(/* before */ foo),
					import(/* before */ 'foo'),
					import(foo /* after */),
					import('foo' /* after */),
				)

				console.log(
					import('foo', /* before */ { assert: { type: 'json' } }),
					import('foo', { /* before */ assert: { type: 'json' } }),
					import('foo', { assert: /* before */ { type: 'json' } }),
					import('foo', { assert: { /* before */ type: 'json' } }),
					import('foo', { assert: { type: /* before */ 'json' } }),
					import('foo', { assert: { type: 'json' /* before */ } }),
					import('foo', { assert: { type: 'json' } /* before */ }),
					import('foo', { assert: { type: 'json' } } /* before */),
				)

				console.log(
					require(/* before */ foo),
					require(/* before */ 'foo'),
					require(foo /* after */),
					require('foo' /* after */),
				)

				console.log(
					require.resolve(/* before */ foo),
					require.resolve(/* before */ 'foo'),
					require.resolve(foo /* after */),
					require.resolve('foo' /* after */),
				)

				let [/* foo */] = [/* bar */];
				let [
					// foo
				] = [
					// bar
				];
				let [/*before*/ ...s] = [/*before*/ ...s]
				let [... /*before*/ s2] = [... /*before*/ s2]

				let { /* foo */ } = { /* bar */ };
				let {
					// foo
				} = {
					// bar
				};
				let { /*before*/ ...s3 } = { /*before*/ ...s3 }
				let { ... /*before*/ s4 } = { ... /*before*/ s4 }

				let [/* before */ x] = [/* before */ x];
				let [/* before */ x2 /* after */] = [/* before */ x2 /* after */];
				let [
					// before
					x3
					// after
				] = [
					// before
					x3
					// after
				];

				let { /* before */ y } = { /* before */ y };
				let { /* before */ y2 /* after */ } = { /* before */ y2 /* after */ };
				let {
					// before
					y3
					// after
				} = {
					// before
					y3
					// after
				};
				let { /* before */ [y4]: y4 } = { /* before */ [y4]: y4 };
				let { [/* before */ y5]: y5 } = { [/* before */ y5]: y5 };
				let { [y6 /* after */]: y6 } = { [y6 /* after */]: y6 };

				foo[/* before */ x] = foo[/* before */ x]
				foo[x /* after */] = foo[x /* after */]

				console.log(
					// before
					foo,
					/* comment before */
					bar,
					// comment after
				)

				console.log([
					// before
					foo,
					/* comment before */
					bar,
					// comment after
				])

				console.log({
					// before
					foo,
					/* comment before */
					bar,
					// comment after
				})

				console.log(class {
					// before
					foo
					/* comment before */
					bar
					// comment after
				})

				console.log(
					() => { return /* foo */ null },
					() => { throw /* foo */ null },
					() => { return (/* foo */ null) + 1 },
					() => { throw (/* foo */ null) + 1 },
					() => {
						return (// foo
							null) + 1
					},
					() => {
						throw (// foo
							null) + 1
					},
				)

				console.log(
					/*a*/ a ? /*b*/ b : /*c*/ c,
					a /*a*/ ? b /*b*/ : c /*c*/,
				)

				for (/*foo*/a;;);
				for (;/*foo*/a;);
				for (;;/*foo*/a);

				for (/*foo*/a in b);
				for (a in /*foo*/b);

				for (/*foo*/a of b);
				for (a of /*foo*/b);

				if (/*foo*/a);
				with (/*foo*/a);
				while (/*foo*/a);
				do {} while (/*foo*/a);
				switch (/*foo*/a) {}
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestCommentPreservationImportAssertions) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				import 'foo' /* before */ assert { type: 'json' }
				import 'foo' assert /* before */ { type: 'json' }
				import 'foo' assert { /* before */ type: 'json' }
				import 'foo' assert { type: /* before */ 'json' }
				import 'foo' assert { type: 'json' /* before */ }
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"foo", true},
					},
				},
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestCommentPreservationTransformJSX) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(

				console.log(
					<div x={/*before*/x} />,
					<div x={/*before*/'y'} />,
					<div x={/*before*/true} />,
					<div {/*before*/...x} />,
					<div>{/*before*/x}</div>,
					<>{/*before*/x}</>,

					// Comments on absent AST nodes
					<div>before{}after</div>,
					<div>before{/* comment 1 *//* comment 2 */}after</div>,
					<div>before{
						// comment 1
						// comment 2
					}after</div>,
					<>before{}after</>,
					<>before{/* comment 1 *//* comment 2 */}after</>,
					<>before{
						// comment 1
						// comment 2
					}after</>,
				)
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestCommentPreservationPreserveJSX) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.jsx", R"test(
				console.log(
					<div x={/*before*/x} />,
					<div x={/*before*/'y'} />,
					<div x={/*before*/true} />,
					<div {/*before*/...x} />,
					<div>{/*before*/x}</div>,
					<>{/*before*/x}</>,

					// Comments on absent AST nodes
					<div>before{}after</div>,
					<div>before{/* comment 1 *//* comment 2 */}after</div>,
					<div>before{
						// comment 1
						// comment 2
					}after</div>,
					<>before{}after</>,
					<>before{/* comment 1 *//* comment 2 */}after</>,
					<>before{
						// comment 1
						// comment 2
					}after</>,
				)
			)test"},
		},
		.entry_paths = {"/entry.jsx"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.JSX = guchho::config::JSXOptions{.Preserve = true},
			
		},
		
	});
}

TEST(BundlerDefault, TestErrorMessageCrashStdinIssue2913) {
	guchho::config::StdinInfo stdin_info{
		.Contents = "import \"node_modules/fflate\"",
		.AbsResolveDir = "/project",
	};
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/project/node_modules/fflate/package.json", R"test({ "main": "main.js" })test"},
			{"/project/node_modules/fflate/main.js", ""},
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputPlatform = guchho::config::Platform::kNeutral,
			.AbsOutputDir = "/out",
			.Stdin = &stdin_info,
		},
		.expected_scan_log = R"scan(
<stdin>: ERROR: Could not resolve "node_modules/fflate"
NOTE: You can mark the path "node_modules/fflate" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.
)scan",
		
	});
}

TEST(BundlerDefault, TestLineLimitNotMinified) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/script.jsx", R"test(

				import fileURL from './x.file'
				import copyURL from './x.copy'
				import dataURL from './x.data'
				export const SignUpForm = (props) => {
					return <p class="signup">
						<label>Username: <input class="username" type="text"/></label>
						<label>Password: <input class="password" type="password"/></label>
						<div class="primary disabled">
							{props.buttonText}
						</div>
						<small>By signing up, you are agreeing to our <a href="/tos/">terms of service</a>.</small>
						<img src={fileURL} />
						<img src={copyURL} />
						<img src={dataURL} />
					</p>
				}
			)test"},
			{"/style.css", R"test(
				body.light-mode.new-user-segment:not(.logged-in) .signup,
				body.light-mode.new-user-segment:not(.logged-in) .login {
					font: 10px/12px 'Font 1', 'Font 2', 'Font 3', 'Font 4', sans-serif;
					user-select: none;
					color: var(--fg, rgba(11, 22, 33, 0.5));
					background: url(data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iMjAwIiBoZWlnaHQ9IjIwMCIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KICA8Y2lyY2xlIGN4PSIxMDAiIGN5PSIxMDAiIHI9IjEwMCIgZmlsbD0iI0ZGQ0YwMCIvPgogIDxwYXRoIGQ9Ik00Ny41IDUyLjVMOTUgMTAwbC00Ny41IDQ3LjVtNjAtOTVMMTU1IDEwMGwtNDcuNSA0Ny41IiBmaWxsPSJub25lIiBzdHJva2U9IiMxOTE5MTkiIHN0cm9rZS13aWR0aD0iMjQiLz4KPC9zdmc+Cg==);
					cursor: url(x.file);
					cursor: url(x.copy);
					cursor: url(x.data);
				}
			)test"},
			{"/x.file", "...file..."},
			{"/x.copy", "...copy..."},
			{"/x.data", "...lots of long data...lots of long data..."},
		},
		.entry_paths = {
			"/script.jsx",
			"/style.css",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExtensionToLoader = {
				{".jsx", guchho::config::Loader::kJSX},
				{".css", guchho::config::Loader::kCSS},
				{".file", guchho::config::Loader::kFile},
				{".copy", guchho::config::Loader::kCopy},
				{".data", guchho::config::Loader::kDataURL},
			},
			.LineLimit = 32,

			
		},
		
	});
}

TEST(BundlerDefault, TestLineLimitMinified) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/script.jsx", R"test(

				export const SignUpForm = (props) => {
					return <p class="signup">
						<label>Username: <input class="username" type="text"/></label>
						<label>Password: <input class="password" type="password"/></label>
						<div class="primary disabled">
							{props.buttonText}
						</div>
						<small>By signing up, you are agreeing to our <a href="/tos/">terms of service</a>.</small>
					</p>
				}
			)test"},
			{"/style.css", R"test(
				body.light-mode.new-user-segment:not(.logged-in) .signup,
				body.light-mode.new-user-segment:not(.logged-in) .login {
					font: 10px/12px 'Font 1', 'Font 2', 'Font 3', 'Font 4', sans-serif;
					user-select: none;
					color: var(--fg, rgba(11, 22, 33, 0.5));
					background: url(data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iMjAwIiBoZWlnaHQ9IjIwMCIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KICA8Y2lyY2xlIGN4PSIxMDAiIGN5PSIxMDAiIHI9IjEwMCIgZmlsbD0iI0ZGQ0YwMCIvPgogIDxwYXRoIGQ9Ik00Ny41IDUyLjVMOTUgMTAwbC00Ny41IDQ3LjVtNjAtOTVMMTU1IDEwMGwtNDcuNSA0Ny41IiBmaWxsPSJub25lIiBzdHJva2U9IiMxOTE5MTkiIHN0cm9rZS13aWR0aD0iMjQiLz4KPC9zdmc+Cg==);
				}
			)test"},
		},
		.entry_paths = {
			"/script.jsx",
			"/style.css",
		},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
			.MinifyWhitespace = true,
			.LineLimit = 32,
			
		},
		
	});
}

TEST(BundlerDefault, TestBadImportErrorMessageWithHandlesImportErrorsFlag) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import('foo')
				import('foo')
				import('foo').catch()
				import('foo').catch()

				import('bar').catch()
				import('bar').catch()
				import('bar') // We should get an error report here even though the earlier imports have the "HandlesImportErrors" flag
				import('bar')

				import('baz').catch()
				import('baz').catch()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		.expected_scan_log = R"scan(
entry.js: ERROR: Could not resolve "foo"
NOTE: You can mark the path "foo" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle. You can also add ".catch()" here to handle this failure at run-time instead of bundle-time.
entry.js: ERROR: Could not resolve "bar"
NOTE: You can mark the path "bar" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle. You can also add ".catch()" here to handle this failure at run-time instead of bundle-time.
)scan",
		
	});
}

TEST(BundlerDefault, TestDecoratorPrintingESM) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import { constant } from './constants'
				import { imported } from 'somewhere'
				import { undef } from './empty'

				_ = class Outer {
					#bar;

					classes = [
						class { @imported @imported() imported },
						class { @unbound @unbound() unbound },
						class { @constant @constant() constant },
						class { @undef @undef() undef },

						class { @(element[access]) indexed },
						class { @foo.#bar private },
						class { @foo.\u30FF unicode },
						class { @(() => {}) arrow },
					]
				}
			)test"},
			{"/constants.js", R"test(

				export const constant = 123
			)test"},
			{"/empty.js", ""},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kESModule,
			.AbsOutputFile = "/out.js",
			.ExternalPackages = true,
			.MinifySyntax = true,
			
		},
		.expected_compile_log = R"compile(
entry.js: WARNING: Import "undef" will always be undefined because the file "empty.js" has no exports
)compile",
		
	});
}

TEST(BundlerDefault, TestDecoratorPrintingCJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import { constant } from './constants'
				import { imported } from 'somewhere'
				import { undef } from './empty'

				_ = class Outer {
					#bar;

					classes = [
						class { @imported @imported() imported },
						class { @unbound @unbound() unbound },
						class { @constant @constant() constant },
						class { @undef @undef() undef },

						class { @(element[access]) indexed },
						class { @foo.#bar private },
						class { @foo.\u30FF unicode },
						class { @(() => {}) arrow },
					]
				}
			)test"},
			{"/constants.js", R"test(

				export const constant = 123
			)test"},
			{"/empty.js", ""},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			.ExternalPackages = true,
			.MinifySyntax = true,
			
		},
		.expected_compile_log = R"compile(
entry.js: WARNING: Import "undef" will always be undefined because the file "empty.js" has no exports
)compile",
		
	});
}

TEST(BundlerDefault, TestJSXDevSelfEdgeCases) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/function-this.jsx", "export function Foo() { return <div/> }"},
			{"/class-this.jsx", "export class Foo { foo() { return <div/> } }"},
			{"/normal-constructor.jsx", "export class Foo { constructor() { this.foo = <div/> } }"},
			{"/derived-constructor.jsx", "export class Foo extends Object { constructor() { super(<div/>); this.foo = <div/> } }"},
			{"/normal-constructor-arg.jsx", "export class Foo { constructor(foo = <div/>) {} }"},
			{"/derived-constructor-arg.jsx", "export class Foo extends Object { constructor(foo = <div/>) { super() } }"},
			{"/normal-constructor-field.tsx", "export class Foo { foo = <div/> }"},
			{"/derived-constructor-field.tsx", "export class Foo extends Object { foo = <div/> }"},
			{"/static-field.jsx", "export class Foo { static foo = <div/> }"},
			{"/top-level-this-esm.jsx", "export let foo = <div/>; if (Foo) { foo = <Foo>nested top-level this</Foo> }"},
			{"/top-level-this-cjs.jsx", "exports.foo = <div/>"},
			{"/typescript-namespace.tsx", "export namespace Foo { export let foo = <div/> }"},
			{"/typescript-enum.tsx", "export enum Foo { foo = <div/> }"},
			{"/tsconfig.json", R"test({ "compilerOptions": { "useDefineForClassFields": false } })test"},
		},
		.entry_paths = {"*"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/out",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"react/jsx-dev-runtime", true},
					},
				},
			},
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kClassStaticField,
			.JSX = guchho::config::JSXOptions{
				.AutomaticRuntime = true,
				.Development = true,
			},
			
		},
		
	});
}

TEST(BundlerDefault, TestObjectLiteralProtoSetterEdgeCases) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/local-shorthand.js", R"test(

				function foo(__proto__, bar) {
					{
						let __proto__, bar // These locals will be renamed
						console.log(
							'this must not become "{ __proto__: ... }":',
							{
								__proto__,
								bar,
							},
						)
					}
				}
			)test"},
			{"/local-normal.js", R"test(

				function foo(__proto__, bar) {
					console.log(
						'this must not become "{ __proto__ }":',
						{
							__proto__: __proto__,
							bar: bar,
						},
					)
				}
			)test"},
			{"/import-shorthand.js", R"test(

				import { __proto__, bar } from 'foo'
				function foo() {
					console.log(
						'this must not become "{ __proto__: ... }":',
						{
							__proto__,
							bar,
						},
					)
				}
			)test"},
			{"/import-normal.js", R"test(

				import { __proto__, bar } from 'foo'
				function foo() {
					console.log(
						'this must not become "{ __proto__ }":',
						{
							__proto__: __proto__,
							bar: bar,
						},
					)
				}
			)test"},
		},
		.entry_paths = {"*"},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestObjectLiteralProtoSetterEdgeCasesMinifySyntax) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/local-computed.js", R"test(

				function foo(__proto__, bar) {
					{
						let __proto__, bar // These locals will be renamed
						console.log(
							'this must not become "{ __proto__: ... }":',
							{
								['__proto__']: __proto__,
								['bar']: bar,
							},
						)
					}
				}
			)test"},
			{"/local-normal.js", R"test(

				function foo(__proto__, bar) {
					console.log(
						'this must not become "{ __proto__ }":',
						{
							__proto__: __proto__,
							bar: bar,
						},
					)
				}
			)test"},
			{"/import-computed.js", R"test(

				import { __proto__, bar } from 'foo'
				function foo() {
					console.log(
						'this must not become "{ __proto__: ... }":',
						{
							['__proto__']: __proto__,
							['bar']: bar,
						},
					)
				}
			)test"},
			{"/import-normal.js", R"test(

				import { __proto__, bar } from 'foo'
				function foo() {
					console.log(
						'this must not become "{ __proto__ }":',
						{
							__proto__: __proto__,
							bar: bar,
						},
					)
				}
			)test"},
		},
		.entry_paths = {"*"},
		.options = guchho::config::Options{
			.AbsOutputDir = "/out",
			.MinifySyntax = true,
			
		},
		
	});
}

TEST(BundlerDefault, TestForbidStringImportNamesNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import { "an import" as anImport } from "./foo"
				export { "another import" as "an export" } from "./foo"
				anImport()
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kArbitraryModuleNamespaceNames,
			
		},
		.expected_compile_log = R"compile(
entry.js: ERROR: Using the string "an import" as an import name is not supported in the configured target environment
entry.js: ERROR: Using the string "another import" as an import name is not supported in the configured target environment
entry.js: ERROR: Using the string "an export" as an export name is not supported in the configured target environment
)compile",
		
	});
}

TEST(BundlerDefault, TestForbidStringExportNamesNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				let ok = true
				export { ok as "ok", ok as "not ok" }
				export { "same name" } from "./foo"
				export { "name 1" as "name 2" } from "./foo"
				export * as "name space" from "./foo"
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kArbitraryModuleNamespaceNames,
			
		},
		.expected_compile_log = R"compile(
entry.js: ERROR: Using the string "not ok" as an export name is not supported in the configured target environment
entry.js: ERROR: Using the string "same name" as an import name is not supported in the configured target environment
entry.js: ERROR: Using the string "name 1" as an import name is not supported in the configured target environment
entry.js: ERROR: Using the string "name 2" as an export name is not supported in the configured target environment
entry.js: ERROR: Using the string "name space" as an export name is not supported in the configured target environment
)compile",
		
	});
}

TEST(BundlerDefault, TestForbidStringImportNamesBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import { "nest ed" as nested } from "./nested.js"
				export { nested }
			)test"},
			{"/nested.js", R"test(

				import { "some import" as nested } from "external"
				export { nested as "nest ed" }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExternalSettingsData = guchho::config::ExternalSettings{
				.PreResolve = guchho::config::ExternalMatchers{
					.Exact = {
						{"external", true},
					},
				},
			},
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kArbitraryModuleNamespaceNames,
			
		},
		.expected_compile_log = R"compile(
nested.js: ERROR: Using the string "some import" as an import name is not supported in the configured target environment
)compile",
		
	});
}

TEST(BundlerDefault, TestForbidStringExportNamesBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import { "o.k." as ok } from "./internal.js"
				export { ok as "ok", ok as "not ok" }
				export * from "./nested.js"
				export * as "name space" from "./nested.js"
			)test"},
			{"/internal.js", R"test(

				let ok = true
				export { ok as "o.k." }
			)test"},
			{"/nested.js", R"test(

				export * from "./very-nested.js"
				let nested = 1
				export { nested as "nested name" }
			)test"},
			{"/very-nested.js", R"test(

				let nested = 2
				export { nested as "very nested name" }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kArbitraryModuleNamespaceNames,
			
		},
		.expected_compile_log = R"compile(
entry.js: ERROR: Using the string "not ok" as an export name is not supported in the configured target environment
entry.js: ERROR: Using the string "name space" as an export name is not supported in the configured target environment
nested.js: ERROR: Using the string "nested name" as an export name is not supported in the configured target environment
very-nested.js: ERROR: Using the string "very nested name" as an export name is not supported in the configured target environment
)compile",
		
	});
}

TEST(BundlerDefault, TestInjectWithStringExportNameNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				console.log(test)
			)test"},
			{"/inject.js", R"test(
				const old = console.log
				const fn = (...args) => old.apply(console, ['log:'].concat(args))
				export { fn as "console.log" }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kArbitraryModuleNamespaceNames,
			.InjectPaths = {"/inject.js"},
		},
	});
}

TEST(BundlerDefault, TestInjectWithStringExportNameBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				console.log(test)
				console.info(test)
				console.warn(test)
			)test"},
			{"/inject.js", R"test(
				const old = console.log
				const fn = (...args) => old.apply(console, ['log:'].concat(args))
				export { fn as "console.log" }
				export { "console.log" as "console.info" } from "./inject.js"
				import { "console.info" as info } from "./inject.js"
				export { info as "console.warn" }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kArbitraryModuleNamespaceNames,
			.InjectPaths = {"/inject.js"},
		},
	});
}

TEST(BundlerDefault, TestInjectWithStringReExportNameNoBundle) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(
				console.log(test)
			)test"},
			{"/inject.js", R"test(
				export { fn as "console.log" } from 'pkg'
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kPassThrough,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kArbitraryModuleNamespaceNames,
			.InjectPaths = {"/inject.js"},
		},
	});
}

TEST(BundlerDefault, TestStringExportNamesCommonJS) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import { "some import" as someImport } from "./foo"
				export { someImport as "some export" }
				export * as "all the stuff" from "./foo"
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kCommonJS,
			.AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kArbitraryModuleNamespaceNames,
			
		},
		
	});
}

TEST(BundlerDefault, TestStringExportNamesIIFE) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import { "some import" as someImport } from "./foo"
				export { someImport as "some export" }
				export * as "all the stuff" from "./foo"
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			
			.BuildMode = guchho::config::Mode::kConvertFormat,
			.OutputFormat = guchho::config::Format::kIIFE,
			.AbsOutputFile = "/out.js",
			.GlobalName = {"global", "name"},
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kArbitraryModuleNamespaceNames,
			
		},
		
	});
}

TEST(BundlerDefault, TestSourceIdentifierNameIndexSingleEntry) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/index.js", R"test(

				require('.')
				require('pkg')
				require('./nested')
			)test"},
			{"/Users/user/project/nested/index.js", "exports.nested = true"},
			{"/Users/user/project/node_modules/pkg/index.js", "exports.pkg = true"},
		},
		.entry_paths = {"/Users/user/project/index.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/Users/user/project/out",
			
		},
		
	});
}

TEST(BundlerDefault, TestSourceIdentifierNameIndexMultipleEntry) {
	default_suite.ExpectBundled(Bundled{
		.files = {
			{"/Users/user/project/home/index.js", R"test(

				require('.')
				require('pkg')
				require('../common')
			)test"},
			{"/Users/user/project/about/index.js", R"test(

				require('.')
				require('pkg')
				require('../common')
			)test"},
			{"/Users/user/project/common/index.js", "exports.common = true"},
			{"/Users/user/project/node_modules/pkg/index.js", "exports.pkg = true"},
		},
		.entry_paths = {
			"/Users/user/project/home/index.js",
			"/Users/user/project/about/index.js",
		},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputDir = "/Users/user/project/out",
			
		},
		
	});
}

TEST(BundlerCSS, TestResolveExtensionsOrderIssue4053) {
	css_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				import test from "./Test"
				import image from "expo-image"
				console.log(test === 'Test.web.tsx')
				console.log(image === 'Image.web.tsx')
			)test"},
			{"/Test.web.tsx", "export default 'Test.web.tsx'"},
			{"/Test.tsx", "export default 'Test.tsx'"},
			{"/node_modules/expo-image/index.js", R"test(

				export { default } from "./Image"
			)test"},
			{"/node_modules/expo-image/Image.web.tsx", "export default 'Image.web.tsx'"},
			{"/node_modules/expo-image/Image.tsx", "export default 'Image.tsx'"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			.ExtensionOrder = {
				".web.mjs", ".mjs", ".web.js", ".js",
				".web.mts", ".mts", ".web.ts", ".ts",
				".web.jsx", ".jsx", ".web.tsx", ".tsx",
				".json",
			},
			
		},
		
	});
}

TEST(BundlerCSS, TestBundleESMWithNestedVarIssue4348) {
	css_suite.ExpectBundled(Bundled{
		.files = {
			{"/entry.js", R"test(

				require('./foo')
			)test"},
			{"/foo.js", R"test(

				var a = 'a'
				for (var b = 'b'; 0; ) ;
				if (true) { var c = 'c' }
				if (true) var d = 'd'
				if (false) {} else var e = 'e'
				var x = 1
				while (x--) var f = 'f'
				do var g = 'g'; while (0);
				for (; x++; ) var h = 'h'
				for (var y in 'y') var i = 'i'
				for (var y of 'y') var j = 'j'
				export { a, b, c, d, e, f, g, h, i, j }
			)test"},
		},
		.entry_paths = {"/entry.js"},
		.options = guchho::config::Options{
			.BuildMode = guchho::config::Mode::kBundle,
			.AbsOutputFile = "/out.js",
			
		},
		
	});
}

} // namespace bundler::test
